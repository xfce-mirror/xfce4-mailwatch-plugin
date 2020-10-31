/*
 *  xfce4-mailwatch-plugin - a mail notification applet for the xfce4 panel
 *  Copyright (c) 2005-2008 Brian Tarricone <bjt23@cornell.edu>
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

#include <glib.h>
#include <gtk/gtk.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfce4ui/libxfce4ui.h>

#include "mailwatch-utils.h"
#include "mailwatch.h"
#include "mailwatch-net-conn.h"

#define BORDER                   8

#define POP3_PORT_S              "110"
#define POP3S_PORT_S             "995"

#define XFCE_MAILWATCH_POP3_MAILBOX(ptr) ((XfceMailwatchPOP3Mailbox *)ptr)

typedef struct
{
    XfceMailwatchMailbox mailbox;
    
    GMutex config_mx;
    
    guint timeout;
    gchar *host;
    gchar *username;
    gchar *password;
    
    gboolean use_standard_port;
    gint nonstandard_port;
    XfceMailwatchAuthType auth_type;
    
    gint running;
    guint check_id;
    gpointer th;  /* really a GThread *, but avoids casts later */
    
    XfceMailwatch *mailwatch;
    
    /* state related to the current connection (if any) */
    XfceMailwatchNetConn *net_conn;
} XfceMailwatchPOP3Mailbox;


static gboolean
pop3_should_continue(XfceMailwatchNetConn *net_conn,
                     gpointer user_data)
{
    XfceMailwatchPOP3Mailbox *pmailbox = user_data;
    return g_atomic_int_get(&pmailbox->running);
}

static gssize
pop3_send(XfceMailwatchPOP3Mailbox *pmailbox, const gchar *buf)
{
    GError *error = NULL;
    gssize sent;
    
    sent = xfce_mailwatch_net_conn_send_data(pmailbox->net_conn,
                                             (guchar *)buf, strlen(buf),
                                             &error);
    if(sent < 0) {
        xfce_mailwatch_log_message(pmailbox->mailwatch,
                                   XFCE_MAILWATCH_MAILBOX(pmailbox),
                                   XFCE_MAILWATCH_LOG_ERROR,
                                   error->message);
        g_error_free(error);
    }
    
    return sent;
}

static gssize
pop3_recv(XfceMailwatchPOP3Mailbox *pmailbox, gchar *buf, gsize len)
{
    GError *error = NULL;
    gssize recvd;
    
    recvd = xfce_mailwatch_net_conn_recv_line(pmailbox->net_conn,
                                              buf, len, &error);
    
    if(recvd < 0) {
        xfce_mailwatch_log_message(pmailbox->mailwatch,
                                   XFCE_MAILWATCH_MAILBOX(pmailbox),
                                   XFCE_MAILWATCH_LOG_ERROR,
                                   error->message);
        g_error_free(error);
    }

    if((gsize)recvd == len)
        return -1;

    buf[recvd] = '\n';
    buf[++recvd] = 0;
    
    return recvd;
}

static gssize
pop3_recv_command(XfceMailwatchPOP3Mailbox *pmailbox,
                  gchar *buf,
                  gsize len,
                  gboolean multiline)
{
    gssize bin, tot = 0;
    gboolean got_ok = FALSE;

    if(len)
        *buf = 0;

    while(len - tot > 0) {
        DBG("trying to get line");
        bin = pop3_recv(pmailbox, buf+tot, len-tot);
        DBG("got line: %s", bin > 0 ? buf+tot : "(nada)");
        if(bin <= 0)
            return -1;
        if(!strncmp(buf+tot, "-ERR", 4))
            return -1;

        if(multiline && got_ok) {
            if(!strcmp(buf+tot, ".\n"))
                return tot + bin;
        } else if(!strncmp(buf+tot, "+OK", 3)) {
            if(!multiline)
                return tot + bin;
            got_ok = TRUE;
        }

        tot += bin;

        if(!xfce_mailwatch_net_conn_should_continue(pmailbox->net_conn))
            return -1;
    }

    g_critical("pop3_recv_command(): buffer full!");

    return -1;
}

static gboolean
pop3_send_login_info(XfceMailwatchPOP3Mailbox *pmailbox, const gchar *username,
        const gchar *password)
{
#define BUFSIZE 8191
    gint bin, bout;
    gchar buf[BUFSIZE+1];
    
#ifdef HAVE_SSL_SUPPORT
    gchar *p, *q;
    
    /* see if CRAM-MD5 is supported */
    g_strlcpy(buf, "CAPA\r\n", BUFSIZE);
    bout = pop3_send(pmailbox, buf);
    if(bout != (gint)strlen(buf))
        return FALSE;

    bin = pop3_recv_command(pmailbox, buf, BUFSIZE, TRUE);
    if(bin < 0)
        return FALSE;

    if((p = strstr(buf, "SASL ")) && (q = strstr(p, "\n"))
       && (p = strstr(p, "CRAM-MD5")) && p < q)
    {
        /* server supports CRAM-MD5 */
        g_strlcpy(buf, "AUTH CRAM-MD5\r\n", BUFSIZE);
        bout = pop3_send(pmailbox, buf);
        if(bout != (gint)strlen(buf))
            return FALSE;

        bin = pop3_recv(pmailbox, buf, BUFSIZE);
        if(bin <= 0)
            return FALSE;
        DBG("got cram-md5 challenge: %s\n", buf);

        if(*buf == '+' && *(buf+1) == ' ' && *(buf+2)) {
            gchar *response_base64;

            p = strstr(buf, "\n");
            if(!p) {
                DBG("cram-md5 challenge wasn't a full line?");
                return FALSE;
            }
            *p = 0;

            response_base64 = xfce_mailwatch_cram_md5(username, password,
                                                      buf + 2);
            if(!response_base64)
                return FALSE;
            g_strlcpy(buf, response_base64, BUFSIZE);
            g_strlcat(buf, "\r\n", BUFSIZE);
            g_free(response_base64);
            bout = pop3_send(pmailbox, buf);
            if(bout != (gint)strlen(buf))
                return FALSE;

            bin = pop3_recv_command(pmailbox, buf, BUFSIZE, FALSE);
            if(bin <= 0) {
                if(bin < 0) {
                    if(strstr(buf, "-ERR")) {
                        xfce_mailwatch_log_message(pmailbox->mailwatch,
                                                   XFCE_MAILWATCH_MAILBOX(pmailbox),
                                                   XFCE_MAILWATCH_LOG_ERROR,
                                                   _("Authentication failed.  Perhaps your username or password is incorrect?"));
                    }
                }
                return FALSE;
            }

            TRACE("leaving (success)");

            return TRUE;
        }
    }
#endif

    /* send the username */
    g_snprintf(buf, BUFSIZE, "USER %s\r\n", username);
    bout = pop3_send(pmailbox, buf);
    DBG("sent user (%d)", bout);
    if(bout != (gint)strlen(buf))
        return FALSE;
    
    /* check for OK response */
    bin = pop3_recv_command(pmailbox, buf, BUFSIZE, FALSE);
    DBG("response from USER (%d): %s", bin, bin>0?buf:"(nada)");
    if(bin <= 0)
        return FALSE;
    
    /* send the password */
    g_snprintf(buf, BUFSIZE, "PASS %s\r\n", password);
    bout = pop3_send(pmailbox, buf);
    DBG("sent password (%d)", bout);
    if(bout != (gint)strlen(buf))
        return FALSE;
    
    /* check for OK response */
    bin = pop3_recv_command(pmailbox, buf, BUFSIZE, FALSE);
    DBG("response from PASS (%d): %s", bin, bin>0?buf:"(nada)");
    if(bin <= 0) {
        if(bin < 0) {
            if(strstr(buf, "-ERR")) {
                xfce_mailwatch_log_message(pmailbox->mailwatch,
                                           XFCE_MAILWATCH_MAILBOX(pmailbox),
                                           XFCE_MAILWATCH_LOG_ERROR,
                                           _("Authentication failed.  Perhaps your username or password is incorrect?"));
            }
        }
        return FALSE;
    }
    
    TRACE("leaving (success)");
    
    return TRUE;
#undef BUFSIZE
}

static gboolean
pop3_negotiate_ssl(XfceMailwatchPOP3Mailbox *pmailbox, const gchar *host)
{
    gboolean ret;
    GError *error = NULL;
    
    ret = xfce_mailwatch_net_conn_make_secure(pmailbox->net_conn, &error);
    
    if(!ret) {
        xfce_mailwatch_log_message(pmailbox->mailwatch,
                                   XFCE_MAILWATCH_MAILBOX(pmailbox),
                                   XFCE_MAILWATCH_LOG_ERROR,
                                   _("TLS handshake failed: %s"),
                                   error->message);
        g_error_free(error);
    }
    
    return ret;
}

static gboolean
pop3_do_stls(XfceMailwatchPOP3Mailbox *pmailbox, const gchar *host,
        const gchar *username, const gchar *password)
{
#define BUFSIZE 8191
    gint bin;
    gchar buf[BUFSIZE+1];
    
    TRACE("entering");
    
    if(pop3_send(pmailbox, "CAPA\r\n") != 6)
        return FALSE;
    
    bin = pop3_recv_command(pmailbox, buf, BUFSIZE, TRUE);
    DBG("checking for STLS caps (%d): %s", bin, bin>0?buf:"(nada)");
    if(bin <= 0)
        return FALSE;
    if(!strstr(buf, "\nSTLS\n"))
        return FALSE;
    
    if(pop3_send(pmailbox, "STLS\r\n") != 6)
        return FALSE;
    
    if(pop3_recv_command(pmailbox, buf, BUFSIZE, FALSE) < 0)
        return FALSE;
    
    return TRUE;
#undef BUFSIZE
}

static gboolean
pop3_connect(XfceMailwatchPOP3Mailbox *pmailbox, const gchar *host,
        const gchar *service, gint nonstandard_port)
{
    GError *error = NULL;
    
    TRACE("entering (%s)", service);
    
    xfce_mailwatch_net_conn_set_service(pmailbox->net_conn, service);
    if(nonstandard_port > 0)
        xfce_mailwatch_net_conn_set_port(pmailbox->net_conn, nonstandard_port);
    
    if(xfce_mailwatch_net_conn_connect(pmailbox->net_conn, &error))
        return TRUE;
    else {
        xfce_mailwatch_log_message(pmailbox->mailwatch,
                                   XFCE_MAILWATCH_MAILBOX(pmailbox),
                                   XFCE_MAILWATCH_LOG_ERROR,
                                   "%s", error->message);
        g_error_free(error);
        return FALSE;
    }
}

static inline gboolean
pop3_slurp_banner(XfceMailwatchPOP3Mailbox *pmailbox)
{
    gchar buf[2048];
    gint bin;
    
    bin = pop3_recv_command(pmailbox, buf, sizeof(buf)-1, FALSE);
    if(bin < 0) {
        DBG("failed to get banner");
    } else {
        DBG("got banner, discarding: %s\n", buf);
    }
    
    return (bin != -1);
}

static gboolean
pop3_authenticate(XfceMailwatchPOP3Mailbox *pmailbox, const gchar *host,
        const gchar *username, const gchar *password,
        XfceMailwatchAuthType auth_type, gint nonstandard_port)
{
    gboolean ret = FALSE;
    
    TRACE("entering, auth_type is %d", auth_type);
    
    switch(auth_type) {
        case AUTH_NONE:
            ret = pop3_connect(pmailbox, host, "pop3", nonstandard_port);
            if(ret)
                ret = pop3_slurp_banner(pmailbox);
            break;
        
        case AUTH_STARTTLS:
            ret = pop3_connect(pmailbox, host, "pop3", nonstandard_port);
            if(ret)
                ret = pop3_slurp_banner(pmailbox);
            if(ret)
                ret = pop3_do_stls(pmailbox, host, username, password);
            if(ret)
                ret = pop3_negotiate_ssl(pmailbox, host);
            break;
        
        case AUTH_SSL_PORT:
            ret = pop3_connect(pmailbox, host, "pop3s", nonstandard_port);
            if(ret)
                ret = pop3_negotiate_ssl(pmailbox, host);
            if(ret)
                ret = pop3_slurp_banner(pmailbox);
            break;
        
        default:
            g_critical("XfceMailwatchPOP3Mailbox: Unknown auth type (%d)", auth_type);
            return FALSE;
    }
    
    if(ret)
        ret = pop3_send_login_info(pmailbox, username, password);

    return ret;
}

static guint
pop3_check_inbox(XfceMailwatchPOP3Mailbox *pmailbox)
{
    gchar buf[1024], *p;
    gint bin;
    gint new_messages;
    
    if(pop3_send(pmailbox, "STAT\r\n") != 6)
        return 0;
    
    bin = pop3_recv_command(pmailbox, buf, 1023, FALSE);
    if(bin <= 0)
        return 0;
    DBG("got response from STAT (%d): %s", bin, buf);
    
    p = strstr(buf, "\n");
    if(!p)
        return 0;
    *p = 0;
    
    new_messages = atoi(buf+4);
    if(new_messages < 0)
       return 0;
    
    return new_messages;
}

static gpointer
pop3_check_mail_th(gpointer user_data)
{
#define BUFSIZE 1024
    XfceMailwatchPOP3Mailbox *pmailbox = user_data;
    gchar host[BUFSIZE], username[BUFSIZE], password[BUFSIZE];
    guint new_messages = 0;
    XfceMailwatchAuthType auth_type;
    gint nonstandard_port = -1;

    while(!g_atomic_pointer_get(&pmailbox->th)
          && g_atomic_int_get(&pmailbox->running))
    {
        g_thread_yield();
    }

    if(!g_atomic_int_get(&pmailbox->running)) {
        g_atomic_pointer_set(&pmailbox->th, NULL);
        return NULL;
    }
    
    g_mutex_lock(&(pmailbox->config_mx));
    
    if(!pmailbox->host || !pmailbox->username || !pmailbox->password) {
        g_mutex_unlock(&(pmailbox->config_mx));
        g_atomic_pointer_set(&pmailbox->th, NULL);
        return NULL;
    }
    
    g_strlcpy(host, pmailbox->host, BUFSIZE);
    g_strlcpy(username, pmailbox->username, BUFSIZE);
    g_strlcpy(password, pmailbox->password, BUFSIZE);
    auth_type = pmailbox->auth_type;
    if(!pmailbox->use_standard_port)
        nonstandard_port = pmailbox->nonstandard_port;
    
    g_mutex_unlock(&(pmailbox->config_mx));
    
    pmailbox->net_conn = xfce_mailwatch_net_conn_new(host, NULL);
    xfce_mailwatch_net_conn_set_should_continue_func(pmailbox->net_conn,
                                                     pop3_should_continue,
                                                     pmailbox);
    if(pop3_authenticate(pmailbox, host, username, password, auth_type,
                         nonstandard_port))
    {
        new_messages = pop3_check_inbox(pmailbox);
        DBG("checked inbox, %d new messages", new_messages);
        
        xfce_mailwatch_signal_new_messages(pmailbox->mailwatch,
                XFCE_MAILWATCH_MAILBOX(pmailbox), new_messages);
    }
    
    if(xfce_mailwatch_net_conn_is_connected(pmailbox->net_conn))
        pop3_send(pmailbox, "QUIT\r\n");
    
    if(pmailbox->net_conn) {
        xfce_mailwatch_net_conn_destroy(pmailbox->net_conn);
        pmailbox->net_conn = NULL;
    }

    g_atomic_pointer_set(&pmailbox->th, NULL);
    return NULL;
#undef BUFSIZE
}

static XfceMailwatchMailbox *
pop3_mailbox_new(XfceMailwatch *mailwatch, XfceMailwatchMailboxType *type)
{
    XfceMailwatchPOP3Mailbox *pmailbox = g_new0(XfceMailwatchPOP3Mailbox, 1);
    pmailbox->mailbox.type = type;
    pmailbox->mailwatch = mailwatch;
    pmailbox->timeout = XFCE_MAILWATCH_DEFAULT_TIMEOUT;
    pmailbox->use_standard_port = TRUE;
    g_mutex_init(&pmailbox->config_mx);

    xfce_mailwatch_net_conn_init();
    
    return XFCE_MAILWATCH_MAILBOX(pmailbox);
}

static gboolean
pop3_check_mail_timeout(gpointer data)
{
    XfceMailwatchPOP3Mailbox *pmailbox = data;
    GThread *new_th;

    if(g_atomic_pointer_get(&pmailbox->th)) {
        xfce_mailwatch_log_message(pmailbox->mailwatch,
                                   XFCE_MAILWATCH_MAILBOX(pmailbox),
                                   XFCE_MAILWATCH_LOG_WARNING,
                                   _("Previous thread hasn't exited yet, not checking mail this time."));
        return TRUE;
    }

    new_th = g_thread_try_new(NULL, pop3_check_mail_th, pmailbox, NULL);
    g_atomic_pointer_set(&pmailbox->th, new_th);

    return TRUE;
}

static void
pop3_set_activated(XfceMailwatchMailbox *mailbox, gboolean activated)
{
    XfceMailwatchPOP3Mailbox *pmailbox = XFCE_MAILWATCH_POP3_MAILBOX(mailbox);

    if(activated == g_atomic_int_get(&pmailbox->running))
        return;

    if(activated) {
        g_atomic_int_set(&pmailbox->running, TRUE);
        pmailbox->check_id = g_timeout_add(pmailbox->timeout * 1000,
                                           pop3_check_mail_timeout,
                                           pmailbox);
    } else {
        g_atomic_int_set(&pmailbox->running, FALSE);
        g_source_remove(pmailbox->check_id);
        pmailbox->check_id = 0;
    }
}

static void
pop3_force_update_cb(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchPOP3Mailbox *pmailbox = XFCE_MAILWATCH_POP3_MAILBOX(mailbox);

    if(!g_atomic_pointer_get(&pmailbox->th)) {
        gboolean restart = FALSE;

        if(pmailbox->check_id) {
            g_source_remove(pmailbox->check_id);
            restart = TRUE;
        }

        pop3_check_mail_timeout(pmailbox);

        if(restart) {
            pmailbox->check_id = g_timeout_add(pmailbox->timeout * 1000,
                                               pop3_check_mail_timeout,
                                               pmailbox);
        }
    }
}

static gboolean
pop3_host_entry_focus_out_cb(GtkWidget *w, GdkEventFocus *evt,
        gpointer user_data)
{
    XfceMailwatchPOP3Mailbox *pmailbox = user_data;
    gchar *str;
    
    str = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    
    g_mutex_lock(&(pmailbox->config_mx));
    
    g_free(pmailbox->host);
    if(!str || !*str) {
        pmailbox->host = NULL;
        g_free(str);
    } else
        pmailbox->host = str;
    
    g_mutex_unlock(&(pmailbox->config_mx));
    
    return FALSE;
}

static gboolean
pop3_username_entry_focus_out_cb(GtkWidget *w, GdkEventFocus *evt,
        gpointer user_data)
{
    XfceMailwatchPOP3Mailbox *pmailbox = user_data;
    gchar *str;
    
    str = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    
    g_mutex_lock(&(pmailbox->config_mx));
    
    g_free(pmailbox->username);
    if(!str || !*str) {
        pmailbox->username = NULL;
        g_free(str);
    } else
        pmailbox->username = str;
    
    g_mutex_unlock(&(pmailbox->config_mx));
    
    return FALSE;
}

static gboolean
pop3_password_entry_focus_out_cb(GtkWidget *w, GdkEventFocus *evt,
        gpointer user_data)
{
    XfceMailwatchPOP3Mailbox *pmailbox = user_data;
    gchar *str;
    
    str = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    
    g_mutex_lock(&(pmailbox->config_mx));
    
    g_free(pmailbox->password);
    if(!str || !*str) {
        pmailbox->password = NULL;
        g_free(str);
    } else
        pmailbox->password = str;
    
    g_mutex_unlock(&(pmailbox->config_mx));
    
    return FALSE;
}

static void
pop3_config_timeout_spinbutton_changed_cb(GtkSpinButton *sb,
                                          gpointer user_data)
{
    XfceMailwatchPOP3Mailbox *pmailbox = user_data;
    guint value = gtk_spin_button_get_value_as_int(sb) * 60;

    if(value == pmailbox->timeout)
        return;
    
    pmailbox->timeout = value;
    if(g_atomic_int_get(&pmailbox->running)) {
        if(pmailbox->check_id)
            g_source_remove(pmailbox->check_id);
        pmailbox->check_id = g_timeout_add(pmailbox->timeout * 1000,
                                           pop3_check_mail_timeout,
                                           pmailbox);
    }
}

static void
pop3_config_nonstandard_chk_cb(GtkToggleButton *tb, gpointer user_data)
{
    XfceMailwatchPOP3Mailbox *pmailbox = user_data;
    GtkWidget *entry = g_object_get_data(G_OBJECT(tb), "xfmw-entry");
    
    g_mutex_lock(&(pmailbox->config_mx));
    
    pmailbox->use_standard_port = !gtk_toggle_button_get_active(tb);
    gtk_widget_set_sensitive(entry, !pmailbox->use_standard_port);
    
    g_mutex_unlock(&(pmailbox->config_mx));
}

static gboolean
pop3_config_nonstandard_focusout_cb(GtkWidget *w, GdkEventFocus *evt, 
        gpointer user_data)
{
    XfceMailwatchPOP3Mailbox *pmailbox = user_data;
    
    g_mutex_lock(&(pmailbox->config_mx));
    
    pmailbox->nonstandard_port = atoi(gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1));
    
    g_mutex_unlock(&(pmailbox->config_mx));
    
    return FALSE;
}

static void
pop3_config_security_combo_changed_cb(GtkWidget *w, gpointer user_data)
{
    XfceMailwatchPOP3Mailbox *pmailbox = user_data;
    GtkWidget *entry = g_object_get_data(G_OBJECT(w), "xfmw-entry");
    
    g_mutex_lock(&(pmailbox->config_mx));
    
    pmailbox->auth_type = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
    
    if(pmailbox->use_standard_port) {
        if(pmailbox->auth_type == AUTH_SSL_PORT)
            gtk_entry_set_text(GTK_ENTRY(entry), POP3S_PORT_S);
        else
            gtk_entry_set_text(GTK_ENTRY(entry), POP3_PORT_S);
    }
    
    g_mutex_unlock(&(pmailbox->config_mx));
}

static void
pop3_config_advanced_btn_clicked_cb(GtkWidget *w, gpointer user_data)
{
    XfceMailwatchPOP3Mailbox *pmailbox = user_data;
    GtkWidget *dlg, *topvbox, *vbox, *hbox, *entry, *frame, *frame_bin, *chk,
              *combo;
    
    dlg = gtk_dialog_new_with_buttons(_("Advanced POP3 Options"),
            GTK_WINDOW(gtk_widget_get_toplevel(w)),
            GTK_DIALOG_DESTROY_WITH_PARENT,
                                        _("_Close"), GTK_RESPONSE_ACCEPT,
                                        NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
    
    topvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BORDER/2);
    gtk_container_set_border_width(GTK_CONTAINER(topvbox), BORDER/2);
    gtk_widget_show(topvbox);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                       topvbox, TRUE, TRUE, 0);
    
    frame = xfce_gtk_frame_box_new(_("Connection"), &frame_bin);
    gtk_widget_show(frame);
    gtk_box_pack_start(GTK_BOX(topvbox), frame, FALSE, FALSE, 0);
    
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BORDER/2);
    gtk_widget_show(vbox);
    gtk_container_add(GTK_CONTAINER(frame_bin), vbox);
    
    combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Use unsecured connection"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Use SSL/TLS on alternate port"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _("Use SSL/TLS via STLS"));
#ifdef HAVE_SSL_SUPPORT
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), pmailbox->auth_type);
#else
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_widget_set_sensitive(combo, FALSE);
#endif
    gtk_widget_show(combo);
    gtk_box_pack_start(GTK_BOX(vbox), combo, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(combo), "changed",
            G_CALLBACK(pop3_config_security_combo_changed_cb), pmailbox);
    
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    chk = gtk_check_button_new_with_mnemonic(_("Use non-standard POP3 _port:"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk),
            !pmailbox->use_standard_port);
    gtk_widget_show(chk);
    gtk_box_pack_start(GTK_BOX(hbox), chk, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(chk), "toggled",
            G_CALLBACK(pop3_config_nonstandard_chk_cb), pmailbox);
    
    entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 5);
    if(!pmailbox->use_standard_port) {
        gchar portstr[16];
        g_snprintf(portstr, 16, "%d", pmailbox->nonstandard_port);
        gtk_entry_set_text(GTK_ENTRY(entry), portstr);
    } else {
        gtk_widget_set_sensitive(entry, FALSE);
        if(pmailbox->auth_type == AUTH_SSL_PORT)
            gtk_entry_set_text(GTK_ENTRY(entry), POP3S_PORT_S);
        else
            gtk_entry_set_text(GTK_ENTRY(entry), POP3_PORT_S);
    }
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
            G_CALLBACK(pop3_config_nonstandard_focusout_cb), pmailbox);
    
    g_object_set_data(G_OBJECT(chk), "xfmw-entry", entry);
    g_object_set_data(G_OBJECT(combo), "xfmw-entry", entry);
    
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static GtkContainer *
pop3_get_setup_page(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchPOP3Mailbox *pmailbox = XFCE_MAILWATCH_POP3_MAILBOX(mailbox);
    GtkWidget *topvbox, *vbox, *hbox, *frame, *frame_bin, *lbl, *entry, *btn,
              *sbtn;
    GtkSizeGroup *sg;
    
    topvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BORDER/2);
    gtk_widget_show(topvbox);
    
    frame = xfce_gtk_frame_box_new(_("POP3 Server"), &frame_bin);
    gtk_widget_show(frame);
    gtk_box_pack_start(GTK_BOX(topvbox), frame, FALSE, FALSE, 0);
    
    sg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BORDER/2);
    gtk_widget_show(vbox);
    gtk_container_add(GTK_CONTAINER(frame_bin), vbox);
    
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("_Mail server:"));
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    gtk_size_group_add_widget(sg, lbl);
    
    entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    if(pmailbox->host)
        gtk_entry_set_text(GTK_ENTRY(entry), pmailbox->host);
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
            G_CALLBACK(pop3_host_entry_focus_out_cb), pmailbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), entry);
    
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("_Username:"));
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    gtk_size_group_add_widget(sg, lbl);
    
    entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    if(pmailbox->username)
        gtk_entry_set_text(GTK_ENTRY(entry), pmailbox->username);
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
            G_CALLBACK(pop3_username_entry_focus_out_cb), pmailbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), entry);
    
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("_Password:"));
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    gtk_size_group_add_widget(sg, lbl);
    
    entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    if(pmailbox->password)
        gtk_entry_set_text(GTK_ENTRY(entry), pmailbox->password);
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
            G_CALLBACK(pop3_password_entry_focus_out_cb), pmailbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), entry);
    
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(topvbox), hbox, FALSE, FALSE, 0);
    
    btn = gtk_button_new_with_mnemonic(_("_Advanced..."));
    gtk_button_set_image(GTK_BUTTON(btn),
                         gtk_image_new_from_icon_name("preferences-other",
                                                      GTK_ICON_SIZE_BUTTON));
    gtk_widget_show(btn);
    gtk_box_pack_start(GTK_BOX(hbox), btn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
            G_CALLBACK(pop3_config_advanced_btn_clicked_cb), pmailbox);
    
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(topvbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("Check for _new messages every"));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    
    sbtn = gtk_spin_button_new_with_range(1.0, 1440.0, 1.0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(sbtn), TRUE);
    gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(sbtn), FALSE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sbtn), pmailbox->timeout/60);
    gtk_widget_show(sbtn);
    gtk_box_pack_start(GTK_BOX(hbox), sbtn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(sbtn), "value-changed",
            G_CALLBACK(pop3_config_timeout_spinbutton_changed_cb), pmailbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), sbtn);
    
    lbl = gtk_label_new(_("minute(s)."));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    
    return GTK_CONTAINER(topvbox);
}

static void
pop3_restore_param_list(XfceMailwatchMailbox *mailbox, GList *params)
{
    XfceMailwatchPOP3Mailbox *pmailbox = XFCE_MAILWATCH_POP3_MAILBOX(mailbox);
    GList *l;
    
    g_mutex_lock(&(pmailbox->config_mx));
    
    for(l = params; l; l = l->next) {
        XfceMailwatchParam *param = l->data;
        
        if(!strcmp(param->key, "host"))
            pmailbox->host = g_strdup(param->value);
        else if(!strcmp(param->key, "username"))
            pmailbox->username = g_strdup(param->value);
        else if(!strcmp(param->key, "password"))
            pmailbox->password = g_strdup(param->value);
        else if(!strcmp(param->key, "auth_type"))
            pmailbox->auth_type = atoi(param->value);
        else if(!strcmp(param->key, "use_standard_port"))
            pmailbox->use_standard_port = *(param->value) == '0' ? FALSE : TRUE;
        else if(!strcmp(param->key, "nonstandard_port"))
            pmailbox->nonstandard_port = atoi(param->value);
        else if(!strcmp(param->key, "timeout"))
            pmailbox->timeout = atoi(param->value);
    }
    
    g_mutex_unlock(&(pmailbox->config_mx));
}

static GList *
pop3_save_param_list(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchPOP3Mailbox *pmailbox = XFCE_MAILWATCH_POP3_MAILBOX(mailbox);
    GList *params = NULL;
    XfceMailwatchParam *param;
    
    g_mutex_lock(&(pmailbox->config_mx));
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("host");
    param->value = g_strdup(pmailbox->host);
    params = g_list_prepend(params, param);
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("username");
    param->value = g_strdup(pmailbox->username);
    params = g_list_prepend(params, param);
    
    /* FIXME: probably would be nice to obscure this somewhat to deter casual
     * accidental exposure */
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("password");
    param->value = g_strdup(pmailbox->password);
    params = g_list_prepend(params, param);
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("auth_type");
    param->value = g_strdup_printf("%d", pmailbox->auth_type);
    params = g_list_prepend(params, param);
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("use_standard_port");
    param->value = g_strdup(pmailbox->use_standard_port ? "1" : "0");
    params = g_list_prepend(params, param);
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("nonstandard_port");
    param->value = g_strdup_printf("%d", pmailbox->nonstandard_port);
    params = g_list_prepend(params, param);
    
    param = g_new(XfceMailwatchParam, 1);
    param->key = g_strdup("timeout");
    param->value = g_strdup_printf("%d", pmailbox->timeout);
    params = g_list_prepend(params, param);
    
    g_mutex_unlock(&(pmailbox->config_mx));
    
    return g_list_reverse(params);
}

static void 
pop3_mailbox_free(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchPOP3Mailbox *pmailbox = XFCE_MAILWATCH_POP3_MAILBOX(mailbox);

    pop3_set_activated(mailbox, FALSE);
    while(g_atomic_pointer_get(&pmailbox->th))
        g_thread_yield();
    
    g_mutex_clear(&pmailbox->config_mx);
    
    g_free(pmailbox->host);
    g_free(pmailbox->username);
    g_free(pmailbox->password);
    
    g_free(pmailbox);
}

XfceMailwatchMailboxType builtin_mailbox_type_pop3 = {
    "pop3",
    N_("Remote POP3 Mailbox"),
    N_("The POP3 plugin can connect to a remote mail server that supports the POP3 protocol, optionally using SSL for link protection."),
    
    pop3_mailbox_new,
    pop3_set_activated,
    pop3_force_update_cb,
    pop3_get_setup_page,
    pop3_restore_param_list,
    pop3_save_param_list,
    pop3_mailbox_free
};
