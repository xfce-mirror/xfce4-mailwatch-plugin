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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gmodule.h>

#include "mailwatch-common.h"

static GMutex *big_happy_mailwatch_mx = NULL;

GQuark
xfce_mailwatch_get_error_quark()
{
    static GQuark q = 0;
    
    if(!q)
        q = g_quark_from_string("xfce-mailwatch-error");
    
    return q;
}

void
xfce_mailwatch_threads_enter()
{
    if(G_UNLIKELY(!big_happy_mailwatch_mx))
        big_happy_mailwatch_mx = g_mutex_new();

    g_mutex_lock(big_happy_mailwatch_mx);
}

void xfce_mailwatch_threads_leave()
{
    g_return_if_fail(big_happy_mailwatch_mx);
    
    g_mutex_unlock(big_happy_mailwatch_mx);
}

/* FIXME: this might not be a good idea */
G_MODULE_EXPORT void
g_module_unload(GModule *module)
{
    if(big_happy_mailwatch_mx) {
        g_mutex_free(big_happy_mailwatch_mx);
        big_happy_mailwatch_mx = NULL;
    }
}

