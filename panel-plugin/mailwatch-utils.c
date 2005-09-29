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

#include <stdio.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#if HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#include <gtk/gtk.h>

#include <libxfcegui4/libxfcegui4.h>

#include "mailwatch-utils.h"
#include "mailwatch.h"

#ifdef HAVE_SSL_SUPPORT

#include <gcrypt.h>

/* missing from 1.2.0? */
#ifndef _GCRY_PTH_SOCKADDR
#define _GCRY_PTH_SOCKADDR  struct sockaddr
#endif
#ifndef _GCRY_PTH_SOCKLEN_T
#define _GCRY_PTH_SOCKLEN_T socklen_t
#endif

#define GNUTLS_CA_FILE           "ca.pem"
    
/* stuff to support 'gthreads' with gcrypt */
static int my_g_mutex_init(void **priv);
static int my_g_mutex_destroy(void **priv);
static int my_g_mutex_lock(void **priv);
static int my_g_mutex_unlock(void **priv);
static struct gcry_thread_cbs gcry_threads_gthread = {
    GCRY_THREAD_OPTION_USER,
    NULL,
    my_g_mutex_init,
    my_g_mutex_destroy,
    my_g_mutex_lock,
    my_g_mutex_unlock,
    read,
    write,
    select,
    waitpid,
    accept,
    (int (*)(int, _GCRY_PTH_SOCKADDR *, _GCRY_PTH_SOCKLEN_T))connect,
    sendmsg,
    recvmsg
};

/*
 * gthread -> gcrypt support wrappers
 */
static int
my_g_mutex_init(void **priv)
{
    GMutex **gmx = (GMutex **)priv;
    
    *gmx = g_mutex_new();
    if(!*gmx)
        return -1;
    return 0;
}

static int
my_g_mutex_destroy(void **priv)
{
    GMutex **gmx = (GMutex **)priv;
    
    g_mutex_free(*gmx);
    return 0;
}

static int
my_g_mutex_lock(void **priv)
{
    GMutex **gmx = (GMutex **)priv;
    
    g_mutex_lock(*gmx);
    return 0;
}

static int
my_g_mutex_unlock(void **priv)
{
    GMutex **gmx = (GMutex **)priv;
    
    g_mutex_unlock(*gmx);
    return 0;
}

/***/

#endif  /* defined(HAVE_SSL_SUPPORT) */

gboolean
xfce_mailwatch_net_get_sockaddr(const gchar *host, const gchar *service,
        struct addrinfo *hints, struct sockaddr_in *addr, GError **error)
{
    struct addrinfo *res = NULL;
    gint ret;
    
    /* according to getaddrinfo(3), this should be reentrant.  however, calling
     * it from several threads often causes a crash.  bactraces show that we're
     * indeed inside getaddrinfo() in more than one thread, and I can't figure
     * out any other explanation. */
    
    xfce_mailwatch_threads_enter();
    ret = getaddrinfo(host, service, hints, &res);
    xfce_mailwatch_threads_leave();
    if(ret) {
        if(error) {
            g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                        "getaddrinfo(): %s", gai_strerror(ret));
        }
        return FALSE;
    }
    
    if(res->ai_addrlen != sizeof(struct sockaddr_in)) {
        if(error) {
            g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                        "getaddrinfo(): res->ai_addrlen != sizeof(struct sockaddr_in)");
        }
        freeaddrinfo(res);
        return FALSE;
    }
    
    memcpy(addr, res->ai_addr, sizeof(struct sockaddr_in));
    freeaddrinfo(res);
    
    return TRUE;
}

gboolean
xfce_mailwatch_net_negotiate_tls(gint sockfd,
                                 XfceMailwatchSecurityInfo *security_info,
                                 const gchar *host,
                                 GError **error)
{
#ifdef HAVE_SSL_SUPPORT
    gint gt_ret;
    const int cert_type_prio[2] = { GNUTLS_CRT_X509, 0 };
    
    TRACE("entering");
    
    /* init */
    gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_gthread);
    gnutls_global_init();
    security_info->gnutls_inited = TRUE;
    
    /* init the x509 cert */
    gnutls_certificate_allocate_credentials(&security_info->gt_creds);
    gnutls_certificate_set_x509_trust_file(security_info->gt_creds,
            GNUTLS_CA_FILE, GNUTLS_X509_FMT_PEM);
    
    /* init the session and set it up */
    gnutls_init(&security_info->gt_session, GNUTLS_CLIENT);
    gnutls_set_default_priority(security_info->gt_session);
    gnutls_certificate_type_set_priority(security_info->gt_session,
            cert_type_prio);
    gnutls_credentials_set(security_info->gt_session, GNUTLS_CRD_CERTIFICATE,
            security_info->gt_creds);
    gnutls_transport_set_ptr(security_info->gt_session,
            (gnutls_transport_ptr_t)sockfd);
    
    /* just do it */
    gt_ret = gnutls_handshake(security_info->gt_session);
    if(gt_ret < 0) {
        if(error) {
            g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                        gnutls_strerror(gt_ret));
        }
        g_critical(_("XfceMailwatch: TLS handshake failed: %s"), gnutls_strerror(gt_ret));
        return FALSE;
    } else {
        DBG("TLS handshake succeeded");
    }
    
    return TRUE;
#else
    if(error) {
        g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                    _("Not compiled with SSL/TLS support"));
    }
    g_critical(_("XfceMailwatch: TLS handshake failed: not compiled with SSL support."));
    
    return FALSE;
#endif
}


gssize
xfce_mailwatch_net_send(gint sockfd,
                        XfceMailwatchSecurityInfo *security_info,
                        const gchar *buf,
                        GError **error)
{
    gint bout = 0;
    
#ifdef HAVE_SSL_SUPPORT
    if(security_info->using_tls) {
        gint ret = 0, totallen = strlen(buf);
        gint bytesleft = totallen;
        
        if(!security_info->gnutls_inited) {
            if(error) {
                g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                            _("A secure connection was requested, but gnutls was not initialised"));
            }
            g_critical("XfceMailwatch: using_tls is TRUE, but gnutls was not inited");
            return -1;
        }
        
        while(bytesleft > 0) {
            ret = gnutls_record_send(security_info->gt_session,
                    buf+totallen-bytesleft, bytesleft);
            if(ret < 0 && ret != GNUTLS_E_INTERRUPTED && ret != GNUTLS_E_AGAIN) {
                if(error) {
                    g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                                "gnutls_record_send() [%d]: %s", ret,
                                gnutls_strerror(ret));
                }
                DBG("gnutls_record_send() failed (%d): %s", ret, gnutls_strerror(ret));
                bout = -1;
                break;
            } else if(ret > 0) {
                bout += ret;
                bytesleft -= ret;
            }
        }
    } else {
#endif
        bout = send(sockfd, buf, strlen(buf), MSG_NOSIGNAL);
        if(bout < 0 && error) {
            g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                        "send(): %s", strerror(errno));
        }
#ifdef HAVE_SSL_SUPPORT
    }
#endif
    
    return bout;
}

gssize
xfce_mailwatch_net_recv(gint sockfd,
                        XfceMailwatchSecurityInfo *security_info,
                        gchar *buf,
                        gsize len,
                        GError **error)
{
    fd_set rfd;
    struct timeval tv;
    gint ret, bin;
    
#ifdef HAVE_SSL_SUPPORT
    if(security_info->using_tls) {
        if(!security_info->gnutls_inited) {
            if(error) {
                g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                            _("A secure connection was requested, but gnutls was not initialised"));
            }
            g_critical("XfceMailwatch: using_tls is TRUE, but gnutls was not inited");
            return -1;
        }
        
        do {
            ret = gnutls_record_recv(security_info->gt_session, buf, len);
        } while(ret == GNUTLS_E_INTERRUPTED || ret == GNUTLS_E_AGAIN);
        
        if(ret < 0) {
            if(error) {
                g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                            "gnutls_record_recv() [%d]: %s", ret,
                            gnutls_strerror(ret));
            }
            bin = -1;
        } else
            bin = ret;
    } else {
#endif
        FD_ZERO(&rfd);
        FD_SET(sockfd, &rfd);
        tv.tv_sec = 45;
        tv.tv_usec = 0;
        
        ret = select(FD_SETSIZE, &rfd, NULL, NULL, &tv);
        if(ret < 0) {
            if(error) {
                g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                            "select(): %s", strerror(errno));
            }
            return -1;
        }
        
        if(!FD_ISSET(sockfd, &rfd))
            return 0;
        
        bin = recv(sockfd, buf, len, MSG_NOSIGNAL);
        if(bin < 0 && error) {
            g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                        "recv(): %s", strerror(errno));
        }
#ifdef HAVE_SSL_SUPPORT
    }
#endif
    
    if(bin >= 0)
        buf[bin] = 0;
    
    return bin;
}

void
xfce_mailwatch_net_tls_teardown(XfceMailwatchSecurityInfo *security_info)
{
#ifdef HAVE_SSL_SUPPORT
    if(security_info->gnutls_inited) {
        gnutls_deinit(security_info->gt_session);
        gnutls_certificate_free_credentials(security_info->gt_creds);
        gnutls_global_deinit();
        security_info->gnutls_inited = FALSE;
    }
#endif
}


GtkWidget *
xfce_mailwatch_custom_button_new(const gchar *text, const gchar *icon)
{
    GtkWidget *btn, *hbox, *img, *lbl;
    GdkPixbuf *pix;
    gint iw, ih;
    
    g_return_val_if_fail((text && *text) || icon, NULL);
    
    btn = gtk_button_new();
    
    hbox = gtk_hbox_new(FALSE, 4);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 0);
    gtk_widget_show(hbox);
    gtk_container_add(GTK_CONTAINER(btn), hbox);
    
    if(icon) {
        img = gtk_image_new_from_stock(icon, GTK_ICON_SIZE_BUTTON);
        if(!img || gtk_image_get_storage_type(GTK_IMAGE(img)) == GTK_IMAGE_EMPTY) {
            gtk_icon_size_lookup(GTK_ICON_SIZE_BUTTON, &iw, &ih);
            pix = xfce_themed_icon_load(icon, iw);
            if(pix) {
                if(img)
                    gtk_image_set_from_pixbuf(GTK_IMAGE(img), pix);
                else
                    img = gtk_image_new_from_pixbuf(pix);
                g_object_unref(G_OBJECT(pix));
            }
        }
        if(img) {
            gtk_widget_show(img);
            gtk_box_pack_start(GTK_BOX(hbox), img, FALSE, FALSE, 0);
        }
    }
    
    if(text) {
        lbl = gtk_label_new_with_mnemonic(text);
        gtk_widget_show(lbl);
        gtk_box_pack_start(GTK_BOX(hbox), lbl, FALSE, FALSE, 0);
        gtk_label_set_mnemonic_widget(GTK_LABEL(lbl), btn);
    }
    
    return btn;
}

GtkWidget *
xfce_mailwatch_create_framebox(const gchar *title, GtkWidget **frame_bin)
{
#if LIBXFCEGUI4_CHECK_VERSION(4, 3, 4)
    return xfce_create_framebox(title, frame_bin);
#else
    GtkWidget *frame = xfce_framebox_new(title, TRUE);
    *frame_bin = XFCE_FRAMEBOX(frame)->hbox;
    return frame;
#endif
}
