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

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <gtk/gtk.h>

#include <dirent.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfcegui4/libxfcegui4.h>

#include "mailwatch.h"

#define XFCE_MAILWATCH_MAILDIR_MAILBOX( p ) ( (XfceMailwatchMaildirMailbox *) p )
#define BORDER                              ( 8 )

typedef enum {
    MAILDIR_MSG_START,
    MAILDIR_MSG_PAUSE,
    MAILDIR_MSG_FORCE_UPDATE,
    MAILDIR_MSG_QUIT
} XfceMailwatchMaildirMessageType;

typedef struct {
    XfceMailwatchMailbox    xfce_mailwatch_mailbox;

    XfceMailwatch           *mailwatch;

    gchar                   *path;
    gboolean                active;
    time_t                  mtime;

    guint                   interval;
    guint                   last_update;
} XfceMailwatchMaildirMailbox;

typedef struct {
    XfceMailwatchMaildirMessageType type;
    XfceMailwatchMaildirMailbox     *maildir;
    gpointer                        data;
} XfceMailwatchMaildirMessage;

static GList        *maildir_list = NULL;
static GMutex       *maildir_list_mutex = NULL;
static GAsyncQueue  *maildir_queue = NULL;
static GThread      *maildir_thread = NULL;

static void
maildir_check_mail( XfceMailwatchMaildirMailbox *maildir )
{
    DIR                         *dir;
    struct dirent               *dent;
    gchar                       *path;
    int                         count_new = 0;
    struct stat                 st;

    DBG( "entering" );

    path = g_build_filename( maildir->path, "new", NULL );
    if ( stat( path, &st ) == 0 ) {
        if ( S_ISDIR( st.st_mode ) && st.st_mtime > maildir->mtime ) {
            dir = opendir( path );
            if ( dir ) {
                while ( ( dent = readdir( dir ) ) ) {
                    if ( dent->d_name[0] != '.' && dent->d_reclen > 1 ) {
                        count_new++;
                    }
                }
                closedir( dir );
                
                xfce_mailwatch_signal_new_messages( maildir->mailwatch,
                        (XfceMailwatchMailbox *) maildir, count_new );
            }
            maildir->mtime = st.st_mtime;
        }
    }
    g_free( path );

}

static gpointer
maildir_main_thread( gpointer data ) {
    GTimeVal        tv;
    GList           *li = NULL;

    DBG( "entering" );

    g_async_queue_ref( maildir_queue );

    for ( ;; ) {
        XfceMailwatchMaildirMessage *msg;

        g_get_current_time( &tv );
        g_time_val_add( &tv, G_USEC_PER_SEC * 5 );
        
        msg = g_async_queue_timed_pop( maildir_queue, &tv );

        if ( msg ) {
            switch ( msg->type ) {
                case MAILDIR_MSG_START:
                    msg->maildir->active = TRUE;
                    break;

                case MAILDIR_MSG_PAUSE:
                    msg->maildir->active = FALSE;
                    break;

                case MAILDIR_MSG_FORCE_UPDATE:
                    msg->maildir->last_update = 0;
                    break;

                case MAILDIR_MSG_QUIT:
                    g_async_queue_unref( maildir_queue );
                    g_thread_exit( NULL );
                    break;
            }
            g_free( msg );
        }

        g_mutex_lock( maildir_list_mutex );

        for ( li = maildir_list; li != NULL; li = g_list_next( li ) ) {
            XfceMailwatchMaildirMailbox     *m = XFCE_MAILWATCH_MAILDIR_MAILBOX( li->data );

            if ( ( m->active && m->path )
                    && tv.tv_sec - m->last_update > m->interval ) {
                maildir_check_mail( m );
                m->last_update = tv.tv_sec;
            }
        }

        g_mutex_unlock( maildir_list_mutex );
    }
    return ( NULL );
}

static XfceMailwatchMailbox *
maildir_new( XfceMailwatch *mailwatch, XfceMailwatchMailboxType *type )
{
    XfceMailwatchMaildirMailbox *maildir = NULL;

    DBG( "entering" );

    maildir = g_new0( XfceMailwatchMaildirMailbox, 1 );
    
    maildir->mailwatch      = mailwatch;
    maildir->path           = NULL;
    maildir->active         = FALSE;
    maildir->interval       = XFCE_MAILWATCH_DEFAULT_TIMEOUT / 1000;
        
    if ( !maildir_thread ) {
        maildir_list_mutex = g_mutex_new();
        maildir_queue = g_async_queue_new();
        maildir_thread = g_thread_create( maildir_main_thread, NULL, TRUE, NULL );
    }

    g_mutex_lock( maildir_list_mutex );
    maildir_list = g_list_append( maildir_list, maildir );
    g_mutex_unlock( maildir_list_mutex );
    
    return ( (XfceMailwatchMailbox *) maildir );
}

static void
maildir_free( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    XfceMailwatchMaildirMessage *msg;

    DBG( "entering" );

    g_mutex_lock( maildir_list_mutex );
    
    maildir_list = g_list_remove( maildir_list, maildir );
    
    if ( maildir->path ) {
        g_free( maildir->path );
    }
    g_free( maildir );

    if ( !g_list_length( maildir_list ) ) {
        /* Let the messages flow */
        g_mutex_unlock( maildir_list_mutex );
        
        msg = g_new0( XfceMailwatchMaildirMessage, 1 );
        msg->type   = MAILDIR_MSG_QUIT;

        g_async_queue_push( maildir_queue, msg );
        g_thread_join( maildir_thread );

        maildir_thread = NULL;
        g_async_queue_unref( maildir_queue );
        maildir_queue = NULL;

        g_mutex_free( maildir_list_mutex );
        maildir_list_mutex = NULL;
    }
    else {
        g_mutex_unlock( maildir_list_mutex );
    }
}

static GList *
maildir_save_param_list( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    XfceMailwatchParam          *param;
    GList                       *settings = NULL;

    DBG( "entering" );

    g_mutex_lock( maildir_list_mutex );
    
    param           = g_new( XfceMailwatchParam, 1 );
    param->key      = g_strdup( "path" );
    param->value    = g_strdup( ( maildir->path ) ? maildir->path : "" );
    settings        = g_list_append( settings, param );

    param           = g_new( XfceMailwatchParam, 1 );
    param->key      = g_strdup( "mtime" );
    param->value    = g_strdup_printf( "%li", maildir->mtime );
    settings        = g_list_append( settings, param );

    param           = g_new( XfceMailwatchParam, 1 );
    param->key      = g_strdup( "interval" );
    param->value    = g_strdup_printf( "%u", maildir->interval );
    settings        = g_list_append( settings, param );

    g_mutex_unlock( maildir_list_mutex );

    return ( settings );
}

static void
maildir_restore_param_list( XfceMailwatchMailbox *mailbox, GList *params )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    GList                       *li;

    DBG( "entering" );

    g_mutex_lock( maildir_list_mutex );
    
    for ( li = g_list_first( params ); li != NULL; li = g_list_next( li ) ) {
        XfceMailwatchParam  *param = (XfceMailwatchParam *) li->data;

        if ( !strcmp( param->key, "path" ) ) {
            if ( maildir->path ) {
                g_free( maildir->path );
            }
            maildir->path = g_strdup( param->value );
        }
        else if ( !strcmp( param->key, "mtime" ) ) {
            maildir->mtime = atol( param->value );
        }
        else if ( !strcmp( param->key, "interval" ) ) {
            maildir->interval = (guint) atol( param->value );
        }
    }

    g_mutex_unlock( maildir_list_mutex );
}

static gboolean
maildir_path_entry_changed_cb( GtkWidget *widget, XfceMailwatchMaildirMailbox *maildir )
{
    const gchar                 *text;

    DBG( "entering" );

    g_mutex_lock( maildir_list_mutex );
    if ( maildir->path ) {
        g_free( maildir->path );
    }
    
    text = gtk_entry_get_text( GTK_ENTRY( widget ) );
    maildir->path = g_strdup( text ? text : "" );

    g_mutex_unlock( maildir_list_mutex );

    return ( FALSE );
}

static void
maildir_browse_button_clicked_cb( GtkWidget *button,
        XfceMailwatchMaildirMailbox *maildir )
{
    GtkWidget       *chooser;
    gint            result;
    GtkWidget       *parent;

    DBG( "entering" );

    xfce_textdomain( GETTEXT_PACKAGE, LOCALEDIR, "UTF-8" );

    parent = gtk_widget_get_toplevel( button );
    chooser = xfce_file_chooser_new( _( "Select Maildir Folder" ),
            GTK_WINDOW(parent),
            XFCE_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_OPEN, GTK_RESPONSE_OK,
            NULL );
    if ( maildir->path ) {
        xfce_file_chooser_set_filename( XFCE_FILE_CHOOSER( chooser ), maildir->path );
    }
    
    result = gtk_dialog_run( GTK_DIALOG( chooser ) );
    if ( result == GTK_RESPONSE_OK ) {
        gchar       *path = xfce_file_chooser_get_current_folder( XFCE_FILE_CHOOSER( chooser ) );
        GtkWidget   *entry = g_object_get_data( G_OBJECT( button ), "maildir_entry" );
        
        gtk_entry_set_text( GTK_ENTRY( entry ), ( path ) ? path : "" );
        g_free( path );
    }

    gtk_widget_destroy( chooser );
}

static void
maildir_interval_changed_cb( GtkWidget *spinner, XfceMailwatchMaildirMailbox *maildir ) {

    DBG( "entering" );

    maildir->interval = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON( spinner ) ) * 60;
}

static GtkContainer *
maildir_get_setup_page( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    GtkWidget                   *vbox, *hbox;
    GtkWidget                   *label, *entry;
    GtkWidget                   *button, *image;
    GtkWidget                   *spin;
    GtkSizeGroup                *sg;

    DBG( "entering" );

    xfce_textdomain( GETTEXT_PACKAGE, LOCALEDIR, "UTF-8" );

    vbox = gtk_vbox_new( FALSE, BORDER / 2 );
    gtk_widget_show( vbox );

    hbox = gtk_hbox_new( FALSE, BORDER );
    gtk_widget_show( hbox );
    gtk_box_pack_start( GTK_BOX( vbox ), hbox, FALSE, FALSE, 0 );

    sg = gtk_size_group_new( GTK_SIZE_GROUP_HORIZONTAL );

    label = gtk_label_new_with_mnemonic( _( "Maildir _Path:" ) );
    gtk_widget_show( label );
    gtk_box_pack_start( GTK_BOX( hbox ), label, FALSE, FALSE, 0 );
    gtk_size_group_add_widget( sg, label );

    entry = gtk_entry_new();
    g_mutex_lock( maildir_list_mutex );
    if ( maildir->path ) {
        gtk_entry_set_text( GTK_ENTRY( entry ), maildir->path );
    }
    g_mutex_unlock( maildir_list_mutex );
    
    gtk_widget_show( entry );
    gtk_box_pack_start( GTK_BOX( hbox ), entry, FALSE, FALSE, 0 );
    gtk_label_set_mnemonic_widget( GTK_LABEL( label ), entry );

    g_signal_connect( G_OBJECT( entry ), "changed",
            G_CALLBACK( maildir_path_entry_changed_cb ), maildir );

    button = gtk_button_new();
    gtk_widget_show( button );

    image = gtk_image_new_from_stock( GTK_STOCK_OPEN, GTK_ICON_SIZE_LARGE_TOOLBAR );
    gtk_widget_show( image );

    gtk_container_add( GTK_CONTAINER( button ), image );
    gtk_box_pack_start( GTK_BOX( hbox ), button, FALSE, FALSE, 0 );

    g_object_set_data( G_OBJECT( button ), "maildir_entry", entry );
    g_signal_connect( G_OBJECT( button ), "clicked",
            G_CALLBACK( maildir_browse_button_clicked_cb ), maildir );

    hbox = gtk_hbox_new( FALSE, BORDER );
    gtk_widget_show( hbox );
    gtk_box_pack_start( GTK_BOX( vbox ), hbox, FALSE, FALSE, 0 );

    label = gtk_label_new_with_mnemonic( _( "_Interval:" ) );
    gtk_widget_show( label );
    gtk_misc_set_alignment( GTK_MISC( label ), 1, 0.5 );
    gtk_box_pack_start( GTK_BOX( hbox ), label, FALSE, FALSE, 0 );
    gtk_size_group_add_widget( sg, label );

    spin = gtk_spin_button_new_with_range( 1.0, 1440.0, 1.0 );
    gtk_spin_button_set_numeric( GTK_SPIN_BUTTON( spin ), TRUE );
    gtk_spin_button_set_wrap( GTK_SPIN_BUTTON( spin ), FALSE );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON( spin ), maildir->interval / 60 );
    gtk_widget_show( spin );
    gtk_box_pack_start( GTK_BOX( hbox ), spin, FALSE, FALSE, 0 );
    g_signal_connect( G_OBJECT( spin ), "value-changed",
            G_CALLBACK( maildir_interval_changed_cb ), maildir );
    gtk_label_set_mnemonic_widget( GTK_LABEL( label ), spin );

    label = gtk_label_new( _( "minute(s)." ) );
    gtk_widget_show( label );
    gtk_box_pack_start( GTK_BOX( hbox ), label, FALSE, FALSE, 0 );
    
    return ( GTK_CONTAINER( vbox ) );
}

static void
maildir_force_update_cb( XfceMailwatchMailbox *mailbox ) {
    XfceMailwatchMaildirMessage     *msg = g_new( XfceMailwatchMaildirMessage, 1 );

    DBG( "entering" );

    msg->type       = MAILDIR_MSG_FORCE_UPDATE;
    msg->maildir    = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    msg->data       = NULL;

    g_async_queue_push( maildir_queue, msg );
}

static void
maildir_set_activated( XfceMailwatchMailbox *mailbox, gboolean activated )
{
    XfceMailwatchMaildirMessage *msg = g_new( XfceMailwatchMaildirMessage, 1 );

    DBG( "entering" );

    msg->type       = ( activated ) ? MAILDIR_MSG_START : MAILDIR_MSG_PAUSE;
    msg->maildir    = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    msg->data       = NULL;

    g_async_queue_push( maildir_queue, msg );
}

XfceMailwatchMailboxType    builtin_mailbox_type_maildir = {
    "maildir",
    N_( "Local Maildir Spool" ),
    N_( "The Maildir plugin can watch a local maildir-style mail spool for new messages." ),
    maildir_new,
    maildir_set_activated,
    maildir_force_update_cb,
    maildir_get_setup_page,
    maildir_restore_param_list,
    maildir_save_param_list,
    maildir_free
};
