/*
 *  xfce4-mailwatch-plugin - a mail notification applet for the xfce4 panel
 *  Copyright (c) 2009 Brian Tarricone <bjt23@cornell.edu>
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

#ifndef __XFCE_MAILWATCH_COMMON_H__
#define __XFCE_MAILWATCH_COMMON_H__

#include <glib.h>

G_BEGIN_DECLS

#define XFCE_MAILWATCH_ERROR  xfce_mailwatch_get_error_quark()

enum
{
    XFCE_MAILWATCH_ERROR_FAILED = 0,
    XFCE_MAILWATCH_ERROR_ABORTED,
};

GQuark xfce_mailwatch_get_error_quark();

void xfce_mailwatch_threads_enter();
void xfce_mailwatch_threads_leave();

G_END_DECLS

#endif  /* __XFCE_MAILWATCH_COMMON_H__ */

