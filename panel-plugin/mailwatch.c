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

#include <glib.h>

#include "mailwatch.h"

#define DEFAULT_TIMEOUT 10000

struct _XfceMailwatch
{
    gint watch_timeout;
    gchar *config_file;
    
    GList *mailbox_types;  /* XfceMailwatchMailboxType * */
    GList *mailboxes;      /* XfceMailwatchMailbox * */
};   

XfceMailwatchMailboxType *builtin_mailbox_types[] = {
    NULL
};
#define N_BUILTIN_MAILBOX_TYPES (sizeof(builtin_mailbox_types)/sizeof(builtin_mailbox_types[0]))

/* private */

static XfceMailwatchMailboxType **
mailwatch_load_mailbox_types()
{
    GList *mailbox_types = NULL;
    gint i;
    
    for(i = 0; builtin_mailbox_types[i]; i++)
        mailbox_types = g_list_prepend(mailox_types, builtin_mailbox_types[i]);
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
    
    mailwatch->watch_timeout = DEFAULT_TIMEOUT;
    mailwatch->mailbox_types = mailwatch_load_mailbox_types();
    
    return mailwatch;
}

void
xfce_mailwatch_destroy(XfceMailwatch *mailwatch)
{
    g_return_if_fail(mailwatch);
    
    g_list_free(mailwatch->mailbox_types);
    g_free(mailbox->config_file);
    g_free(mailbox);
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
    g_return_val_if_fail(mailwatch, FALSE);
    g_return_val_if_fail(mailwatch->config_file, FALSE);
}

gboolean
xfce_mailwatch_save_config(XfceMailwatch *mailwatch)
{
    g_return_val_if_fail(mailwatch, FALSE);
    g_return_val_if_fail(mailwatch->config_file, FALSE);
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
    g_return_val_if_fail(mailwatch, 0);
    
    return 0;
}

void
xfce_mailwatch_signal_new_messages(XfceMailwatch *mailwatch,
        XfceMailwatchMailbox *mailbox, guint num_new_messages)
{
    g_return_if_fail(mailwatch && mailbox);
}
