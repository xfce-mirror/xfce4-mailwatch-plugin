/*
 *  xfce4-mailwatch-plugin - a mail notification applet for the xfce4 panel
 *  Copyright (c) 2005 Pasi Orovuo <pasi.ov@gmail.com>
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

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#include <gtk/gtk.h>

#include <dirent.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/libxfcegui4.h>

#include "mailwatch.h"

#define XFCE_MAILWATCH_MAILDIR_MAILBOX( p ) ( (XfceMailwatchMaildirMailbox *) p )
#define BORDER                              ( 8 )

typedef struct {
    XfceMailwatchMailbox    xfce_mailwatch_mailbox;

    XfceMailwatch           *mailwatch;

    gchar                   *path;
    gboolean                active;
    guint                   timeout_id;
    GtkWidget               *path_entry;
} XfceMailwatchMaildirMailbox;

static gboolean
maildir_check_mail( gpointer data )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( data );
    DIR                         *dir;
    struct dirent               *dent;
    gchar                       *path;
    int                         count_new = 0;

    g_return_val_if_fail( maildir->path, TRUE );

    if ( !maildir->active ) return ( TRUE );
    
    path = g_build_filename( maildir->path, "new", NULL );
    if ( path ) {
        dir = opendir( path );
        if ( dir ) {
            while ( ( dent = readdir( dir ) ) ) {
                if ( dent->d_name[0] != '.' && dent->d_reclen > 1 ) {
                    count_new++;
                }
            }
            closedir( dir );
        }
        g_free( path );
    }

    xfce_mailwatch_signal_new_messages( maildir->mailwatch, (XfceMailwatchMailbox *) maildir, count_new );

    return ( TRUE );
}

static void
maildir_free( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMaildirMailbox     *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );

    if ( maildir->timeout_id ) {
        g_source_remove( maildir->timeout_id );
    }
    if ( maildir->path ) {
        g_free( maildir->path );
    }
    g_free( maildir );
}

static GList *
maildir_save_param_list( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    XfceMailwatchParam          *param;
    GList                       *param_list = NULL;

    param = g_new( XfceMailwatchParam, 1 );
    param->key      = g_strdup( "path" );
    param->value    = g_strdup( ( maildir->path ) ? maildir->path : "" );
    param_list = g_list_append( param_list, param );

    return ( param_list );
}

static void
maildir_restore_param_list( XfceMailwatchMailbox *mailbox, GList *params )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    GList                       *li;
    
    for ( li = g_list_first( params ); li != NULL; li = g_list_next( li ) ) {
        XfceMailwatchParam  *param = (XfceMailwatchParam *) li->data;

        if ( !strcmp( param->key, "path" ) ) {
            if ( maildir->path ) {
                g_free( maildir->path );
            }
            maildir->path = g_strdup( param->value );
        }
    }
}

static gboolean
maildir_path_entry_changed_cb( GtkWidget *widget, XfceMailwatchMaildirMailbox *maildir )
{
    const gchar     *text;

    if ( maildir->path ) {
        g_free( maildir->path );
    }

    text = gtk_entry_get_text( GTK_ENTRY( widget ) );
    if ( text ) {
        maildir->path = g_strdup( text );
    }
    else {
        maildir->path = g_strdup( "" );
    }

    return ( FALSE );
}

static void
maildir_browse_button_clicked_cb( GtkWidget *widget,
        XfceMailwatchMaildirMailbox *maildir )
{
    GtkWidget       *chooser;
    gint            result;
    GtkWidget       *parent;

    parent = GTK_WIDGET( gtk_widget_get_parent_window( widget ) );

    xfce_textdomain( GETTEXT_PACKAGE, LOCALEDIR, "UTF-8" );

    chooser = xfce_file_chooser_new( _( "Select Maildir Folder" ),
            GTK_WINDOW(parent),
            XFCE_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_OPEN, GTK_RESPONSE_OK,
            NULL );
    if ( maildir->path ) {
        xfce_file_chooser_set_current_folder( XFCE_FILE_CHOOSER( chooser ), maildir->path );
    }
    
    result = gtk_dialog_run( GTK_DIALOG( chooser ) );
    if ( result == GTK_RESPONSE_OK ) {
        gchar       *path = xfce_file_chooser_get_current_folder( XFCE_FILE_CHOOSER( chooser ) );
        
        gtk_entry_set_text( GTK_ENTRY( maildir->path_entry ), ( path ) ? path : "" );
        g_free( path );
    }

    gtk_widget_destroy( chooser );
}

static GtkContainer *
maildir_get_setup_page( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    GtkWidget                   *hbox;
    GtkWidget                   *label;
    GtkWidget                   *button, *image;

    xfce_textdomain( GETTEXT_PACKAGE, LOCALEDIR, "UTF-8" );

    hbox = gtk_hbox_new( FALSE, BORDER );
    gtk_widget_show( hbox );

    label = gtk_label_new( _( "Maildir Path:" ) );
    gtk_widget_show( label );
    gtk_box_pack_start( GTK_BOX( hbox ), label, FALSE, FALSE, 0 );

    maildir->path_entry = gtk_entry_new();
    if ( maildir->path ) {
        gtk_entry_set_text( GTK_ENTRY( maildir->path_entry ), maildir->path );
    }
    gtk_widget_show( maildir->path_entry );
    gtk_box_pack_start( GTK_BOX( hbox ), maildir->path_entry, FALSE, FALSE, 0 );

    g_signal_connect( G_OBJECT( maildir->path_entry ), "changed",
            G_CALLBACK( maildir_path_entry_changed_cb ), maildir );

    button = gtk_button_new();
    gtk_widget_show( button );

    image = gtk_image_new_from_stock( GTK_STOCK_OPEN, GTK_ICON_SIZE_LARGE_TOOLBAR );
    gtk_widget_show( image );

    gtk_container_add( GTK_CONTAINER( button ), image );
    gtk_box_pack_start( GTK_BOX( hbox ), button, FALSE, FALSE, 0 );

    g_signal_connect( G_OBJECT( button ), "clicked",
            G_CALLBACK( maildir_browse_button_clicked_cb ), maildir );

    return ( GTK_CONTAINER( hbox ) );
}

static void
maildir_timeout_changed_cb( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    guint                       timeout;
    
    if ( maildir->timeout_id ) {
        g_source_remove( maildir->timeout_id );
    }
    timeout = xfce_mailwatch_get_timeout( maildir->mailwatch );
    maildir->timeout_id = g_timeout_add( timeout * 1000, maildir_check_mail, maildir );
}

static void
maildir_set_activated( XfceMailwatchMailbox *mailbox, gboolean activated )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    guint                       timeout;

    maildir->active = activated;

    if ( activated ) {
        if ( maildir->timeout_id ) {
            g_source_remove( maildir->timeout_id );
        }
        timeout = xfce_mailwatch_get_timeout( maildir->mailwatch );
        maildir->timeout_id = g_timeout_add( timeout * 1000, maildir_check_mail, maildir );
    }
}

static XfceMailwatchMailbox *
maildir_new( XfceMailwatch *mailwatch, XfceMailwatchMailboxType *type )
{
    XfceMailwatchMaildirMailbox *mailbox = NULL;

    mailbox = g_new0( XfceMailwatchMaildirMailbox, 1 );
    
    mailbox->mailwatch      = mailwatch;
    mailbox->path           = NULL;
    mailbox->active         = FALSE;
    mailbox->timeout_id     = 0;

    return ( (XfceMailwatchMailbox *) mailbox );
}

XfceMailwatchMailboxType    builtin_mailbox_type_maildir = {
    N_( "Local Maildir Spool" ),
    N_( "The Maildir plugin can watch a local maildir-style mail spool for new messages." ),
    maildir_new,
    maildir_set_activated,
    maildir_timeout_changed_cb,
    maildir_get_setup_page,
    maildir_restore_param_list,
    maildir_save_param_list,
    maildir_free
};
