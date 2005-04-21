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

typedef struct
{
    XfceMailwatch *mailwatch;
    
    GtkWidget *button;
    GtkWidget *image;
    
    gboolean newmail_icon_visible;
    GdkPixbuf *pix_normal;
    GdkPixbuf *pix_newmail;
    
    GtkTooltips *tooltip;
    
    guint check_timeout_id;
} XfceMailwatchPlugin;


static gboolean
mailwatch_check_timeout(gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    guint new_messages;
    
    new_messages = xfce_mailwatch_get_new_messages(mwp->mailwatch);
    if(new_messages == 0 && mwp->newmail_icon_visible) {
        xfce_scaled_image_set_from_pixbuf(XFCE_SCALED_IMAGE(mwp->image),
                mwp->pix_normal);
        gtk_tooltips_set_tip(mwp->tooltip, mwp->button, _("No new mail"), NULL);
        mwp->newmail_icon_visible = FALSE;
    } else if(new_messages > 0 && !mwp->newmail_icon_visible) {
        xfce_scaled_image_set_from_pixbuf(XFCE_SCALED_IMAGE(mwp->image),
                mwp->pix_newmail);
        if(new_messages == 1) {
            gtk_tooltips_set_tip(mwp->tooltip, mwp->button,
                    _("You have 1 new message"), NULL);
        } else {
            gchar *str = g_strdup_printf(_("You have %d new messages"),
                    new_messages);
            gtk_tooltips_set_tip(mwp->tooltip, mwp->button, str, NULL);
            g_free(str);
        }
        mwp->newmail_icon_visible = TRUE;
    }
    
    return TRUE;
}

static void
mailwatch_timeout_changed_cb(XfceMailwatch *mailwatch, guint timeout,
        gpointer user_data)
{
    XfceMailwatchPlugin *mwp = user_data;
    
    if(mwp->check_timeout_id)
        g_source_remove(mwp->check_timeout_id);
    mwp->check_timeout_id = g_timeout_add(timeout,
            (GSourceFunc)mailwatch_check_timeout, mwp);
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
    
    mwp->image = xfce_scaled_image_new();
    gtk_widget_show(mwp->image);
    gtk_container_add(GTK_CONTAINER(mwp->button), mwp->image);
    
    return TRUE;
}

static void
mailwatch_read_config(Control *c, xmlNodePtr node)
{
    XfceMailwatchPlugin *mwp = c->data;
    xmlChar *value;
    gchar *cfgfile;
    
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
    xfce_mailwatch_load_config(mwp->mailwatch);
    g_free(cfgfile);
    
    xfce_mailwatch_hook_timeout_change(mwp->mailwatch,
            mailwatch_timeout_changed_cb, mwp);
    mwp->check_timeout_id = g_timeout_add(xfce_mailwatch_get_timeout(mwp->mailwatch),
            (GSourceFunc)mailwatch_check_timeout, mwp);
}

static void
mailwatch_write_config(Control *c, xmlNodePtr node)
{
    XfceMailwatchPlugin *mwp = c->data;
    const gchar *cfgfile = xfce_mailwatch_get_config_file(mwp->mailwatch);
    gchar *p, *cfgfile_suffix;
    
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

static void
mailwatch_create_options(Control *c, GtkContainer *con, GtkWidget *done)
{
    XfceMailwatchPlugin *mwp = c->data;
    GtkContainer *cfg_page;
    
    cfg_page = xfce_mailwatch_get_configuration_page(mwp->mailwatch);
    if(cfg_page)
        gtk_container_add(con, GTK_WIDGET(cfg_page));
}

static void
mailwatch_free(Control *c)
{
    XfceMailwatchPlugin *mwp = c->data;
    
    if(mwp->check_timeout_id) {
        g_source_remove(mwp->check_timeout_id);
        mwp->check_timeout_id = 0;
    }
    
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
