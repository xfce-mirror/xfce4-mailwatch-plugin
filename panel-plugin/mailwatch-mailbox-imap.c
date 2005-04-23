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

#include <glib.h>
#include <gtk/gtk.h>

#include <libxfce4util/libxfce4util.h>

#include "mailwatch.h"

#define BORDER 8

#define MAILWATCH_IMAP_MAILBOX(ptr) ((XfceMailwatchIMAPMailbox *)ptr)

#define IMAP_CMD_START   GINT_TO_POINTER(1)
#define IMAP_CMD_PAUSE   GINT_TO_POINTER(2)
#define IMAP_CMD_TIMEOUT GINT_TO_POINTER(3)
#define IMAP_CMD_QUIT    GINT_TO_POINTER(4)

typedef struct
{
    XfceMailwatchMailbox mailbox;
    
    gchar *host;
    gchar *username;
    gchar *password;
    gboolean use_imaps;
    
    GThread *th;
    GAsyncQueue *aqueue;
    
    XfceMailwatch *mailwatch;
} XfceMailwatchIMAPMailbox;


static void
imap_check_mail(XfceMailwatchIMAPMailbox *imailbox)
{
    
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
        
        if(running && g_timer_elapsed(timer, &dummy) >= timeout-delta) {
            gdouble time_before, time_after;
            time_before = g_timer_elapsed(timer, &dummy);
            imap_check_mail(imailbox);
            time_after = g_timer_elapsed(timer, &dummy);
            delta = (gint)(time_after - time_before);
            g_timer_start(timer);
        }
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
    XfceMailwatchIMAPMailbox *imailbox = MAILWATCH_IMAP_MAILBOX(mailbox);
    
    g_async_queue_push(imailbox->aqueue, activated ? IMAP_CMD_START : IMAP_CMD_PAUSE);
}

static void
imap_timeout_changed_cb(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchIMAPMailbox *imailbox = MAILWATCH_IMAP_MAILBOX(mailbox);
    
    g_async_queue_push(imailbox->aqueue, IMAP_CMD_TIMEOUT);
    g_async_queue_push(imailbox->aqueue,
            GUINT_TO_POINTER(xfce_mailwatch_get_timeout(imailbox->mailwatch)));
}

static GtkContainer *
imap_get_setup_page(XfceMailwatchMailbox *mailbox)
{
    GtkWidget *topvbox, *lbl;
    
    topvbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(topvbox);
    
    lbl = gtk_label_new("This is kinda gay.");
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(topvbox), lbl, FALSE, FALSE, 0);
    
    return GTK_CONTAINER(topvbox);
}

static void
imap_restore_param_list(XfceMailwatchMailbox *mailbox, GList *params)
{
    
}

static GList *
imap_save_param_list(XfceMailwatchMailbox *mailbox)
{
    GList *params = NULL;
    
    return params;
}

static void 
imap_mailbox_free(XfceMailwatchMailbox *mailbox)
{
    XfceMailwatchIMAPMailbox *imailbox = MAILWATCH_IMAP_MAILBOX(mailbox);
    
    g_async_queue_push(imailbox->aqueue, IMAP_CMD_QUIT);
    g_thread_join(imailbox->th);
    g_async_queue_unref(imailbox->aqueue);
    
    g_free(imailbox->host);
    g_free(imailbox->username);
    g_free(imailbox->password);
    
    g_free(imailbox);
}

XfceMailwatchMailboxType builtin_mailbox_type_imap = {
    N_("Remote IMAP Mailbox"),
    N_("The IMAP plugin can connect to a remote mail server that supports the IMAP protocol."),
    
    imap_mailbox_new,
    imap_set_activated,
    imap_timeout_changed_cb,
    imap_get_setup_page,
    imap_restore_param_list,
    imap_save_param_list,
    imap_mailbox_free
};
