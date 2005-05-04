/*
 *  xfce4-mailwatch-plugin - a mail notification applet for the xfce4 panel
 *  Copyright (c) 2005 Brian Tarricone <bjt23@cornell.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License ONLY.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#include <glib.h>
#include <gtk/gtk.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/libxfcegui4.h>

#include <gnutls/gnutls.h>
#include <gcrypt.h>
/* stuff to support gthreads */
static int my_g_mutex_init(void **priv);
static int my_g_mutex_destroy(void **priv);
static int my_g_mutex_lock(void **priv);
static int my_g_mutex_unlock(void **priv);
static struct gcry_thread_cbs gcry_threads_gthread = {
    GCRY_THREAD_OPTION_USER,
    NULL,
    my_g_mutex_init,
    my_g_mutex_destroy,
    my_g_mutex_lock,
    my_g_mutex_unlock,
    read,
    write,
    select,
    waitpid,
    accept,
    (int (*)(int, _GCRY_PTH_SOCKADDR *, _GCRY_PTH_SOCKLEN_T))connect,
    sendmsg,
    recvmsg
};

#include "mailwatch.h"

#define BORDER 8
#define GNUTLS_CA_FILE   "ca.pem"

#define XFCE_MAILWATCH_IMAP_MAILBOX(ptr) ((XfceMailwatchIMAPMailbox *)ptr)

#define IMAP_CMD_START   GINT_TO_POINTER(1)
#define IMAP_CMD_PAUSE   GINT_TO_POINTER(2)
#define IMAP_CMD_TIMEOUT GINT_TO_POINTER(3)
#define IMAP_CMD_QUIT    GINT_TO_POINTER(4)
#define IMAP_CMD_UPDATE  GINT_TO_POINTER(5)

typedef struct
{
    XfceMailwatchMailbox mailbox;
    
    GMutex *config_mx;
    
    guint timeout;
    gchar *host;
    gchar *username;
    gchar *password;
    gboolean require_ssl;
    GList *mailboxes_to_check;
    
    GThread *th;
    GAsyncQueue *aqueue;
    
    XfceMailwatch *mailwatch;
    
    /* state related to the current connection (if any) */
    gint sockfd;
    guint imap_tag;
    gboolean requiring_ssl;
    gboolean using_ssl;
    
    /* secure this, dude */
    gboolean gnutls_inited;
    gnutls_session_t gt_session;
    gnutls_certificate_credentials_t gt_creds;
} XfceMailwatchIMAPMailbox;

typedef enum
{
    IMAP_SUCCEEDED = 0,
    IMAP_FAILED = -1,
    IMAP_NO_STARTTLS_SUPPORT = -2
} IMAPResult;

/*
 * gthread -> gcrypt support wrappers
 */
static int
my_g_mutex_init(void **priv)
{
    GMutex **gmx = (GMutex **)priv;
    
    *gmx = g_mutex_new();
    if(!*gmx)
        return -1;
    return 0;
}

static int
my_g_mutex_destroy(void **priv)
{
    GMutex **gmx = (GMutex **)priv;
    
    g_mutex_free(*gmx);
    return 0;
}

static int
my_g_mutex_lock(void **priv)
{
    GMutex **gmx = (GMutex **)priv;
    
    g_mutex_lock(*gmx);
    return 0;
}

static int
my_g_mutex_unlock(void **priv)
{
    GMutex **gmx = (GMutex **)priv;
    
    g_mutex_unlock(*gmx);
    return 0;
}

/***/

static gssize
imap_send(XfceMailwatchIMAPMailbox *imailbox, const gchar *buf)
{
    gint bout = 0;
    
    if(imailbox->using_ssl) {
        gint ret = 0, totallen = strlen(buf);
        gint bytesleft = totallen;
        
        while(bytesleft > 0) {
            ret = gnutls_record_send(imailbox->gt_session,
                    buf+totallen-bytesleft, bytesleft);
            if(ret < 0 && ret != GNUTLS_E_INTERRUPTED && ret != GNUTLS_E_AGAIN) {
                DBG("gnutls_record_send() failed (%d): %s", ret, gnutls_strerror(ret));
                bout = -1;
                break;
            } else if(ret > 0) {
                bout += ret;
                bytesleft -= ret;
            }
        }
    } else
        bout = send(imailbox->sockfd, buf, strlen(buf), MSG_NOSIGNAL);
    
    return bout;
}

static gssize
imap_recv(XfceMailwatchIMAPMailbox *imailbox, gchar *buf, gsize len)
{
    fd_set rfd;
    struct timeval tv;
    gint ret, bin;
    
    if(imailbox->using_ssl) {
        do {
            ret = gnutls_record_recv(imailbox->gt_session, buf, len);
        } while(ret == GNUTLS_E_INTERRUPTED || ret == GNUTLS_E_AGAIN);
        
        if(ret < 0)
            bin = -1;
        else
            bin = ret;
    } else {
        FD_ZERO(&rfd);
        FD_SET(imailbox->sockfd, &rfd);
        tv.tv_sec = 45;
        tv.tv_usec = 0;
        
        ret = select(FD_SETSIZE, &rfd, NULL, NULL, &tv);
        if(ret < 0)
            return -1;
        
        if(!FD_ISSET(imailbox->sockfd, &rfd))
            return 0;
        
        bin = recv(imailbox->sockfd, buf, len, MSG_NOSIGNAL);
    }
    
    if(bin >= 0)
        buf[bin] = 0;
    
    return bin;
}


static void
imap_do_logout(XfceMailwatchIMAPMailbox *imailbox)
{
    imap_send(imailbox, "ABCD LOGOUT\r\n");
    
    shutdown(imailbox->sockfd, SHUT_RDWR);
    close(imailbox->sockfd);
    imailbox->sockfd = -1;
}

static gboolean
imap_get_sockaddr(const gchar *host, const gchar *service,
        struct sockaddr_in *addr)
{
    struct addrinfo hints = { 0, PF_INET, SOCK_STREAM, IPPROTO_TCP,
            sizeof(struct sockaddr_in), NULL, NULL, NULL };
    struct addrinfo *res = NULL;
    
    TRACE("entering (%s, %s, %p)", host, service, addr);
    
    g_return_val_if_fail(host && service && addr, FALSE);
    
    if(getaddrinfo(host, service, &hints, &res))
        return FALSE;
    
    if(res->ai_addrlen != sizeof(struct sockaddr_in)) {
        freeaddrinfo(res);
        return FALSE;
    }
    
    memcpy(addr, res->ai_addr, sizeof(struct sockaddr_in));
    freeaddrinfo(res);
    
    return TRUE;
}

static gboolean
imap_auth_plain(XfceMailwatchIMAPMailbox *imailbox, const gchar *username,
        const gchar *password)
{
#define BUFSIZE 8191
    gint bin, bout;
    gchar buf[BUFSIZE+1];
    
    TRACE("entering");
    
    /* check capabilities */
    g_snprintf(buf, BUFSIZE, "%05d CAPABILITY\r\n", ++imailbox->imap_tag);
    bout = imap_send(imailbox, buf);
    DBG("sent CAPABILITY (%d)", bout);
    if(bout != strlen(buf))
        goto cleanuperr;
    bin = imap_recv(imailbox, buf, BUFSIZE);
    DBG("response from CAPABILITY (%d): %s", bin, bin>0?buf:"(nada)");
    if(bin <= 0)
        goto cleanuperr;
    
    if(strstr(buf, " LOGINDISABLED")) {
        xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");
        g_warning(_("Secure IMAP is not available, and the IMAP server does not support plaintext logins."));
        goto cleanuperr;
    }
    
    /* send the creds */
    g_snprintf(buf, BUFSIZE, "%05d LOGIN \"%s\" \"%s\"\r\n",
            ++imailbox->imap_tag, username, password);
    bout = imap_send(imailbox, buf);
    DBG("sent login (%d)", bout);
    if(bout != strlen(buf))
        goto cleanuperr;
    
    /* and see if we actually got auth-ed */
    bin = imap_recv(imailbox, buf, BUFSIZE);
    DBG("response from login (%d): %s", bin, bin>0?buf:"(nada)");
    if(bin <= 0)
        goto cleanuperr;
    DBG("strstr() returns %p", strstr(buf, "OK LOGIN"));
    if(!strstr(buf, "OK LOGIN"))
        goto cleanuperr;
    
    TRACE("leaving (success)");
    
    return TRUE;
    
    cleanuperr:
    
    shutdown(imailbox->sockfd, SHUT_RDWR);
    close(imailbox->sockfd);
    imailbox->sockfd = -1;
    
    return FALSE;
#undef BUFSIZE
}

static gboolean
imap_negotiate_ssl(XfceMailwatchIMAPMailbox *imailbox, const gchar *host)
{
    gint gt_ret;
    const int cert_type_prio[2] = { GNUTLS_CRT_X509, 0 };
    
    TRACE("entering");
    
    /* init */
    gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_gthread);
    gnutls_global_init();
    imailbox->gnutls_inited = TRUE;
    
    /* init the x509 cert */
    gnutls_certificate_allocate_credentials(&imailbox->gt_creds);
    gnutls_certificate_set_x509_trust_file(imailbox->gt_creds, GNUTLS_CA_FILE,
            GNUTLS_X509_FMT_PEM);
    
    /* init the session and set it up */
    gnutls_init(&imailbox->gt_session, GNUTLS_CLIENT);
    gnutls_set_default_priority(imailbox->gt_session);
    gnutls_certificate_type_set_priority(imailbox->gt_session, cert_type_prio);
    gnutls_credentials_set(imailbox->gt_session, GNUTLS_CRD_CERTIFICATE,
            imailbox->gt_creds);
    gnutls_transport_set_ptr(imailbox->gt_session,
            (gnutls_transport_ptr_t)imailbox->sockfd);
    
    /* just do it */
    gt_ret = gnutls_handshake(imailbox->gt_session);
    if(gt_ret < 0) {
        g_critical("TLS handshake failed: %s", gnutls_strerror(gt_ret));
        
        shutdown(imailbox->sockfd, SHUT_RDWR);
        close(imailbox->sockfd);
        imailbox->sockfd = -1;
        
        return FALSE;
    } else
        g_message("TLS handshake succeeded!");
    
    return TRUE;
}

static IMAPResult
imap_do_starttls(XfceMailwatchIMAPMailbox *imailbox, const gchar *host,
        const gchar *username, const gchar *password)
{
#define BUFSIZE 8191
    IMAPResult res = IMAP_FAILED;
    gint bin;
    gchar buf[BUFSIZE+1];
    
    TRACE("entering");
    
    /* turn off SSL, because obviously we haven't negotiated it yet. */
    imailbox->using_ssl = FALSE;
    
    g_snprintf(buf, BUFSIZE, "%05d CAPABILITY\r\n", ++imailbox->imap_tag);
    if(imap_send(imailbox, buf) != strlen(buf))
        goto cleanuperr;
    
    bin = imap_recv(imailbox, buf, BUFSIZE);
    DBG("checking for STARTTLS caps (%d): %s", bin, bin>0?buf:"(nada)");
    if(bin <= 0)
        goto cleanuperr;
    if(!strstr(buf, " STARTTLS")) {
        res = IMAP_NO_STARTTLS_SUPPORT;
        goto cleanuperr;
    }
    
    g_snprintf(buf, BUFSIZE, "%05d STARTTLS\r\n", ++imailbox->imap_tag);
    if(imap_send(imailbox, buf) != strlen(buf))
        goto cleanuperr;
    
    if(imap_recv(imailbox, buf, BUFSIZE) < 0)
        goto cleanuperr;
    if(!strstr(buf, " OK"))
        goto cleanuperr;
    
    /* now that we've negotiated SSL, reenable using_ssl */
    imailbox->using_ssl = TRUE;
    
    return IMAP_SUCCEEDED;
    
    cleanuperr:
    
    shutdown(imailbox->sockfd, SHUT_RDWR);
    close(imailbox->sockfd);
    imailbox->sockfd = -1;
    
    return res;
#undef BUFSIZE
}

static gboolean
imap_connect(XfceMailwatchIMAPMailbox *imailbox, const gchar *host,
        const gchar *service)
{
    struct sockaddr_in addr;
    gchar buf[1024];
    
    TRACE("entering (%s)", service);
    
    if(!imap_get_sockaddr(host, service, &addr)) {
        DBG("failed to get sockaddr");
        return FALSE;
    }
    
    imailbox->sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(imailbox->sockfd < 0) {
        DBG("failed to open socket");
        return FALSE;
    }
    
    if(connect(imailbox->sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))) {
        DBG("failed to connect");
        close(imailbox->sockfd);
        imailbox->sockfd = -1;
        return FALSE;
    }
    
    /* discard opening banner, but only if on the normal imap port */
    if(!strcmp(service, "imap") && imap_recv(imailbox, buf, 1023) < 0) {
        DBG("failed to get banner");
        shutdown(imailbox->sockfd, SHUT_RDWR);
        close(imailbox->sockfd);
        imailbox->sockfd = -1;
        return FALSE;
    }
    
    return TRUE;
}

static gboolean
imap_authenticate(XfceMailwatchIMAPMailbox *imailbox, const gchar *host,
        const gchar *username, const gchar *password)
{
    IMAPResult res;
    gboolean need_to_trash_banner = FALSE;
    gchar buf[1024];
    
    TRACE("entering");
    
    /* turn this off for connecting */
    imailbox->using_ssl = FALSE;
    
    if(!imap_connect(imailbox, host, "imap"))
        return FALSE;
    
    /* but otherwise we really love SSL. */
    imailbox->using_ssl = TRUE;
    
    /* first try STARTTLS.  if that fails, try connecting on the imaps port.
     * if that fails, and we're requiring SSL, bail.  otherwise, try
     * plaintext (yuck), but only if the user allows it. */
    res = imap_do_starttls(imailbox, host, username, password);
    DBG("res is %d", res);
    if(res == IMAP_FAILED)
        return FALSE;
    else if(res == IMAP_NO_STARTTLS_SUPPORT) {
        if(!imap_connect(imailbox, host, "imaps")) {
            if(imailbox->requiring_ssl)
                return FALSE;
            else {
                if(!imap_connect(imailbox, host, "imap"))
                    return FALSE;
                else
                    imailbox->using_ssl = FALSE;
            }
        } else {
            imailbox->using_ssl = TRUE;
            need_to_trash_banner = TRUE;
        }
    }
    
    DBG("using_ssl is %s", imailbox->using_ssl?"TRUE":"FALSE");
    
    if(imailbox->using_ssl && !imap_negotiate_ssl(imailbox, host))
        return FALSE;
    
    /* discard opening banner, in this case it happens after TLS negotiation */
    if(need_to_trash_banner && imap_recv(imailbox, buf, 1023) < 0) {
        DBG("failed to get banner");
        shutdown(imailbox->sockfd, SHUT_RDWR);
        close(imailbox->sockfd);
        imailbox->sockfd = -1;
        return FALSE;
    }
    
    if(!imap_auth_plain(imailbox, username, password))
        return FALSE;
    
    return TRUE;
}

static guint
imap_check_mailbox(XfceMailwatchIMAPMailbox *imailbox,
        const gchar *mailbox_name)
{
#define BUFSIZE 8191
    gint new_messages = 0;
    gchar buf[BUFSIZE+1], *p;
    
    TRACE("entering, folder %s", mailbox_name);
    
    /* ask the server to look at the mailbox */
    g_snprintf(buf, BUFSIZE, "%05d EXAMINE %s\r\n", ++imailbox->imap_tag,
            mailbox_name);
    if(imap_send(imailbox, buf) != strlen(buf))
        return 0;
    DBG("  successfully sent cmd '%s'", buf);
    
    /* grab the response; there should be a line like "* # RECENT" */
    if(imap_recv(imailbox, buf, BUFSIZE) < 0)
        return 0;
    DBG("  successfully got reply '%s'", buf);
    
    if(!(p=strstr(buf, " RECENT")) || p == buf)
        return 0;
    *p = 0;
    DBG("  found 'RECENT' text");
    
    while(p > buf && *p != ' ')
        p--;
    p++;
    DBG(" moved pointer to beginning of number '%s'", p);
    
    new_messages = atoi(p);
    if(new_messages < 0)
        new_messages = 0;
    
    DBG("new message count in mailbox '%s' is %d", mailbox_name, new_messages);
    
    return (guint)new_messages;
#undef BUFSIZE
}

static void
imap_check_mail(XfceMailwatchIMAPMailbox *imailbox)
{
#define BUFSIZE 1024
    gchar host[BUFSIZE], username[BUFSIZE], password[BUFSIZE];
    guint new_messages = 0;
    GList *mailboxes_to_check = NULL, *l;
    
    g_mutex_lock(imailbox->config_mx);
    
    if(!imailbox->host || !imailbox->username || !imailbox->password) {
        g_mutex_unlock(imailbox->config_mx);
        return;
    }
    
    g_strlcpy(host, imailbox->host, BUFSIZE);
    g_strlcpy(username, imailbox->username, BUFSIZE);
    g_strlcpy(password, imailbox->password, BUFSIZE);
    imailbox->requiring_ssl = imailbox->require_ssl;
    
    /* make a deep copy of the mailbox list */
    for(l = imailbox->mailboxes_to_check; l; l = l->next)
        mailboxes_to_check = g_list_prepend(mailboxes_to_check, g_strdup(l->data));
    
    g_mutex_unlock(imailbox->config_mx);
    
    if(!imap_authenticate(imailbox, host, username, password)) {
        DBG("failed to connect to imap server");
        goto cleanup;
    }
    
    new_messages = imap_check_mailbox(imailbox, "INBOX");
    DBG("checked inbox, %d new messages", new_messages);
    for(l = mailboxes_to_check; l; l = l->next) {
        new_messages += imap_check_mailbox(imailbox, l->data);
        DBG("checked mail folder %s, total is now %d new messages", (gchar *)l->data, new_messages);
    }
    
    xfce_mailwatch_signal_new_messages(imailbox->mailwatch,
            XFCE_MAILWATCH_MAILBOX(imailbox), new_messages);
    
    imap_do_logout(imailbox);
    
    cleanup:
    
    if(mailboxes_to_check) {
        g_list_foreach(mailboxes_to_check, (GFunc)g_free, NULL);
        g_list_free(mailboxes_to_check);
    }
    
    if(imailbox->gnutls_inited) {
        gnutls_deinit(imailbox->gt_session);
        gnutls_certificate_free_credentials(imailbox->gt_creds);
        gnutls_global_deinit();
        imailbox->gnutls_inited = FALSE;
    }
    
#undef BUFSIZE
}

static gpointer
imap_check_mail_th(gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    GTimer *timer = NULL;
    gboolean running = FALSE;
    guint timeout = 0, delta = 0;
    gulong dummy;
    
    g_async_queue_ref(imailbox->aqueue);
    
    timer = g_timer_new();
    
    for(;;) {
        gpointer msg = g_async_queue_try_pop(imailbox->aqueue);
        
        if(msg) {
            if(msg == IMAP_CMD_START) {
                g_timer_start(timer);
                running = TRUE;
            } else if(msg == IMAP_CMD_PAUSE)
                running = FALSE;
            else if(msg == IMAP_CMD_TIMEOUT)
                timeout = GPOINTER_TO_UINT(g_async_queue_pop(imailbox->aqueue));
            else if(msg == IMAP_CMD_QUIT) {
                g_timer_destroy(timer);
                g_async_queue_unref(imailbox->aqueue);
                g_thread_exit(NULL);
            }
        }
        
        if(running && (msg == IMAP_CMD_UPDATE
                || g_timer_elapsed(timer, &dummy) >= timeout-delta))
        {
            gdouble time_before, time_after;
            time_before = g_timer_elapsed(timer, &dummy);
            imap_check_mail(imailbox);
            time_after = g_timer_elapsed(timer, &dummy);
            delta = (gint)(time_after - time_before);
            g_timer_start(timer);
        } else
            g_usleep(250000);
    }
    
    /* NOTREACHED */
    g_timer_destroy(timer);
    g_async_queue_unref(imailbox->aqueue);
    return NULL;
}

static XfceMailwatchMailbox *
imap_mailbox_new(XfceMailwatch *mailwatch, XfceMailwatchMailboxType *type)
{
    XfceMailwatchIMAPMailbox *imailbox = g_new0(XfceMailwatchIMAPMailbox, 1);
    imailbox->mailbox.type = type;
    imailbox->mailwatch = mailwatch;
    imailbox->timeout = XFCE_MAILWATCH_DEFAULT_TIMEOUT;
    imailbox->require_ssl = TRUE;
    imailbox->config_mx = g_mutex_new();
    /* init the queue */
    imailbox->aqueue = g_async_queue_new();
    /* and init the timeout */
    g_async_queue_push(imailbox->aqueue, IMAP_CMD_TIMEOUT);
    g_async_queue_push(imailbox->aqueue,
            GUINT_TO_POINTER(xfce_mailwatch_get_timeout(imailbox->mailwatch)));
    /* create checker thread */
    imailbox->th = g_thread_create(imap_check_mail_th, imailbox, TRUE, NULL);
    
    return (XfceMailwatchMailbox *)imailbox;
}

static void
imap_set_activated(XfceMailwatchMailbox *mailbox, gboolean activated)
{
    XfceMailwatchIMAPMailbox *imailbox = XFCE_MAILWATCH_IMAP_MAILBOX(mailbox);
    
    g_async_queue_push(imailbox->aqueue, activated ? IMAP_CMD_START : IMAP_CMD_PAUSE);
}

static void
imap_force_update_cb(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchIMAPMailbox *imailbox = XFCE_MAILWATCH_IMAP_MAILBOX(mailbox);
    
    g_async_queue_push(imailbox->aqueue, IMAP_CMD_UPDATE);
}

static gboolean
imap_host_entry_focus_out_cb(GtkWidget *w, GdkEventFocus *evt,
        gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    gchar *str;
    
    str = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    
    g_mutex_lock(imailbox->config_mx);
    
    g_free(imailbox->host);
    if(!str || !*str) {
        imailbox->host = NULL;
        g_free(str);
    } else
        imailbox->host = str;
    
    g_mutex_unlock(imailbox->config_mx);
    
    return FALSE;
}

static gboolean
imap_username_entry_focus_out_cb(GtkWidget *w, GdkEventFocus *evt,
        gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    gchar *str;
    
    str = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    
    g_mutex_lock(imailbox->config_mx);
    
    g_free(imailbox->username);
    if(!str || !*str) {
        imailbox->username = NULL;
        g_free(str);
    } else
        imailbox->username = str;
    
    g_mutex_unlock(imailbox->config_mx);
    
    return FALSE;
}

static gboolean
imap_password_entry_focus_out_cb(GtkWidget *w, GdkEventFocus *evt,
        gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    gchar *str;
    
    str = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    
    g_mutex_lock(imailbox->config_mx);
    
    g_free(imailbox->password);
    if(!str || !*str) {
        imailbox->password = NULL;
        g_free(str);
    } else
        imailbox->password = str;
    
    g_mutex_unlock(imailbox->config_mx);
    
    return FALSE;
}

static void
imap_require_secure_chk_cb(GtkToggleButton *tb, gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    
    imailbox->require_ssl = gtk_toggle_button_get_active(tb);
}

static void
imap_config_add_btn_clicked_cb(GtkWidget *w, gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    GtkWindow *toplevel;
    GtkWidget *dlg, *hbox, *lbl, *entry;
    
    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");
    
    toplevel = GTK_WINDOW(gtk_widget_get_toplevel(w));
    dlg = gtk_dialog_new_with_buttons(_("Add Mailbox"), toplevel,
            GTK_DIALOG_NO_SEPARATOR|GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_ADD,
            GTK_RESPONSE_ACCEPT, NULL);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("Mailbox _name:"));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    
    entry = gtk_entry_new();
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), entry);
    
    while(gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        GtkWidget *treeview = g_object_get_data(G_OBJECT(w),
                "mailwatch-treeview");
        GtkListStore *ls = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(treeview)));
        GtkTreeIter itr;
        gchar *name;
        
        name = gtk_editable_get_chars(GTK_EDITABLE(entry), 0, -1);
        if(!name || !*name) {
            g_free(name);
            xfce_message_dialog(GTK_WINDOW(dlg), _("IMAP"),
                        GTK_STOCK_DIALOG_ERROR, _("Mailbox name required."),
                        _("Please enter the name of a mailbox that receives new mail."),
                        GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT, NULL);
            continue;
        }
        
        if(!g_ascii_strcasecmp(name, "inbox")) {
            g_free(name);
            xfce_message_dialog(GTK_WINDOW(dlg), _("IMAP"),
                        GTK_STOCK_DIALOG_INFO, _("Adding INBOX is unnecessary."),
                        _("XfceMailcheck will automatically check your Inbox; there is no need to explicitly add it to the list."),
                        GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT, NULL);
            break;
        }
        
        gtk_list_store_append(ls, &itr);
        gtk_list_store_set(ls, &itr, 0, name, -1);
        
        g_mutex_lock(imailbox->config_mx);
        imailbox->mailboxes_to_check = g_list_append(imailbox->mailboxes_to_check, name);
        g_mutex_unlock(imailbox->config_mx);
        
        break;
    }
    
    gtk_widget_destroy(dlg);
}
static void
imap_config_remove_btn_clicked_cb(GtkWidget *w, gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    GtkWidget *treeview = g_object_get_data(G_OBJECT(w), "mailwatch-treeview");
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    GList *selected, *l, *lmbox;
    GtkTreeModel *model = NULL;
    GtkTreeIter itr;
    gchar *name = NULL;
    
    selected = gtk_tree_selection_get_selected_rows(sel, &model);
    if(!selected)
        return;
    
    g_mutex_lock(imailbox->config_mx);
    
    for(l = selected; l; l = l->next) {
        GtkTreePath *path = l->data;
        
        if(gtk_tree_model_get_iter(model, &itr, path)) {
            gtk_tree_model_get(model, &itr, 0, &name, -1);
            gtk_list_store_remove(GTK_LIST_STORE(model), &itr);
            
            if((lmbox = g_list_find_custom(imailbox->mailboxes_to_check, name,
                    (GCompareFunc)strcmp)))
            {
                g_free(lmbox->data);
                imailbox->mailboxes_to_check =
                        g_list_delete_link(imailbox->mailboxes_to_check, lmbox);
            }
            g_free(name);
        }
    }
    
    g_mutex_unlock(imailbox->config_mx);
}

static gboolean
imap_config_set_button_sensitive(GtkTreeView *treeview, GdkEventButton *evt, 
        GtkWidget *w)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(treeview);
    
    if(gtk_tree_selection_count_selected_rows(sel) > 0)
        gtk_widget_set_sensitive(w, TRUE);
    else
        gtk_widget_set_sensitive(w, FALSE);
    
    return FALSE;
}

static gboolean
imap_config_timeout_spinbutton_changed_cb(GtkSpinButton *sb, GdkEventFocus *evt,
        gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    gint value = gtk_spin_button_get_value_as_int(sb) * 60 * 1000;
    
    imailbox->timeout = value;
    g_async_queue_push(imailbox->aqueue, IMAP_CMD_TIMEOUT);
    g_async_queue_push(imailbox->aqueue, GUINT_TO_POINTER(value));
    
    return FALSE;
}

static GtkContainer *
imap_get_setup_page(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchIMAPMailbox *imailbox = XFCE_MAILWATCH_IMAP_MAILBOX(mailbox);
    GtkWidget *topvbox, *vbox, *hbox, *frame, *lbl, *entry, *chk, *sw,
            *treeview, *btn, *sbtn;
    GtkSizeGroup *sg;
    GtkListStore *ls;
    GtkTreeIter itr;
    GList *l;
    GtkCellRenderer *render;
    GtkTreeViewColumn *col;
    GtkTreeSelection *sel;
    
    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");
    
    topvbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(topvbox);
    
    frame = xfce_framebox_new(_("IMAP Server"), TRUE);
    gtk_widget_show(frame);
    gtk_box_pack_start(GTK_BOX(topvbox), frame, FALSE, FALSE, 0);
    
    sg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(vbox);
    xfce_framebox_add(XFCE_FRAMEBOX(frame), vbox);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("_Mail server:"));
    gtk_misc_set_alignment(GTK_MISC(lbl), 0.0, 0.5);
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    gtk_size_group_add_widget(sg, lbl);
    
    entry = gtk_entry_new();
    if(imailbox->host)
        gtk_entry_set_text(GTK_ENTRY(entry), imailbox->host);
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
            G_CALLBACK(imap_host_entry_focus_out_cb), imailbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), entry);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("_Username:"));
    gtk_misc_set_alignment(GTK_MISC(lbl), 0.0, 0.5);
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    gtk_size_group_add_widget(sg, lbl);
    
    entry = gtk_entry_new();
    if(imailbox->username)
        gtk_entry_set_text(GTK_ENTRY(entry), imailbox->username);
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
            G_CALLBACK(imap_username_entry_focus_out_cb), imailbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), entry);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("_Password:"));
    gtk_misc_set_alignment(GTK_MISC(lbl), 0.0, 0.5);
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    gtk_size_group_add_widget(sg, lbl);
    
    entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    if(imailbox->password)
        gtk_entry_set_text(GTK_ENTRY(entry), imailbox->password);
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
            G_CALLBACK(imap_password_entry_focus_out_cb), imailbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), entry);
    
    chk = gtk_check_button_new_with_mnemonic(_("_Require secure connection"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk), imailbox->require_ssl);
    gtk_widget_show(chk);
    gtk_box_pack_start(GTK_BOX(vbox), chk, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(chk), "toggled",
            G_CALLBACK(imap_require_secure_chk_cb), imailbox);
    
    frame = xfce_framebox_new(_("New Mail Folders"), TRUE);
    gtk_widget_show(frame);
    gtk_box_pack_start(GTK_BOX(topvbox), frame, TRUE, TRUE, 0);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    xfce_framebox_add(XFCE_FRAMEBOX(frame), hbox);
    
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER,
            GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
            GTK_SHADOW_ETCHED_IN);
    gtk_widget_show(sw);
    gtk_box_pack_start(GTK_BOX(hbox), sw, TRUE, TRUE, 0);
    
    ls = gtk_list_store_new(1, G_TYPE_STRING);
    for(l = imailbox->mailboxes_to_check; l; l = l->next) {
        gtk_list_store_append(ls, &itr);
        gtk_list_store_set(ls, &itr, 0, l->data, -1);
    }
    
    treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ls));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);
    gtk_widget_add_events(treeview, GDK_BUTTON_PRESS|GDK_BUTTON_RELEASE);
    
    render = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("mailbox-name", render,
            "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
    
    gtk_widget_show(treeview);
    gtk_container_add(GTK_CONTAINER(sw), treeview);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), treeview);
    
    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
    gtk_tree_selection_unselect_all(sel);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(vbox);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);
    
    btn = gtk_button_new_from_stock(GTK_STOCK_ADD);
    gtk_widget_show(btn);
    gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(btn), "mailwatch-treeview", treeview);
    g_signal_connect(G_OBJECT(btn), "clicked",
            G_CALLBACK(imap_config_add_btn_clicked_cb), imailbox);
    
    btn = gtk_button_new_from_stock(GTK_STOCK_REMOVE);
    gtk_widget_set_sensitive(btn, FALSE);
    gtk_widget_show(btn);
    gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(btn), "mailwatch-treeview", treeview);
    g_signal_connect_after(G_OBJECT(treeview), "button-release-event",
            G_CALLBACK(imap_config_set_button_sensitive), btn);
    g_signal_connect(G_OBJECT(btn), "clicked",
            G_CALLBACK(imap_config_remove_btn_clicked_cb), imailbox);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_set_sensitive(btn, FALSE);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(topvbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("Check for _new messages every"));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    
    sbtn = gtk_spin_button_new_with_range(1.0, 1440.0, 1.0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(sbtn), TRUE);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(sbtn), FALSE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sbtn), imailbox->timeout/60000);
    gtk_widget_show(sbtn);
    gtk_box_pack_start(GTK_BOX(hbox), sbtn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(sbtn), "focus-out-event",
            G_CALLBACK(imap_config_timeout_spinbutton_changed_cb), imailbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), sbtn);
    
    lbl = gtk_label_new(_("minute(s)."));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    
    return GTK_CONTAINER(topvbox);
}

static void
imap_restore_param_list(XfceMailwatchMailbox *mailbox, GList *params)
{
    XfceMailwatchIMAPMailbox *imailbox = XFCE_MAILWATCH_IMAP_MAILBOX(mailbox);
    GList *l;
    gint n_newmail_boxes = -1;
    
    g_mutex_lock(imailbox->config_mx);
    
    for(l = params; l; l = l->next) {
        XfceMailwatchParam *param = l->data;
        
        if(!strcmp(param->key, "host"))
            imailbox->host = g_strdup(param->value);
        else if(!strcmp(param->key, "username"))
            imailbox->username = g_strdup(param->value);
        else if(!strcmp(param->key, "password"))
            imailbox->password = g_strdup(param->value);
        else if(!strcmp(param->key, "require_ssl"))
            imailbox->require_ssl = *(param->value) == '0' ? FALSE : TRUE;
        else if(!strcmp(param->key, "timeout")) {
            imailbox->timeout = atoi(param->value);
            g_async_queue_push(imailbox->aqueue, IMAP_CMD_TIMEOUT);
            g_async_queue_push(imailbox->aqueue,
                    GUINT_TO_POINTER(imailbox->timeout));
        } else if(!strcmp(param->key, "n_newmail_boxes"))
            n_newmail_boxes = atoi(param->value);
    }
    
    for(l = params; l; l = l->next) {
        XfceMailwatchParam *param = l->data;
        
        if(!strncmp(param->key, "newmail_box_", 12)) {
            imailbox->mailboxes_to_check =
                    g_list_prepend(imailbox->mailboxes_to_check,
                            g_strdup(param->value));
        }
    }
    imailbox->mailboxes_to_check = g_list_reverse(imailbox->mailboxes_to_check);
    
    g_mutex_unlock(imailbox->config_mx);
}

static GList *
imap_save_param_list(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchIMAPMailbox *imailbox = XFCE_MAILWATCH_IMAP_MAILBOX(mailbox);
    GList *params = NULL;
    XfceMailwatchParam *param;
    gint i;
    
    g_mutex_lock(imailbox->config_mx);
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("host");
    param->value = g_strdup(imailbox->host);
    params = g_list_prepend(params, param);
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("username");
    param->value = g_strdup(imailbox->username);
    params = g_list_prepend(params, param);
    
    /* FIXME: probably would be nice to obscure this somewhat to deter casual
     * accidental exposure */
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("password");
    param->value = g_strdup(imailbox->password);
    params = g_list_prepend(params, param);
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("require_ssl");
    param->value = g_strdup(imailbox->require_ssl ? "1" : "0");
    params = g_list_prepend(params, param);
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("timeout");
    param->value = g_strdup_printf("%d", imailbox->timeout);
    params = g_list_prepend(params, param);
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("n_newmail_boxes");
    param->value = g_strdup_printf("%d", g_list_length(imailbox->mailboxes_to_check));
    params = g_list_prepend(params, param);
    
    for(i = 0; i < g_list_length(imailbox->mailboxes_to_check); i++) {
        param = g_new(XfceMailwatchParam, 1);
        param->key = g_strdup_printf("newmail_box_%d", i);
        param->value = g_strdup(g_list_nth_data(imailbox->mailboxes_to_check, i));
        params = g_list_prepend(params, param);
    }
    
    g_mutex_unlock(imailbox->config_mx);
    
    return g_list_reverse(params);
}

static void 
imap_mailbox_free(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchIMAPMailbox *imailbox = XFCE_MAILWATCH_IMAP_MAILBOX(mailbox);
    
    g_async_queue_push(imailbox->aqueue, IMAP_CMD_QUIT);
    g_thread_join(imailbox->th);
    g_async_queue_unref(imailbox->aqueue);
    
    g_mutex_free(imailbox->config_mx);
    
    g_free(imailbox->host);
    g_free(imailbox->username);
    g_free(imailbox->password);
    
    g_free(imailbox);
}

XfceMailwatchMailboxType builtin_mailbox_type_imap = {
    "imap",
    N_("Remote IMAP Mailbox"),
    N_("The IMAP plugin can connect to a remote mail server that supports the IMAP protocol, optionally using SSL for link protection."),
    
    imap_mailbox_new,
    imap_set_activated,
    NULL,
    imap_force_update_cb,
    imap_get_setup_page,
    imap_restore_param_list,
    imap_save_param_list,
    imap_mailbox_free
};
