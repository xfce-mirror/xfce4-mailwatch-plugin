/*
 *  xfce4-mailwatch-plugin - a mail notification applet for the xfce4 panel
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/libxfcegui4.h>
#include <libxfce4panel/xfce-panel-plugin.h>
#include <libxfce4panel/xfce-panel-convenience.h>

#include "mailwatch.h"
#include "mailwatch-mailbox.h"
#include "mailwatch-utils.h"

#define BORDER                             8
#define DEFAULT_LOG_LINES                500
#define XFCE_MAILWATCH_RESPONSE_CLEAR  72347
#define DEFAULT_NORMAL_ICON            "xfce-nomail"
#define DEFAULT_NEW_MAIL_ICON          "xfce-newmail"

typedef struct
{
    XfcePanelPlugin *plugin;
    XfceMailwatch *mailwatch;
    
    GtkWidget *button;
    GtkWidget *image;
    
    gboolean newmail_icon_visible;
    guint new_messages;
    GdkPixbuf *pix_normal;
    GdkPixbuf *pix_newmail;
    
    GtkTooltips *tooltip;
    
    gchar *click_command;
    gchar *new_messages_command;
    
    gchar *normal_icon;
    gchar *new_mail_icon;
    
    guint log_lines;
    gboolean show_log_status;

    GdkPixbuf               *pix_log[XFCE_MAILWATCH_N_LOG_LEVELS];
    XfceMailwatchLogLevel   log_status;
    GtkListStore            *loglist;

    GtkWidget *about_dialog;
} XfceMailwatchPlugin;

enum {
    LOGLIST_COLUMN_PIXBUF = 0,
    LOGLIST_COLUMN_TIME,
    LOGLIST_COLUMN_MESSAGE,
    LOGLIST_N_COLUMNS
};

static gboolean mailwatch_set_size(XfcePanelPlugin *plugin,
                                   gint wsize,
                                   XfceMailwatchPlugin *mwp);

static void
mailwatch_set_default_config(XfceMailwatchPlugin *mwp)
{
    mwp->log_lines = 200;
    mwp->show_log_status = TRUE;
}
        
static void
mailwatch_new_messages_changed_cb(XfceMailwatch *mailwatch, gpointer arg,
        gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    guint new_messages = GPOINTER_TO_UINT( arg );
    
    if(new_messages == 0 && mwp->newmail_icon_visible) {
        mwp->newmail_icon_visible = FALSE;
        mwp->new_messages = 0;
        mailwatch_set_size(mwp->plugin,
                           xfce_panel_plugin_get_size(mwp->plugin),
                           mwp);
        gtk_tooltips_set_tip(mwp->tooltip, mwp->button, _("No new mail"), NULL);
    } else if(new_messages > 0) {
        if(!mwp->newmail_icon_visible) {
            mwp->newmail_icon_visible = TRUE;
            mailwatch_set_size(mwp->plugin,
                               xfce_panel_plugin_get_size(mwp->plugin),
                               mwp);
        }
        if(new_messages != mwp->new_messages) {
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
            for(i = 0; mailbox_names[i]; i++) {
                if(new_message_counts[i] > 0) {
                    g_string_append_c(ttip_str, '\n');
                    g_string_append_printf(ttip_str,
                                           Q_("tells how many new messages in each mailbox|    %d in %s"),
                                           new_message_counts[i],
                                           mailbox_names[i]);
                }
            }
            
            g_strfreev(mailbox_names);
            g_free(new_message_counts);
            
            gtk_tooltips_set_tip(mwp->tooltip, mwp->button, ttip_str->str, NULL);
            g_string_free(ttip_str, TRUE);
            
            if(mwp->new_messages_command)
                xfce_exec(mwp->new_messages_command, FALSE, FALSE, NULL);
        }
    }
}

static gboolean
mailwatch_button_press_cb(GtkWidget *w,
                          GdkEventButton *evt,
                          gpointer user_data)
{
    if(evt->button == 2)
        gtk_button_pressed(GTK_BUTTON(w));
    
    return FALSE;
}

static gboolean
mailwatch_button_release_cb(GtkWidget *w, GdkEventButton *evt,
        gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    
    if(evt->x >= w->allocation.x
       && evt->x < w->allocation.x + w->allocation.width
       && evt->y >= w->allocation.y
       && evt->y < w->allocation.y + w->allocation.height)
    {
        switch(evt->button) {
            case 1:  /* left */
                if(mwp->click_command && *mwp->click_command)
                    xfce_exec(mwp->click_command, FALSE, FALSE, NULL);
                break;
        
            case 2:  /* middle */
                xfce_mailwatch_force_update(mwp->mailwatch);
                break;
        }
    }
    
    if(evt->button == 2)
        gtk_button_released(GTK_BUTTON(w));
    
    return FALSE;
}

static void
mailwatch_log_message_cb(XfceMailwatch *mailwatch,
                         gpointer arg,
                         gpointer user_data)
{
    XfceMailwatchLogEntry   *entry = arg;
    XfceMailwatchPlugin     *mwp = user_data;
    GtkTreeIter             iter;
    struct tm ltm;
    gchar time_str[256] = "", *new_message = NULL;
    
    if(localtime_r(&entry->timestamp, &ltm))
        strftime(time_str, 256, "%x %T:", &ltm);
    
    if(entry->level >= XFCE_MAILWATCH_N_LOG_LEVELS)
        entry->level = XFCE_MAILWATCH_N_LOG_LEVELS - 1;
    
    if(entry->mailbox_name) {
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
    
    if( entry->level > mwp->log_status ) {
        mwp->log_status = entry->level;
        mailwatch_set_size(mwp->plugin,
                           xfce_panel_plugin_get_size(mwp->plugin),
                           mwp);
    }
    
    while(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(mwp->loglist), NULL) > mwp->log_lines) {
        if(gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(mwp->loglist), &iter, NULL, 0))
            gtk_list_store_remove(mwp->loglist, &iter);
    }
}

static GdkPixbuf *
mailwatch_get_mini_icon(GtkWidget *dummy, const gchar *icon_name, gint size)
{
    GdkPixbuf *pix;
    
    pix = gtk_widget_render_icon(dummy,
                                 icon_name,
                                 GTK_ICON_SIZE_DIALOG,
                                 NULL);
    if(pix) {
        if(size / 2 != gdk_pixbuf_get_width(pix)) {
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
                     gboolean newmail)
{
    GdkPixbuf *pb = newmail ? gdk_pixbuf_copy(mwp->pix_newmail)
                            : gdk_pixbuf_copy(mwp->pix_normal);
    GdkPixbuf *overlay = NULL;

    if(mwp->log_status && mwp->show_log_status
       && mwp->log_status < XFCE_MAILWATCH_N_LOG_LEVELS)
    {
        overlay = mwp->pix_log[mwp->log_status];
    }
    
    if(overlay) {
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
mailwatch_set_size(XfcePanelPlugin *plugin, gint wsize, 
                   XfceMailwatchPlugin *mwp)
{
    gint size, img_width, img_height, width, height, i;
    gchar *icon;
    GdkPixbuf *pb;
    GtkIconTheme *itheme;
    GtkIconInfo *info;
    
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

    if(mwp->pix_normal)
        g_object_unref(G_OBJECT(mwp->pix_normal));
    if(mwp->pix_newmail)
        g_object_unref(G_OBJECT(mwp->pix_newmail));

    for(i = 0; i < XFCE_MAILWATCH_N_LOG_LEVELS; i++) {
        if(mwp->pix_log[i])
            g_object_unref(G_OBJECT(mwp->pix_log[i]));
    }

    /* find and load the two main icons, preserving aspect ratio */
    itheme = gtk_icon_theme_get_for_screen(gtk_widget_get_screen(GTK_WIDGET(plugin)));

    icon = mwp->normal_icon ? mwp->normal_icon : DEFAULT_NORMAL_ICON;
    info = gtk_icon_theme_lookup_icon(itheme, icon, size, 0);
    if(info) {
        const gchar *file = gtk_icon_info_get_filename(info);
        mwp->pix_normal = gdk_pixbuf_new_from_file_at_scale(file, img_width,
                                                            img_height, TRUE,
                                                            NULL);
        gtk_icon_info_free(info);
    }

    icon = mwp->new_mail_icon ? mwp->new_mail_icon : DEFAULT_NEW_MAIL_ICON;
    info = gtk_icon_theme_lookup_icon(itheme, icon, size, 0);
    if(info) {
        const gchar *file = gtk_icon_info_get_filename(info);
        mwp->pix_newmail = gdk_pixbuf_new_from_file_at_scale(file, img_width,
                                                            img_height, TRUE,
                                                            NULL);
        gtk_icon_info_free(info);
    }

    /* find the smallest dimensions of the two icons */
    width = gdk_pixbuf_get_width(mwp->pix_normal);
    if(gdk_pixbuf_get_width(mwp->pix_newmail) < width)
        width = gdk_pixbuf_get_width(mwp->pix_newmail);
    height = gdk_pixbuf_get_height(mwp->pix_normal);
    if(gdk_pixbuf_get_height(mwp->pix_newmail) < height)
        height = gdk_pixbuf_get_height(mwp->pix_newmail);

    if(!GTK_WIDGET_REALIZED(plugin))
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
    
    if(G_UNLIKELY(!mwp->mailwatch)) {
        xfce_message_dialog(NULL, _("Xfce Mailwatch"), GTK_STOCK_DIALOG_ERROR,
                _("The mailwatch applet cannot be added to the panel."),
                _("It is possible that your version of GLib does not have threads support."),
                GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT, NULL);
        g_free(mwp);
        return NULL;
    }
    
    mwp->tooltip = gtk_tooltips_new();

    mwp->button = xfce_create_panel_button();
    gtk_button_set_relief(GTK_BUTTON(mwp->button), GTK_RELIEF_NONE);
    gtk_widget_show(mwp->button);
    gtk_container_add(GTK_CONTAINER(plugin), mwp->button);
    g_signal_connect(mwp->button, "button-press-event",
            G_CALLBACK(mailwatch_button_press_cb), mwp);
    g_signal_connect(mwp->button, "button-release-event",
            G_CALLBACK(mailwatch_button_release_cb), mwp);
    gtk_tooltips_set_tip(mwp->tooltip, mwp->button, _("No new mail"), NULL);
    
    xfce_panel_plugin_add_action_widget(plugin, mwp->button);
    
    mwp->image = gtk_image_new();
    gtk_widget_show(mwp->image);
    gtk_container_add(GTK_CONTAINER(mwp->button), mwp->image);

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
mailwatch_read_config(XfcePanelPlugin *plugin, XfceMailwatchPlugin *mwp)
{
    const char *value;
    gchar *file;
    gboolean reload_icon = TRUE;
    XfceRc *rc;
    
    DBG("entering");
    
    if(!(file = xfce_panel_plugin_lookup_rc_file(plugin))) {
        DBG("Mailwatch: no config found; setting defaults");
        mailwatch_set_default_config(mwp);
        return;
    }
    
    rc = xfce_rc_simple_open(file, TRUE);

    if(!rc) {
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
    if(value) {
        mwp->normal_icon = g_strdup(value);
        reload_icon = TRUE;
    } else
        mwp->normal_icon = g_strdup(DEFAULT_NORMAL_ICON);
    
    value = xfce_rc_read_entry(rc, "new_mail_icon", NULL);
    if(value) {
        mwp->new_mail_icon = g_strdup(value);
        reload_icon = TRUE;
    } else
        mwp->new_mail_icon = g_strdup(DEFAULT_NEW_MAIL_ICON);
    
    if(reload_icon)
        mailwatch_set_size(plugin, xfce_panel_plugin_get_size(plugin), mwp);
    
    mwp->log_lines = xfce_rc_read_int_entry(rc, "log_lines", DEFAULT_LOG_LINES);
    
    mwp->show_log_status = xfce_rc_read_bool_entry(rc, "show_log_status", TRUE);
    
    xfce_rc_close(rc);
    
    xfce_mailwatch_set_config_file(mwp->mailwatch, file);
    xfce_mailwatch_load_config(mwp->mailwatch);
    
    g_free(file);
}

static void
mailwatch_write_config(XfcePanelPlugin *plugin, XfceMailwatchPlugin *mwp)
{
    gchar *file;
    XfceRc *rc;
    
    TRACE("entering");
    
    if(!(file = xfce_panel_plugin_save_location(plugin, TRUE))) {
        g_critical("Mailwatch: Unable to find save location for configuration file");
        return;
    }
    
    rc = xfce_rc_simple_open(file, FALSE);

    if(!rc) {
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
    
    xfce_rc_close(rc);
    
    xfce_mailwatch_set_config_file(mwp->mailwatch, file);
    xfce_mailwatch_save_config(mwp->mailwatch);
    
    g_free(file);
}

static gboolean
mailwatch_click_command_focusout_cb(GtkWidget *w, GdkEventFocus *evt,
        gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    gchar *command;
    
    g_free(mwp->click_command);
    
    command = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    mwp->click_command = g_strdup(command?command:"");
    
    return FALSE;
}

static void
mailwatch_log_window_response_cb(GtkDialog *dialog,
                                 gint response,
                                 gpointer user_data)
{
    if(response == XFCE_MAILWATCH_RESPONSE_CLEAR) {
        GtkListStore *ls = user_data;
        gtk_list_store_clear(ls);
    } else
        gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
mailwatch_zero_pointer(GtkWidget **w)
{
    if(w)
        *w = NULL;
}

static void
mailwatch_log_lines_changed_cb(GtkWidget *w, gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    
    mwp->log_lines = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w));
}

static void
mailwatch_log_status_toggled_cb(GtkToggleButton *tb, gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    gboolean have_new_msgs;
    
    mwp->show_log_status = gtk_toggle_button_get_active(tb);
    
    have_new_msgs = !!xfce_mailwatch_get_new_messages(mwp->mailwatch);
    mailwatch_set_size(mwp->plugin,
                       xfce_panel_plugin_get_size(mwp->plugin),
                       mwp);
}

static void
mailwatch_view_log_clicked_cb( GtkWidget *widget, gpointer user_data )
{
    XfceMailwatchPlugin     *mwp = user_data;
    static GtkWidget        *dialog = NULL;
    GtkWidget               *vbox, *hbox, *scrollw, *treeview, *button, *lbl,
                            *sbtn, *chk;
    
    if(dialog) {
        gtk_window_present(GTK_WINDOW(dialog));
        return;
    }

    mwp->log_status = 0;
    mailwatch_set_size(mwp->plugin,
                       xfce_panel_plugin_get_size(mwp->plugin),
                       mwp);

    dialog = gtk_dialog_new_with_buttons(_( "Mailwatch log" ),
                                         GTK_WINDOW(gtk_widget_get_toplevel(widget)),
                                         GTK_DIALOG_MODAL
                                         | GTK_DIALOG_DESTROY_WITH_PARENT
                                         | GTK_DIALOG_NO_SEPARATOR,
                                         NULL);
    gtk_widget_set_size_request(dialog, 480, 240 );
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(mailwatch_log_window_response_cb), mwp->loglist);
    g_signal_connect_swapped(G_OBJECT(dialog), "destroy",
                     G_CALLBACK(mailwatch_zero_pointer), &dialog);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), BORDER/2);
    gtk_widget_show(vbox);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), vbox, TRUE, TRUE, 0);

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
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button,
                                 XFCE_MAILWATCH_RESPONSE_CLEAR);
    
    button = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
    gtk_widget_show( button );
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button, GTK_RESPONSE_ACCEPT);
    
    gtk_widget_show(dialog);
}

static gboolean
mailwatch_newmsg_command_focusout_cb(GtkWidget *w, GdkEventFocus *evt,
        gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    gchar *command;
    
    g_free(mwp->new_messages_command);
    
    command = gtk_editable_get_chars(GTK_EDITABLE(w), 0, -1);
    mwp->new_messages_command = g_strdup(command?command:"");
    
    return FALSE;
}

static void
mailwatch_iconbtn_clicked_cb(GtkWidget *w, XfceMailwatchPlugin *mwp)
{
    gint icon_type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),
                                                       "mailwatch-icontype"));
    GtkWidget *chooser, *toplevel;
    
    g_return_if_fail(icon_type == 1 || icon_type == 2);
    
    toplevel = gtk_widget_get_toplevel(w);
    chooser = gtk_file_chooser_dialog_new(_("Select Icon"),
                                          GTK_WINDOW(toplevel),
                                          GTK_FILE_CHOOSER_ACTION_OPEN,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                          GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                          NULL);
    
    if(gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        if(filename) {
            switch(icon_type) {
                case 1:
                    {
                        GtkWidget *vbox, *img, *lbl;
                        
                        g_free(mwp->normal_icon);
                        mwp->normal_icon = filename;
                        mailwatch_set_size(mwp->plugin, 
                                xfce_panel_plugin_get_size(mwp->plugin), mwp);
                        
                        gtk_container_remove(GTK_CONTAINER(w),
                                             gtk_bin_get_child(GTK_BIN(w)));
                        
                        vbox = gtk_vbox_new(FALSE, BORDER/2);
                        gtk_widget_show(vbox);
                        gtk_container_add(GTK_CONTAINER(w), vbox);
                        
                        img = gtk_image_new_from_pixbuf(mwp->pix_normal);
                        gtk_widget_show(img);
                        gtk_box_pack_start(GTK_BOX(vbox), img, TRUE, TRUE, 0);
                        
                        lbl = gtk_label_new_with_mnemonic(_("_Normal"));
                        gtk_widget_show(lbl);
                        gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);
                    }
                    break;
                
                case 2:
                    {
                        GtkWidget *vbox, *img, *lbl;
                        
                        g_free(mwp->new_mail_icon);
                        mwp->new_mail_icon = filename;
                        mailwatch_set_size(mwp->plugin, 
                                xfce_panel_plugin_get_size(mwp->plugin), mwp);
                        
                        gtk_container_remove(GTK_CONTAINER(w),
                                             gtk_bin_get_child(GTK_BIN(w)));
                        
                        vbox = gtk_vbox_new(FALSE, BORDER/2);
                        gtk_widget_show(vbox);
                        gtk_container_add(GTK_CONTAINER(w), vbox);
                        
                        img = gtk_image_new_from_pixbuf(mwp->pix_newmail);
                        gtk_widget_show(img);
                        gtk_box_pack_start(GTK_BOX(vbox), img, TRUE, TRUE, 0);
                        
                        lbl = gtk_label_new_with_mnemonic(_("Ne_w mail"));
                        gtk_widget_show(lbl);
                        gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);
                    }
                    break;
            }
        }
    }
    gtk_widget_destroy(chooser);
}

static void
mailwatch_help_clicked_cb(GtkWidget *w,
                          gpointer user_data)
{
    GError *err = NULL;
    
    if(!xfce_exec("xfhelp4 xfce4-mailwatch-plugin.html", FALSE, FALSE, &err)) {
        gchar *secondary = g_strdup_printf(_("Help is unavailable because 'xfhelp4' could not be run: %s"),
                                           err->message);
        xfce_message_dialog(NULL, _("Help Unavailable"), GTK_STOCK_DIALOG_ERROR,
                            _("Failed to run xfhelp4"), secondary,
                            GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT, NULL);
        g_free(secondary);
        g_error_free(err);
    }
}

static void
mailwatch_dialog_response(GtkWidget *dlg, int response, 
                           XfceMailwatchPlugin *mwp)
{
    gtk_widget_destroy(dlg);
    xfce_panel_plugin_unblock_menu(mwp->plugin);
    mailwatch_write_config(mwp->plugin, mwp);
}

static void
mailwatch_create_options(XfcePanelPlugin *plugin, XfceMailwatchPlugin *mwp)
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
    gtk_widget_show(btn);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->action_area), btn, FALSE,
                       FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
                     G_CALLBACK(mailwatch_help_clicked_cb), mwp);
    
    btn = xfce_mailwatch_custom_button_new(_("_View Log..."), GTK_STOCK_FIND);
    gtk_widget_show(btn);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->action_area), btn, FALSE,
                       FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
                     G_CALLBACK(mailwatch_view_log_clicked_cb), mwp);
    
    btn = gtk_button_new_from_stock(GTK_STOCK_CLOSE);
    gtk_widget_show(btn);
    gtk_dialog_add_action_widget(GTK_DIALOG(dlg), btn, GTK_RESPONSE_ACCEPT);
                     
    topvbox = gtk_vbox_new(FALSE, BORDER);
    gtk_container_set_border_width(GTK_CONTAINER(topvbox), BORDER - 2);
    gtk_widget_show(topvbox);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), topvbox,
                        TRUE, TRUE, 0);
    
    cfg_page = xfce_mailwatch_get_configuration_page(mwp->mailwatch);
    if(cfg_page)
        gtk_box_pack_start(GTK_BOX(topvbox), GTK_WIDGET(cfg_page), TRUE, TRUE, 0);
    
    frame = xfce_mailwatch_create_framebox(_("External Programs"), &frame_bin);
    gtk_widget_show(frame);
    gtk_box_pack_start(GTK_BOX(topvbox), frame, FALSE, FALSE, 0);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(vbox);
    gtk_container_add(GTK_CONTAINER(frame_bin), vbox);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("Run _on click:"));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    
    entry = gtk_entry_new();
    if(mwp->click_command)
        gtk_entry_set_text(GTK_ENTRY(entry), mwp->click_command);
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), entry);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
            G_CALLBACK(mailwatch_click_command_focusout_cb), mwp);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("Run on new _messages:"));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    
    entry = gtk_entry_new();
    if(mwp->new_messages_command)
        gtk_entry_set_text(GTK_ENTRY(entry), mwp->new_messages_command);
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), entry);
    g_signal_connect(G_OBJECT(entry), "focus-out-event",
            G_CALLBACK(mailwatch_newmsg_command_focusout_cb), mwp);
    
    frame = xfce_mailwatch_create_framebox(_("Icons"), &frame_bin);
    gtk_widget_show(frame);
    gtk_box_pack_start(GTK_BOX(topvbox), frame, FALSE, FALSE, 0);
    
    sg = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_container_add(GTK_CONTAINER(frame_bin), hbox);
    
    btn = gtk_button_new();
    g_object_set_data(G_OBJECT(btn), "mailwatch-icontype", GINT_TO_POINTER(1));
    gtk_widget_show(btn);
    gtk_box_pack_start(GTK_BOX(hbox), btn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
                     G_CALLBACK(mailwatch_iconbtn_clicked_cb), mwp);
    gtk_size_group_add_widget(sg, btn);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(vbox);
    gtk_container_add(GTK_CONTAINER(btn), vbox);
    
    img = gtk_image_new_from_pixbuf(mwp->pix_normal);
    gtk_widget_show(img);
    gtk_box_pack_start(GTK_BOX(vbox), img, TRUE, TRUE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("_Normal"));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);
    
    btn = gtk_button_new();
    g_object_set_data(G_OBJECT(btn), "mailwatch-icontype", GINT_TO_POINTER(2));
    gtk_widget_show(btn);
    gtk_box_pack_start(GTK_BOX(hbox), btn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
                     G_CALLBACK(mailwatch_iconbtn_clicked_cb), mwp);
    gtk_size_group_add_widget(sg, btn);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(vbox);
    gtk_container_add(GTK_CONTAINER(btn), vbox);
    
    img = gtk_image_new_from_pixbuf(mwp->pix_newmail);
    gtk_widget_show(img);
    gtk_box_pack_start(GTK_BOX(vbox), img, TRUE, TRUE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("Ne_w Mail"));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(vbox), lbl, FALSE, FALSE, 0);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(topvbox), hbox, FALSE, FALSE, 0);
   
    gtk_widget_show(dlg);
}

static void
mailwatch_free(XfcePanelPlugin *plugin, XfceMailwatchPlugin *mwp)
{
    gint i;

    if(mwp->about_dialog)
        gtk_widget_destroy(mwp->about_dialog);
    
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
    
    gtk_object_sink(GTK_OBJECT(mwp->tooltip));
    
    g_free(mwp);
}

static void
mailwatch_update_now_clicked_cb(GtkMenuItem *mi,
                                gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    xfce_mailwatch_force_update(mwp->mailwatch);
}

static void
mailwatch_show_about(XfcePanelPlugin *plugin,
                     gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    XfceAboutInfo *ainfo;
    GdkPixbuf *icon;

    if(G_UNLIKELY(mwp->about_dialog)) {
        gtk_window_present(GTK_WINDOW(mwp->about_dialog));
        return;
    }

    ainfo = xfce_about_info_new(_("Xfce4 Mailwatch Plugin"),
                                VERSION,
                                _("A featureful mail-checker applet for the Xfce Panel"),
                                _("Copyright (c) 2005-2008 Brian Tarricone\n"
                                  "Copyright (c) 2005 Pasi Orovuo"),
                                  XFCE_LICENSE_GPL);
    xfce_about_info_set_homepage(ainfo, WEBSITE);
    xfce_about_info_add_credit(ainfo, "Brian J. Tarricone",
                               "bjt23@cornell.edu",
                               _("Maintainer, Original Author"));
    xfce_about_info_add_credit(ainfo, "Pasi Orovuo", "pasi.ov@gmail.com",
                               _("Developer"));
    
    icon = xfce_themed_icon_load("xfce-mail", 32);
    
    mwp->about_dialog = xfce_about_dialog_new_with_values(NULL, ainfo, icon);
    g_object_add_weak_pointer(G_OBJECT(mwp->about_dialog),
                              (gpointer)&mwp->about_dialog);
    gtk_widget_show_all(mwp->about_dialog);
    g_signal_connect(G_OBJECT(mwp->about_dialog), "response",
                     G_CALLBACK(gtk_widget_destroy), NULL);

    if(icon)
        g_object_unref(G_OBJECT(icon));
}

static void
mailwatch_construct(XfcePanelPlugin *plugin)
{
    XfceMailwatchPlugin *mwp;
    GtkWidget *mi, *img;

    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");

    if(!(mwp = mailwatch_create(plugin)))
        exit(1);
    
    mailwatch_read_config(plugin, mwp);

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

XFCE_PANEL_PLUGIN_REGISTER_EXTERNAL(mailwatch_construct);

