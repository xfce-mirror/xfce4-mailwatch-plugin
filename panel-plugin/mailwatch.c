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

#include <glib.h>

#include <libxfce4util/libxfce4util.h>

#include "mailwatch.h"

#define DEFAULT_TIMEOUT (10000*60)

typedef struct
{
    XfceMailwatchMailbox *mailbox;
    gchar *mailbox_name;
    guint num_new_messages;
} XfceMailwatchMailboxData;

struct _XfceMailwatch
{
    gint watch_timeout;
    gchar *config_file;
    
    GList *mailbox_types;  /* XfceMailwatchMailboxType * */
    GList *mailboxes;      /* XfceMailwatchMailboxData * */
    
    GMutex *mailboxes_mx;
};   

XfceMailwatchMailboxType *builtin_mailbox_types[] = {
    NULL
};
#define N_BUILTIN_MAILBOX_TYPES (sizeof(builtin_mailbox_types)/sizeof(builtin_mailbox_types[0]))

/* private */

static GList *
mailwatch_load_mailbox_types()
{
    GList *mailbox_types = NULL;
    gint i;
    
    for(i = 0; builtin_mailbox_types[i]; i++)
        mailbox_types = g_list_prepend(mailbox_types, builtin_mailbox_types[i]);
    mailbox_types = g_list_reverse(mailbox_types);
    
    return mailbox_types;
}


/* public */

XfceMailwatch *
xfce_mailwatch_new()
{
    XfceMailwatch *mailwatch = g_new0(XfceMailwatch, 1);
    
    if(!g_thread_supported())
        g_thread_init(NULL);
    if(!g_thread_supported()) {
        g_critical(_("xfce4-mailwatch-plugin: Unable to initialise GThread support!"));
        return NULL;
    }
    
    mailwatch->watch_timeout = DEFAULT_TIMEOUT;
    mailwatch->mailbox_types = mailwatch_load_mailbox_types();
    mailwatch->mailboxes_mx = g_mutex_new();
    
    return mailwatch;
}

void
xfce_mailwatch_destroy(XfceMailwatch *mailwatch)
{
    GList *l;
    
    g_return_if_fail(mailwatch);
    
    /* lock it, bitch! */
    g_mutex_lock(mailwatch->mailboxes_mx);
    
    for(l = mailwatch->mailboxes; l; l = l->next) {
        XfceMailwatchMailboxData *mdata = l->data;
        
        mdata->mailbox->type->free_mailbox_func(mdata->mailbox);
        g_free(mdata->mailbox_name);
        g_free(mdata);
    }
    if(mailwatch->mailboxes)
        g_list_free(mailwatch->mailboxes);
    
    /* we are SO done. */
    g_mutex_unlock(mailwatch->mailboxes_mx);
    g_mutex_free(mailwatch->mailboxes_mx);
    
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

G_CONST_RETURN gchar *
xfce_mailwatch_get_config_file(XfceMailwatch *mailwatch)
{
    g_return_val_if_fail(mailwatch, NULL);
    
    return mailwatch->config_file;
}

gboolean
xfce_mailwatch_load_config(XfceMailwatch *mailwatch)
{
    XfceRc *rcfile;
    gchar buf[32];
    GList *l;
    gint i, j, nmailboxes;
    
    g_return_val_if_fail(mailwatch, FALSE);
    g_return_val_if_fail(mailwatch->config_file, FALSE);
    g_return_val_if_fail(!mailwatch->mailboxes, FALSE);  /* FIXME: yeah? */
    
    rcfile = xfce_rc_config_open(XFCE_RESOURCE_CONFIG, mailwatch->config_file, TRUE);
    if(!rcfile)
        return TRUE;  /* assume no config file exists yet? */
    
    xfce_rc_set_group(rcfile, "mailwatch");
    mailwatch->watch_timeout = xfce_rc_read_int_entry(rcfile, "watch_timeout",
            DEFAULT_TIMEOUT);
    nmailboxes = xfce_rc_read_int_entry(rcfile, "nmailboxes", 0);
    
    /* lock mutex - doesn't matter yet, but once we start creating mailboxes,
     * it will. */
    g_mutex_lock(mailwatch->mailboxes_mx);
    
    for(i = 0; i < nmailboxes; i++) {
        const gchar *mailbox_type, *mailbox_name;
        XfceMailwatchMailbox *mailbox = NULL;
        XfceMailwatchMailboxData *mdata;
        gchar **cfg_entries;
        GList *config_params = NULL;
        
        g_snprintf(buf, 32, "mailbox_name%d", i);
        mailbox_name = xfce_rc_read_entry(rcfile, buf, NULL);
        if(!mailbox_name)
            continue;
        
        g_snprintf(buf, 32, "mailbox%d", i);
        mailbox_type = xfce_rc_read_entry(rcfile, buf, NULL);
        if(!mailbox_type)
            continue;
        
        for(l = mailwatch->mailbox_types; l; l = l->next) {
            XfceMailwatchMailboxType *mtype = l->data;
            if(!strcmp(mtype->name, mailbox_type)) {
                mailbox = mtype->new_mailbox_func(mailwatch);
                mailbox->type = mtype;
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
            gchar **keyvalue = g_strsplit(cfg_entries[j], "=", 2);
            
            if(!keyvalue)
                continue;
            if(!keyvalue[0] || !keyvalue[1]) {
                g_strfreev(keyvalue);
                continue;
            }
            
            param = g_new(XfceMailwatchParam, 1);
            param->key = keyvalue[0];
            param->value = keyvalue[1];
            g_free(keyvalue);  /* yes, not using g_strfreev() is correct */
            
            config_params = g_list_append(config_params, param);
        }
        g_strfreev(cfg_entries);
        
        mailbox->type->restore_param_list_func(mailbox, config_params);
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
    g_mutex_unlock(mailwatch->mailboxes_mx);
    
    xfce_rc_close(rcfile);
    
    return TRUE;
}

gboolean
xfce_mailwatch_save_config(XfceMailwatch *mailwatch)
{
    XfceRc *rcfile;
    gchar *config_file, *config_file_new, buf[32];
    GList *l;
    gint i;
    
    g_return_val_if_fail(mailwatch, FALSE);
    g_return_val_if_fail(mailwatch->config_file, FALSE);
    
    config_file = xfce_resource_save_location(XFCE_RESOURCE_CONFIG,
            mailwatch->config_file, TRUE);
    if(!config_file)
        return FALSE;
    
    config_file_new = g_strconcat(config_file, ".new", NULL);
    unlink(config_file_new);
    
    rcfile = xfce_rc_simple_open(config_file_new, FALSE);
    if(!rcfile) {
        g_free(config_file);
        g_free(config_file_new);
        return FALSE;
    }
    
    /* we probably don't need to lock here, but it can't hurt */
    g_mutex_lock(mailwatch->mailboxes_mx);
    
    /* write out global config and index */
    xfce_rc_set_group(rcfile, "mailwatch");
    xfce_rc_write_int_entry(rcfile, "watch_timeout", mailwatch->watch_timeout);
    xfce_rc_write_int_entry(rcfile, "nmailboxes",
            g_list_length(mailwatch->mailboxes));
    for(l = mailwatch->mailboxes, i = 0; l; l = l->next, i++) {
        XfceMailwatchMailboxData *mdata = l->data;
        
        g_snprintf(buf, 32, "mailbox%d", i);
        xfce_rc_write_entry(rcfile, buf, mdata->mailbox->type->name);
        g_snprintf(buf, 32, "mailbox_name%d", i);
        xfce_rc_write_entry(rcfile, buf, mdata->mailbox_name);
    }
    
    /* write out config data for each mailbox */
    for(l = mailwatch->mailboxes, i = 0; l; l = l->next, i++) {
        XfceMailwatchMailboxData *mdata = l->data;
        GList *config_data, *m;
        
        g_snprintf(buf, 32, "mailbox%d", i);
        xfce_rc_set_group(rcfile, buf);
        
        config_data = mdata->mailbox->type->save_param_list_func(mdata->mailbox);
        for(m = config_data; m; m = m->next) {
            XfceMailwatchParam *param = m->data;
            
            xfce_rc_write_entry(rcfile, param->key, param->value);
            g_free(param->key);
            g_free(param->value);
            g_free(param);
        }
        if(config_data)
            g_list_free(config_data);
    }
    
    /* and we're done, unlock */
    g_mutex_unlock(mailwatch->mailboxes_mx);
    
    xfce_rc_close(rcfile);
    
    if(rename(config_file_new, config_file)) {
        g_free(config_file);
        g_free(config_file_new);
        return FALSE;
    }
    
    g_free(config_file);
    g_free(config_file_new);
    
    return TRUE;
}

guint
xfce_mailwatch_get_timeout(XfceMailwatch *mailwatch)
{
    g_return_val_if_fail(mailwatch, 0);
    
    return mailwatch->watch_timeout;
}

void
xfce_mailwatch_set_timeout(XfceMailwatch *mailwatch, guint msecs)
{
    g_return_if_fail(mailwatch);
    
    mailwatch->watch_timeout = msecs;
}

guint
xfce_mailwatch_get_new_messages(XfceMailwatch *mailwatch)
{
    GList *l;
    guint num_new_messages = 0;
    
    g_return_val_if_fail(mailwatch, 0);
    
    /* we don't want to be trying to access the mailbox list while they might
     * be in the process of being destroyed. */
    g_mutex_lock(mailwatch->mailboxes_mx);
    
    for(l = mailwatch->mailboxes; l; l = l->next) {
        XfceMailwatchMailboxData *mdata = l->data;
        num_new_messages += mdata->num_new_messages;
    }
    
    /* and we're done, unlock */
    g_mutex_unlock(mailwatch->mailboxes_mx);
    
    return num_new_messages;
}

void
xfce_mailwatch_signal_new_messages(XfceMailwatch *mailwatch,
        XfceMailwatchMailbox *mailbox, guint num_new_messages)
{
    GList *l;
    
    g_return_if_fail(mailwatch && mailbox);
    
    /* we don't want to be trying to access the mailbox list while they might
     * be in the process of being destroyed. */
    g_mutex_lock(mailwatch->mailboxes_mx);
    
    for(l = mailwatch->mailboxes; l; l = l->next) {
        XfceMailwatchMailboxData *mdata = l->data;
        
        if(mdata->mailbox == mailbox) {
            mdata->num_new_messages = num_new_messages;
            break;
        }
    }
    
    /* and we're done, unlock */
    g_mutex_unlock(mailwatch->mailboxes_mx);
}
