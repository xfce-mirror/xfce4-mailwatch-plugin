/*
 *  xfce4-mailwatch-plugin - a mail notification applet for the xfce4 panel
 *  Copyright (c) 2013 Ján Sučan <sucan@runbox.com>
 *  Copyright (c) 2005-2008 Brian Tarricone <bjt23@cornell.edu>
 *  Copyright (c) 2005 Jasper Huijsmans <jasper@xfce.org>
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

/*
 * Source code of functions mailwatch_help_auto_toggled_cb,
 * mailwatch_help_show_uri, mailwatch_help_response_cb and
 * mailwatch_help_clicked_cb was taken from the xfce-dialogs.c file of
 * libxfce4ui library and modified on (dd-mm-yyyy):
 *
 *   15-10-2013
 *
 * Below is the copyright notice of the xfce-dialogs.c code.
 *
 * Copyright (c) 2006-2007 Benedikt Meurer <benny@xfce.org>
 * Copyright (c) 2007      The Xfce Development Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_SIGNAL_H 
#include <signal.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libxfce4panel/libxfce4panel.h>
#include <exo/exo.h>

#include "mailwatch.h"
#include "mailwatch-mailbox.h"
#include "mailwatch-utils.h"

#define BORDER                             8
#define DEFAULT_LOG_LINES                500
#define DEFAULT_NORMAL_ICON            "xfce-nomail"
#define DEFAULT_NEW_MAIL_ICON          "xfce-newmail"
#define MOUSE_BUTTON_LEFT                  1
#define MOUSE_BUTTON_MIDDLE                2

typedef struct
{
    XfcePanelPlugin *plugin;
    XfceMailwatch   *mailwatch;
    
    GtkWidget *button;
    GtkWidget *image;
    
    gboolean newmail_icon_visible;
    guint    new_messages;
    
    gchar *click_command;
    gchar *new_messages_command;

    GdkPixbuf *pix_normal;
    GdkPixbuf *pix_newmail;    
    gchar     *normal_icon;
    gchar     *new_mail_icon;

    GtkWidget             *log_dialog;
    guint                 log_lines;
    gboolean              show_log_status;
    GdkPixbuf             *pix_log[XFCE_MAILWATCH_N_LOG_LEVELS];
    XfceMailwatchLogLevel log_status;
    GtkListStore          *loglist;

    gboolean auto_open_online_doc;
} XfceMailwatchPlugin;

enum {
    ICON_TYPE_NORMAL,
    ICON_TYPE_NEW_MAIL,
    XFCE_MAILWATCH_RESPONSE_CLEAR
};

enum {
    LOGLIST_COLUMN_PIXBUF = 0,
    LOGLIST_COLUMN_TIME,
    LOGLIST_COLUMN_MESSAGE,
    LOGLIST_N_COLUMNS
};

static gboolean mailwatch_set_size(XfcePanelPlugin     *plugin,
                                   gint                 wsize,
                                   XfceMailwatchPlugin *mwp);

static void
mailwatch_set_default_config(XfceMailwatchPlugin *mwp)
{
    mwp->log_lines = 200;
    mwp->show_log_status = TRUE;
}
        
static void
mailwatch_new_messages_changed_cb(XfceMailwatch *mailwatch,
                                  gpointer       new_message_count,
                                  gpointer       user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    guint                new_messages = GPOINTER_TO_UINT(new_message_count);
    
    if (new_messages == 0 && mwp->newmail_icon_visible) {
        mwp->newmail_icon_visible = FALSE;
        mwp->new_messages = 0;
        mailwatch_set_size(mwp->plugin,
                           xfce_panel_plugin_get_size(mwp->plugin),
                           mwp);
        gtk_widget_set_tooltip_text(mwp->button, _("No new mail"));
        gtk_widget_trigger_tooltip_query(mwp->button);
    } else if (new_messages > 0) {
        if (!mwp->newmail_icon_visible) {
            mwp->newmail_icon_visible = TRUE;
            mailwatch_set_size(mwp->plugin,
                               xfce_panel_plugin_get_size(mwp->plugin),
                               mwp);
        }
        if (new_messages != mwp->new_messages) {
            GString *ttip_str = g_string_sized_new(64);
            gchar **mailbox_names = NULL;
            guint *new_message_counts = NULL;
            gint i;
            
            g_string_append_printf(ttip_str,
                                   ngettext("You have %d new message:",
                                            "You have %d new messages:",
                                            new_messages), new_messages);
            mwp->new_messages = new_messages;
            
            xfce_mailwatch_get_new_message_breakdown(mwp->mailwatch,
                    &mailbox_names, &new_message_counts);
            for (i = 0; mailbox_names[i]; i++) {
                if (new_message_counts[i] > 0) {
                    g_string_append_c(ttip_str, '\n');
                    g_string_append_printf(ttip_str,
                                           Q_("tells how many new messages in each mailbox|    %d in %s"),
                                           new_message_counts[i],
                                           mailbox_names[i]);
                }
            }
            
            g_strfreev(mailbox_names);
            g_free(new_message_counts);
            
            gtk_widget_set_tooltip_text(mwp->button, ttip_str->str);
            gtk_widget_trigger_tooltip_query(mwp->button);
            g_string_free(ttip_str, TRUE);
            
            if (mwp->new_messages_command)
                xfce_spawn_command_line_on_screen(gdk_screen_get_default(),
                                                  mwp->new_messages_command,
                                                  FALSE, FALSE, NULL);
        }
    }
}

static gboolean
mailwatch_button_press_cb(GtkWidget      *w,
                          GdkEventButton *evt,
                          gpointer        user_data)
{
    if (evt->button == MOUSE_BUTTON_MIDDLE)
        gtk_button_pressed(GTK_BUTTON(w));
    
    return FALSE;
}

static gboolean
mailwatch_button_release_cb(GtkWidget      *w,
                            GdkEventButton *evt,
                            gpointer        user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    
    if (evt->x >= w->allocation.x
        && evt->x < w->allocation.x + w->allocation.width
        && evt->y >= w->allocation.y
        && evt->y < w->allocation.y + w->allocation.height)
    {
        switch (evt->button) {
            case MOUSE_BUTTON_LEFT:
                if (mwp->click_command && *mwp->click_command)
                    xfce_spawn_command_line_on_screen(gdk_screen_get_default(),
                                                      mwp->click_command,
                                                      FALSE, FALSE, NULL);
                break;
        
            case MOUSE_BUTTON_MIDDLE:
                xfce_mailwatch_force_update(mwp->mailwatch);
                break;
        }
    }
    
    if (evt->button == MOUSE_BUTTON_MIDDLE)
        gtk_button_released(GTK_BUTTON(w));
    
    return FALSE;
}

static void
mailwatch_log_message_cb(XfceMailwatch *mailwatch,
                         gpointer       arg,
                         gpointer       user_data)
{
    XfceMailwatchLogEntry   *entry = arg;
    XfceMailwatchPlugin     *mwp = user_data;
    GtkTreeIter             iter;
    struct tm ltm;
    gchar time_str[256] = "", *new_message = NULL;
    
    if (localtime_r(&entry->timestamp, &ltm))
        strftime(time_str, 256, "%x %T:", &ltm);
    
    if (entry->level >= XFCE_MAILWATCH_N_LOG_LEVELS)
        entry->level = XFCE_MAILWATCH_N_LOG_LEVELS - 1;
    
    if (entry->mailbox_name) {
        new_message = g_strdup_printf("[%s] %s", entry->mailbox_name,
                                      entry->message);
    }

    gtk_list_store_append(mwp->loglist, &iter);
    gtk_list_store_set(mwp->loglist, &iter,
                       LOGLIST_COLUMN_PIXBUF, mwp->pix_log[entry->level],
                       LOGLIST_COLUMN_TIME, time_str,
                       LOGLIST_COLUMN_MESSAGE, new_message ? new_message : entry->message,
                       -1);
    
    g_free(new_message);
    
    if (entry->level > mwp->log_status) {
        mwp->log_status = entry->level;
        mailwatch_set_size(mwp->plugin,
                           xfce_panel_plugin_get_size(mwp->plugin),
                           mwp);
    }
    
    while (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(mwp->loglist), NULL) > (gint)mwp->log_lines) {
        if (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(mwp->loglist), &iter, NULL, 0))
            gtk_list_store_remove(mwp->loglist, &iter);
    }
}

static GdkPixbuf *
mailwatch_get_mini_icon(GtkWidget   *dummy,
                        const gchar *icon_name,
                        gint         size)
{
    GdkPixbuf *pix;
    
    pix = gtk_widget_render_icon(dummy,
                                 icon_name,
                                 GTK_ICON_SIZE_DIALOG,
                                 NULL);
    if (pix) {
        if (size / 2 != gdk_pixbuf_get_width(pix)) {
            GdkPixbuf *tmp = gdk_pixbuf_scale_simple(pix, size / 2,
                                                      size / 2,
                                                      GDK_INTERP_BILINEAR);
            g_object_unref(G_OBJECT(pix));
            pix = tmp;
        }
    }
    
    return pix;
}

static GdkPixbuf *
mailwatch_build_icon(XfceMailwatchPlugin *mwp,
                     gboolean             newmail)
{
    GdkPixbuf *pb = newmail ? gdk_pixbuf_copy(mwp->pix_newmail)
                            : gdk_pixbuf_copy(mwp->pix_normal);
    GdkPixbuf *overlay = NULL;

    if (mwp->log_status && mwp->show_log_status
       && mwp->log_status < XFCE_MAILWATCH_N_LOG_LEVELS)
    {
        overlay = mwp->pix_log[mwp->log_status];
    }
    
    if (overlay) {
        gint h, ow, oh;

        h = gdk_pixbuf_get_height(pb);
        ow = gdk_pixbuf_get_width(overlay);
        oh = gdk_pixbuf_get_height(overlay);
        gdk_pixbuf_composite(overlay, pb, 0, h - oh, ow, oh, 0, h - oh,
                             1.0, 1.0, GDK_INTERP_HYPER, 255);
    }

    return pb;
}

static gboolean
mailwatch_set_size(XfcePanelPlugin     *plugin,
                   gint                 wsize, 
                   XfceMailwatchPlugin *mwp)
{
    gint size, img_width, img_height, width, height, i;
    const gchar *icon;
    GdkPixbuf *pb;
    GtkIconTheme *itheme;
    GtkIconInfo *info = NULL;
    
    /* this is such lame lame voodoo magic.  the x/ythickness stuff
     * shouldn't be needed, since i think the panel button convienence
     * thingo sets them to zero, but we'll leave it for now.  i'm
     * not sure where the '- 2' at the end comes from. */
    size = wsize - MAX(GTK_WIDGET(mwp->button)->style->xthickness, 
                       GTK_WIDGET(mwp->button)->style->ythickness) * 2 - 2;
    
    if(xfce_panel_plugin_get_orientation(plugin) == GTK_ORIENTATION_HORIZONTAL) {
        img_width = -1;
        img_height = size;
    } else {
        img_width = size;
        img_height = -1;
    }

    if (mwp->pix_normal)
        g_object_unref(G_OBJECT(mwp->pix_normal));
    if (mwp->pix_newmail)
        g_object_unref(G_OBJECT(mwp->pix_newmail));

    for (i = 0; i < XFCE_MAILWATCH_N_LOG_LEVELS; i++) {
        if (mwp->pix_log[i])
            g_object_unref(G_OBJECT(mwp->pix_log[i]));
    }

    /* find and load the two main icons, preserving aspect ratio */
    itheme = gtk_icon_theme_get_for_screen(gtk_widget_get_screen(GTK_WIDGET(plugin)));

    icon = mwp->normal_icon ? mwp->normal_icon : DEFAULT_NORMAL_ICON;
    if (!g_path_is_absolute(icon)) {
        info = gtk_icon_theme_lookup_icon(itheme, icon, size, 0);
        if (info)
            icon = gtk_icon_info_get_filename(info);
    }

    mwp->pix_normal = gdk_pixbuf_new_from_file_at_scale(icon, img_width,
                                                        img_height, TRUE,
                                                        NULL);
    if (info) {
        gtk_icon_info_free(info);
        info = NULL;
    }

    icon = mwp->new_mail_icon ? mwp->new_mail_icon : DEFAULT_NEW_MAIL_ICON;
    if (!g_path_is_absolute(icon)) {
        info = gtk_icon_theme_lookup_icon(itheme, icon, size, 0);
        if (info)
            icon = gtk_icon_info_get_filename(info);
    }

    mwp->pix_newmail = gdk_pixbuf_new_from_file_at_scale(icon, img_width,
                                                        img_height, TRUE,
                                                        NULL);
    if (info) {
        gtk_icon_info_free(info);
        info = NULL;
    }

    /* find the smallest dimensions of the two icons */
    width = gdk_pixbuf_get_width(mwp->pix_normal);
    if (gdk_pixbuf_get_width(mwp->pix_newmail) < width)
        width = gdk_pixbuf_get_width(mwp->pix_newmail);
    height = gdk_pixbuf_get_height(mwp->pix_normal);
    if (gdk_pixbuf_get_height(mwp->pix_newmail) < height)
        height = gdk_pixbuf_get_height(mwp->pix_newmail);

    if (!GTK_WIDGET_REALIZED(plugin))
        gtk_widget_realize(GTK_WIDGET(plugin));

    /* load log mini-icons.  use the smallest dimension of the smaller
     * main icon as a base for the mini icons to ensure they aren't too
     * large in the smaller dimension */
    mwp->pix_log[XFCE_MAILWATCH_LOG_INFO] =
            mailwatch_get_mini_icon(GTK_WIDGET(plugin), GTK_STOCK_DIALOG_INFO,
                                    width < height ? width : height);
    mwp->pix_log[XFCE_MAILWATCH_LOG_WARNING] =
            mailwatch_get_mini_icon(GTK_WIDGET(plugin),
                                    GTK_STOCK_DIALOG_WARNING,
                                    width < height ? width : height);
    mwp->pix_log[XFCE_MAILWATCH_LOG_ERROR] = 
            mailwatch_get_mini_icon(GTK_WIDGET(plugin), GTK_STOCK_DIALOG_ERROR,
                                    width < height ? width : height);
    
    pb = mailwatch_build_icon(mwp, mwp->newmail_icon_visible);
    width = gdk_pixbuf_get_width(pb);
    height = gdk_pixbuf_get_height(pb);
    gtk_image_set_from_pixbuf(GTK_IMAGE(mwp->image), pb);
    g_object_unref(G_OBJECT(pb));

    width += wsize - size;
    height += wsize - size;
    gtk_widget_set_size_request(mwp->button, width, height);

    return TRUE;
}

static XfceMailwatchPlugin *
mailwatch_create(XfcePanelPlugin *plugin)
{
    XfceMailwatchPlugin *mwp = g_new0(XfceMailwatchPlugin, 1);
    
    mwp->plugin = plugin;
    mwp->mailwatch = xfce_mailwatch_new();
    
    if (G_UNLIKELY(!mwp->mailwatch)) {
        xfce_message_dialog(NULL, _("Xfce Mailwatch"), GTK_STOCK_DIALOG_ERROR,
                _("The mailwatch applet cannot be added to the panel."),
                _("It is possible that your version of GLib does not have threads support."),
                GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT, NULL);
        g_free(mwp);
        return NULL;
    }

    mwp->button = xfce_create_panel_button();
    gtk_button_set_relief(GTK_BUTTON(mwp->button), GTK_RELIEF_NONE);
    gtk_widget_show(mwp->button);
    gtk_container_add(GTK_CONTAINER(plugin), mwp->button);
    g_signal_connect(mwp->button, "button-press-event",
            G_CALLBACK(mailwatch_button_press_cb), mwp);
    g_signal_connect(mwp->button, "button-release-event",
            G_CALLBACK(mailwatch_button_release_cb), mwp);
    
    gtk_widget_set_tooltip_text(mwp->button, _("No new mail"));
    
    xfce_panel_plugin_add_action_widget(plugin, mwp->button);
    
    mwp->image = gtk_image_new();
    gtk_widget_show(mwp->image);
    gtk_container_add(GTK_CONTAINER(mwp->button), mwp->image);

    mwp->log_dialog = NULL;
    mwp->loglist = gtk_list_store_new(LOGLIST_N_COLUMNS,
                                      GDK_TYPE_PIXBUF,
                                      G_TYPE_STRING,
                                      G_TYPE_STRING );
    
    xfce_mailwatch_signal_connect(mwp->mailwatch,
            XFCE_MAILWATCH_SIGNAL_NEW_MESSAGE_COUNT_CHANGED,
            mailwatch_new_messages_changed_cb, mwp);
    xfce_mailwatch_signal_connect( mwp->mailwatch,
            XFCE_MAILWATCH_SIGNAL_LOG_MESSAGE,
            mailwatch_log_message_cb, mwp);
    
    return mwp;
}

static void
mailwatch_read_config(XfcePanelPlugin     *plugin,
                      XfceMailwatchPlugin *mwp)
{
    const char *value;
    gchar *file;
    gboolean reload_icon = TRUE;
    XfceRc *rc;
    
    DBG("entering");

    file = xfce_panel_plugin_lookup_rc_file(plugin);
    if (!file) {
        DBG("Mailwatch: no config found; setting defaults");
        mailwatch_set_default_config(mwp);
        return;
    }
    
    rc = xfce_rc_simple_open(file, TRUE);
    if (!rc) {
        DBG("Mailwatch: no config found in \"%s\"; setting defaults", file);
        g_free(file);
        mailwatch_set_default_config(mwp);
        return;
    }
    
    xfce_rc_set_group(rc, "mailwatch-plugin");
    
    value = xfce_rc_read_entry(rc, "click_command", NULL);
    if(value)
        mwp->click_command = g_strdup(value);
    
    value = xfce_rc_read_entry(rc, "new_messages_command", NULL);
    if(value)
        mwp->new_messages_command = g_strdup(value);
    
    value = xfce_rc_read_entry(rc, "normal_icon", NULL);
    if (value) {
        mwp->normal_icon = g_strdup(value);
        reload_icon = TRUE;
    } else {
        mwp->normal_icon = g_strdup(DEFAULT_NORMAL_ICON);
    }
    
    value = xfce_rc_read_entry(rc, "new_mail_icon", NULL);
    if (value) {
        mwp->new_mail_icon = g_strdup(value);
        reload_icon = TRUE;
    } else {
        mwp->new_mail_icon = g_strdup(DEFAULT_NEW_MAIL_ICON);
    }
    
    if(reload_icon)
        mailwatch_set_size(plugin, xfce_panel_plugin_get_size(plugin), mwp);
    
    mwp->log_lines = xfce_rc_read_int_entry(rc, "log_lines", DEFAULT_LOG_LINES);
    
    mwp->show_log_status = xfce_rc_read_bool_entry(rc, "show_log_status", TRUE);

    mwp->auto_open_online_doc = xfce_rc_read_bool_entry(rc, "auto_open_online_doc", FALSE);

    xfce_rc_close(rc);
    
    xfce_mailwatch_set_config_file(mwp->mailwatch, file);
    xfce_mailwatch_load_config(mwp->mailwatch);
    
    g_free(file);
}

static void
mailwatch_write_config(XfcePanelPlugin     *plugin, 
                       XfceMailwatchPlugin *mwp)
{
    gchar *file;
    XfceRc *rc;
    
    TRACE("entering");
    
    file = xfce_panel_plugin_save_location(plugin, TRUE);
    if (!file) {
        g_critical("Mailwatch: Unable to find save location for configuration file");
        return;
    }
    
    rc = xfce_rc_simple_open(file, FALSE);
    if (!rc) {
        g_critical("Mailwatch: Unable to open \"%s\" for writing", file);
        g_free(file);
        return;
    }
    
    xfce_rc_set_group(rc, "mailwatch-plugin");
    
    xfce_rc_write_entry(rc, "click_command", 
                        mwp->click_command?mwp->click_command:"");
    xfce_rc_write_entry(rc, "new_messages_command",
                        mwp->new_messages_command?mwp->new_messages_command:"");
    xfce_rc_write_entry(rc, "normal_icon",
                        mwp->normal_icon?mwp->normal_icon:DEFAULT_NORMAL_ICON);
    xfce_rc_write_entry(rc, "new_mail_icon",
                        mwp->new_mail_icon?mwp->new_mail_icon:DEFAULT_NEW_MAIL_ICON);
    xfce_rc_write_int_entry(rc, "log_lines", mwp->log_lines);
    xfce_rc_write_bool_entry(rc, "show_log_status", mwp->show_log_status);
    xfce_rc_write_bool_entry(rc, "auto_open_online_doc", mwp->auto_open_online_doc);

    xfce_rc_close(rc);
    
    xfce_mailwatch_set_config_file(mwp->mailwatch, file);
    xfce_mailwatch_save_config(mwp->mailwatch);
    
    g_free(file);
}

static gboolean
mailwatch_click_command_focusout_cb(GtkWidget     *w,
                                    GdkEventFocus *evt,
                                    gpointer       user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    gchar *command;
    
    g_free(mwp->click_command);
    
    command = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    mwp->click_command = g_strdup(command ? command : "");
    
    return FALSE;
}

static void
mailwatch_log_window_response_cb(GtkDialog *dialog,
                                 gint       response,
                                 gpointer   user_data)
{
    if(response == XFCE_MAILWATCH_RESPONSE_CLEAR) {
        GtkListStore *ls = user_data;
        gtk_list_store_clear(ls);
    } else {
        gtk_widget_destroy(GTK_WIDGET(dialog));
    }
}

static void
mailwatch_zero_pointer(GtkWidget **w)
{
    if(w)
        *w = NULL;
}

static void
mailwatch_log_lines_changed_cb(GtkWidget *w,
                               gpointer   user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    
    mwp->log_lines = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w));
}

static void
mailwatch_log_status_toggled_cb(GtkToggleButton *tb,
                                gpointer         user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    
    mwp->show_log_status = gtk_toggle_button_get_active(tb);
    
    xfce_mailwatch_get_new_messages(mwp->mailwatch);
    mailwatch_set_size(mwp->plugin,
                       xfce_panel_plugin_get_size(mwp->plugin),
                       mwp);
}

static void
mailwatch_view_log_clicked_cb(GtkWidget *widget,
                              gpointer   user_data )
{
    XfceMailwatchPlugin     *mwp = user_data;
    GtkWidget               *vbox, *hbox, *scrollw, *treeview, *button, *lbl,
                            *sbtn, *chk;
    
    if (mwp->log_dialog) {
        gtk_window_present(GTK_WINDOW(mwp->log_dialog));
        return;
    }

    mwp->log_status = 0;
    mailwatch_set_size(mwp->plugin,
                       xfce_panel_plugin_get_size(mwp->plugin),
                       mwp);

    mwp->log_dialog = gtk_dialog_new_with_buttons(_( "Mailwatch log" ),
                                                  GTK_WINDOW(gtk_widget_get_toplevel(widget)),
                                                  GTK_DIALOG_MODAL
                                                  | GTK_DIALOG_DESTROY_WITH_PARENT
                                                  | GTK_DIALOG_NO_SEPARATOR,
                                                  NULL);
    gtk_widget_set_size_request(mwp->log_dialog, 480, 240 );
    g_signal_connect(G_OBJECT(mwp->log_dialog), "response",
                     G_CALLBACK(mailwatch_log_window_response_cb), mwp->loglist);
    g_signal_connect_swapped(G_OBJECT(mwp->log_dialog), "destroy",
                     G_CALLBACK(mailwatch_zero_pointer), &mwp->log_dialog);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), BORDER/2);
    gtk_widget_show(vbox);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(mwp->log_dialog)->vbox), vbox, TRUE, TRUE, 0);

    scrollw = gtk_scrolled_window_new( NULL, NULL );
    gtk_widget_show( scrollw );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scrollw ),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC );
    gtk_scrolled_window_set_shadow_type( GTK_SCROLLED_WINDOW( scrollw ),
                                         GTK_SHADOW_IN );
    gtk_box_pack_start( GTK_BOX( vbox ), scrollw, TRUE, TRUE, 0 );
    
    treeview = gtk_tree_view_new_with_model( GTK_TREE_MODEL( mwp->loglist ) );
    gtk_tree_view_set_headers_visible( GTK_TREE_VIEW( treeview ), FALSE );
    gtk_tree_view_insert_column_with_attributes( GTK_TREE_VIEW( treeview ),
                                                 -1,
                                                 "Level",
                                                 gtk_cell_renderer_pixbuf_new(),
                                                 "pixbuf", LOGLIST_COLUMN_PIXBUF,
                                                 NULL );
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(treeview), -1,
                                                "Timestamp",
                                                gtk_cell_renderer_text_new(),
                                                "text", LOGLIST_COLUMN_TIME,
                                                NULL);
    gtk_tree_view_insert_column_with_attributes( GTK_TREE_VIEW( treeview ),
                                                 -1,
                                                 "Message",
                                                 gtk_cell_renderer_text_new(),
                                                 "text", LOGLIST_COLUMN_MESSAGE,
                                                 NULL );
    g_object_set(G_OBJECT(gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), 0)),
                 "expand", FALSE, NULL);
    g_object_set(G_OBJECT(gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), 1)),
                 "expand", FALSE, NULL);
    g_object_set(G_OBJECT(gtk_tree_view_get_column(GTK_TREE_VIEW(treeview), 2)),
                 "expand", TRUE, NULL);
    gtk_widget_show( treeview );
    gtk_container_add( GTK_CONTAINER( scrollw ), treeview );
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("Log _lines:"));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    
    sbtn = gtk_spin_button_new_with_range(0.0, G_MAXDOUBLE, 1.0);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(sbtn), 0);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(sbtn), TRUE);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sbtn), mwp->log_lines);
    gtk_widget_show(sbtn);
    gtk_box_pack_start(GTK_BOX(hbox), sbtn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(sbtn), "value-changed",
                     G_CALLBACK(mailwatch_log_lines_changed_cb), mwp);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), sbtn);
    
    chk = gtk_check_button_new_with_mnemonic(_("Show log _status in icon"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk), mwp->show_log_status);
    gtk_widget_show(chk);
    gtk_box_pack_start(GTK_BOX(vbox), chk, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(chk), "toggled",
                     G_CALLBACK(mailwatch_log_status_toggled_cb), mwp);
    
    button = gtk_button_new_from_stock(GTK_STOCK_CLEAR);
    gtk_widget_show( button );
    gtk_dialog_add_action_widget(GTK_DIALOG(mwp->log_dialog), button,
                                 XFCE_MAILWATCH_RESPONSE_CLEAR);
    
    button = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
    gtk_widget_show( button );
    gtk_dialog_add_action_widget(GTK_DIALOG(mwp->log_dialog), button,
                                 GTK_RESPONSE_ACCEPT);
    
    gtk_widget_show(mwp->log_dialog);
}

static gboolean
mailwatch_newmsg_command_focusout_cb(GtkWidget     *w,
                                     GdkEventFocus *evt,
                                     gpointer       user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    gchar *command;
    
    g_free(mwp->new_messages_command);
    
    command = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    mwp->new_messages_command = g_strdup(command ? command : "");
    
    return FALSE;
}

static void
mailwatch_iconbtn_clicked_cb(GtkWidget           *w,
                             XfceMailwatchPlugin *mwp)
{
    GtkWidget *chooser, *toplevel;
    gint icon_type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),
                                                       "mailwatch-icontype"));
    
    g_return_if_fail(icon_type == ICON_TYPE_NORMAL || icon_type == ICON_TYPE_NEW_MAIL);
    
    toplevel = gtk_widget_get_toplevel(w);
    chooser = exo_icon_chooser_dialog_new (_("Select Icon"),
                                           GTK_WINDOW(gtk_widget_get_toplevel(toplevel)),
                                           GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                           GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
                                           NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(chooser), GTK_RESPONSE_ACCEPT);
    /* Preselect actually used icon */
    switch(icon_type) {
    case ICON_TYPE_NORMAL:
        exo_icon_chooser_dialog_set_icon (EXO_ICON_CHOOSER_DIALOG (chooser),
                                          exo_str_is_empty (mwp->normal_icon) ? 
                                          DEFAULT_NORMAL_ICON : mwp->normal_icon);
        break;
    case ICON_TYPE_NEW_MAIL:
        exo_icon_chooser_dialog_set_icon (EXO_ICON_CHOOSER_DIALOG (chooser),
                                          exo_str_is_empty (mwp->new_mail_icon) ?
                                          DEFAULT_NEW_MAIL_ICON : mwp->new_mail_icon);
        break;
    }
    
    if(gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename = exo_icon_chooser_dialog_get_icon(EXO_ICON_CHOOSER_DIALOG(chooser));
        if(filename) {
            GtkWidget *image, *vbox;
            GtkWidget *label = NULL;
            GdkPixbuf **icon_pix = NULL;
            gchar **icon_path = NULL;

            switch(icon_type) {
            case ICON_TYPE_NORMAL:
                icon_path = &(mwp->normal_icon);
                icon_pix = &(mwp->pix_normal);
                label = gtk_label_new_with_mnemonic(_("_Normal"));
                break;
            case ICON_TYPE_NEW_MAIL:
                icon_path = &(mwp->new_mail_icon);
                icon_pix = &(mwp->pix_newmail);
                label = gtk_label_new_with_mnemonic(_("Ne_w mail"));
                break;
            }

            g_free(*icon_path);
            *icon_path = filename;
            mailwatch_set_size(mwp->plugin, 
                               xfce_panel_plugin_get_size(mwp->plugin), mwp);
            
            gtk_container_remove(GTK_CONTAINER(w), gtk_bin_get_child(GTK_BIN(w)));
            
            vbox = gtk_vbox_new(FALSE, BORDER / 2);
            gtk_widget_show(vbox);
            gtk_container_add(GTK_CONTAINER(w), vbox);
            
            image = gtk_image_new_from_pixbuf(*icon_pix);
            gtk_widget_show(image);
            gtk_box_pack_start(GTK_BOX(vbox), image, TRUE, TRUE, 0);
            
            gtk_widget_show(label);
            gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
        }
    }
    gtk_widget_destroy(chooser);
}

static void
mailwatch_help_auto_toggled_cb(GtkWidget *button,
                               gpointer   user_data)
{
    XfceMailwatchPlugin *mwp = user_data;

    if (button != NULL)
        mwp->auto_open_online_doc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
    else
        mwp->auto_open_online_doc = FALSE;
}

static void
mailwatch_help_show_uri(GdkScreen *screen,
                        GtkWindow *parent)
{
    GError *error = NULL;

    g_return_if_fail(GDK_IS_SCREEN(screen));
    g_return_if_fail(parent == NULL || GTK_IS_WINDOW(parent));

    if (!gtk_show_uri(screen, WEBSITE, gtk_get_current_event_time(), &error)) {
        xfce_dialog_show_error(parent, error,
                               _("Failed to open web browser for online documentation"));
        g_error_free(error);
    }
}

static void
mailwatch_help_response_cb(GtkWidget *dialog,
                           gint       response_id,
                           gpointer   user_data)
{
    XfceMailwatchPlugin *mwp = user_data;

    gtk_widget_hide(dialog);

    if (response_id == GTK_RESPONSE_YES) {
        mailwatch_help_show_uri(gtk_widget_get_screen(dialog),
                                gtk_window_get_transient_for(GTK_WINDOW(dialog)));
    } else {
        /* Unset auto. */
        mailwatch_help_auto_toggled_cb(NULL, mwp);
    }

    gtk_widget_destroy(dialog);
}

static void
mailwatch_help_clicked_cb(GtkWidget *w,
                          gpointer   user_data)
{
    GtkWidget   *dialog;
    GtkWidget   *message_box;
    GtkWidget   *button;
    GdkScreen   *screen;
    GtkWidget   *toplevel;
    GtkWindow   *parent;
    XfceMailwatchPlugin *mwp = user_data;

    toplevel = gtk_widget_get_toplevel(w);
    g_return_if_fail(gtk_widget_is_toplevel(toplevel) &&  GTK_IS_WINDOW(toplevel));
    parent = (GtkWindow *) toplevel;
    /* Check if we should automatically go online. */
    if (mwp->auto_open_online_doc) {
        screen = gtk_window_get_screen (GTK_WINDOW (parent));
        mailwatch_help_show_uri (screen, parent);
        return;
    }

    dialog = xfce_message_dialog_new (parent,
                                      _("Online Documentation"),
                                      GTK_STOCK_DIALOG_QUESTION,
                                      _("Do you want to read the manual online?"),
                                      _("You will be redirected to the documentation website "
                                        "where the help pages are maintained."),
                                      GTK_STOCK_CANCEL, GTK_RESPONSE_NO,
                                      XFCE_BUTTON_TYPE_MIXED,
                                      GTK_STOCK_HELP, _("_Read Online"),
                                      GTK_RESPONSE_YES,
                                      NULL);

#if GTK_CHECK_VERSION(2, 22, 0)
    message_box = gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog));
#else
    message_box = gtk_widget_get_parent (GTK_MESSAGE_DIALOG (dialog)->label);
    g_return_if_fail (GTK_IS_VBOX (message_box));
#endif

    button = gtk_check_button_new_with_mnemonic (_("_Always go directly to the online documentation"));
    gtk_box_pack_end (GTK_BOX (message_box), button, FALSE, TRUE, 0);
    g_signal_connect (G_OBJECT (button), "toggled",
                      G_CALLBACK (mailwatch_help_auto_toggled_cb), mwp);
    gtk_widget_show (button);

    /* Don't focus the checkbutton. */
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);
    button = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);
    gtk_widget_grab_focus (button);

    /* Show the dialog without locking the mainloop. */
    gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);
    g_signal_connect (G_OBJECT (dialog), "response",
        G_CALLBACK (mailwatch_help_response_cb), mwp);
    gtk_window_present (GTK_WINDOW (dialog));
}

static void
mailwatch_dialog_response(GtkWidget           *dlg, 
                          int                  response, 
                          XfceMailwatchPlugin *mwp)
{
    gtk_widget_destroy(dlg);
    xfce_panel_plugin_unblock_menu(mwp->plugin);
    mailwatch_write_config(mwp->plugin, mwp);
}

static void
mailwatch_create_options(XfcePanelPlugin     *plugin,
                         XfceMailwatchPlugin *mwp)
{
    GtkWidget *dlg, *topvbox, *frame, *frame_bin, *hbox, *lbl, *entry, *btn,
              *vbox, *img;
    GtkContainer *cfg_page;
    GtkSizeGroup *sg;
    
    xfce_panel_plugin_block_menu(plugin);
    
    dlg = xfce_titled_dialog_new_with_buttons(_("Mail Watcher"), 
                GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(plugin))),
                GTK_DIALOG_DESTROY_WITH_PARENT |
                GTK_DIALOG_NO_SEPARATOR, NULL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(GTK_DIALOG(dlg)->action_area),
                              GTK_BUTTONBOX_EDGE);
    g_signal_connect(G_OBJECT(dlg), "response",
                     G_CALLBACK(mailwatch_dialog_response), mwp);

    gtk_container_set_border_width(GTK_CONTAINER(dlg), 2);
    gtk_window_set_icon_name(GTK_WINDOW(dlg), "xfce4-settings");
    
    btn = gtk_button_new_from_stock(GTK_STOCK_HELP);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->action_area), btn, FALSE,
                       FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
                     G_CALLBACK(mailwatch_help_clicked_cb), mwp);
    
    btn = xfce_mailwatch_custom_button_new(_("_View Log..."), GTK_STOCK_FIND);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->action_area), btn, FALSE,
                       FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
                     G_CALLBACK(mailwatch_view_log_clicked_cb), mwp);
    
    btn = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
    gtk_dialog_add_action_widget(GTK_DIALOG(dlg), btn, GTK_RESPONSE_ACCEPT);
                     
    topvbox = gtk_vbox_new(FALSE, BORDER);
    gtk_container_set_border_width(GTK_CONTAINER(topvbox), BORDER - 2);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), topvbox,
                        TRUE, TRUE, 0);
    
    cfg_page = xfce_mailwatch_get_configuration_page(mwp->mailwatch);
    if(cfg_page)
        gtk_box_pack_start(GTK_BOX(topvbox), GTK_WIDGET(cfg_page), TRUE, TRUE, 0);
    /* External programs. */
    GtkWidget *table;
    GtkWidget *lbl_onclick;
    GtkWidget *lbl_onnewmessages;
    GtkWidget *halign;

    frame = xfce_mailwatch_create_framebox(_("External Programs"), &frame_bin);
    gtk_box_pack_start(GTK_BOX(topvbox), frame, FALSE, FALSE, 0);

    table = gtk_table_new(2, 2, FALSE);
    gtk_container_add(GTK_CONTAINER(frame_bin), table);
    /* External programs - Labels. */
    lbl_onclick = gtk_label_new_with_mnemonic(_("Run _on click:"));
    halign = gtk_alignment_new(0, 0.5, 0, 0);
    gtk_container_add(GTK_CONTAINER(halign), lbl_onclick);
    gtk_table_attach(GTK_TABLE(table), halign, 0, 1, 0, 1,
                     GTK_FILL, GTK_FILL, BORDER / 4, BORDER / 4);

    lbl_onnewmessages = gtk_label_new_with_mnemonic(_("Run on new _messages:"));
    halign = gtk_alignment_new(0, 0.5, 0, 0);
    gtk_container_add(GTK_CONTAINER(halign), lbl_onnewmessages);
    gtk_table_attach(GTK_TABLE(table), halign, 0, 1, 1, 2,
                     GTK_FILL, GTK_FILL, BORDER / 4, BORDER / 4);
    /* External programs - Entries. */
    entry = gtk_entry_new();
    if(mwp->click_command)
        gtk_entry_set_text(GTK_ENTRY(entry), mwp->click_command);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl_onclick), entry);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
            G_CALLBACK(mailwatch_click_command_focusout_cb), mwp);
    gtk_table_attach(GTK_TABLE(table), entry, 1, 2, 0, 1,
                     GTK_FILL | GTK_EXPAND, GTK_FILL, BORDER / 4, BORDER / 4);

    entry = gtk_entry_new();
    if(mwp->new_messages_command)
        gtk_entry_set_text(GTK_ENTRY(entry), mwp->new_messages_command);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl_onnewmessages), entry);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
            G_CALLBACK(mailwatch_newmsg_command_focusout_cb), mwp);
    gtk_table_attach(GTK_TABLE(table), entry, 1, 2, 1, 2,
                     GTK_FILL | GTK_EXPAND, GTK_FILL, BORDER / 4, BORDER / 4);
    /* Icons. */
    frame = xfce_mailwatch_create_framebox(_("Icons"), &frame_bin);
    gtk_box_pack_start(GTK_BOX(topvbox), frame, FALSE, FALSE, 0);
    
    sg = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_container_add(GTK_CONTAINER(frame_bin), hbox);
    
    btn = gtk_button_new();
    g_object_set_data(G_OBJECT(btn), "mailwatch-icontype",
                      GINT_TO_POINTER(ICON_TYPE_NORMAL));
    gtk_box_pack_start(GTK_BOX(hbox), btn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
                     G_CALLBACK(mailwatch_iconbtn_clicked_cb), mwp);
    gtk_size_group_add_widget(sg, btn);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_container_add(GTK_CONTAINER(btn), vbox);
    
    img = gtk_image_new_from_pixbuf(mwp->pix_normal);
    gtk_box_pack_start(GTK_BOX(vbox), img, TRUE, TRUE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("_Normal"));
    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);
    
    btn = gtk_button_new();
    g_object_set_data(G_OBJECT(btn), "mailwatch-icontype",
                      GINT_TO_POINTER(ICON_TYPE_NEW_MAIL));
    gtk_box_pack_start(GTK_BOX(hbox), btn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
                     G_CALLBACK(mailwatch_iconbtn_clicked_cb), mwp);
    gtk_size_group_add_widget(sg, btn);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_container_add(GTK_CONTAINER(btn), vbox);
    
    img = gtk_image_new_from_pixbuf(mwp->pix_newmail);
    gtk_box_pack_start(GTK_BOX(vbox), img, TRUE, TRUE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("Ne_w Mail"));
    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_box_pack_start(GTK_BOX(topvbox), hbox, FALSE, FALSE, 0);
   
    gtk_widget_show_all(dlg);
}

static void
mailwatch_free(XfcePanelPlugin     *plugin,
               XfceMailwatchPlugin *mwp)
{
    gint i;

#ifdef HAVE_XFCE_POSIX_SIGNAL_HANDLER_INIT
    xfce_posix_signal_handler_restore_handler(SIGUSR2);
#endif
    
    xfce_mailwatch_destroy(mwp->mailwatch);
    
    g_free(mwp->normal_icon);
    g_free(mwp->new_mail_icon);
    
    if(mwp->pix_normal)
        g_object_unref(G_OBJECT(mwp->pix_normal));
    if(mwp->pix_newmail)
        g_object_unref(G_OBJECT(mwp->pix_newmail));

    for(i = 0; i < XFCE_MAILWATCH_N_LOG_LEVELS; i++) {
        if(mwp->pix_log[i])
            g_object_unref(G_OBJECT(mwp->pix_log[i]));
    }

    g_object_unref(G_OBJECT(mwp->loglist));
    
    g_free(mwp);
}

static void
mailwatch_update_now_clicked_cb(GtkMenuItem *mi,
                                gpointer     user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    xfce_mailwatch_force_update(mwp->mailwatch);
}

static void
mailwatch_show_about(XfcePanelPlugin *plugin,
                     gpointer         user_data)
{
    GdkPixbuf *icon;

    const gchar *auth[] = { "Ján Sučan sucan@runbox.com Maintainer",
                            "Brian J. Tarricone bjt23@cornell.edu Maintainer, Original Author",
                            "Pasi Orovuo pasi.ov@gmail.com Developer",
                            NULL };

    icon = xfce_panel_pixbuf_from_source("xfce-mail", NULL, 32);

    gtk_show_about_dialog(NULL,
                          "logo", icon,
                          "program-name", _("Xfce4 Mailwatch Plugin"),
                          "license", xfce_get_license_text (XFCE_LICENSE_TEXT_GPL),
                          "version", VERSION,
                          "comments", _("A featureful mail-checker applet for the Xfce Panel"),
                          "website", WEBSITE,
                          "copyright", _("Copyright (c) 2013 Ján Sučan\n"
                                         "Copyright (c) 2005-2008 Brian Tarricone\n"
                                         "Copyright (c) 2005 Pasi Orovuo"),
                          "authors", auth,
                          NULL);

    if(icon)
        g_object_unref(G_OBJECT(icon));
}

static void
mailwatch_handle_sigusr2(gint     signal_,
                         gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    xfce_mailwatch_force_update(mwp->mailwatch);
}

static void
mailwatch_construct(XfcePanelPlugin *plugin)
{
    XfceMailwatchPlugin *mwp;
    GtkWidget *mi, *img;
    struct sigaction sa = {
        .sa_handler = SIG_IGN,
#ifdef SA_RESTART
        .sa_flags = SA_RESTART,
#endif
    };

    xfce_textdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");

    if(!(mwp = mailwatch_create(plugin)))
        exit(1);
    
    mailwatch_read_config(plugin, mwp);

    if(xfce_posix_signal_handler_init(NULL)) {
        GError *error = NULL;

        if(!xfce_posix_signal_handler_set_handler(SIGUSR2,
                                                  mailwatch_handle_sigusr2,
                                                  mwp, &error))
        {
            g_warning("Failed to set SIGUSR2 handler: %s", error->message);
            g_error_free(error);
            sigaction(SIGUSR2, &sa, NULL);
        }
    } else {
        g_warning("failed to init POSIX signal handler helper");
        sigaction(SIGUSR2, &sa, NULL);
    }

    g_signal_connect(plugin, "free-data", 
                     G_CALLBACK(mailwatch_free), mwp);
    
    g_signal_connect(plugin, "save", 
                     G_CALLBACK(mailwatch_write_config), mwp);
    
    xfce_panel_plugin_menu_show_configure(plugin);
    g_signal_connect(plugin, "configure-plugin",
                     G_CALLBACK(mailwatch_create_options), mwp);
    
    xfce_panel_plugin_menu_show_about(plugin);
    g_signal_connect(plugin, "about",
                     G_CALLBACK(mailwatch_show_about), mwp);

    g_signal_connect(plugin, "size-changed",
                     G_CALLBACK(mailwatch_set_size), mwp);
    
    mi = gtk_image_menu_item_new_with_label(_("Update Now"));
    img = gtk_image_new_from_stock(GTK_STOCK_REFRESH, GTK_ICON_SIZE_MENU);
    gtk_widget_show(img);
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), img);
    gtk_widget_show(mi);
    g_signal_connect(G_OBJECT(mi), "activate",
                     G_CALLBACK(mailwatch_update_now_clicked_cb), mwp);
    xfce_panel_plugin_menu_insert_item(plugin, GTK_MENU_ITEM(mi));

    xfce_mailwatch_force_update(mwp->mailwatch);
}

XFCE_PANEL_PLUGIN_REGISTER(mailwatch_construct);
