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

#include <gtk/gtk.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/libxfcegui4.h>
#include <libxfcegui4/xfce_scaled_image.h>

#include <panel/plugins.h>
#include <panel/xfce.h>

#include "mailwatch.h"
#include "mailwatch-mailbox.h"
#include "mailwatch-utils.h"

#define BORDER                             8
#define DEFAULT_LOG_LINES                500
#define XFCE_MAILWATCH_RESPONSE_CLEAR  72347
#define DEFAULT_NORMAL_ICON            "xfce-nomail"
#define DEFAULT_NEW_MAIL_ICON          "xfce-newmail"

static void mailwatch_set_size(Control *c, gint size);

typedef struct
{
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
} XfceMailwatchPlugin;

enum {
    LOGLIST_COLUMN_PIXBUF = 0,
    LOGLIST_COLUMN_TIME,
    LOGLIST_COLUMN_MESSAGE,
    LOGLIST_N_COLUMNS
};

static void
mailwatch_set_icon(XfceMailwatchPlugin *mwp, gboolean newmail)
{
    GdkPixbuf       *pb = newmail ?
                            gdk_pixbuf_copy(mwp->pix_newmail) :
                            gdk_pixbuf_copy(mwp->pix_normal);
    GdkPixbuf       *overlay = NULL;
    gint h, ow, oh;

    if ( mwp->log_status && mwp->show_log_status &&
            mwp->log_status < XFCE_MAILWATCH_N_LOG_LEVELS ) {
        overlay = mwp->pix_log[mwp->log_status];
    }
    
    h = gdk_pixbuf_get_height(pb);
    if(overlay) {
        ow = gdk_pixbuf_get_width(overlay);
        oh = gdk_pixbuf_get_height(overlay);
        gdk_pixbuf_composite(overlay, pb, 0, h - oh, ow, oh, 0, h - oh,
                             1.0, 1.0, GDK_INTERP_BILINEAR, 255);
    }
    
    xfce_scaled_image_set_from_pixbuf(XFCE_SCALED_IMAGE(mwp->image ), pb);
    g_object_unref(G_OBJECT(pb));
}
        
static void
mailwatch_new_messages_changed_cb(XfceMailwatch *mailwatch, gpointer arg,
        gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    guint new_messages = GPOINTER_TO_UINT( arg );
    
    if(new_messages == 0 && mwp->newmail_icon_visible) {
        mailwatch_set_icon( mwp, FALSE );
        gtk_tooltips_set_tip(mwp->tooltip, mwp->button, _("No new mail"), NULL);
        mwp->newmail_icon_visible = FALSE;
        mwp->new_messages = 0;
    } else if(new_messages > 0) {
        if(!mwp->newmail_icon_visible) {
            mailwatch_set_icon( mwp, TRUE );
            mwp->newmail_icon_visible = TRUE;
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
mailwatch_log_message_cb( XfceMailwatch *mailwatch, gpointer arg, gpointer user_data )
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
    
    if ( entry->level > mwp->log_status ) {
        mwp->log_status = entry->level;
        mailwatch_set_icon( mwp, mwp->newmail_icon_visible );
    }
    
    while(gtk_tree_model_iter_n_children(GTK_TREE_MODEL(mwp->loglist), NULL) > mwp->log_lines) {
        if(gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(mwp->loglist), &iter, NULL, 0))
            gtk_list_store_remove(mwp->loglist, &iter);
    }
}

/*
 * xfce4-panel functions
 */

static gboolean
mailwatch_create(Control *c)
{
    XfceMailwatchPlugin *mwp = g_new0(XfceMailwatchPlugin, 1);
    c->data = mwp;
    
    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");
    
    mwp->mailwatch = xfce_mailwatch_new();
    
    if(G_UNLIKELY(!mwp->mailwatch)) {
        xfce_message_dialog(NULL, _("Xfce Mailwatch"), GTK_STOCK_DIALOG_ERROR,
                _("The mailwatch applet cannot be added to the panel."),
                _("It is possible that your version of GLib does not have threads support."),
                GTK_STOCK_CLOSE, GTK_RESPONSE_ACCEPT, NULL);
        g_free(mwp);
        return FALSE;
    }
    
    mwp->tooltip = gtk_tooltips_new();
    
    mwp->button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(mwp->button), GTK_RELIEF_NONE);
    gtk_widget_show(mwp->button);
    gtk_container_add(GTK_CONTAINER(c->base), mwp->button);
    g_signal_connect(mwp->button, "button-press-event",
            G_CALLBACK(mailwatch_button_press_cb), mwp);
    g_signal_connect(mwp->button, "button-release-event",
            G_CALLBACK(mailwatch_button_release_cb), mwp);
    gtk_tooltips_set_tip(mwp->tooltip, mwp->button, _("No new mail"), NULL);
    
    mwp->image = xfce_scaled_image_new();
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
    
    xfce_mailwatch_force_update(mwp->mailwatch);
    
    return TRUE;
}

static void
mailwatch_read_config(Control *c, xmlNodePtr node)
{
    XfceMailwatchPlugin *mwp = c->data;
    xmlChar *value;
    gchar *cfgfile;
    gboolean reload_icon = TRUE;
    
    DBG("entering");
    
    value = xmlGetProp(node, (const xmlChar *)"click_command");
    if(value) {
        mwp->click_command = g_strdup(value);
        xmlFree(value);
    }
    
    value = xmlGetProp(node, (const xmlChar *)"new_messages_command");
    if(value) {
        mwp->new_messages_command = g_strdup(value);
        xmlFree(value);
    }
    
    value = xmlGetProp(node, (const xmlChar *)"normal_icon");
    if(value) {
        mwp->normal_icon = g_strdup(value);
        xmlFree(value);
        reload_icon = TRUE;
    } else
        mwp->normal_icon = g_strdup(DEFAULT_NORMAL_ICON);
    
    value = xmlGetProp(node, (const xmlChar *)"new_mail_icon");
    if(value) {
        mwp->new_mail_icon = g_strdup(value);
        xmlFree(value);
        reload_icon = TRUE;
    } else
        mwp->new_mail_icon = g_strdup(DEFAULT_NEW_MAIL_ICON);
    
    if(reload_icon)
        mailwatch_set_size(c, settings.size);
    
    value = xmlGetProp(node, (const xmlChar *)"log_lines");
    if(value) {
        mwp->log_lines = atoi(value);
        xmlFree(value);
    } else
        mwp->log_lines = DEFAULT_LOG_LINES;
    
    value = xmlGetProp(node, (const xmlChar *)"show_log_status");
    if(value) {
        mwp->show_log_status = (*value == '0' ? FALSE : TRUE);
        xmlFree(value);
    } else
        mwp->show_log_status = TRUE;
    
    value = xmlGetProp(node, (const xmlChar *)"cfgfile_suffix");
    if(!value) {
        GTimeVal gtv = { 0, 0 };
        
        g_get_current_time(&gtv);
        cfgfile = g_strdup_printf("xfce4/panel/mailwatch/mailwatch.%ld.%ld.rc",
                gtv.tv_sec, gtv.tv_usec);
    } else {
        cfgfile = g_strdup_printf("xfce4/panel/mailwatch/mailwatch.%s.rc", value);
        xmlFree(value);
    }
    
    xfce_mailwatch_set_config_file(mwp->mailwatch, cfgfile);
    DBG("cfgfile = %s", cfgfile);
    xfce_mailwatch_load_config(mwp->mailwatch);
    g_free(cfgfile);
}

static void
mailwatch_write_config(Control *c, xmlNodePtr node)
{
    XfceMailwatchPlugin *mwp = c->data;
    const gchar *cfgfile = xfce_mailwatch_get_config_file(mwp->mailwatch);
    gchar *p, *cfgfile_suffix, buf[128];
    
    DBG("entering(%p, %p) (%s)",c, node, cfgfile?cfgfile:"[nil]");
    
    xmlSetProp(node, (const xmlChar *)"click_command",
            mwp->click_command?mwp->click_command:"");
    xmlSetProp(node, (const xmlChar *)"new_messages_command",
            mwp->new_messages_command?mwp->new_messages_command:"");
    
    xmlSetProp(node, (const xmlChar *)"normal_icon",
            mwp->normal_icon?mwp->normal_icon:DEFAULT_NORMAL_ICON);
    xmlSetProp(node, (const xmlChar *)"new_mail_icon",
            mwp->new_mail_icon?mwp->new_mail_icon:DEFAULT_NEW_MAIL_ICON);
    
    g_snprintf(buf, 256, "%u", mwp->log_lines);
    xmlSetProp(node, (const xmlChar *)"log_lines", buf);
    
    xmlSetProp(node, (const xmlChar *)"show_log_status",
               mwp->show_log_status ? "1" : "0");
    
    if(!cfgfile)
        return;
    
    xfce_mailwatch_save_config(mwp->mailwatch);
    
    p = g_strrstr(cfgfile, ".rc");
    if(!p || p - cfgfile <= 32 || strlen(cfgfile+32) <= 3) {
        g_critical("xfce4-mailwatch-plugin: config file length is not as expected");
        return;
    }
    
    cfgfile_suffix = g_strndup(cfgfile+32, strlen(cfgfile+32)-3);
    xmlSetProp(node, (const xmlChar *)"cfgfile_suffix", cfgfile_suffix);
    g_free(cfgfile_suffix);
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
    mailwatch_set_icon(mwp, have_new_msgs);
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

    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");
    
    mwp->log_status = 0;
    mailwatch_set_icon( mwp, mwp->newmail_icon_visible );

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
mailwatch_iconbtn_clicked_cb(GtkWidget *w, gpointer user_data)
{
    Control *c = user_data;
    XfceMailwatchPlugin *mwp = c->data;
    gint icon_type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),
                                                       "mailwatch-icontype"));
    GtkWidget *chooser, *toplevel;
    
    g_return_if_fail(icon_type == 1 || icon_type == 2);
    
    toplevel = gtk_widget_get_toplevel(w);
    chooser = xfce_file_chooser_dialog_new(_("Select Icon"),
                                           GTK_WINDOW(toplevel),
                                           XFCE_FILE_CHOOSER_ACTION_OPEN,
                                           GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                           GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                           NULL);
    
    if(gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename = xfce_file_chooser_get_filename(XFCE_FILE_CHOOSER(chooser));
        if(filename) {
            switch(icon_type) {
                case 1:
                    {
                        GtkWidget *vbox, *img, *lbl;
                        
                        g_free(mwp->normal_icon);
                        mwp->normal_icon = filename;
                        mailwatch_set_size(c, settings.size);
                        
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
                        mailwatch_set_size(c, settings.size);
                        
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
mailwatch_create_options(Control *c, GtkContainer *con, GtkWidget *done)
{
    XfceMailwatchPlugin *mwp = c->data;
    GtkWidget *topvbox, *frame, *hbox, *lbl, *entry, *btn, *vbox, *img;
    GtkContainer *cfg_page;
    GtkSizeGroup *sg;
    
    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");
    
    topvbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(topvbox);
    gtk_container_add(con, topvbox);
    
    cfg_page = xfce_mailwatch_get_configuration_page(mwp->mailwatch);
    if(cfg_page)
        gtk_box_pack_start(GTK_BOX(topvbox), GTK_WIDGET(cfg_page), TRUE, TRUE, 0);
    
    frame = xfce_framebox_new(_("External Programs"), TRUE);
    gtk_widget_show(frame);
    gtk_box_pack_start(GTK_BOX(topvbox), frame, FALSE, FALSE, 0);
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(vbox);
    xfce_framebox_add(XFCE_FRAMEBOX(frame), vbox);
    
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
    
    frame = xfce_framebox_new(_("Icons"), TRUE);
    gtk_widget_show(frame);
    gtk_box_pack_start(GTK_BOX(topvbox), frame, FALSE, FALSE, 0);
    
    sg = gtk_size_group_new(GTK_SIZE_GROUP_BOTH);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    xfce_framebox_add(XFCE_FRAMEBOX(frame), hbox);
    
    btn = gtk_button_new();
    g_object_set_data(G_OBJECT(btn), "mailwatch-icontype", GINT_TO_POINTER(1));
    gtk_widget_show(btn);
    gtk_box_pack_start(GTK_BOX(hbox), btn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
                     G_CALLBACK(mailwatch_iconbtn_clicked_cb), c);
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
                     G_CALLBACK(mailwatch_iconbtn_clicked_cb), c);
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
   
    btn = xfce_mailwatch_custom_button_new(_("_View Log..."), GTK_STOCK_FIND);
    gtk_widget_show(btn);
    gtk_box_pack_end(GTK_BOX(hbox), btn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
                     G_CALLBACK(mailwatch_view_log_clicked_cb), mwp);
}

static void
mailwatch_free(Control *c)
{
    XfceMailwatchPlugin *mwp = c->data;
    gint i;
    
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
    c->data = NULL;
}

static void
mailwatch_attach_callback(Control *c, const gchar *signal, GCallback callback,
        gpointer data)
{
    XfceMailwatchPlugin *mwp = c->data;
    
    g_signal_connect(G_OBJECT(mwp->button), signal, callback, data);
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
        if(icon_size[size] / 2 != gdk_pixbuf_get_width(pix)) {
            GdkPixbuf *tmp = gdk_pixbuf_scale_simple(pix, icon_size[size] / 2,
                                                      icon_size[size] / 2,
                                                      GDK_INTERP_BILINEAR);
            g_object_unref(G_OBJECT(pix));
            pix = tmp;
        }
    }
    
    return pix;
}

static void
mailwatch_set_size(Control *c, gint size)
{
    XfceMailwatchPlugin *mwp = c->data;
    gint wsize = icon_size[size] + border_width, i;
    GtkWidget *dummy;
    gchar *icon;
    
    if(mwp->pix_normal)
        g_object_unref(G_OBJECT(mwp->pix_normal));
    if(mwp->pix_newmail)
        g_object_unref(G_OBJECT(mwp->pix_newmail));

    for(i = 0; i < XFCE_MAILWATCH_N_LOG_LEVELS; i++) {
        if(mwp->pix_log[i])
            g_object_unref(G_OBJECT(mwp->pix_log[i]));
    }
    
    icon = mwp->normal_icon ? mwp->normal_icon : DEFAULT_NORMAL_ICON;
    mwp->pix_normal = xfce_themed_icon_load(icon, icon_size[size]);
    icon = mwp->new_mail_icon ? mwp->new_mail_icon : DEFAULT_NEW_MAIL_ICON;
    mwp->pix_newmail = xfce_themed_icon_load(icon, icon_size[size]);

    dummy = gtk_invisible_new();
    gtk_widget_realize(dummy);
    
    mwp->pix_log[XFCE_MAILWATCH_LOG_INFO] =
            mailwatch_get_mini_icon(dummy, GTK_STOCK_DIALOG_INFO, size);
    mwp->pix_log[XFCE_MAILWATCH_LOG_WARNING] =
            mailwatch_get_mini_icon(dummy, GTK_STOCK_DIALOG_WARNING, size);
    mwp->pix_log[XFCE_MAILWATCH_LOG_ERROR] = 
            mailwatch_get_mini_icon(dummy, GTK_STOCK_DIALOG_ERROR, size);
    
    gtk_widget_destroy(dummy);
    
    mailwatch_set_icon(mwp, mwp->newmail_icon_visible);
    
    gtk_widget_set_size_request(c->base, wsize, wsize);
}

G_MODULE_EXPORT void
xfce_control_class_init(ControlClass *cc)
{
    cc->name = "xfce-mailwatch";
    
    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");
    cc->caption = _("Mail Watcher");
    
    cc->create_control = mailwatch_create;
    cc->read_config = mailwatch_read_config;
    cc->write_config = mailwatch_write_config;
    cc->create_options = mailwatch_create_options;
    cc->free = mailwatch_free;
    cc->attach_callback = mailwatch_attach_callback;
    cc->set_size = mailwatch_set_size;
    cc->set_orientation = NULL;
}


XFCE_PLUGIN_CHECK_INIT
