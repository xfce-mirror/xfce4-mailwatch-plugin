/*
 *  xfce4-mailwatch-plugin - a mail notification applet for the xfce4 panel
 *  Copyright (c) 2005 Pasi Orovuo <pasi.ov@gmail.com>
 *  Copyright (c) 2005,2008 Brian Tarricone <bjt23@cornell.edu>
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

#include <libxfce4util/libxfce4util.h>
#include <libxfce4ui/libxfce4ui.h>

#include "mailwatch.h"

#define XFCE_MAILWATCH_MBOX_MAILBOX( p )    ( (XfceMailwatchMboxMailbox *) p )
#define BORDER                              ( 8 )

typedef struct {
    XfceMailwatchMailbox    xfce_mailwatch_mailbox;
    XfceMailwatch           *mailwatch;

    gchar                   *fn;
    time_t                  ctime;
    size_t                  size;
    guint                   new_messages;
    guint                   interval;
    
    gint                    running;
    gpointer                thread;  /* (GThread *) */
    guint                   check_id;
    GMutex                   settings_mutex;
} XfceMailwatchMboxMailbox;

static void
mbox_check_mail( XfceMailwatchMboxMailbox *mbox )
{
    gchar           *mailbox;
    struct stat     st;
    guint           num_new = 0;

    g_mutex_lock(&(mbox->settings_mutex));
    if ( !mbox->fn ) {
        g_mutex_unlock(&(mbox->settings_mutex));
        return;
    }
    mailbox = g_strdup( mbox->fn );
    g_mutex_unlock(&(mbox->settings_mutex));

    /* For some reason g_stat() doesn't update
     * ctime */
    if ( stat( mailbox, &st ) < 0 ) {
        xfce_mailwatch_log_message( mbox->mailwatch,
                                    XFCE_MAILWATCH_MAILBOX( mbox ),
                                    XFCE_MAILWATCH_LOG_ERROR,
                                    _( "Failed to get status of file %s: %s" ),
                                    mailbox, g_strerror( errno ) );
        g_free( mailbox );
        return;
    }

    if ( st.st_ctime > mbox->ctime ) {
        gboolean        in_header = FALSE;
        gboolean        cur_new = FALSE;
        gchar           *p;
        GIOChannel      *ioc;
        gsize           nl;
        GError          *error = NULL;

        num_new = 0;

        ioc = g_io_channel_new_file( mailbox, "r", &error );
        if ( !ioc ) {
            xfce_mailwatch_log_message( mbox->mailwatch,
                                        XFCE_MAILWATCH_MAILBOX( mbox ),
                                        XFCE_MAILWATCH_LOG_ERROR,
                                        error->message );
            g_free( mailbox );
            g_error_free( error );
            return;
        }
        if ( g_io_channel_set_encoding( ioc, NULL, &error ) != G_IO_STATUS_NORMAL ) {
            xfce_mailwatch_log_message( mbox->mailwatch,
                                        XFCE_MAILWATCH_MAILBOX( mbox ),
                                        XFCE_MAILWATCH_LOG_WARNING,
                                        error->message );
            g_error_free( error );
            error = NULL;
        }
       
        if ( mbox->size && st.st_size > (guint)mbox->size ) {
            /* G_SEEK_CUR is same as G_SEEK_SET in this context. */
            if ( g_io_channel_seek_position( ioc, mbox->size, G_SEEK_CUR, &error ) !=  G_IO_STATUS_NORMAL ) {
                xfce_mailwatch_log_message( mbox->mailwatch,
                                            XFCE_MAILWATCH_MAILBOX( mbox ),
                                            XFCE_MAILWATCH_LOG_ERROR,
                                            error->message );
                g_io_channel_unref( ioc );
                g_free( mailbox );
                g_error_free( error );
                return;
            }
            num_new += mbox->new_messages;
        }

        while ( g_io_channel_read_line( ioc, &p, NULL, &nl, NULL ) == G_IO_STATUS_NORMAL ) {
            p[nl] = 0;
            
            if ( !in_header ) {
                if ( !strncmp( p, "From ", 5 ) ) {
                    in_header = TRUE;
                    cur_new = TRUE;
                }
            }
            else {
                if ( *p == 0 ) {
                    in_header = FALSE;

                    if ( cur_new ) {
                        num_new++;
                    }
                }
                else if ( !strncmp( p, "Status: ", 8 ) ) {
                    gchar       *q = p + 8;
                    if ( strchr( q, 'R' ) || strchr( q, 'O' ) ) {
                        cur_new = FALSE;
                    }
                }
                else if ( !strncmp( p, "X-Mozilla-Status: ", 18 ) ) {
                    if ( strncmp( p + 18, "0000", 4 ) ) {
                        cur_new = FALSE;
                    }
                }
            }
            g_free( p );

            if( !g_atomic_int_get( &mbox->running ) ) {
                g_io_channel_unref( ioc );
                g_free( mailbox );
                return;
            }
        }
        g_io_channel_unref( ioc );
        
        if ( st.st_size > (guint)mbox->size && num_new <= mbox->new_messages ) {
            /* Assume mailbox opened and some headers added by client */
            num_new = mbox->new_messages = 0;
        }
        else {
            mbox->new_messages = num_new;
        }

        xfce_mailwatch_signal_new_messages( mbox->mailwatch, (XfceMailwatchMailbox *) mbox, num_new );

        mbox->ctime = st.st_ctime;
        mbox->size = st.st_size;
    }
    g_free( mailbox );
}

static gpointer
mbox_check_mail_thread( gpointer data )
{
    XfceMailwatchMboxMailbox    *mbox = XFCE_MAILWATCH_MBOX_MAILBOX( data );

    while( !g_atomic_pointer_get( &mbox->thread ) && g_atomic_int_get( &mbox->running ) )
        g_thread_yield();

    if( g_atomic_int_get( &mbox->running ) )
        mbox_check_mail( mbox );

    g_atomic_pointer_set( &mbox->thread, NULL );
    return NULL;
}

static gboolean
mbox_check_mail_timeout( gpointer data )
{
    XfceMailwatchMboxMailbox    *mbox = XFCE_MAILWATCH_MBOX_MAILBOX( data );
    GThread                     *th;

    if( g_atomic_pointer_get( &mbox->thread ) ) {
        xfce_mailwatch_log_message( mbox->mailwatch,
                                    XFCE_MAILWATCH_MAILBOX( mbox ),
                                    XFCE_MAILWATCH_LOG_WARNING,
                                    _( "Previous thread hasn't exited yet, not checking mail this time." ) );
        return TRUE;
    }

    th = g_thread_try_new( NULL, mbox_check_mail_thread, mbox, NULL );
    g_atomic_pointer_set( &mbox->thread, th );

    return TRUE;
}

static XfceMailwatchMailbox *
mbox_new( XfceMailwatch *mailwatch, XfceMailwatchMailboxType *type )
{
    XfceMailwatchMboxMailbox    *mbox = NULL;

    mbox = g_new0( XfceMailwatchMboxMailbox, 1 );

    mbox->mailwatch     = mailwatch;

    g_mutex_init( &mbox->settings_mutex );
    mbox->interval = XFCE_MAILWATCH_DEFAULT_TIMEOUT;

    return ( (XfceMailwatchMailbox *) mbox );
}

static GList *
mbox_save_settings( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMboxMailbox    *mbox = XFCE_MAILWATCH_MBOX_MAILBOX( mailbox );
    XfceMailwatchParam          *param;
    GList                       *settings = NULL;

    g_mutex_lock(&(mbox->settings_mutex));
    
    param = g_new( XfceMailwatchParam, 1 );
    param->key      = g_strdup( "filename" );
    param->value    = g_strdup( ( mbox->fn ) ? mbox->fn : "" );
    settings = g_list_append( settings, param );

    param = g_new( XfceMailwatchParam, 1 );
    param->key      = g_strdup( "ctime" );
    param->value    = g_strdup_printf( "%ld", (glong)mbox->ctime );
    settings = g_list_append( settings, param );

    param = g_new( XfceMailwatchParam, 1 );
    param->key      = g_strdup( "size" );
    param->value    = g_strdup_printf( "%lu", (gulong)mbox->size );
    settings = g_list_append( settings, param );

    param = g_new( XfceMailwatchParam, 1 );
    param->key      = g_strdup( "interval" );
    param->value    = g_strdup_printf( "%u", mbox->interval );
    settings = g_list_append( settings, param );

    g_mutex_unlock(&(mbox->settings_mutex));

    return ( settings );
}

static void
mbox_restore_settings( XfceMailwatchMailbox *mailbox, GList *settings )
{
    XfceMailwatchMboxMailbox    *mbox = XFCE_MAILWATCH_MBOX_MAILBOX( mailbox );
    GList                       *li;

    g_mutex_lock(&(mbox->settings_mutex));
    
    for ( li = g_list_first( settings ); li != NULL; li = g_list_next( li ) ) {
        XfceMailwatchParam      *p = (XfceMailwatchParam *) li->data;

        if ( !strcmp( p->key, "filename" ) ) {
            if ( mbox->fn ) {
                g_free( mbox->fn );
            }
            mbox->fn = g_strdup( p->value );
        }
        else if ( !strcmp( p->key, "ctime" ) ) {
            mbox->ctime = atol( p->value );
        }
        else if ( !strcmp( p->key, "size" ) ) {
            mbox->size = (size_t) atol( p->value );
        }
        else if ( !strcmp( p->key, "interval" ) ) {
            mbox->interval = (guint) atol( p->value );
        }
    }

    g_mutex_unlock(&(mbox->settings_mutex));
}

static void
mbox_file_set_cb( GtkWidget *button,
        XfceMailwatchMboxMailbox *mbox )
{
    gchar *text;

    text = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( button ) );

    g_mutex_lock(&(mbox->settings_mutex));
    if ( mbox->fn ) {
        g_free( mbox->fn );
    }

    if ( text ) {
        mbox->fn = text;
    }
    else {
        mbox->fn = g_strdup( "" );
    }
    g_mutex_unlock(&(mbox->settings_mutex));
}

static void
mbox_interval_changed_cb( GtkWidget *spinner, XfceMailwatchMboxMailbox *mbox ) {
    guint val = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON( spinner ) ) * 60;

    if( val == mbox->interval )
        return;

    g_assert( !g_atomic_int_get( &mbox->running ) );
    mbox->interval = val;
}
    
static GtkContainer *
mbox_get_setup_page( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMboxMailbox    *mbox = XFCE_MAILWATCH_MBOX_MAILBOX( mailbox );
    GtkWidget                   *vbox, *hbox;
    GtkWidget                   *label;
    GtkWidget                   *button, *spinner;
    GtkSizeGroup                *sg;

    vbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, BORDER / 2 );
    gtk_widget_show( vbox );
    
    hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, BORDER );
    gtk_widget_show( hbox );
    gtk_box_pack_start( GTK_BOX( vbox ), hbox, FALSE, FALSE, 0 );

    sg = gtk_size_group_new( GTK_SIZE_GROUP_HORIZONTAL );

    label = gtk_label_new_with_mnemonic( _( "Mbox _Filename:" ) );
    gtk_widget_show( label );
    gtk_box_pack_start( GTK_BOX( hbox ), label, FALSE, FALSE, 0 );

    gtk_size_group_add_widget( GTK_SIZE_GROUP( sg ), label );

    button = gtk_file_chooser_button_new( _("Select mbox file"),
                                          GTK_FILE_CHOOSER_ACTION_OPEN );
    g_mutex_lock(&(mbox->settings_mutex));
    if ( mbox->fn ) {
        gtk_file_chooser_set_filename( GTK_FILE_CHOOSER( button ), mbox->fn );
    }
    g_mutex_unlock(&(mbox->settings_mutex));
    gtk_widget_show( button );
    gtk_box_pack_start( GTK_BOX( hbox ), button, TRUE, TRUE, 0 );
    g_signal_connect( G_OBJECT( button ), "file-set",
            G_CALLBACK( mbox_file_set_cb ), mbox );

    gtk_label_set_mnemonic_widget( GTK_LABEL( label ), button );

    hbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, BORDER );
    gtk_widget_show( hbox );
    gtk_box_pack_start( GTK_BOX( vbox ), hbox, FALSE, FALSE, 0 );

    label = gtk_label_new_with_mnemonic( _( "_Interval:" ) );
    gtk_widget_show( label );
    gtk_label_set_xalign( GTK_LABEL( label ), 1.0 );
    gtk_box_pack_start( GTK_BOX( hbox ), label, FALSE, FALSE, 0 );

    gtk_size_group_add_widget( GTK_SIZE_GROUP( sg ), label );

    spinner = gtk_spin_button_new_with_range( 1.0, 1440.0, 1.0 );
    gtk_spin_button_set_numeric( GTK_SPIN_BUTTON( spinner ), TRUE );
    gtk_spin_button_set_wrap( GTK_SPIN_BUTTON( spinner ), FALSE );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON( spinner ), mbox->interval / 60 );
    gtk_widget_show( spinner );
    gtk_box_pack_start( GTK_BOX( hbox ), spinner, FALSE, FALSE, 0 );
    g_signal_connect( G_OBJECT( spinner ), "value-changed",
            G_CALLBACK( mbox_interval_changed_cb ), mbox );
    gtk_label_set_mnemonic_widget( GTK_LABEL( label ), spinner );

    label = gtk_label_new( _( "minute(s)." ) );
    gtk_widget_show( label );
    gtk_box_pack_start( GTK_BOX( hbox ), label, FALSE, FALSE, 0 );

    return ( GTK_CONTAINER( vbox ) );
}

static void
mbox_activate( XfceMailwatchMailbox *mailbox, gboolean activated )
{
    XfceMailwatchMboxMailbox    *mbox = XFCE_MAILWATCH_MBOX_MAILBOX( mailbox );

    if(activated == g_atomic_int_get( &mbox->running ))
        return;

    if( activated ) {
        g_atomic_int_set( &mbox->running, TRUE );
        mbox->check_id = g_timeout_add( mbox->interval * 1000, mbox_check_mail_timeout, mbox );
    } else {
        g_atomic_int_set( &mbox->running, FALSE );
        g_source_remove( mbox->check_id );
        mbox->check_id = 0;
    }
}

static void
mbox_force_update( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMboxMailbox    *mbox = XFCE_MAILWATCH_MBOX_MAILBOX( mailbox );

    if( !g_atomic_pointer_get( &mbox->thread ) ) {
        gboolean restart = FALSE;

        if( mbox->check_id ) {
            g_source_remove( mbox->check_id );
            restart = TRUE;
        }

        mbox_check_mail_timeout( mbox );

        if( restart )
            mbox->check_id = g_timeout_add( mbox->interval * 1000, mbox_check_mail_timeout, mbox);
    }
}

static void
mbox_free( XfceMailwatchMailbox *mailbox )
{
    XfceMailwatchMboxMailbox    *mbox = XFCE_MAILWATCH_MBOX_MAILBOX( mailbox );

    mbox_activate( mailbox, FALSE );
    while( g_atomic_pointer_get( &mbox->thread ) )
        g_thread_yield();
    
    g_mutex_clear( &mbox->settings_mutex );

    if ( mbox->fn ) {
        g_free( mbox->fn );
    }
    g_free( mbox );
}

XfceMailwatchMailboxType    builtin_mailbox_type_mbox = {
    "mbox",
    N_( "Local Mbox spool" ),
    N_( "Mbox plugin watches a local mbox-type mail spool for new messages." ),
    mbox_new,
    mbox_activate,
    mbox_force_update,
    mbox_get_setup_page,
    mbox_restore_settings,
    mbox_save_settings,
    mbox_free
};
