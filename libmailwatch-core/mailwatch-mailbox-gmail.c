/*
 *  xfce4-mailwatch-plugin - a mail notification applet for the xfce4 panel
 *  Copyright (c) 2005-2008 Brian Tarricone <bjt23@cornell.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License ONLY.
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
#include "mailwatch.h"
#include "mailwatch-net-conn.h"

#define BORDER         8
#define GMAIL_HOST     "mail.google.com"
#define GMAIL_ATOMURI  "/mail/feed/atom"
#define XFCE_MAILWATCH_GMAIL_MAILBOX(ptr)  ((XfceMailwatchGMailMailbox *)ptr)

typedef struct
{
    XfceMailwatchMailbox mailbox;
    
    GMutex *config_mx;
    
    gchar *username;
    gchar *password;
    guint timeout;
    
    XfceMailwatch *mailwatch;
    
    /* current connection state */
    gint running;
    gpointer th;
    XfceMailwatchNetConn *net_conn;
    guint check_id;
} XfceMailwatchGMailMailbox;


static gboolean
gmail_should_continue(XfceMailwatchNetConn *net_conn,
                      gpointer user_data)
{
    XfceMailwatchGMailMailbox *gmailbox = user_data;
    return g_atomic_int_get(&gmailbox->running);
}

static gssize
gmail_send(XfceMailwatchGMailMailbox *gmailbox, const gchar *buf)
{
    GError *error = NULL;
    gssize sent;
    
    sent = xfce_mailwatch_net_conn_send_data(gmailbox->net_conn,
                                             (const guchar *)buf, -1,
                                             &error);
    if(sent < 0) {
        xfce_mailwatch_log_message(gmailbox->mailwatch,
                                   XFCE_MAILWATCH_MAILBOX(gmailbox),
                                   XFCE_MAILWATCH_LOG_ERROR,
                                   error->message);
        g_error_free(error);
    }
    
    return sent;
}

static gssize
gmail_recv(XfceMailwatchGMailMailbox *gmailbox, gchar *buf, gsize len)
{
    GError *error = NULL;
    gssize recvd;
    
    recvd = xfce_mailwatch_net_conn_recv_data(gmailbox->net_conn,
                                              (guchar *)buf, len, &error);
    if(recvd < 0) {
        xfce_mailwatch_log_message(gmailbox->mailwatch,
                                   XFCE_MAILWATCH_MAILBOX(gmailbox),
                                   XFCE_MAILWATCH_LOG_ERROR,
                                   error->message);
        g_error_free(error);
    }
    
    return recvd;
}

static gboolean
gmail_connect(XfceMailwatchGMailMailbox *gmailbox, gint *port)
{
    GError *error = NULL;

    TRACE("entering");
    
    g_return_val_if_fail(port, FALSE);
    
    gmailbox->net_conn = xfce_mailwatch_net_conn_new(GMAIL_HOST, "https");
    xfce_mailwatch_net_conn_set_should_continue_func(gmailbox->net_conn,
                                                     gmail_should_continue,
                                                     gmailbox);
    
    if(xfce_mailwatch_net_conn_connect(gmailbox->net_conn, &error)) {
        *port = xfce_mailwatch_net_conn_get_port(gmailbox->net_conn);
        return TRUE;
    } else {
        xfce_mailwatch_log_message(gmailbox->mailwatch,
                                   XFCE_MAILWATCH_MAILBOX(gmailbox),
                                   XFCE_MAILWATCH_LOG_ERROR,
                                   "%s", error->message);
        g_error_free(error);
        return FALSE;
    }
}

static gboolean
gmail_check_atom_feed(XfceMailwatchGMailMailbox *gmailbox,
                      const gchar *username,
                      const gchar *password,
                      guint *new_messages)
{
#define BUFSIZE 8191
    gboolean ret = FALSE, first_recv = TRUE;
    GError *error = NULL;
    gchar buf[BUFSIZE+1], *base64_creds, *p, *q;
    gint bin, port = 0, respcode, tmp;
    
    if(!gmail_connect(gmailbox, &port)) {
        DBG("failed to connect to gmail server");
        return FALSE;
    }
    
    if(!xfce_mailwatch_net_conn_make_secure(gmailbox->net_conn, &error)) {
        xfce_mailwatch_log_message(gmailbox->mailwatch,
                                   XFCE_MAILWATCH_MAILBOX(gmailbox),
                                   XFCE_MAILWATCH_LOG_ERROR,
                                   _("TLS handshake failed: %s"),
                                   error->message);
        g_error_free(error);
        goto cleanup;
    }
    
    g_snprintf(buf, BUFSIZE, "%s:%s", username, password);
    if(xfce_mailwatch_base64_encode((guchar *)buf, strlen(buf), &base64_creds) <= 0) {
        DBG("failed to base64 enc credentials");
        goto cleanup;
    }
    
    g_snprintf(buf, BUFSIZE, "GET %s HTTP/1.1\r\n" \
                             "Host: %s:%d\r\n" \
                             "User-Agent: %s/%s\r\n" \
                             "Authorization: Basic %s\r\n" \
                             "Connection: close\r\n" \
                             "\r\n",
               GMAIL_ATOMURI, GMAIL_HOST, port, PACKAGE, VERSION, base64_creds);
    g_free(base64_creds);
    
    if(gmail_send(gmailbox, buf) != strlen(buf)) {
        DBG("failed to send req");
        goto cleanup;
    }
    
    for(;;) {
        if(!gmail_should_continue(gmailbox->net_conn, gmailbox))
            break;

        bin = gmail_recv(gmailbox, buf, BUFSIZE);
        if(bin <= 0) {
            DBG("failed to recv response (%d)", bin);
            break;
        }
        
        if(first_recv) {
            p = strstr(buf, " ");
            DBG("got first space");
            if(p) {
                q = strstr(p+1, " ");
                if(q) {
                    DBG("got second space");
                    *q = 0;
                    respcode = atoi(p+1);
                    DBG("response code is %d", respcode);
                    if(respcode != 200) {
                        if(respcode == 403 || respcode == 401) {
                            xfce_mailwatch_log_message(gmailbox->mailwatch,
                                                       XFCE_MAILWATCH_MAILBOX(gmailbox),
                                                       XFCE_MAILWATCH_LOG_ERROR,
                                                       _("Received HTTP response code %d.  The most likely reason for this is that your GMail username or password is incorrect."),
                                                       respcode);
                        } else {
                            xfce_mailwatch_log_message(gmailbox->mailwatch,
                                                       XFCE_MAILWATCH_MAILBOX(gmailbox),
                                                       XFCE_MAILWATCH_LOG_ERROR,
                                                       _("Received HTTP response code %d, which should be 200.  There may be a problem with GMail's servers, or they have incompatibly changed their authentication method or location of the new messages feed."),
                                                       respcode);
                        }
                        break;
                    }
                    *q = ' ';
                }
            }
            first_recv = FALSE;
        }
        
        p = strstr(buf, "<fullcount>");
        if(!p)
            continue;
        
        DBG("got opening <fullcount> tag: '%s'", p);
        
        q = strstr(p+1, "<");
        if(!q) {
            gchar buf1[1024];
            bin = gmail_recv(gmailbox, buf1, BUFSIZE);
            if(bin <= 0) {
                DBG("failed to recv response (%d)", bin);
                break;
            }
            
            q = strstr(buf1, "<");
            if(!q) {
                DBG("can't find </fullcount> closing tag");
                break;
            }
            
            memmove(buf, p, strlen(p));
            memcpy(buf+strlen(p), buf1, strlen(buf1));
            buf[strlen(p)+strlen(buf1)] = 0;
            p = buf;
            q = strstr(p+1, "<");
        }
        
        DBG("p=%p, q=%p", p, q);
        
        *q = 0;
        p += 11;
        if(p >= q) {
            DBG("that's not right...");
            break;
        }
        
        tmp = atoi(p);
        if(tmp < 0) {
            DBG("new message count is <0");
            break;
        }
        
        *new_messages = tmp;
        ret = TRUE;
        break;
    }
    
cleanup:
    
    if(gmailbox->net_conn) {
        xfce_mailwatch_net_conn_destroy(gmailbox->net_conn);
        gmailbox->net_conn = NULL;
    }
    
    return ret;
#undef BUFSIZE
}

static void
gmail_check_mail(XfceMailwatchGMailMailbox *gmailbox)
{
#define BUFSIZE 1024
    gchar username[BUFSIZE], password[BUFSIZE];
    guint new_messages = 0;
    
    g_mutex_lock(gmailbox->config_mx);
    
    if(!gmailbox->username || !gmailbox->password) {
        g_mutex_unlock(gmailbox->config_mx);
        return;
    }
    
    g_strlcpy(username, gmailbox->username, BUFSIZE);
    g_strlcpy(password, gmailbox->password, BUFSIZE);
    
    g_mutex_unlock(gmailbox->config_mx);
    
    if(gmail_check_atom_feed(gmailbox, username, password, &new_messages)) {
        DBG("checked gmail, %u new messages", new_messages);
        xfce_mailwatch_signal_new_messages(gmailbox->mailwatch,
                                           XFCE_MAILWATCH_MAILBOX(gmailbox),
                                           new_messages);
    } else {
        DBG("failed to connect to gmail server");
    }
#undef BUFSIZE
}

static gpointer
gmail_check_mail_th(gpointer data)
{
    XfceMailwatchGMailMailbox *gmailbox = XFCE_MAILWATCH_GMAIL_MAILBOX(data);
    
    while(!g_atomic_pointer_get(&gmailbox->th)
          && g_atomic_int_get(&gmailbox->running))
    {
        g_thread_yield();
    }

    if(!g_atomic_int_get(&gmailbox->running)) {
        g_atomic_pointer_set(&gmailbox->th, NULL);
        return NULL;
    }
    
    gmail_check_mail(gmailbox);

    g_atomic_pointer_set(&gmailbox->th, NULL);
    return NULL;
}

static gboolean
gmail_check_mail_timeout(gpointer data)
{
    XfceMailwatchGMailMailbox *gmailbox = XFCE_MAILWATCH_GMAIL_MAILBOX(data);

    GThread *th;

    if(g_atomic_pointer_get(&gmailbox->th)) {
        xfce_mailwatch_log_message(gmailbox->mailwatch,
                                   XFCE_MAILWATCH_MAILBOX(gmailbox),
                                   XFCE_MAILWATCH_LOG_WARNING,
                                   _("Previous thread hasn't exited yet, not checking mail this time."));
        return TRUE;
    }

    th = g_thread_create(gmail_check_mail_th, gmailbox, FALSE, NULL);
    g_atomic_pointer_set(&gmailbox->th, th);

    return TRUE;
}

static XfceMailwatchMailbox *
gmail_mailbox_new(XfceMailwatch *mailwatch, XfceMailwatchMailboxType *type)
{
    XfceMailwatchGMailMailbox *gmailbox = g_new0(XfceMailwatchGMailMailbox, 1);
    gmailbox->mailbox.type = type;
    gmailbox->mailwatch = mailwatch;
    gmailbox->timeout = XFCE_MAILWATCH_DEFAULT_TIMEOUT;
    gmailbox->config_mx = g_mutex_new();

    xfce_mailwatch_net_conn_init();
    
    return (XfceMailwatchMailbox *)gmailbox;
}

static void
gmail_set_activated(XfceMailwatchMailbox *mailbox, gboolean activated)
{
    XfceMailwatchGMailMailbox *gmailbox = XFCE_MAILWATCH_GMAIL_MAILBOX(mailbox);

    if(activated == g_atomic_int_get(&gmailbox->running))
        return;

    if(activated) {
        g_atomic_int_set(&gmailbox->running, TRUE);
        gmailbox->check_id = g_timeout_add(gmailbox->timeout * 1000,
                                           gmail_check_mail_timeout,
                                           gmailbox);
    } else {
        g_atomic_int_set(&gmailbox->running, FALSE);
        g_source_remove(gmailbox->check_id);
        gmailbox->check_id = 0;
    }
}

static void
gmail_force_update_cb(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchGMailMailbox *gmailbox = XFCE_MAILWATCH_GMAIL_MAILBOX(mailbox);

    if(!g_atomic_pointer_get(&gmailbox->th)) {
        gboolean restart = FALSE;

        if(gmailbox->check_id) {
            g_source_remove(gmailbox->check_id);
            restart = TRUE;
        }

        gmail_check_mail_timeout(gmailbox);

        if(restart) {
            gmailbox->check_id = g_timeout_add(gmailbox->timeout * 1000,
                                               gmail_check_mail_timeout,
                                               gmailbox);
        }
    }
}

static gboolean
gmail_config_username_focus_out_cb(GtkWidget *w,
                                   GdkEventFocus *evt,
                                   gpointer user_data)
{
    XfceMailwatchGMailMailbox *gmailbox = XFCE_MAILWATCH_GMAIL_MAILBOX(user_data);
    
    g_mutex_lock(gmailbox->config_mx);
    
    g_free(gmailbox->username);
    gmailbox->username = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    
    g_mutex_unlock(gmailbox->config_mx);
    
    return FALSE;
}

static gboolean
gmail_config_password_focus_out_cb(GtkWidget *w,
                                   GdkEventFocus *evt,
                                   gpointer user_data)
{
    XfceMailwatchGMailMailbox *gmailbox = XFCE_MAILWATCH_GMAIL_MAILBOX(user_data);
    
    g_mutex_lock(gmailbox->config_mx);
    
    g_free(gmailbox->password);
    gmailbox->password = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    
    g_mutex_unlock(gmailbox->config_mx);
    
    return FALSE;
}

static gboolean
gmail_config_timeout_spinbutton_changed_cb(GtkSpinButton *sb,
                                           gpointer user_data)
{
    XfceMailwatchGMailMailbox *gmailbox = XFCE_MAILWATCH_GMAIL_MAILBOX(user_data);
    gint value = gtk_spin_button_get_value_as_int(sb) * 60;
    
    if(value == gmailbox->timeout)
        return FALSE;

    gmailbox->timeout = value;

    if(g_atomic_int_get(&gmailbox->running)) {
        /* probably shouldn't do this so frequently */
        if(gmailbox->check_id)
            g_source_remove(gmailbox->check_id);
        gmailbox->check_id = g_timeout_add(gmailbox->timeout * 1000,
                                           gmail_check_mail_timeout,
                                           gmailbox);
    }

    return FALSE;
}

static GtkContainer *
gmail_get_setup_page(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchGMailMailbox *gmailbox = XFCE_MAILWATCH_GMAIL_MAILBOX(mailbox);
    GtkWidget *vbox, *hbox, *lbl, *entry, *sbtn;
    GtkSizeGroup *sg;
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(vbox);
    
    sg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("_Username:"));
    gtk_misc_set_alignment(GTK_MISC(lbl), 0.0, 0.5);
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    gtk_size_group_add_widget(sg, lbl);
    
    entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    if(gmailbox->username)
        gtk_entry_set_text(GTK_ENTRY(entry), gmailbox->username);
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
                     G_CALLBACK(gmail_config_username_focus_out_cb), gmailbox);
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
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    if(gmailbox->password)
        gtk_entry_set_text(GTK_ENTRY(entry), gmailbox->password);
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
                     G_CALLBACK(gmail_config_password_focus_out_cb), gmailbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), entry);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    lbl = gtk_label_new_with_mnemonic(_("Check for _new messages every"));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);

    sbtn = gtk_spin_button_new_with_range(1.0, 1440.0, 1.0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(sbtn), TRUE);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(sbtn), FALSE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sbtn), gmailbox->timeout/60);
    gtk_widget_show(sbtn);
    gtk_box_pack_start(GTK_BOX(hbox), sbtn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(sbtn), "value-changed",
                     G_CALLBACK(gmail_config_timeout_spinbutton_changed_cb),
                     gmailbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), sbtn);

    lbl = gtk_label_new(_("minute(s)."));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    
    return GTK_CONTAINER(vbox);
}

static void
gmail_restore_param_list(XfceMailwatchMailbox *mailbox, GList *params)
{
    XfceMailwatchGMailMailbox *gmailbox = XFCE_MAILWATCH_GMAIL_MAILBOX(mailbox);
    GList *l;
    
    g_mutex_lock(gmailbox->config_mx);
    
    for(l = params; l; l = l->next) {
        XfceMailwatchParam *param = l->data;
        
        if(!strcmp(param->key, "username"))
            gmailbox->username = g_strdup(param->value);
        else if(!strcmp(param->key, "password"))
            gmailbox->password = g_strdup(param->value);
        else if(!strcmp(param->key, "timeout"))
            gmailbox->timeout = atoi(param->value);
    }
    
    g_mutex_unlock(gmailbox->config_mx);
}

static GList *
gmail_save_param_list(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchGMailMailbox *gmailbox = XFCE_MAILWATCH_GMAIL_MAILBOX(mailbox);
    GList *params = NULL;
    XfceMailwatchParam *param;
    
    g_mutex_lock(gmailbox->config_mx);
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("username");
    param->value = g_strdup(gmailbox->username);
    params = g_list_prepend(params, param);
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("password");
    param->value = g_strdup(gmailbox->password);
    params = g_list_prepend(params, param);
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("timeout");
    param->value = g_strdup_printf("%u", gmailbox->timeout);
    params = g_list_prepend(params, param);
    
    g_mutex_unlock(gmailbox->config_mx);
    
    return g_list_reverse(params);
}

static void
gmail_mailbox_free(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchGMailMailbox *gmailbox = XFCE_MAILWATCH_GMAIL_MAILBOX(mailbox);
    
    if(gmailbox->check_id) {
        g_source_remove(gmailbox->check_id);
        gmailbox->check_id = 0;
    }

    g_atomic_int_set(&gmailbox->running, FALSE);
    while(g_atomic_pointer_get(&gmailbox->th))
        g_thread_yield();
    
    g_mutex_free(gmailbox->config_mx);
    
    g_free(gmailbox->username);
    g_free(gmailbox->password);
    
    g_free(gmailbox);
}

XfceMailwatchMailboxType builtin_mailbox_type_gmail = {
    "gmail",
    N_("Remote GMail Mailbox"),
    N_("The GMail plugin can connect to Google's mail service and securely retrieve the number of new messages."),

    gmail_mailbox_new,
    gmail_set_activated,
    gmail_force_update_cb,
    gmail_get_setup_page,
    gmail_restore_param_list,
    gmail_save_param_list,
    gmail_mailbox_free
};
