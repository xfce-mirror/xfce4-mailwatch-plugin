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

#include "mailwatch.h"

typedef struct {
    XfceMailwatchMailbox    xfce_mailwatch_mailbox;

    struct _XfceMailwatch   *mailwatch;
    
    gchar                   *path;
    gboolean                active;
    guint                   timeout_id;
} MaildirMailbox;

static gboolean maildir_check_mail( gpointer data ) {
    MaildirMailbox  *maildir = (MaildirMailbox *) data;
    DIR             *dir;
    struct dirent   *dent;
    gchar           *path;
    int             count_new = 0;

    g_message( "maildir_check_mail()" );

    g_return_val_if_fail( maildir->active && maildir->path, TRUE );
    
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

    g_message( "maildir_check_mail(): %d new messages", count_new );
    /* Is this REALLY the proper way??? */
    xfce_mailwatch_signal_new_messages( maildir->mailwatch, (XfceMailwatchMailbox *) maildir, count_new );

    return ( TRUE );
}

static void maildir_free( XfceMailwatchMailbox *mailbox ) {
    MaildirMailbox      *maildir = (MaildirMailbox *) mailbox;

    g_message( "maildir_free()" );
    
    if ( maildir->timeout_id ) {
        g_source_remove( maildir->timeout_id );
    }
    if ( maildir->path ) {
        g_free( maildir->path );
    }
    g_free( maildir );
}

static GList *maildir_save_param_list( XfceMailwatchMailbox *mailbox ) {
    MaildirMailbox      *maildir = (MaildirMailbox *) mailbox;
    XfceMailwatchParam  *param;
    GList               *param_list = NULL;
    g_message( "maildir_save_param_list()" );

    param = g_new( XfceMailwatchParam, 1 );
    param->key      = g_strdup( "path" );
    param->value    = g_strdup( maildir->path );
    param_list = g_list_append( param_list, param );

    return ( param_list );
}

static void maildir_restore_param_list( XfceMailwatchMailbox *mailbox, GList *params ) {
    MaildirMailbox  *maildir = (MaildirMailbox *) mailbox;
    GList           *li;
    
    g_message( "maildir_restore_param_list()" );

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

static gboolean maildir_path_entry_changed_cb( GtkWidget *widget,
        GdkEventFocus *event, MaildirMailbox *maildir ) {
    const gchar     *text;

    g_message( "maildir_path_entry_changed_cb()" );

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

static GtkContainer *maildir_get_setup_page( XfceMailwatchMailbox *mailbox ) {
    MaildirMailbox  *maildir = (MaildirMailbox *) mailbox;
    GtkWidget       *vbox;
    GtkWidget       *entry;
    GtkWidget       *label;

    g_message( "maildir_get_setup_page()" );

    xfce_textdomain( GETTEXT_PACKAGE, LOCALEDIR, "UTF-8" );

    vbox = gtk_vbox_new( FALSE, 8 );
    gtk_container_set_border_width( GTK_CONTAINER( vbox ), 8 );
    gtk_widget_show( vbox );

    label = gtk_label_new( _( "Enter maildir path:" ) );
    gtk_widget_show( label );
    gtk_box_pack_start( GTK_BOX( vbox ), label, FALSE, FALSE, 0 );

    entry = gtk_entry_new();
    gtk_entry_set_text( GTK_ENTRY( entry ), maildir->path );
    gtk_widget_show( entry );
    gtk_box_pack_start( GTK_BOX( vbox ), entry, FALSE, FALSE, 0 );

    g_signal_connect( G_OBJECT( entry ), "focus-out-event",
            G_CALLBACK( maildir_path_entry_changed_cb ), mailbox );

    return ( GTK_CONTAINER( vbox ) );
}

static void maildir_timeout_changed_cb( XfceMailwatchMailbox *mailbox ) {
    MaildirMailbox      *maildir = (MaildirMailbox *) mailbox;
    guint               timeout;
    
    g_message( "maildir_timeout_changed_cb()" );

    if ( maildir->timeout_id ) {
        g_source_remove( maildir->timeout_id );
    }

    timeout = xfce_mailwatch_get_timeout( maildir->mailwatch );
    
    /* Is it *really* intended here to call xfce_mailwatch_get_timeout()??? */
    maildir->timeout_id = g_timeout_add( timeout * 1000, maildir_check_mail, maildir );
}

static void maildir_set_activated( XfceMailwatchMailbox *mailbox, gboolean activated ) {
    MaildirMailbox      *maildir = (MaildirMailbox *) mailbox;
    guint               timeout;

    g_message( "maildir_set_activated()" );

    maildir->active = activated;

    if ( activated == TRUE ) {
        if ( maildir->timeout_id ) {
            g_source_remove( maildir->timeout_id );
        }
        timeout = xfce_mailwatch_get_timeout( maildir->mailwatch );

        maildir->timeout_id = g_timeout_add( timeout * 1000, maildir_check_mail, maildir );
    }
}

static XfceMailwatchMailbox *maildir_new( struct _XfceMailwatch *mailwatch,
        XfceMailwatchMailboxType *type )
{
    MaildirMailbox      *mailbox = NULL;

    g_message( "maildir_new()" );

    mailbox = g_new0( MaildirMailbox, 1 );
    
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
