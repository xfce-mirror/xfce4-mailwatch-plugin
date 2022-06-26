/*
 *  xfce4-mailwatch-plugin - a mail notification applet for the xfce4 panel
 *  Copyright (c) 2005 Pasi Orovuo <pasi.ov@gmail.com>
 *  Copyright (c) 2008 Brian Tarricone <bjt23@cornell.edu>
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

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include <gtk/gtk.h>
#include <gio/gio.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfce4ui/libxfce4ui.h>

#include "mailwatch.h"

#define XFCE_MAILWATCH_MAILDIR_MAILBOX( p ) ( (XfceMailwatchMaildirMailbox *) p )
#define BORDER                              ( 8 )

typedef struct {
    XfceMailwatchMailbox    xfce_mailwatch_mailbox;

    XfceMailwatch           *mailwatch;

    gchar                   *path;
    time_t                  mtime;

    guint                   interval;
    gboolean                monitor;
    guint                   last_update;

    GMutex                   mutex;
    gboolean                running;
    gpointer                thread;  /* (GThread *) */
    guint                   check_id;
    GFileMonitor            *file_monitor;
} XfceMailwatchMaildirMailbox;

static void
maildir_check_mail( XfceMailwatchMaildirMailbox *maildir )
{
    gchar           *path = NULL;
    struct stat     st;

    DBG( "-->>" );
    
    g_mutex_lock(&(maildir->mutex));
    if ( !maildir->path || !*(maildir->path) ) {
        goto out;
    }

    path = g_build_filename( maildir->path, "new", NULL );
    if ( stat( path, &st ) < 0 ) {
        xfce_mailwatch_log_message( maildir->mailwatch,
                                    XFCE_MAILWATCH_MAILBOX( maildir ),
                                    XFCE_MAILWATCH_LOG_ERROR,
                                    _( "Failed to get status of file %s: %s" ),
                                    path, g_strerror( errno ) );
        goto out;
    }
        
    if ( !S_ISDIR( st.st_mode ) ) {
        xfce_mailwatch_log_message( maildir->mailwatch,
                                    XFCE_MAILWATCH_MAILBOX( maildir ),
                                    XFCE_MAILWATCH_LOG_ERROR,
                                    _( "%s is not a directory. Is %s really a valid maildir?" ),
                                    path, maildir->path );
        goto out;
    }

    if ( ( st.st_mtime > maildir->mtime ) || maildir->monitor ) {
        GDir        *dir;
        GError      *error = NULL;

        dir = g_dir_open( path, 0, &error );
        
        if ( dir ) {
            int             count_new = 0;
            const gchar     *entry;
            
            while ( ( entry = g_dir_read_name( dir ) ) ) {
                count_new++;

                /* only check every 25 entries */
                if( !( count_new % 25 ) ) {
                    if( !g_atomic_int_get( &maildir->running ) ) {
                        g_dir_close( dir );
                        g_atomic_pointer_set( &maildir->thread, NULL );
                        return;
                    }
                }
            }
            g_dir_close( dir );

            xfce_mailwatch_signal_new_messages( maildir->mailwatch,
                    (XfceMailwatchMailbox *) maildir, count_new );
        }
        else {
            xfce_mailwatch_log_message( maildir->mailwatch,
                                        XFCE_MAILWATCH_MAILBOX( maildir ),
                                        XFCE_MAILWATCH_LOG_ERROR,
                                        "%s", error->message );
            g_error_free( error );
        }
        maildir->mtime = st.st_mtime;
    }

out:
    g_mutex_unlock(&(maildir->mutex));
    if ( path ) {
        g_free( path );
    }

    DBG( "<<--" );
}

static gpointer
maildir_main_thread( gpointer data ) {
    XfceMailwatchMaildirMailbox     *maildir = data;

    DBG( "-->>" );

    while( !g_atomic_pointer_get( &maildir->thread ) && g_atomic_int_get( &maildir->running ) )
        g_thread_yield();

    if( g_atomic_int_get( &maildir->running ) )
        maildir_check_mail( maildir );

    g_atomic_pointer_set( &maildir->thread, NULL );
    return ( NULL );
}

static gboolean
maildir_check_mail_timeout( gpointer data )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( data );
    GThread                     *th;

    if( g_atomic_pointer_get( &maildir->thread ) ) {
        xfce_mailwatch_log_message( maildir->mailwatch,
                                    XFCE_MAILWATCH_MAILBOX( maildir ),
                                    XFCE_MAILWATCH_LOG_WARNING,
                                    _( "Previous thread hasn't exited yet, not checking mail this time." ) );
        return TRUE;
    }

    th = g_thread_try_new( NULL, maildir_main_thread, maildir, NULL );
    g_atomic_pointer_set( &maildir->thread, th );

    return TRUE;
}

static void
maildir_maybe_add_timeout( gpointer data )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( data );

    if ( !maildir->monitor )
        maildir->check_id = g_timeout_add( maildir->interval * 1000,
                                           maildir_check_mail_timeout,
                                           maildir );
}

static void
maildir_maybe_remove_timeout( gpointer data )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( data );

    if ( maildir->check_id ) {
        g_source_remove( maildir->check_id );
        maildir->check_id = 0;
    }
}

static void
maildir_file_monitor_on_changed( GFileMonitor *mon,
        GFile *file, GFile *other_file,
        GFileMonitorEvent event_type,
        gpointer data )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( data );

    switch (event_type)
    {
        case G_FILE_MONITOR_EVENT_CHANGED:
        case G_FILE_MONITOR_EVENT_CREATED:
        case G_FILE_MONITOR_EVENT_DELETED:
            if( !g_atomic_pointer_get( &maildir->thread ) ) {
                g_assert( maildir->check_id == 0 );
                maildir_check_mail_timeout( maildir );
            }
            break;
        default:
            /* Silence gcc -Wswitch */
            break;
    }
}

static void
maildir_maybe_start_monitor( gpointer data )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( data );
    if ( maildir->monitor ) {
        GFile           *file;
        GError          *error = NULL;

        if ( !maildir->path )
            return;

        file = g_file_new_build_filename( maildir->path, "new", NULL );
        maildir->file_monitor = g_file_monitor(
                file, G_FILE_MONITOR_NONE, NULL, &error);
        g_object_unref( file );

        if ( error != NULL ) {
            g_assert( maildir->file_monitor == NULL );
            xfce_mailwatch_log_message( maildir->mailwatch,
                                        XFCE_MAILWATCH_MAILBOX( maildir ),
                                        XFCE_MAILWATCH_LOG_ERROR,
                                        _( "Failed to create monitor over directory %s: %s" ),
                                        maildir->path, g_strerror( errno ) );
            return;
        }

        g_signal_connect( G_OBJECT( maildir->file_monitor ), "changed",
                G_CALLBACK( maildir_file_monitor_on_changed ), maildir );

        maildir_check_mail_timeout( maildir );
    }
}

static void
maildir_maybe_stop_monitor( gpointer data )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( data );

    if ( maildir->file_monitor != NULL ) {
        g_object_unref( maildir->file_monitor );
        maildir->file_monitor = NULL;
    }
}

static XfceMailwatchMailbox *
maildir_new( XfceMailwatch *mailwatch, XfceMailwatchMailboxType *type )
{
    XfceMailwatchMaildirMailbox *maildir = NULL;

    DBG( "entering" );

    maildir = g_new0( XfceMailwatchMaildirMailbox, 1 );
    
    maildir->mailwatch      = mailwatch;
    maildir->path           = NULL;
    maildir->interval       = XFCE_MAILWATCH_DEFAULT_TIMEOUT;
    maildir->monitor        = FALSE;
    g_mutex_init( &maildir->mutex );
    
    return ( (XfceMailwatchMailbox *) maildir );
}

static GList *
maildir_save_param_list( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    XfceMailwatchParam          *param;
    GList                       *settings = NULL;

    DBG( "-->>" );

    g_mutex_lock(&(maildir->mutex));
    
    param           = g_new( XfceMailwatchParam, 1 );
    param->key      = g_strdup( "path" );
    param->value    = g_strdup( ( maildir->path ) ? maildir->path : "" );
    settings        = g_list_append( settings, param );

    param           = g_new( XfceMailwatchParam, 1 );
    param->key      = g_strdup( "mtime" );
    param->value    = g_strdup_printf( "%ld", (glong)maildir->mtime );
    settings        = g_list_append( settings, param );

    param           = g_new( XfceMailwatchParam, 1 );
    param->key      = g_strdup( "interval" );
    param->value    = g_strdup_printf( "%u", maildir->interval );
    settings        = g_list_append( settings, param );

    param           = g_new( XfceMailwatchParam, 1 );
    param->key      = g_strdup( "monitor" );
    param->value    = g_strdup( maildir->monitor ? "1" : "0" );
    settings        = g_list_append( settings, param );

    g_mutex_unlock(&(maildir->mutex));

    DBG( "<<--" );

    return ( settings );
}

static void
maildir_restore_param_list( XfceMailwatchMailbox *mailbox, GList *params )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    GList                       *li;

    DBG( "-->>" );

    g_mutex_lock(&(maildir->mutex));
    
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
        else if ( !strcmp( param->key, "monitor" ) ) {
            maildir->monitor = *( param->value ) == '0' ? FALSE : TRUE;
        }
    }

    g_mutex_unlock(&(maildir->mutex));

    DBG( "<<--" );
}

static void
maildir_folder_set_cb( GtkWidget *button,
        XfceMailwatchMaildirMailbox *maildir )
{
    gchar *folder;

    DBG( "-->>" );

    folder = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( button ) );
    g_mutex_lock(&(maildir->mutex));
    g_free( maildir->path );
    if( folder ) {
        maildir->path = folder;
    } else {
        maildir->path = g_strdup( "" );
    }
    g_mutex_unlock(&(maildir->mutex));

    DBG( "<<--" );
}

static void
maildir_interval_changed_cb( GtkWidget *spinner, XfceMailwatchMaildirMailbox *maildir ) {
    guint value = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON( spinner ) ) * 60;

    DBG( "-->>" );

    if( value == maildir->interval )
        return;

    maildir->interval = value;

    g_assert( !g_atomic_int_get( &maildir->running ) );

    DBG( "<<--" );
}

static void
maildir_monitor_changed_cb( GtkToggleButton *tb,
        XfceMailwatchMaildirMailbox *maildir )
{
    gboolean enabled = gtk_toggle_button_get_active( tb );
    GtkWidget *spinner = g_object_get_data( G_OBJECT(tb), "xfmw-spinner" );

    if ( enabled == maildir->monitor )
        return;

    g_assert( !g_atomic_int_get( &maildir->running ) );
    maildir->monitor = enabled;

    gtk_widget_set_sensitive( spinner, !enabled );
}

static GtkContainer *
maildir_get_setup_page( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    GtkWidget                   *vbox, *hbox;
    GtkWidget                   *label;
    GtkWidget                   *button;
    GtkWidget                   *spin;
    GtkWidget                   *chk;
    GtkSizeGroup                *sg;

    DBG( "-->>" );

    vbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, BORDER / 2 );
    gtk_widget_show( vbox );

    hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, BORDER );
    gtk_widget_show( hbox );
    gtk_box_pack_start( GTK_BOX( vbox ), hbox, FALSE, FALSE, 0 );

    sg = gtk_size_group_new( GTK_SIZE_GROUP_HORIZONTAL );

    label = gtk_label_new_with_mnemonic( _( "Maildir _Path:" ) );
    gtk_widget_show( label );
    gtk_box_pack_start( GTK_BOX( hbox ), label, FALSE, FALSE, 0 );
    gtk_size_group_add_widget( sg, label );

    button = gtk_file_chooser_button_new( _("Select Maildir Folder"),
                                          GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER );
    g_mutex_lock(&(maildir->mutex));
    if ( maildir->path ) {
        gtk_file_chooser_set_filename( GTK_FILE_CHOOSER( button ), maildir->path );
    }
    g_mutex_unlock(&(maildir->mutex));
    gtk_widget_show( button );
    gtk_box_pack_start( GTK_BOX( hbox ), button, TRUE, TRUE, 0 );
    g_signal_connect( G_OBJECT( button ), "file-set",
            G_CALLBACK( maildir_folder_set_cb ), maildir );

    gtk_label_set_mnemonic_widget( GTK_LABEL( label ), button );

    hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, BORDER );
    gtk_widget_show( hbox );
    gtk_box_pack_start( GTK_BOX( vbox ), hbox, FALSE, FALSE, 0 );

    label = gtk_label_new_with_mnemonic( _( "_Interval:" ) );
    gtk_widget_show( label );
    gtk_label_set_xalign( GTK_LABEL( label ), 1.0 );
    gtk_box_pack_start( GTK_BOX( hbox ), label, FALSE, FALSE, 0 );
    gtk_size_group_add_widget( sg, label );

    spin = gtk_spin_button_new_with_range( 1.0, 1440.0, 1.0 );
    gtk_spin_button_set_numeric( GTK_SPIN_BUTTON( spin ), TRUE );
    gtk_spin_button_set_wrap( GTK_SPIN_BUTTON( spin ), FALSE );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON( spin ), maildir->interval / 60 );
    if ( maildir->monitor )
        gtk_widget_set_sensitive( spin, FALSE );
    gtk_widget_show( spin );
    gtk_box_pack_start( GTK_BOX( hbox ), spin, FALSE, FALSE, 0 );
    g_signal_connect( G_OBJECT( spin ), "value-changed",
            G_CALLBACK( maildir_interval_changed_cb ), maildir );
    gtk_label_set_mnemonic_widget( GTK_LABEL( label ), spin );

    label = gtk_label_new( _( "minute(s)." ) );
    gtk_widget_show( label );
    gtk_box_pack_start( GTK_BOX( hbox ), label, FALSE, FALSE, 0 );
    
    hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, BORDER );
    gtk_widget_show( hbox );
    gtk_box_pack_start( GTK_BOX( vbox ), hbox, FALSE, FALSE, 0 );

    chk = gtk_check_button_new_with_mnemonic( _( "_Monitor mailbox in real-time" ) );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( chk ), maildir->monitor );
    gtk_widget_show( chk );
    gtk_box_pack_start( GTK_BOX( hbox ), chk, FALSE, FALSE, 0 );
    g_signal_connect( G_OBJECT( chk ), "toggled",
            G_CALLBACK( maildir_monitor_changed_cb ), maildir );

    g_object_set_data( G_OBJECT(chk), "xfmw-spinner", spin );

    DBG( "<<--" );
    
    return ( GTK_CONTAINER( vbox ) );
}

static void
maildir_force_update_cb( XfceMailwatchMailbox *mailbox ) {
    XfceMailwatchMaildirMailbox     *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );
    DBG( "-->>" );

    if( !g_atomic_pointer_get( &maildir->thread ) ) {
        gboolean restart = FALSE;

        if( maildir->check_id ) {
            maildir_maybe_remove_timeout( maildir );
            restart = TRUE;
        }

        maildir_check_mail_timeout( maildir );

        if( restart )
            maildir_maybe_add_timeout( maildir );
    }
    DBG( "<<--" );
}

static void
maildir_set_activated( XfceMailwatchMailbox *mailbox, gboolean activated )
{
    XfceMailwatchMaildirMailbox     *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );

    DBG( "-->>" );

   if( activated == g_atomic_int_get( &maildir->running ) )
        return;

    if( activated ) {
        g_atomic_int_set( &maildir->running, TRUE );
        maildir_maybe_add_timeout( maildir );
        maildir_maybe_start_monitor( maildir );
    } else {
        g_atomic_int_set( &maildir->running, FALSE );
        maildir_maybe_remove_timeout( maildir );
        maildir_maybe_stop_monitor( maildir );
    }

    DBG( "<<--" );
}

static void
maildir_free( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMaildirMailbox *maildir = XFCE_MAILWATCH_MAILDIR_MAILBOX( mailbox );

    DBG( "-->>" );

    maildir_set_activated( mailbox, FALSE );
    while( g_atomic_pointer_get( &maildir->thread ) )
        g_thread_yield();

    g_mutex_clear( &maildir->mutex );

    if ( maildir->path ) {
        g_free( maildir->path );
    }
    g_free( maildir );

    DBG( "<<--" );
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
