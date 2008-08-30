/*
 *  xfce4-mailwatch-plugin - a mail notification applet for the xfce4 panel
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

#ifndef __MAILWATCH_NET_H__
#define __MAILWATCH_NET_H__

#include <glib.h>

G_BEGIN_DECLS

typedef struct _XfceMailwatchNetConn  XfceMailwatchNetConn;

typedef gboolean (*XMNCShouldContinueFunc)(XfceMailwatchNetConn *net_conn,
                                           gpointer user_data);


void xfce_mailwatch_net_conn_init();

XfceMailwatchNetConn *xfce_mailwatch_net_conn_new(const gchar *hostname,
                                                  const gchar *service);

void xfce_mailwatch_net_conn_set_should_continue_func(XfceMailwatchNetConn *net_conn,
                                                      XMNCShouldContinueFunc func,
                                                      gpointer user_data);

gboolean xfce_mailwatch_net_conn_should_continue(XfceMailwatchNetConn *net_conn);

void xfce_mailwatch_net_conn_set_service(XfceMailwatchNetConn *net_conn,
                                         const gchar *service);

void xfce_mailwatch_net_conn_set_port(XfceMailwatchNetConn *net_conn,
                                      guint port);
guint xfce_mailwatch_net_conn_get_port(XfceMailwatchNetConn *net_conn);

void xfce_mailwatch_net_conn_set_line_terminator(XfceMailwatchNetConn *net_conn,
                                                 const gchar *line_term);
const gchar *xfce_mailwatch_net_conn_get_line_terminator(XfceMailwatchNetConn *net_conn);

gboolean xfce_mailwatch_net_conn_is_secure(XfceMailwatchNetConn *net_conn);

gboolean xfce_mailwatch_net_conn_connect(XfceMailwatchNetConn *net_conn,
                                         GError **error);

gboolean xfce_mailwatch_net_conn_is_connected(XfceMailwatchNetConn *net_conn);

gboolean xfce_mailwatch_net_conn_make_secure(XfceMailwatchNetConn *net_conn,
                                             GError **error);

gint xfce_mailwatch_net_conn_send_data(XfceMailwatchNetConn *net_conn,
                                       const guchar *buf,
                                       gssize buf_len,
                                       GError **error);

gint xfce_mailwatch_net_conn_recv_data(XfceMailwatchNetConn *net_conn,
                                       guchar *buf,
                                       gsize buf_len,
                                       GError **error);
gint xfce_mailwatch_net_conn_recv_line(XfceMailwatchNetConn *net_conn,
                                       gchar *buf,
                                       gsize buf_len,
                                       GError **error);

void xfce_mailwatch_net_conn_disconnect(XfceMailwatchNetConn *net_conn);
void xfce_mailwatch_net_conn_destroy(XfceMailwatchNetConn *net_conn);

G_END_DECLS

#endif  /* __MAILWATCH_NET_H__ */

