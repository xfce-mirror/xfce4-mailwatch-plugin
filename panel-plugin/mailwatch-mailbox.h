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

#ifndef __MAILWATCH_MAILBOX_H__
#define __MAILWATCH_MAILBOX_H__

#include <glib.h>

#include "mailwatch-parameters.h"

G_BEGIN_DECLS

/* this is used for shared objects only; unsupported as of yet; may not be
 * useful/necessary */
#define MAILWATCH_MAILBOX_TYPE(mmt_struct) \
G_MODULE_EXPORT XfceMailwatchkMailboxType *\
xfce_mailwatch_mailbox_get_type()\
{\
	return mmt_struct;\
}

struct _XfceMailwatch;
typedef struct _XfceMailwatchMailboxType XfceMailwatchMailboxType;

typedef struct
{
    XfceMailwatchMailboxType *type;
} XfceMailwatchMailbox;

typedef XfceMailwatchMailbox *(*NewMailboxFunc)(struct _XfceMailwatch *);
typedef XfceMailwatchParam **(*GetParamsFunc)(XfceMailwatchMailbox *);
typedef gboolean (*SetParamsFunc)(XfceMailwatchMailbox *, XfceMailwatchParam **, GError **);
typedef void (*FreeMailboxFunc)(XfceMailwatchMailbox *);

struct _XfceMailwatchMailboxType
{
    gchar *name;
    gchar *description;

	NewMailboxFunc new_mailbox_func;
	GetParamsFunc get_params_func;
	SetParamsFunc set_params_func;
    FreeMailboxFunc free_mailbox_func;
};

G_END_DECLS

#endif
