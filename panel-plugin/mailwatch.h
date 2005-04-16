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

#ifndef __MAILWATCH_H__
#define __MAILWATCH_H__

#include <glib.h>

#include "mailwatch-mailbox.h"

G_BEGIN_DECLS

typedef struct _XfceMailwatch XfceMailwatch;

XfceMailwatch *xfce_mailwatch_new      ();
void xfce_mailwatch_destroy            (XfceMailwatch *mailwatch);

void xfce_mailwatch_set_config_file    (XfceMailwatch *mailwatch,
                                        const gchar *filename);
G_CONST_RETURN gchar *xfce_mailwatch_get_config_file
                                       (XfceMailwatch *mailwatch);

gboolean xfce_mailwatch_load_config    (XfceMailwatch *mailwatch);
gboolean xfce_mailwatch_save_config    (XfceMailwatch *mailwatch);

guint xfce_mailwatch_get_timeout       (XfceMailwatch *mailwatch);
void xfce_mailwatch_set_timeout        (XfceMailwatch *mailwatch,
                                        guint msecs);

guint xfce_mailwatch_get_new_messages  (XfceMailwatch *mailwatch);

/*< only used by XfceMailwatchMailboxType implementations >*/
void xfce_mailwatch_signal_new_messages(XfceMailwatch *mailwatch,
                                        XfceMailwatchMailbox *mailbox,
                                        guint num_new_messages);

G_END_DECLS

#endif
