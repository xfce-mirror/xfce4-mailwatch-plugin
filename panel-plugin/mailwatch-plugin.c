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

#define BORDER 8

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
} XfceMailwatchPlugin;


static void
mailwatch_new_messages_changed_cb(XfceMailwatch *mailwatch, guint new_messages,
        gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    
    if(new_messages == 0 && mwp->newmail_icon_visible) {
        xfce_scaled_image_set_from_pixbuf(XFCE_SCALED_IMAGE(mwp->image),
                mwp->pix_normal);
        gtk_tooltips_set_tip(mwp->tooltip, mwp->button, _("No new mail"), NULL);
        mwp->newmail_icon_visible = FALSE;
        mwp->new_messages = 0;
    } else if(new_messages > 0) {
        if(!mwp->newmail_icon_visible) {
            xfce_scaled_image_set_from_pixbuf(XFCE_SCALED_IMAGE(mwp->image),
                    mwp->pix_newmail);
            mwp->newmail_icon_visible = TRUE;
        }
        if(new_messages != mwp->new_messages) {
            GString *ttip_str = g_string_sized_new(32);
            gchar **mailbox_names = NULL;
            guint *new_message_counts = NULL;
            gint i;
            
            if(new_messages == 1)
                g_string_assign(ttip_str, _("You have 1 new message:\n"));
            else {
                gchar *str = g_strdup_printf(_("You have %d new messages:\n"),
                        new_messages);
                g_string_assign(ttip_str, str);
                g_free(str);
            }
            mwp->new_messages = new_messages;
            
            xfce_mailwatch_get_new_message_breakdown(mwp->mailwatch,
                    &mailbox_names, &new_message_counts);
            for(i = 0; mailbox_names[i]; i++) {
                if(new_message_counts[i] > 0) {
                    g_string_append_printf(ttip_str, "    %s: %d%s",
                            mailbox_names[i], new_message_counts[i],
                            (mailbox_names[i+1] ? "\n" : ""));
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
mailwatch_button_release_cb(GtkWidget *w, GdkEventButton *evt,
        gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    
    switch(evt->button) {
        case 1:  /* left */
            if(mwp->click_command && *mwp->click_command)
                xfce_exec(mwp->click_command, FALSE, FALSE, NULL);
            break;
        
        case 2:  /* middle */
            xfce_mailwatch_force_update(mwp->mailwatch);
            break;
    }
    
    return FALSE;
}


/*
 * xfce4-panel functions
 */

static gboolean
mailwatch_create(Control *c)
{
    XfceMailwatchPlugin *mwp = g_new0(XfceMailwatchPlugin, 1);
    c->data = mwp;
    
    mwp->mailwatch = xfce_mailwatch_new();
    
    mwp->tooltip = gtk_tooltips_new();
    
    mwp->button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(mwp->button), GTK_RELIEF_NONE);
    gtk_widget_show(mwp->button);
    gtk_container_add(GTK_CONTAINER(c->base), mwp->button);
    g_signal_connect(mwp->button, "button-release-event",
            G_CALLBACK(mailwatch_button_release_cb), mwp);
    gtk_tooltips_set_tip(mwp->tooltip, mwp->button, _("No new mail"), NULL);
    
    mwp->image = xfce_scaled_image_new();
    gtk_widget_show(mwp->image);
    gtk_container_add(GTK_CONTAINER(mwp->button), mwp->image);
    
    xfce_mailwatch_signal_connect(mwp->mailwatch,
            XFCE_MAILWATCH_SIGNAL_NEW_MESSAGE_COUNT_CHANGED,
            mailwatch_new_messages_changed_cb, mwp);
    
    return TRUE;
}

static void
mailwatch_read_config(Control *c, xmlNodePtr node)
{
    XfceMailwatchPlugin *mwp = c->data;
    xmlChar *value;
    gchar *cfgfile;
    
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
    gchar *p, *cfgfile_suffix;
    
    DBG("entering(%p, %p) (%s)",c, node, cfgfile?cfgfile:"[nil]");
    
    xmlSetProp(node, (const xmlChar *)"click_command",
            mwp->click_command?mwp->click_command:"");
    xmlSetProp(node, (const xmlChar *)"new_messages_command",
            mwp->new_messages_command?mwp->new_messages_command:"");
    
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
mailwatch_create_options(Control *c, GtkContainer *con, GtkWidget *done)
{
    XfceMailwatchPlugin *mwp = c->data;
    GtkWidget *vbox, *hbox, *lbl, *entry;
    GtkContainer *cfg_page;
    
    xfce_textdomain(GETTEXT_PACKAGE, LOCALEDIR, "UTF-8");
    
    vbox = gtk_vbox_new(FALSE, BORDER/2);
    gtk_widget_show(vbox);
    gtk_container_add(con, vbox);
    
    cfg_page = xfce_mailwatch_get_configuration_page(mwp->mailwatch);
    if(cfg_page)
        gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(cfg_page), TRUE, TRUE, 0);
    
    hbox = gtk_hbox_new(FALSE, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("_Run on click:"));
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
    
    lbl = gtk_label_new_with_mnemonic(_("Run on _new messages:"));
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
}

static void
mailwatch_free(Control *c)
{
    XfceMailwatchPlugin *mwp = c->data;
    
    xfce_mailwatch_destroy(mwp->mailwatch);
    
    if(mwp->pix_normal)
        g_object_unref(G_OBJECT(mwp->pix_normal));
    if(mwp->pix_newmail)
        g_object_unref(G_OBJECT(mwp->pix_newmail));
    
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

static void
mailwatch_set_size(Control *c, gint size)
{
    XfceMailwatchPlugin *mwp = c->data;
    gint wsize = icon_size[size] + border_width;
    
    if(mwp->pix_normal)
        g_object_unref(G_OBJECT(mwp->pix_normal));
    if(mwp->pix_newmail)
        g_object_unref(G_OBJECT(mwp->pix_newmail));
    
    mwp->pix_normal = xfce_themed_icon_load("xfce-nomail", icon_size[size]);
    mwp->pix_newmail = xfce_themed_icon_load("xfce-newmail", icon_size[size]);
    
    if(!mwp->newmail_icon_visible) {
        if(mwp->pix_normal)
            xfce_scaled_image_set_from_pixbuf(XFCE_SCALED_IMAGE(mwp->image),
                    mwp->pix_normal);
    } else {
        if(mwp->pix_newmail)
            xfce_scaled_image_set_from_pixbuf(XFCE_SCALED_IMAGE(mwp->image),
                    mwp->pix_newmail);
    }
    
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
