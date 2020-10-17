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

#ifdef HAVE_UNISTD_H
#include <unistd.h>
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

#include <glib.h>
#include <gmodule.h>
#include <gtk/gtk.h>

#include <libxfce4util/libxfce4util.h>
#include <libxfce4ui/libxfce4ui.h>

#include "mailwatch.h"
#include "mailwatch-utils.h"
#include "mailwatch-common.h"

#define BORDER          8

#if GLIB_CHECK_VERSION (2, 32, 0)
#define _mailwatch_lock(mailwatch)   g_mutex_lock(&((mailwatch)->mailboxes_mx))
#define _mailwatch_unlock(mailwatch) g_mutex_unlock(&((mailwatch)->mailboxes_mx))
#else
#define _mailwatch_lock(mailwatch)   g_mutex_lock((mailwatch)->mailboxes_mx)
#define _mailwatch_unlock(mailwatch) g_mutex_unlock((mailwatch)->mailboxes_mx)
#endif

typedef struct
{
    XfceMailwatchMailbox *mailbox;
    gchar *mailbox_name;
    guint num_new_messages;
} XfceMailwatchMailboxData;

struct _XfceMailwatch
{
    gchar *config_file;
    
    GList *mailbox_types;  /* XfceMailwatchMailboxType * */
    GList *mailboxes;      /* XfceMailwatchMailboxData * */
    
#if GLIB_CHECK_VERSION (2, 32, 0)
    GMutex mailboxes_mx;
#else
    GMutex *mailboxes_mx;
#endif
    
    GList *xm_callbacks[XFCE_MAILWATCH_NUM_SIGNALS];
    GList *xm_data[XFCE_MAILWATCH_NUM_SIGNALS];
    
    /* config GUI */
    GtkWidget *config_treeview;
    GtkWidget *mbox_types_lbl;
};

/* fwd decl from other modules... */
extern XfceMailwatchMailboxType builtin_mailbox_type_imap;
extern XfceMailwatchMailboxType builtin_mailbox_type_pop3;
extern XfceMailwatchMailboxType builtin_mailbox_type_maildir;
extern XfceMailwatchMailboxType builtin_mailbox_type_mbox;
extern XfceMailwatchMailboxType builtin_mailbox_type_mh;
#ifdef HAVE_SSL_SUPPORT
extern XfceMailwatchMailboxType builtin_mailbox_type_gmail;
#endif

XfceMailwatchMailboxType *builtin_mailbox_types[] = {
    &builtin_mailbox_type_imap,
    &builtin_mailbox_type_pop3,
#ifdef HAVE_SSL_SUPPORT
    &builtin_mailbox_type_gmail,
#endif
    &builtin_mailbox_type_maildir,
    &builtin_mailbox_type_mbox,
    &builtin_mailbox_type_mh,
    NULL
};
#define N_BUILTIN_MAILBOX_TYPES (sizeof(builtin_mailbox_types)/sizeof(builtin_mailbox_types[0]))

static GList *
mailwatch_load_mailbox_types(void)
{
    GList *mailbox_types = NULL;
    gint i;
    
    for(i = 0; builtin_mailbox_types[i]; i++)
        mailbox_types = g_list_prepend(mailbox_types, builtin_mailbox_types[i]);
    mailbox_types = g_list_reverse(mailbox_types);
    
    return mailbox_types;
}

XfceMailwatch *
xfce_mailwatch_new(void)
{
    XfceMailwatch *mailwatch;
    
    xfce_textdomain(GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8");

#if !GLIB_CHECK_VERSION (2, 32, 0)
    if(!g_thread_supported()) {
        g_thread_init(NULL);
        if(!g_thread_supported()) {
            g_critical(_("xfce4-mailwatch-plugin: Unable to initialise GThread support.  This is likely a problem with your GLib install."));
            return NULL;
        }
    }
#endif
    
    mailwatch = g_new0(XfceMailwatch, 1);
    mailwatch->mailbox_types = mailwatch_load_mailbox_types();
#if GLIB_CHECK_VERSION (2, 32, 0)
    g_mutex_init(&mailwatch->mailboxes_mx);
#else
    mailwatch->mailboxes_mx = g_mutex_new();
#endif
    
    return mailwatch;
}

void
xfce_mailwatch_destroy(XfceMailwatch *mailwatch)
{
    GList *stuff_to_free, *l;
    
    g_return_if_fail(mailwatch);
    
    /* lock it, bitch! */
    _mailwatch_lock(mailwatch);
    
    /* just clear out the mailbox list.  we have to call free_mailbox_func for
     * each mailbox outside the mailboxes_mx lock so we don't cause deadlocks */
    stuff_to_free = mailwatch->mailboxes;
    mailwatch->mailboxes = NULL;
    
    /* we are SO done. */
    _mailwatch_unlock(mailwatch);
    
    for(l = stuff_to_free; l; l = l->next) {
        XfceMailwatchMailboxData *mdata = l->data;
        
        mdata->mailbox->type->free_mailbox_func(mdata->mailbox);
        g_free(mdata->mailbox_name);
        g_free(mdata);
    }
    if(stuff_to_free)
        g_list_free(stuff_to_free);
    
    /* really.  SO SO done. */
#if GLIB_CHECK_VERSION (2, 32, 0)
    g_mutex_clear(&mailwatch->mailboxes_mx);
#else
    g_mutex_free(mailwatch->mailboxes_mx);
#endif
    
    g_list_free(mailwatch->mailbox_types);
    g_free(mailwatch->config_file);
    
    g_free(mailwatch);
}

void
xfce_mailwatch_set_config_file(XfceMailwatch *mailwatch, const gchar *filename)
{
    g_return_if_fail(mailwatch && filename);
    
    g_free(mailwatch->config_file);
    mailwatch->config_file = g_strdup(filename);
}

const gchar *
xfce_mailwatch_get_config_file(XfceMailwatch *mailwatch)
{
    g_return_val_if_fail(mailwatch, NULL);
    
    return mailwatch->config_file;
}

gboolean
xfce_mailwatch_load_config(XfceMailwatch *mailwatch)
{
    gchar *config_file;
    XfceRc *rcfile;
    gchar buf[32];
    GList *l;
    gint i, j, nmailboxes;
    
    g_return_val_if_fail(mailwatch, FALSE);
    g_return_val_if_fail(mailwatch->config_file, FALSE);
    g_return_val_if_fail(!mailwatch->mailboxes, FALSE);  /* FIXME: yeah? */
    
    if(*mailwatch->config_file != '/') {
        config_file = xfce_resource_save_location(XFCE_RESOURCE_CONFIG,
                                                  mailwatch->config_file,
                                                  TRUE);
    } else
        config_file = g_strdup(mailwatch->config_file);
    if(!config_file)
        return FALSE;
    
    DBG("opening config file '%s'", config_file);
    
    rcfile = xfce_rc_simple_open(config_file, TRUE);
    if(!rcfile) {
        g_free(config_file);
        return TRUE;  /* assume no config file exists yet? */
    }
    
    xfce_rc_set_group(rcfile, "mailwatch");
    nmailboxes = xfce_rc_read_int_entry(rcfile, "nmailboxes", 0);
    
    /* lock mutex - doesn't matter yet, but once we start creating mailboxes,
     * it will. */
    _mailwatch_lock(mailwatch);
    
    for(i = 0; i < nmailboxes; i++) {
        const gchar *mailbox_id, *mailbox_name;
        XfceMailwatchMailbox *mailbox = NULL;
        XfceMailwatchMailboxData *mdata;
        gchar **cfg_entries;
        GList *config_params = NULL;
        
        xfce_rc_set_group(rcfile, "mailwatch");
        
        g_snprintf(buf, 32, "mailbox_name%d", i);
        mailbox_name = xfce_rc_read_entry(rcfile, buf, NULL);
        if(!mailbox_name)
            continue;
        
        g_snprintf(buf, 32, "mailbox%d", i);
        mailbox_id = xfce_rc_read_entry(rcfile, buf, NULL);
        if(!mailbox_id)
            continue;
        
        DBG("loading config for mailbox %s, type %s", mailbox_name, mailbox_id);
        
        if(!xfce_rc_has_group(rcfile, buf))
            continue;
        xfce_rc_set_group(rcfile, buf);
        
        for(l = mailwatch->mailbox_types; l; l = l->next) {
            XfceMailwatchMailboxType *mtype = l->data;
            if(!strcmp(mtype->id, mailbox_id)) {
                mailbox = mtype->new_mailbox_func(mailwatch, mtype);
                if(!mailbox->type)
                    mailbox->type = mtype;
                mailbox->type->set_activated_func(mailbox, FALSE);
                break;
            }
        }
        if(!mailbox)
            continue;
        
        mdata = g_new0(XfceMailwatchMailboxData, 1);
        mdata->mailbox = mailbox;
        mdata->mailbox_name = g_strdup(mailbox_name);
        mailwatch->mailboxes = g_list_append(mailwatch->mailboxes, mdata);
        
        cfg_entries = xfce_rc_get_entries(rcfile, buf);
        if(!cfg_entries)
            continue;
        
        for(j = 0; cfg_entries[j]; j++) {
            XfceMailwatchParam *param;
            const gchar *value;
            
            value = xfce_rc_read_entry(rcfile, cfg_entries[j], NULL);
            
            DBG("got param %s = %s", cfg_entries[j],
                value ? (!g_ascii_strcasecmp(cfg_entries[j], "password")
                         ? "[password hidden]" : value) : "[null]");
            
            param = g_new(XfceMailwatchParam, 1);
            param->key = cfg_entries[j];
            param->value = g_strdup(value);
            
            config_params = g_list_append(config_params, param);
        }
        g_free(cfg_entries);  /* yes, not using g_strfreev() is correct */
        
        mailbox->type->restore_param_list_func(mailbox, config_params);
        mailbox->type->set_activated_func(mailbox, TRUE);
        for(l = config_params; l; l = l->next) {
            XfceMailwatchParam *param = l->data;
            g_free(param->key);
            g_free(param->value);
            g_free(param);
        }
        if(config_params)
            g_list_free(config_params);
    }
    
    /* we're done, unlock mutex */
    _mailwatch_unlock(mailwatch);
    
    xfce_rc_close(rcfile);
    
    g_free(config_file);
    
    return TRUE;
}

gboolean
xfce_mailwatch_save_config(XfceMailwatch *mailwatch)
{
    XfceRc *rcfile;
    gchar *config_file, buf[32];
    GList *l;
    gint i;
    
    g_return_val_if_fail(mailwatch, FALSE);
    g_return_val_if_fail(mailwatch->config_file, FALSE);
    
    if(*mailwatch->config_file != '/') {
        config_file = xfce_resource_save_location(XFCE_RESOURCE_CONFIG,
                                                  mailwatch->config_file,
                                                  TRUE);
    } else
        config_file = g_strdup(mailwatch->config_file);
    if(!config_file)
        return FALSE;
    
    DBG("opening config file '%s'", config_file);
    
    rcfile = xfce_rc_simple_open(config_file, FALSE);
    if(!rcfile) {
        xfce_mailwatch_log_message(mailwatch, NULL, XFCE_MAILWATCH_LOG_WARNING,
    	        _("Unable to write config file '%s'"), config_file);
        g_critical(_("Unable to write config file '%s'"), config_file);
        g_free(config_file);
        return FALSE;
    }
    
    /* write out global config and index */
    xfce_rc_set_group(rcfile, "mailwatch");
    xfce_rc_write_int_entry(rcfile, "nmailboxes",
            g_list_length(mailwatch->mailboxes));
    for(l = mailwatch->mailboxes, i = 0; l; l = l->next, i++) {
        XfceMailwatchMailboxData *mdata = l->data;
        
        g_snprintf(buf, sizeof(buf), "mailbox%d", i);
        xfce_rc_write_entry(rcfile, buf, mdata->mailbox->type->id);
        g_snprintf(buf, sizeof(buf), "mailbox_name%d", i);
        xfce_rc_write_entry(rcfile, buf, mdata->mailbox_name);
    }
    /* clear out stale entries */
    while(g_snprintf(buf, sizeof(buf), "mailbox%d", i)
          && xfce_rc_has_entry(rcfile, buf))
    {
        xfce_rc_delete_entry(rcfile, buf, FALSE);
        g_snprintf(buf, sizeof(buf), "mailbox_name%d", i);
        xfce_rc_delete_entry(rcfile, buf, FALSE);
        ++i;
    }
    
    /* write out config data for each mailbox */
    for(l = mailwatch->mailboxes, i = 0; l; l = l->next, i++) {
        XfceMailwatchMailboxData *mdata = l->data;
        GList *config_data, *m;
        
        g_snprintf(buf, sizeof(buf), "mailbox%d", i);
        if(xfce_rc_has_group(rcfile, buf))
            xfce_rc_delete_group(rcfile, buf, FALSE);
        xfce_rc_set_group(rcfile, buf);
        
        config_data = mdata->mailbox->type->save_param_list_func(mdata->mailbox);
        for(m = config_data; m; m = m->next) {
            XfceMailwatchParam *param = m->data;
            
            if(param->key)
                xfce_rc_write_entry(rcfile, param->key,
                        param->value?param->value:"");
            g_free(param->key);
            g_free(param->value);
            g_free(param);
        }
        if(config_data)
            g_list_free(config_data);
    }
    /* clear out stale groups */
    while(g_snprintf(buf, sizeof(buf), "mailbox%d", i)
          && xfce_rc_has_group(rcfile, buf))
    {
        xfce_rc_delete_group(rcfile, buf, FALSE);
        ++i;
    }
    
    xfce_rc_close(rcfile);
    
    /* protect the file in case it has passwords in it */
    if(chmod(config_file, 0600)) {
        xfce_mailwatch_log_message(mailwatch, NULL, XFCE_MAILWATCH_LOG_WARNING,
                _("Unable to set permissions on config file '%s'.  If this file contains passwords or other sensitive information, it may be readable by others on your system."),
                config_file);
        g_critical(_("Unable to set permissions on config file '%s'.  If this file contains passwords or other sensitive information, it may be readable by others on your system."), config_file);
    }
    
    g_free(config_file);
    
    return TRUE;
}

guint
xfce_mailwatch_get_new_messages(XfceMailwatch *mailwatch)
{
    GList *l;
    guint num_new_messages = 0;
    
    g_return_val_if_fail(mailwatch, 0);
    
    /* we don't want to be trying to access the mailbox list while they might
     * be in the process of being destroyed. */
    _mailwatch_lock(mailwatch);
    
    for(l = mailwatch->mailboxes; l; l = l->next) {
        XfceMailwatchMailboxData *mdata = l->data;
        num_new_messages += mdata->num_new_messages;
    }
    
    /* and we're done, unlock */
    _mailwatch_unlock(mailwatch);
    
    return num_new_messages;
}

/**
 * The caller should free @mailbox_names with g_strfreev(), and
 * @new_message_counts with g_free().
 **/
void
xfce_mailwatch_get_new_message_breakdown(XfceMailwatch *mailwatch,
        gchar ***mailbox_names, guint **new_message_counts)
{
    GList *l;
    gint i;
    
    g_return_if_fail(mailbox_names && new_message_counts);
    
    /* fire! */
    _mailwatch_lock(mailwatch);
    
    *mailbox_names = g_new0(gchar *, g_list_length(mailwatch->mailboxes)+1);
    *new_message_counts = g_new0(guint, g_list_length(mailwatch->mailboxes)+1);
    
    for(l = mailwatch->mailboxes, i = 0; l; l = l->next, i++) {
        XfceMailwatchMailboxData *mdata = l->data;
        
        (*mailbox_names)[i] = g_strdup(mdata->mailbox_name);
        (*new_message_counts)[i] = mdata->num_new_messages;
    }
    
    /* direct hit, captain */
    _mailwatch_unlock(mailwatch);
}

void
xfce_mailwatch_force_update(XfceMailwatch *mailwatch)
{
    GList *l;
    
    /* CLEAR! */
    _mailwatch_lock(mailwatch);
    
    for(l = mailwatch->mailboxes; l; l = l->next) {
        XfceMailwatchMailboxData *mdata = l->data;
        mdata->mailbox->type->force_update_callback(mdata->mailbox);
    }
    
    /* mmm, ten thousand volts */
    _mailwatch_unlock(mailwatch);
}

static gboolean
mailwatch_signal_new_messages_idled(gpointer data)
{
    XfceMailwatch *mailwatch = data;
    GList *cl, *dl;
    guint new_messages = xfce_mailwatch_get_new_messages(mailwatch);
    
    for(cl = mailwatch->xm_callbacks[XFCE_MAILWATCH_SIGNAL_NEW_MESSAGE_COUNT_CHANGED],
                dl = mailwatch->xm_data[XFCE_MAILWATCH_SIGNAL_NEW_MESSAGE_COUNT_CHANGED];
        cl && dl;
        cl = cl->next, dl = dl->next)
    {
        XMCallback callback = cl->data;
        gpointer user_data = dl->data;
        
        if(callback)
            callback(mailwatch, GUINT_TO_POINTER( new_messages ), user_data);
    }
    
    return FALSE;
}

void
xfce_mailwatch_signal_new_messages(XfceMailwatch *mailwatch,
        XfceMailwatchMailbox *mailbox, guint num_new_messages)
{
    GList *l;
    gboolean do_signal = FALSE;
    
    g_return_if_fail(mailwatch && mailbox);
    
    /* we don't want to be trying to access the mailbox list while they might
     * be in the process of being destroyed. */
    _mailwatch_lock(mailwatch);
    
    for(l = mailwatch->mailboxes; l; l = l->next) {
        XfceMailwatchMailboxData *mdata = l->data;
        
        if(mdata->mailbox == mailbox) {
            if(mdata->num_new_messages != num_new_messages) {
                mdata->num_new_messages = num_new_messages;
                do_signal = TRUE;
            }
            break;
        }
    }
    
    /* and we're done, unlock */
    _mailwatch_unlock(mailwatch);
    
    if(do_signal)
        g_idle_add(mailwatch_signal_new_messages_idled, mailwatch);
}

static gboolean
xfce_mailwatch_signal_log_message( gpointer data )
{
    XfceMailwatchLogEntry       *entry = data;
    XfceMailwatch               *mailwatch = entry->mailwatch;
    GList                       *cbl, *udl;

    for ( cbl = mailwatch->xm_callbacks[XFCE_MAILWATCH_SIGNAL_LOG_MESSAGE],
          udl = mailwatch->xm_data[XFCE_MAILWATCH_SIGNAL_LOG_MESSAGE];
          cbl && udl;
          cbl = cbl->next, udl = udl->next )
    {
        XMCallback      cb = cbl->data;
        gpointer        user_data = udl->data;

        if ( cb ) {
            cb( mailwatch, entry, user_data );
        }
    }
    g_free( entry->message );
    g_free(entry->mailbox_name);
    g_free( entry );

    return ( FALSE );
}

void
xfce_mailwatch_log_message(XfceMailwatch *mailwatch,
                           XfceMailwatchMailbox *mailbox,
                           XfceMailwatchLogLevel level,
                           const gchar *fmt,
                           ...)
{
    XfceMailwatchLogEntry   *entry = NULL;
    va_list                 args;
    GList *l;
    GTimeVal                gt;
    
    g_return_if_fail( mailwatch &&
            level < XFCE_MAILWATCH_N_LOG_LEVELS && fmt );
    
    entry = g_new0( XfceMailwatchLogEntry, 1 );
    entry->mailwatch        = mailwatch;
    entry->level            = level;
    g_get_current_time(&gt);
    entry->timestamp        = (time_t)gt.tv_sec;

    va_start( args, fmt );
    entry->message          = g_strdup_vprintf( fmt, args );
    va_end( args );
    
    if(mailbox) {
        /* locked on, captain */
        _mailwatch_lock(mailwatch);
        
        for(l = mailwatch->mailboxes; l; l = l->next) {
            XfceMailwatchMailboxData *mdata = l->data;
            
            if(mdata->mailbox == mailbox) {
                entry->mailbox_name = g_strdup(mdata->mailbox_name);
                break;
            }
        }
        
        /* and we're done, unlock */
        _mailwatch_unlock(mailwatch);
    }

    g_idle_add( xfce_mailwatch_signal_log_message, entry );
}

static gboolean
config_run_addedit_window(const gchar *title, GtkWindow *parent,
        const gchar *mailbox_name, XfceMailwatchMailbox *mailbox,
        gchar **new_mailbox_name)
{
    GtkContainer *cfg_box;
    GtkWidget *dlg, *topvbox, *hbox, *lbl, *entry;
    gboolean ret = FALSE;
    
    g_return_val_if_fail(title && mailbox && new_mailbox_name, FALSE);
    
    cfg_box = mailbox->type->get_setup_page_func(mailbox);
    if(!cfg_box) {
        /* Even the mailboxes that don't have configurable settings need a name */
        cfg_box = GTK_CONTAINER(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, BORDER/2));
        gtk_widget_show(GTK_WIDGET(cfg_box));
        
        lbl = gtk_label_new(_("This mailbox type does not require any configuration settings."));
        gtk_widget_show(lbl);
        gtk_box_pack_start(GTK_BOX(cfg_box), lbl, TRUE, TRUE, 0);
    }
    
    if(!mailbox_name) {
        /* add window */
        dlg = gtk_dialog_new_with_buttons(title, parent,
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          _("_Cancel"), GTK_RESPONSE_CANCEL,
                                          _("_OK"), GTK_RESPONSE_ACCEPT,
                                          NULL);
    } else {
        /* edit window */
        dlg = gtk_dialog_new_with_buttons(title, parent,
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          _("_Close"), GTK_RESPONSE_ACCEPT,
                NULL);
    }
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
    
    topvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BORDER/2);
    gtk_container_set_border_width(GTK_CONTAINER(topvbox), BORDER);
    gtk_widget_show(topvbox);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                       topvbox, TRUE, TRUE, 0);
    
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, BORDER/2);
    gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(topvbox), hbox, FALSE, FALSE, 0);
    
    lbl = gtk_label_new_with_mnemonic(_("Mailbox _Name:"));
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
    
    entry = gtk_entry_new();
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    if(mailbox_name)
        gtk_entry_set_text(GTK_ENTRY(entry), mailbox_name);
    gtk_widget_show(entry);
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);
    gtk_label_set_mnemonic_widget( GTK_LABEL( lbl ), entry );
    
    gtk_box_pack_start(GTK_BOX(topvbox), GTK_WIDGET(cfg_box), TRUE, TRUE, 0);
    
    for(;;) {
        if(gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
            *new_mailbox_name = gtk_editable_get_chars(GTK_EDITABLE(entry), 0, -1);
            if(!*new_mailbox_name || !**new_mailbox_name) {
                xfce_message_dialog(GTK_WINDOW(dlg), _("Mailwatch"),
                                    "dialog-error", _("Mailbox name required."),
                        _("Please enter a name for the mailbox."),
                                    _("_Close"), GTK_RESPONSE_ACCEPT,
                                    NULL);
                if(*new_mailbox_name) {
                    g_free(*new_mailbox_name);
                    *new_mailbox_name = NULL;
                }
            } else {
                if(mailbox_name && !g_utf8_collate(mailbox_name, *new_mailbox_name)) {
                    g_free(*new_mailbox_name);
                    *new_mailbox_name = NULL;
                }
                ret = TRUE;
                break;
            }
        } else
            break;
    }
    gtk_widget_destroy(dlg);
    
    return ret;
}

static gboolean
config_do_edit_window(GtkTreeSelection *sel, GtkWindow *parent)
{
    GtkTreeModel *model = NULL;
    GtkTreeIter itr;
    gboolean ret = FALSE;
    
    if(gtk_tree_selection_get_selected(sel, &model, &itr)) {
        gchar *mailbox_name = NULL, *win_title = NULL, *new_mailbox_name = NULL;
        XfceMailwatchMailboxData *mdata = NULL;
        
        gtk_tree_model_get(model, &itr,
                0, &mailbox_name,
                1, &mdata,
                -1);
        
        /* pause the mailbox */
        mdata->mailbox->type->set_activated_func(mdata->mailbox, FALSE);
        
        win_title = g_strdup_printf(_("Edit Mailbox: %s"), mailbox_name);
        if(config_run_addedit_window(win_title, parent,
                mailbox_name, mdata->mailbox, &new_mailbox_name))
        {
            if(new_mailbox_name) {
                gtk_list_store_set(GTK_LIST_STORE(model), &itr,
                        0, new_mailbox_name, -1);
                g_free(mdata->mailbox_name);
                mdata->mailbox_name = new_mailbox_name;
            }
            
            ret = TRUE;
        }
        g_free(win_title);
        g_free(mailbox_name);
        
        /* and unpause */
        mdata->mailbox->type->set_activated_func(mdata->mailbox, TRUE);
    }
    
    return ret;
}

static gint
config_compare_mailbox_data(gconstpointer a, gconstpointer b)
{
    XfceMailwatchMailboxData *xa = (XfceMailwatchMailboxData *)a;
    XfceMailwatchMailboxData *xb = (XfceMailwatchMailboxData *)b;
    
    return g_utf8_collate(xa->mailbox_name, xb->mailbox_name);
}

static void
config_ask_combo_changed_cb(GtkComboBox *cb, gpointer user_data)
{
    XfceMailwatch *mailwatch = user_data;
    gint active_index = gtk_combo_box_get_active(cb);
    XfceMailwatchMailboxType *mbox_type;
    
    if(active_index >= (gint)g_list_length(mailwatch->mailbox_types))
        return;
    
    mbox_type = g_list_nth_data(mailwatch->mailbox_types, active_index);
    
    gtk_label_set_text(GTK_LABEL(mailwatch->mbox_types_lbl),
            _(mbox_type->description));
}
    

static XfceMailwatchMailboxType *
config_ask_new_mailbox_type(XfceMailwatch *mailwatch, GtkWindow *parent)
{
    XfceMailwatchMailboxType *new_mtype = NULL;
    GtkWidget *dlg, *topvbox, *lbl, *combo;
    GList *l;
    
    dlg = gtk_dialog_new_with_buttons(_("Select Mailbox Type"), parent,
                                      GTK_DIALOG_DESTROY_WITH_PARENT,
                                      _("_Cancel"), GTK_RESPONSE_CANCEL,
                                      _("_OK"), GTK_RESPONSE_ACCEPT,
                                      NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 0, -1 );
    
    topvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BORDER/2);
    gtk_container_set_border_width(GTK_CONTAINER(topvbox), BORDER);
    gtk_widget_show(topvbox);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
                       topvbox, TRUE, TRUE, 0);
    
    lbl = gtk_label_new(_("Select a mailbox type.  A description of the type will appear below."));
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(topvbox), lbl, FALSE, FALSE, 0);
    
    combo = gtk_combo_box_text_new();
    for(l = mailwatch->mailbox_types; l; l = l->next) {
        XfceMailwatchMailboxType *mtype = l->data;
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), _(mtype->name));
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_widget_show(combo);
    gtk_box_pack_start(GTK_BOX(topvbox), combo, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(combo), "changed",
            G_CALLBACK(config_ask_combo_changed_cb), mailwatch);
    
    if(mailwatch->mailbox_types) {
        XfceMailwatchMailboxType *mtype = mailwatch->mailbox_types->data;
        mailwatch->mbox_types_lbl = lbl = gtk_label_new(_(mtype->description));
    } else
        mailwatch->mbox_types_lbl = lbl = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_yalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_show(lbl);
    gtk_box_pack_start(GTK_BOX(topvbox), lbl, TRUE, TRUE, 0);
    
    if(gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        gint active = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        
        if(active >= 0 && (guint)active < g_list_length(mailwatch->mailbox_types))
            new_mtype = g_list_nth_data(mailwatch->mailbox_types, active);
    }
    gtk_widget_destroy(dlg);
    
    return new_mtype;
}

static void
config_add_btn_clicked_cb(GtkWidget *w, XfceMailwatch *mailwatch)
{
    XfceMailwatchMailboxType *mailbox_type = NULL;
    XfceMailwatchMailbox *new_mailbox;
    gchar *new_mailbox_name = NULL;
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(w));
    
    mailbox_type = config_ask_new_mailbox_type(mailwatch, parent);
    if(!mailbox_type)
        return;
    
    new_mailbox = mailbox_type->new_mailbox_func(mailwatch, mailbox_type);
    if(!new_mailbox->type)
        new_mailbox->type = mailbox_type;
    mailbox_type->set_activated_func(new_mailbox, FALSE);
    if(config_run_addedit_window(_("Add New Mailbox"), parent, NULL,
                new_mailbox, &new_mailbox_name))
    {
        XfceMailwatchMailboxData *mdata = g_new(XfceMailwatchMailboxData, 1);
        GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(mailwatch->config_treeview));
        GtkTreeIter itr;
        
        /* to serve and protect */
        _mailwatch_lock(mailwatch);
        
        mdata->mailbox = new_mailbox;
        mdata->mailbox_name = new_mailbox_name;
        mdata->num_new_messages = 0;
        
        mailwatch->mailboxes = g_list_insert_sorted(mailwatch->mailboxes,
                mdata, (GCompareFunc)config_compare_mailbox_data);
        
        /* tcetorp dna evres ot */
        _mailwatch_unlock(mailwatch);
        
        mailbox_type->set_activated_func(new_mailbox, TRUE);
        
        gtk_list_store_append(GTK_LIST_STORE(model), &itr);
        gtk_list_store_set(GTK_LIST_STORE(model), &itr,
                0, new_mailbox_name,
                1, mdata,
                -1);
    } else
        mailbox_type->free_mailbox_func(new_mailbox);
}

static void
config_edit_btn_clicked_cb(GtkWidget *w, XfceMailwatch *mailwatch)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(mailwatch->config_treeview));
    
    config_do_edit_window(sel, GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(w))));
}

static void
config_remove_btn_clicked_cb(GtkWidget *w, XfceMailwatch *mailwatch)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(mailwatch->config_treeview));
    GtkTreeModel *model = NULL;
    GtkTreeIter itr;
    XfceMailwatchMailboxData *mailbox_data = NULL;
    XfceMailwatchMailbox *mailbox = NULL;
    GtkWindow *parent;
    gint resp;
    GList *l;
    
    if(!gtk_tree_selection_get_selected(sel, &model, &itr))
        return;
    
    gtk_tree_model_get(model, &itr, 1, &mailbox_data, -1);
    if(!mailbox_data)
        return;
    mailbox = mailbox_data->mailbox;
    
    parent = GTK_WINDOW(gtk_widget_get_toplevel(mailwatch->config_treeview));
    resp = xfce_message_dialog(parent, _("Remove Mailbox"), "dialog-question",
                               _("Are you sure?"),
                               _("Removing a mailbox will discard all settings, "
                               "and cannot be undone."),
                               _("Cancel"), GTK_RESPONSE_CANCEL,
                               _("Remove"), GTK_RESPONSE_ACCEPT,
                               NULL);
    if(resp != GTK_RESPONSE_ACCEPT)
        return;
    
    gtk_list_store_remove(GTK_LIST_STORE(model), &itr);
    
    /* batter up! */
    _mailwatch_lock(mailwatch);
    
    for(l = mailwatch->mailboxes; l; l = l->next) {
        XfceMailwatchMailboxData *mdata = l->data;
        
        if(mdata->mailbox == mailbox) {
            mailwatch->mailboxes = g_list_remove(mailwatch->mailboxes, mdata);
            g_free(mdata->mailbox_name);
            g_free(mdata);
            break;
        }
    }
    
    /* you're out! */
    _mailwatch_unlock(mailwatch);
    
    mailbox->type->free_mailbox_func(mailbox);
    
    mailwatch_signal_new_messages_idled(mailwatch);
}

static gboolean
config_treeview_button_press_cb(GtkTreeView *treeview, GdkEventButton *evt,
        XfceMailwatch *mailwatch)
{
    GtkTreeSelection *sel = gtk_tree_view_get_selection(treeview);
    
    if(evt->type == GDK_2BUTTON_PRESS && evt->button == 1) {
        config_do_edit_window(sel,
                GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(treeview))));
    }
    
    return FALSE;
}

static void
config_set_button_sensitive(GtkTreeSelection *sel, 
                            GtkWidget *w)
{
    if(gtk_tree_selection_get_selected(sel, NULL, NULL))
        gtk_widget_set_sensitive(w, TRUE);
    else
        gtk_widget_set_sensitive(w, FALSE);
}

GtkContainer *
xfce_mailwatch_get_configuration_page(XfceMailwatch *mailwatch)
{
    GtkWidget *frame, *frame_bin, *vbox, *hbox, *sw, *treeview, *btn;
    GtkListStore *ls;
    GList *l;
    GtkTreeIter itr;
    GtkTreeViewColumn *col;
    GtkCellRenderer *render;
    GtkTreeSelection *sel;
    
    frame = xfce_gtk_frame_box_new(_("Mailboxes"), &frame_bin);
    gtk_widget_show(frame);
    
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, BORDER/2);
    gtk_widget_show(hbox);
    gtk_container_add(GTK_CONTAINER(frame_bin), hbox);
    
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
            GTK_SHADOW_ETCHED_IN);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw), GTK_POLICY_NEVER,
            GTK_POLICY_AUTOMATIC);
    gtk_widget_show(sw);
    gtk_box_pack_start(GTK_BOX(hbox), sw, TRUE, TRUE, 0);
    
    /* time to make the doughnuts */
    _mailwatch_lock(mailwatch);
    
    ls = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);
    for(l = mailwatch->mailboxes; l; l = l->next) {
        XfceMailwatchMailboxData *mdata = l->data;
        
        gtk_list_store_append(ls, &itr);
        gtk_list_store_set(ls, &itr,
                0, mdata->mailbox_name,
                1, mdata,
                -1);
    }
    
    /* yum.  they're done. */
    _mailwatch_unlock(mailwatch);
    
    mailwatch->config_treeview = treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ls));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), FALSE);
    gtk_widget_add_events(treeview, GDK_BUTTON_PRESS|GDK_BUTTON_RELEASE);
    
    render = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("mailbox-name", render,
            "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), col);
    
    gtk_widget_show(treeview);
    gtk_container_add(GTK_CONTAINER(sw), treeview);
    g_signal_connect(G_OBJECT(treeview), "button-press-event",
            G_CALLBACK(config_treeview_button_press_cb), mailwatch);
    
    sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);
    gtk_tree_selection_unselect_all(sel);
    
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, BORDER/2);
    gtk_widget_show(vbox);
    gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 0);
    
    btn = gtk_button_new_with_mnemonic(_("_Add"));
    gtk_button_set_image(GTK_BUTTON(btn),
                         gtk_image_new_from_icon_name("list-add",
                                                      GTK_ICON_SIZE_BUTTON));
    gtk_widget_show(btn);
    gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 0);
    g_signal_connect(G_OBJECT(btn), "clicked",
            G_CALLBACK(config_add_btn_clicked_cb), mailwatch);
    
    btn = gtk_button_new_with_mnemonic(_("_Remove"));
    gtk_button_set_image(GTK_BUTTON(btn),
                         gtk_image_new_from_icon_name("list-remove",
                                                      GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_sensitive(btn, FALSE);
    gtk_widget_show(btn);
    gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 0);
    g_signal_connect_after(G_OBJECT(sel), "changed",
            G_CALLBACK(config_set_button_sensitive), btn);
    g_signal_connect(G_OBJECT(btn), "clicked",
            G_CALLBACK(config_remove_btn_clicked_cb), mailwatch);

    btn = gtk_button_new_with_mnemonic(_("_Edit"));
    gtk_button_set_image(GTK_BUTTON(btn),
                         gtk_image_new_from_icon_name("document-edit",
                                                      GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_sensitive(btn, FALSE);
    gtk_widget_show(btn);
    gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 0);
    g_signal_connect_after(G_OBJECT(sel), "changed",
            G_CALLBACK(config_set_button_sensitive), btn);
    g_signal_connect(G_OBJECT(btn), "clicked",
            G_CALLBACK(config_edit_btn_clicked_cb), mailwatch);
    
    return GTK_CONTAINER(frame);
}

void
xfce_mailwatch_signal_connect(XfceMailwatch *mailwatch,
        XfceMailwatchSignal signal_, XMCallback callback, gpointer user_data)
{
    g_return_if_fail(mailwatch && callback
            && signal_ < XFCE_MAILWATCH_NUM_SIGNALS);
    
    mailwatch->xm_callbacks[signal_] =
            g_list_append(mailwatch->xm_callbacks[signal_], callback);
    mailwatch->xm_data[signal_] = g_list_append(mailwatch->xm_data[signal_],
            user_data);
}

void
xfce_mailwatch_signal_disconnect(XfceMailwatch *mailwatch,
        XfceMailwatchSignal signal_, XMCallback callback, gpointer user_data)
{
    GList *cl, *dl;
    g_return_if_fail(mailwatch && callback
            && signal_ < XFCE_MAILWATCH_NUM_SIGNALS);
    
    for(cl = mailwatch->xm_callbacks[signal_], dl = mailwatch->xm_data[signal_];
        cl && dl;
        cl = cl->next, dl = dl->next)
    {
        XMCallback a_callback = cl->data;
        
        if(callback == a_callback) {
            mailwatch->xm_callbacks[signal_] =
                    g_list_delete_link(mailwatch->xm_callbacks[signal_], cl);
            mailwatch->xm_data[signal_] =
                    g_list_delete_link(mailwatch->xm_data[signal_], dl);
            break;
        }
    }
}

