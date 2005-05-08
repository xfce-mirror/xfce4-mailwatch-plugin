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

#include <libxfcegui4/libxfcegui4.h>

#include "mailwatch-utils.h"

GtkWidget *
xfce_mailwatch_custom_button_new(const gchar *text, const gchar *icon)
{
    GtkWidget *btn, *hbox, *img, *lbl;
    GdkPixbuf *pix;
    gint iw, ih;
    
    g_return_val_if_fail((text && *text) || icon, NULL);
    
    btn = gtk_button_new();
    
    hbox = gtk_hbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 0);
    gtk_widget_show(hbox);
    gtk_container_add(GTK_CONTAINER(btn), hbox);
    
    if(icon) {
        img = gtk_image_new_from_stock(icon, GTK_ICON_SIZE_BUTTON);
        if(!img || gtk_image_get_storage_type(GTK_IMAGE(img)) == GTK_IMAGE_EMPTY) {
            gtk_icon_size_lookup(GTK_ICON_SIZE_BUTTON, &iw, &ih);
            pix = xfce_themed_icon_load(icon, iw);
            if(pix) {
                if(img)
                    gtk_image_set_from_pixbuf(GTK_IMAGE(img), pix);
                else
                    img = gtk_image_new_from_pixbuf(pix);
                g_object_unref(G_OBJECT(pix));
            }
        }
        if(img) {
            gtk_widget_show(img);
            gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);
        }
    }
    
    if(text) {
        lbl = gtk_label_new_with_mnemonic(text);
        gtk_widget_show(lbl);
        gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
        gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), btn);
    }
    
    return btn;
}
