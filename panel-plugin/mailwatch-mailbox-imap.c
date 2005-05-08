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

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#include <glib.h>
#include <gtk/gtk.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/libxfcegui4.h>

#include "mailwatch-utils.h"

#include <gnutls/gnutls.h>
#include <gcrypt.h>

/* missing from 1.2.0? */
#ifndef _GCRY_PTH_SOCKADDR
#define _GCRY_PTH_SOCKADDR  struct sockaddr
#endif
#ifndef _GCRY_PTH_SOCKLEN_T
#define _GCRY_PTH_SOCKLEN_T socklen_t
#endif

/* stuff to support 'gthreads' with gcrypt */
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

#define BORDER                   8
#define GNUTLS_CA_FILE           "ca.pem"

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
    gchar *server_directory;
    
    GThread *th;
    GAsyncQueue *aqueue;
    
    XfceMailwatch *mailwatch;
    
    /* state related to the current connection (if any) */
    gint sockfd;
    guint imap_tag;
    gboolean requiring_ssl;
    gboolean using_ssl;
    
    /* some well-meaning admins erroneously ban port 143 and only allow 993,
     * instead of requiring the use of STARTTLS on port 143.  we should keep
     * track of this to save time. */
    guint port143_failures;
    glong last_143_check;
    
    /* secure this, dude */
    gboolean gnutls_inited;
    gnutls_session_t gt_session;
    gnutls_certificate_credentials_t gt_creds;
    
    /* config dlg */
    GtkTreeStore *ts;
    GtkCellRenderer *render;
    GtkWidget *refresh_btn;
    GNode *folder_tree;
} XfceMailwatchIMAPMailbox;

typedef enum
{
    IMAP_SUCCEEDED = 0,
    IMAP_FAILED = -1,
    IMAP_NO_STARTTLS_SUPPORT = -2
} IMAPResult;

enum
{
    IMAP_FOLDERS_NAME = 0,
    IMAP_FOLDERS_WATCHING,
    IMAP_FOLDERS_HOLDS_MESSAGES,
    IMAP_FOLDERS_FULLPATH,
    IMAP_FOLDERS_N_COLUMNS
};

typedef struct
{
    gchar *folder_name;
    gchar *full_path;
    gboolean holds_messages;
} IMAPFolderData;

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
    DBG("strstr() returns %p", strstr(buf, "OK"));
    if(!strstr(buf, "OK"))
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
    } else {
        DBG("TLS handshake succeeded");
    }
    
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
    
    /* this next batch of crap is necessary because it seems like a failed
     * connection (that is, one that isn't ECONNREFUSED) takes over 3 minutes
     * to fail!  if the panel is trying to quit, that's just unacceptable.
     */
    
    if(fcntl(imailbox->sockfd, F_SETFL, fcntl(imailbox->sockfd, F_GETFL) | O_NONBLOCK))
        g_warning(_("Unable to set socket to non-blocking mode.  If the connect attempt hangs, the panel may hang on close."));
    
    if(connect(imailbox->sockfd, (struct sockaddr *)&addr,
            sizeof(struct sockaddr_in)))
    {
        gboolean failed = TRUE;
        
        if(errno == EINPROGRESS) {
            gint iters_left;
            for(iters_left = 25; iters_left >= 0; iters_left--) {
                fd_set wfd;
                struct timeval tv = { 2, 0 };
                int sock_err = 0;
                socklen_t sock_err_len = sizeof(int);
                gpointer msg;
                
                FD_ZERO(&wfd);
                FD_SET(imailbox->sockfd, &wfd);
                
                DBG("checking for a connection...");
                
                /* wait until the connect attempt finishes */
                if(select(FD_SETSIZE, NULL, &wfd, NULL, &tv) < 0)
                    break;
                
                /* check to see if it finished, and, if so, if there was an
                 * error, or if it completed successfully */
                if(FD_ISSET(imailbox->sockfd, &wfd)) {
                    if(!getsockopt(imailbox->sockfd, SOL_SOCKET, SO_ERROR,
                                &sock_err, &sock_err_len)
                            && !sock_err)
                    {
                        DBG("    connection succeeded");
                        failed = FALSE;
                    } else {
                        DBG("    connection failed: sock_err is %d", sock_err);
                    }
                    break;
                }
                
                /* check the main thread to see if we're supposed to quit */
                msg = g_async_queue_try_pop(imailbox->aqueue);
                if(msg) {
                    /* put it back so imap_check_mail_th() can read it */
                    g_async_queue_push(imailbox->aqueue, msg);
                    if(msg == IMAP_CMD_QUIT) {
                        failed = TRUE;
                        break;
                    }
                }
            }
        }
        
        if(failed) {
            DBG("failed to connect");
            close(imailbox->sockfd);
            imailbox->sockfd = -1;
            return FALSE;
        }
    }
    
    if(fcntl(imailbox->sockfd, F_SETFL, fcntl(imailbox->sockfd, F_GETFL) & ~(O_NONBLOCK)))
        g_warning(_("Unable to return socket to blocking mode.  Data may not be retreived correctly."));
    
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
    IMAPResult res = IMAP_FAILED;
    gboolean need_to_trash_banner = FALSE;
    gchar buf[1024];
    GTimeVal now;
    
    TRACE("entering");
    
    /* check the current time.  if it's been more than 4 hours since we last
     * tried port 143 after failing a couple times, give it another shot now. */
    g_get_current_time(&now);
    if(imailbox->port143_failures && now.tv_sec-imailbox->last_143_check > (60*60*4))
        imailbox->port143_failures = 0;
    
    if(imailbox->port143_failures < 3) {
        /* first try STARTTLS.  if that fails, try connecting on the imaps port.
         * if that fails, and we're requiring SSL, bail.  otherwise, try
         * plaintext (yuck), but only if the user allows it. */
        
        /* turn this off for connecting */
        imailbox->using_ssl = FALSE;
    
        if(imap_connect(imailbox, host, "imap")) {
            res = imap_do_starttls(imailbox, host, username, password);
            DBG("res is %d", res);
        }
        
        /* but otherwise we really love SSL. */
        imailbox->using_ssl = TRUE;
        
        if(res != IMAP_SUCCEEDED)
            imailbox->port143_failures++;
        
        /* if we just failed, set the last failure to now.  if we failed
         * because the server says there's no STARTTLS support, don't check
         * again for another 5 days. */
        if(res == IMAP_FAILED)
            imailbox->last_143_check = now.tv_sec;
        else if(res == IMAP_NO_STARTTLS_SUPPORT)
            imailbox->last_143_check = now.tv_sec + (60*60*24*5);
    }
    
    if(res != IMAP_SUCCEEDED) {
        if(!imap_connect(imailbox, host, "imaps")) {
            /* this probably is a transient failure if 143 failed as well */
            if(res == IMAP_FAILED)
                imailbox->port143_failures = 0;
            
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
#define BUFSIZE 16383   /* yeah, this is probably overkill */
    gint new_messages = 0;
    gchar buf[BUFSIZE+1], *p, *q, tmp[64];
    
    TRACE("entering, folder %s", mailbox_name);
    
    /* ask the server to look at the mailbox */
    g_snprintf(buf, BUFSIZE, "%05d EXAMINE %s\r\n", ++imailbox->imap_tag,
            mailbox_name);
    if(imap_send(imailbox, buf) != strlen(buf))
        return 0;
    DBG("  successfully sent cmd '%s'", buf);
    
    /* grab the response; it should end with "##### OK " */
    if(imap_recv(imailbox, buf, BUFSIZE) < 0)
        return 0;
    g_snprintf(tmp, 64, "%d OK ", imailbox->imap_tag);
    if(!strstr(buf, tmp))
        return 0;
    DBG("  successfully got reply '%s'", buf);
    
    /* send SEARCH command */
    g_snprintf(buf, BUFSIZE, "%05d SEARCH UNSEEN\r\n", ++imailbox->imap_tag);
    if(imap_send(imailbox, buf) != strlen(buf))
        return 0;
    DBG("  successfully sent cmd '%s'", buf);
    
    /* get response; it should have "* SEARCH" followed by a space-delimited
     * list of unseen messages.  unfortunately, this means there's no upper
     * bound on string length =p */
    if(imap_recv(imailbox, buf, BUFSIZE) < 0)
        return 0;
    g_snprintf(tmp, 64, "%d OK SEARCH", imailbox->imap_tag);
    if(!strstr(buf, tmp))
        g_warning("Mailwatch: Uh-oh.  We didn't get the full response back!");
    p = strstr(buf, "* SEARCH");
    if(!p)
        return 0;
    DBG("  successfully got reply '%s'", buf);
    
    p += 8;
    
    /* find the end of the line */
    q = strstr(p, "\r");
    if(!q)
        q = strstr(p, "\n");
    if(!q)
        return 0;
    *q = 0;
    DBG("  ok, we have a list of messages: '%s'", p);
    
    /* find each space in the string; that's a message */
    while((p = strstr(p, " "))) {
        new_messages++;
        p++;
    }
    
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
    gboolean running = FALSE;
    GTimeVal start, now;
    guint timeout = 0, delta = 0;
    
    g_async_queue_ref(imailbox->aqueue);
    
    g_get_current_time(&start);
    
    for(;;) {
        gpointer msg = g_async_queue_try_pop(imailbox->aqueue);
        
        if(msg) {
            if(msg == IMAP_CMD_START) {
                g_get_current_time(&start);;
                running = TRUE;
            } else if(msg == IMAP_CMD_PAUSE)
                running = FALSE;
            else if(msg == IMAP_CMD_TIMEOUT)
                timeout = GPOINTER_TO_UINT(g_async_queue_pop(imailbox->aqueue));
            else if(msg == IMAP_CMD_QUIT) {
                g_async_queue_unref(imailbox->aqueue);
                g_thread_exit(NULL);
            }
        }
        
        g_get_current_time(&now);
        
        if(running && (msg == IMAP_CMD_UPDATE
                || now.tv_sec - start.tv_sec >= timeout - delta))
        {
            imap_check_mail(imailbox);
            g_get_current_time(&start);
            delta = (gint)start.tv_sec - now.tv_sec;
        } else
            g_usleep(250000);
    }
    
    /* NOTREACHED */
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

static gboolean
imap_config_timeout_spinbutton_changed_cb(GtkSpinButton *sb, GdkEventFocus *evt,
        gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    gint value = gtk_spin_button_get_value_as_int(sb) * 60;
    
    imailbox->timeout = value;
    g_async_queue_push(imailbox->aqueue, IMAP_CMD_TIMEOUT);
    g_async_queue_push(imailbox->aqueue, GUINT_TO_POINTER(value));
    
    return FALSE;
}

static GNode *
my_g_node_insert_data_sorted(GNode *parent, gpointer data)
{
    IMAPFolderData *fdata = data;
    GNode *new_node = NULL, *n;
    
    g_return_val_if_fail(parent && data, NULL);
    
    for(n = parent->children; n; n = n->next) {
        IMAPFolderData *a_fdata = n->data;
        if(g_ascii_strcasecmp(fdata->folder_name, a_fdata->folder_name) <= 0) {
            new_node = g_node_insert_data_before(parent, n, data);
            break;
        }
    }
    
    if(!new_node)
        new_node = g_node_append_data(parent, data);
    
    return new_node;
}

static gboolean
imap_populate_folder_tree(XfceMailwatchIMAPMailbox *imailbox,
        const gchar *cur_folder, GNode *parent)
{
#define BUFSIZE 16383
    gboolean ret = TRUE;
    gchar buf[BUFSIZE+1], fullpath[(BUFSIZE+1)/8] = "", separator[2] = { 0, 0 };
    gchar *p, *q, **resp_lines;
    gint bin, i;
    gboolean holds_messages = TRUE, has_children = TRUE;
    IMAPFolderData *fdata;
    GNode *node;
    
    g_return_val_if_fail(cur_folder, TRUE);
    
    TRACE("entering (%p, %s, %p)", imailbox, cur_folder, parent);
    
    g_snprintf(buf, BUFSIZE, "%05d LIST \"%s\" \"%%\"\r\n",
            ++imailbox->imap_tag, cur_folder);
    if(imap_send(imailbox, buf) != strlen(buf))
        return FALSE;
    DBG("sent LIST: '%s'", buf);
    
    bin = imap_recv(imailbox, buf, BUFSIZE);
    if(bin < 0) {
        DBG("imap_recv() failed");
        return FALSE;
    }
    DBG("got LIST response (%d): '%s'", bin, buf);
    if(strstr(buf, " NO ") || strstr(buf, " BAD "))
        return FALSE;
    if(!strstr(buf, " OK")) {
        gint bin1;
        DBG("need more data?");
        
        bin1 = imap_recv(imailbox, buf+bin, BUFSIZE-bin);
        if(bin < 0) {
            DBG("imap_recv() failed");
            return FALSE;
        }
        
        DBG("got second LIST response (%d): '%s'", bin1, buf+bin);
    }
    
    if(!strstr(buf, " OK"))
        return FALSE;
    
    if(strstr(buf, "\r"))
        resp_lines = g_strsplit(buf, "\r\n", -1);
    else
        resp_lines = g_strsplit(buf, "\n", -1);
    
    for(i = 0; resp_lines[i]; i++) {
        if(*resp_lines[i] != '*')
            continue;
        
        /* special case: NIL for a separator */
        p = strstr(resp_lines[i], "NIL");
        if(p) {
            p += 4;
            if(!*p)
                continue;
            else if(*p == '"') {
                p++;
                p[strlen(p)-1] = 0;
            }
            
            /* since the separator is NIL, it can't have subfolders.  if it
             * doesn't hold any messages, there's no point in adding it. */
            if(strstr(resp_lines[i], "\\NoSelect"))
                continue;
            
            fdata = g_new0(IMAPFolderData, 1);
            fdata->folder_name = g_strdup(p);
            fdata->full_path = g_strdup(p);
            fdata->holds_messages = TRUE;
            
            my_g_node_insert_data_sorted(parent, fdata);
            
            continue;
        }
            
        
        /* first quote before separator */
        p = strstr(resp_lines[i], "\"");
        if(!p)
            continue;
        *separator = *(p+1);
        
        /* quote after separator */
        p = strstr(p+1, "\"");
        if(!p)
            continue;
        
        /* space before folder name */
        p = strstr(p+1, " ");
        if(!p)
            continue;
        /* this is stupid */
        p++;
        if(*p == '"') {
            p++;
            p[strlen(p)-1] = 0;
        }
        
        /* sometimes the first entry is just the name of the current folder
         * itself. */
        if(!strcmp(p, cur_folder))
            continue;
        
        if(G_NODE_IS_ROOT(parent)) {
            /* if there's no parent, we need to be especially careful about what
             * we list here, as some IMAP servers return the entire content of
             * the home directory in the toplevel listing */
            
            if(imailbox->server_directory && *imailbox->server_directory
                    && strstr(p, imailbox->server_directory) != p)
            {
                continue;
            }
            
            if(*p == '.')
                continue;
            
            if((strstr(resp_lines[i], "\\NoInferiors") || strstr(resp_lines[i], "\\HasNoChildren"))
                    && strstr(resp_lines[i], "\\NoSelect"))
            {
                continue;
            }
            
#if 0
            if(g_ascii_strcasecmp(p, "inbox") && g_ascii_strcasecmp(p, "mail")
                    && g_ascii_strcasecmp(p, "maildir")
                    && g_ascii_strcasecmp(p, "mh-maildir")
                    && g_ascii_strcasecmp(p, "mbox"))
#endif
        }
        
        has_children = (!strstr(resp_lines[i], "\\HasNoChildren")
                && !strstr(resp_lines[i], "\\NoInferiors"));
        holds_messages = !strstr(resp_lines[i], "\\NoSelect");
        
        /* we only want the folder name, not the entire hierarchy */
        q = g_strrstr(p, separator);
        if(q)
            p = q + 1;
        
        /* i'm not sure why this happens sometimes.  my code is probably buggy */
        if(!*p)
            continue;
        
        g_snprintf(fullpath, (BUFSIZE+1)/8, "%s%s", cur_folder, p);
        
        fdata = g_new0(IMAPFolderData, 1);
        fdata->folder_name = g_strdup(p);
        fdata->full_path = g_strdup(fullpath);
        fdata->holds_messages = holds_messages;
        
        node = my_g_node_insert_data_sorted(parent, fdata);
        
        if(has_children) {
            g_strlcat(fullpath, separator, (BUFSIZE+1)/8);
            imap_populate_folder_tree(imailbox, fullpath, node);
        }
    }
    
    g_strfreev(resp_lines);
    
    return ret;
#undef BUFSIZE
}

static void
imap_populate_folder_tree_nodes_rec(XfceMailwatchIMAPMailbox *imailbox,
        GHashTable *mailboxes_to_check, GNode *node, GtkTreeIter *parent)
{
    GtkTreeIter itr;
    GNode *n;
    IMAPFolderData *fdata = node->data;
    gboolean is_inbox;
    
    is_inbox = !g_ascii_strcasecmp(fdata->folder_name, "inbox");
    
    if(is_inbox)
        gtk_tree_store_prepend(imailbox->ts, &itr, parent);
    else
        gtk_tree_store_append(imailbox->ts, &itr, parent);
    
    gtk_tree_store_set(imailbox->ts, &itr,
            IMAP_FOLDERS_NAME, fdata->folder_name,
            IMAP_FOLDERS_WATCHING,
              (is_inbox || g_hash_table_lookup(mailboxes_to_check, fdata->full_path)),
            IMAP_FOLDERS_HOLDS_MESSAGES, fdata->holds_messages,
            IMAP_FOLDERS_FULLPATH, fdata->full_path,
            -1);
    
    node->data = NULL;
    g_free(fdata->folder_name);
    g_free(fdata->full_path);
    g_free(fdata);
    
    for(n = node->children; n; n = n->next)
        imap_populate_folder_tree_nodes_rec(imailbox, mailboxes_to_check, n, &itr);
}

static gboolean
imap_populate_folder_tree_nodes(gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    GHashTable *mailboxes_to_check;
    GList *l;
    GNode *n;
    
    g_mutex_lock(imailbox->config_mx);
    
    /* make a deep copy of the mailbox list */
    mailboxes_to_check = g_hash_table_new_full(g_str_hash, g_str_equal,
            (GDestroyNotify)g_free, NULL);
    for(l = imailbox->mailboxes_to_check; l; l = l->next)
        g_hash_table_insert(mailboxes_to_check, g_strdup(l->data), GINT_TO_POINTER(1));
    
    g_mutex_unlock(imailbox->config_mx);
    
    gtk_tree_store_clear(imailbox->ts);
    g_object_set(G_OBJECT(imailbox->render), "foreground-set", FALSE,
            "style-set", FALSE, NULL);
    
    for(n = imailbox->folder_tree->children; n; n = n->next)
        imap_populate_folder_tree_nodes_rec(imailbox, mailboxes_to_check, n, NULL);
    
    g_node_destroy(imailbox->folder_tree);
    imailbox->folder_tree = NULL;
    
    g_hash_table_destroy(mailboxes_to_check);
    
    return FALSE;
}

static gpointer
imap_populate_folder_tree_th(gpointer data)
{
#define BUFSIZE 1024
    XfceMailwatchIMAPMailbox *imailbox = data;
    gchar host[BUFSIZE], username[BUFSIZE], password[BUFSIZE];
    
    TRACE("entering");
    
    g_mutex_lock(imailbox->config_mx);
    
    if(!imailbox->host || !imailbox->username || !imailbox->password) {
        g_mutex_unlock(imailbox->config_mx);
        gtk_widget_set_sensitive(imailbox->refresh_btn, TRUE);
        return NULL;
    }
    
    g_strlcpy(host, imailbox->host, BUFSIZE);
    g_strlcpy(username, imailbox->username, BUFSIZE);
    g_strlcpy(password, imailbox->password, BUFSIZE);
    imailbox->requiring_ssl = imailbox->require_ssl;
    
    g_mutex_unlock(imailbox->config_mx);
    
    if(imap_authenticate(imailbox, host, username, password)) {
        imailbox->folder_tree = g_node_new((gpointer)0xdeadbeef);
        imap_populate_folder_tree(imailbox, "", imailbox->folder_tree);
        g_idle_add(imap_populate_folder_tree_nodes, imailbox);
    } else {
        DBG("failed to connect to imap server to probe folders");
    }
    
    gtk_widget_set_sensitive(imailbox->refresh_btn, TRUE);
    
    return NULL;
#undef BUFSIZE
}

static void
imap_config_refresh_btn_clicked_cb(GtkWidget *w, gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    GtkTreeIter itr;
    
    if(!imailbox->host || !imailbox->username)
        return;
    
    gtk_widget_set_sensitive(imailbox->refresh_btn, FALSE);
    
    gtk_tree_store_clear(imailbox->ts);
    gtk_tree_store_append(imailbox->ts, &itr, NULL);
    gtk_tree_store_set(imailbox->ts, &itr, IMAP_FOLDERS_NAME,
            _("Please wait..."), -1);
    g_object_set(G_OBJECT(imailbox->render),
                "foreground-set", TRUE,
                "style-set", TRUE, NULL);
    
    g_thread_create(imap_populate_folder_tree_th, imailbox, FALSE, NULL);
}

static gboolean
imap_config_treeview_btnpress_cb(GtkWidget *w, GdkEventButton *evt,
        gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    GtkTreeViewColumn *col = NULL;
    GtkTreePath *path = NULL;
    
    if(!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(w), evt->x, evt->y, &path, &col, NULL, NULL))
        return FALSE;
    
    if(col == gtk_tree_view_get_column(GTK_TREE_VIEW(w), 1)) {
        GtkTreeIter itr;
        
        if(gtk_tree_model_get_iter(GTK_TREE_MODEL(imailbox->ts), &itr, path)) {
            gchar *folder_name = NULL, *folder_path = NULL;
            gboolean watching = FALSE;
            
            gtk_tree_model_get(GTK_TREE_MODEL(imailbox->ts), &itr,
                    IMAP_FOLDERS_NAME, &folder_name,
                    IMAP_FOLDERS_WATCHING, &watching,
                    IMAP_FOLDERS_FULLPATH, &folder_path, -1);
            
            if(!g_ascii_strcasecmp(folder_name, "inbox")) {
                gtk_tree_store_set(imailbox->ts, &itr,
                        IMAP_FOLDERS_WATCHING, !watching, -1);
                
                g_mutex_lock(imailbox->config_mx);
                if(watching) {
                    GList *l;
                    for(l = imailbox->mailboxes_to_check; l; l = l->next) {
                        if(!strcmp(folder_path, l->data)) {
                            g_free(l->data);
                            imailbox->mailboxes_to_check =
                                    g_list_delete_link(imailbox->mailboxes_to_check, l);
                            break;
                        }
                    }
                    g_free(folder_path);
                } else {
                    imailbox->mailboxes_to_check =
                            g_list_prepend(imailbox->mailboxes_to_check,
                                    folder_path);
                }
                g_mutex_unlock(imailbox->config_mx);
            } else
                g_free(folder_path);
            
            g_free(folder_name);
        }
    }
    
    
    if(evt->type == GDK_2BUTTON_PRESS) {
        if(!gtk_tree_view_row_expanded(GTK_TREE_VIEW(w), path))
            gtk_tree_view_expand_row(GTK_TREE_VIEW(w), path, FALSE);
        else
            gtk_tree_view_collapse_row(GTK_TREE_VIEW(w), path);
    }
    
    gtk_tree_path_free(path);
    
    return FALSE;
}

static gboolean
imap_config_serverdir_focusout_cb(GtkWidget *w, GdkEventFocus *evt,
        gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    
    g_mutex_lock(imailbox->config_mx);
    
    g_free(imailbox->server_directory);
    imailbox->server_directory = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    
    g_mutex_unlock(imailbox->config_mx);
    
    return FALSE;
}

static void
imap_config_advanced_btn_clicked_cb(GtkWidget *w, gpointer user_data)
{
    XfceMailwatchIMAPMailbox *imailbox = user_data;
    GtkWidget *dlg, *vbox, *hbox, *lbl, *entry;
    
    dlg = gtk_dialog_new_with_buttons(_("Advanced IMAP Options"),
            GTK_WINDOW(gtk_widget_get_toplevel(w)),
            GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_NO_SEPARATOR,
            GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT, NULL);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), BORDER/2);
    gtk_widget_show(vbox);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), vbox, TRUE, TRUE, 0);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("IMAP server _directory:"));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    
    entry = gtk_entry_new();
    g_mutex_lock(imailbox->config_mx);
    if(imailbox->server_directory)
        gtk_entry_set_text(GTK_ENTRY(entry), imailbox->server_directory);
    g_mutex_unlock(imailbox->config_mx);
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
            G_CALLBACK(imap_config_serverdir_focusout_cb), imailbox);
    
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static GtkContainer *
imap_get_setup_page(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchIMAPMailbox *imailbox = XFCE_MAILWATCH_IMAP_MAILBOX(mailbox);
    GtkWidget *topvbox, *vbox, *hbox, *frame, *lbl, *entry, *chk, *sw,
            *treeview, *btn, *sbtn;
    GtkSizeGroup *sg;
    GtkTreeStore *ts;
    GtkTreeIter itr;
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
    
    chk = gtk_check_button_new_with_mnemonic(_("Require _secure connection"));
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
    
    imailbox->ts = ts = gtk_tree_store_new(IMAP_FOLDERS_N_COLUMNS,
            G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_STRING);
    
    treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ts));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);
    gtk_widget_add_events(treeview, GDK_BUTTON_PRESS);
    
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "mailbox-name");
    gtk_tree_view_column_set_expand(col, TRUE);
    
    render = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, render, FALSE);
#if GTK_CHECK_VERSION(2, 6, 0)
    g_object_set(G_OBJECT(render), "stock-id", GTK_STOCK_DIRECTORY,
            "stock-size", GTK_ICON_SIZE_MENU, NULL);
#else
    {
        gint iw, ih;
        GdkPixbuf *pix;
        GList *icons = NULL;
        GdkScreen *gscreen = gtk_widget_get_screen(treeview);
        XfceIconTheme *itheme = xfce_icon_theme_get_for_screen(gscreen);
        
        icons = g_list_prepend(icons, "stock_open");
        icons = g_list_prepend(icons, "stock_folder");
        icons = g_list_prepend(icons, "stock_directory");
        
        gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &iw, &ih);
        pix = xfce_icon_theme_load_list(itheme, icons, iw);
        if(pix) {
            g_object_set(G_OBJECT(render), "pixbuf", pix, NULL);
            g_object_unref(G_OBJECT(pix));
        }
        
        g_list_free(icons);
    }
#endif
    
    imailbox->render = render = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, render, TRUE);
    gtk_tree_view_column_set_attributes(col, render,
            "text", IMAP_FOLDERS_NAME, NULL);
    /*col = gtk_tree_view_column_new_with_attributes("mailbox-name", render,
            "text", IMAP_FOLDERS_NAME, NULL);*/
    {
        GtkStyle *style;
        gtk_widget_realize(topvbox);
        style = gtk_widget_get_style(topvbox);
        g_object_set(G_OBJECT(render), "foreground-gdk",
                &style->fg[GTK_STATE_INSENSITIVE],
                "foreground-set", TRUE,
                "style", PANGO_STYLE_ITALIC,
                "style-set", TRUE, NULL);
    }
    
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
    gtk_tree_view_set_expander_column(GTK_TREE_VIEW(treeview), col);
    
    render = gtk_cell_renderer_toggle_new();
    col = gtk_tree_view_column_new_with_attributes("watching", render, "active",
            IMAP_FOLDERS_WATCHING, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
    
    gtk_widget_show(treeview);
    gtk_container_add(GTK_CONTAINER(sw), treeview);
    g_signal_connect(G_OBJECT(treeview), "button-press-event",
            G_CALLBACK(imap_config_treeview_btnpress_cb), imailbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), treeview);
    
    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
    gtk_tree_selection_unselect_all(sel);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(vbox);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);
    
    imailbox->refresh_btn = btn = gtk_button_new_from_stock(GTK_STOCK_REFRESH);
    gtk_widget_show(btn);
    gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(btn), "mailwatch-treeview", treeview);
    g_signal_connect(G_OBJECT(btn), "clicked",
            G_CALLBACK(imap_config_refresh_btn_clicked_cb), imailbox);
    
    if(imailbox->host && imailbox->username) {
        gtk_tree_store_append(ts, &itr, NULL);
        gtk_tree_store_set(ts, &itr, IMAP_FOLDERS_NAME, _("Please wait..."), -1);
        gtk_widget_set_sensitive(btn, FALSE);
        g_thread_create(imap_populate_folder_tree_th, imailbox, FALSE, NULL);
    }
    
    btn = xfce_mailwatch_custom_button_new(_("_Advanced..."),
            GTK_STOCK_PREFERENCES);
    gtk_widget_show(btn);
    gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
            G_CALLBACK(imap_config_advanced_btn_clicked_cb), imailbox);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(topvbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("Check for _new messages every"));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    
    sbtn = gtk_spin_button_new_with_range(1.0, 1440.0, 1.0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(sbtn), TRUE);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(sbtn), FALSE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sbtn), imailbox->timeout/60);
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
        else if(!strcmp(param->key, "server_directory"))
            imailbox->server_directory = g_strdup(param->value);
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
    param->key = g_strdup("server_directory");
    param->value = g_strdup(imailbox->server_directory);
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
    imap_force_update_cb,
    imap_get_setup_page,
    imap_restore_param_list,
    imap_save_param_list,
    imap_mailbox_free
};
