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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include <libxfce4util/libxfce4util.h>

#ifdef HAVE_SSL_SUPPORT
#include <gcrypt.h>
#include <gnutls/gnutls.h>
#endif

#include "mailwatch-net-conn.h"
#include "mailwatch-common.h"

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 0
#endif

#define SHOULD_CONTINUE(nc)  ( !(nc)->should_continue \
                               || ( (nc)->should_continue \
                                    && (nc)->should_continue((nc), \
                                                             (nc)->should_continue_user_data) \
                                  ) \
                             )

#define RECV_TIMEOUT            30  /* seconds */
#define TIMER_INIT              time_t __timer_start
#define TIMER_START             __timer_start = time(NULL)
#define TIMER_EXPIRED(endtime)  (time(NULL) - __timer_start >= (endtime))

struct _XfceMailwatchNetConn
{
    gchar *hostname;
    gchar *service;
    guint port;
    const gchar *line_terminator;

    gint fd;
    gint actual_port;

    guchar *buffer;
    gsize buffer_len;
    
    gboolean is_secure;
#ifdef HAVE_SSL_SUPPORT
    gnutls_session_t gt_session;
    gnutls_certificate_credentials_t gt_creds;
#endif

    XMNCShouldContinueFunc should_continue;
    gpointer should_continue_user_data;
};

typedef enum
{
    XFCE_MAILWATCH_NET_CONN_SUCCESS = 0,
    XFCE_MAILWATCH_NET_CONN_FATAL,
    XFCE_MAILWATCH_NET_CONN_ERROR,
} XfceMailwatchNetConnStatus;



#ifdef HAVE_SSL_SUPPORT

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
    (ssize_t (*)(int, fd_set *, fd_set *, fd_set *, struct timeval *))select,
    (ssize_t (*)(pid_t, int *, int))waitpid,
    accept,
    (int (*)(int, _GCRY_PTH_SOCKADDR *, _GCRY_PTH_SOCKLEN_T))connect,
    (int (*)(int, const struct msghdr *, int))sendmsg,
    (int (*)(int, struct msghdr *, int))recvmsg
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

#endif  /* defined(HAVE_SSL_SUPPORT) */



#ifdef HAVE_SSL_SUPPORT
static gboolean
xfce_mailwatch_net_conn_tls_handshake(XfceMailwatchNetConn *net_conn,
                                      GError **error)
{
    gint ret;
    TIMER_INIT;

    TIMER_START;
    do {
        ret = gnutls_handshake(net_conn->gt_session);
    } while((ret == GNUTLS_E_AGAIN || ret == GNUTLS_E_INTERRUPTED)
            && !TIMER_EXPIRED(RECV_TIMEOUT) && SHOULD_CONTINUE(net_conn));

    if(ret != GNUTLS_E_SUCCESS) {
        gint code = XFCE_MAILWATCH_ERROR_FAILED;
        const gchar *reason;

        if(!SHOULD_CONTINUE(net_conn)) {
            code = XFCE_MAILWATCH_ERROR_ABORTED;
            reason = _("Operation aborted");
        } else if(ret == GNUTLS_E_AGAIN || ret == GNUTLS_E_INTERRUPTED)
            reason = strerror(ETIMEDOUT);
        else
            reason = gnutls_strerror(ret);
        if(error)
            g_set_error(error, XFCE_MAILWATCH_ERROR, code, "%s", reason);
        g_critical("XfceMailwatch: TLS handshake failed: %s", reason);

        return FALSE;
    }

    DBG("TLS handshake succeeded");

    return TRUE;
}
#endif

static XfceMailwatchNetConnStatus
xfce_mailwatch_net_conn_do_connect(XfceMailwatchNetConn *net_conn,
                                   struct sockaddr *addr,
                                   size_t addrlen,
                                   GError **error)
{
    gint ret;
    TIMER_INIT;

    TIMER_START;
    do {
        ret = connect(net_conn->fd, addr, addrlen);
    } while(ret < 0 && (errno == EINTR || errno == EAGAIN)
            && !TIMER_EXPIRED(RECV_TIMEOUT) && SHOULD_CONTINUE(net_conn));

    if(!ret || (ret < 0 && errno == EINPROGRESS))  /* we're done here */
        return XFCE_MAILWATCH_NET_CONN_SUCCESS;

    /* this is a little different.  the only 'fatal' error at this
     * point in the overall connect operation is if SHOULD_CONTINUE
     * is false.  in that case, we set |error| and return _FATAL.
     * if the timer expired or another error occurred, we just
     * return a non-fatal _ERROR without setting |error|. */
    if(!SHOULD_CONTINUE(net_conn)) {
        if(error) {
            g_set_error(error, XFCE_MAILWATCH_ERROR,
                        XFCE_MAILWATCH_ERROR_ABORTED,
                        _("Operation aborted"));
        }

        return XFCE_MAILWATCH_NET_CONN_FATAL;
    }

    return XFCE_MAILWATCH_NET_CONN_ERROR;
}

static XfceMailwatchNetConnStatus
xfce_mailwatch_net_conn_get_connect_status(XfceMailwatchNetConn *net_conn,
                                           struct sockaddr *addr,
                                           GError **error)
{
    TIMER_INIT;

    TIMER_START;
    do {
        fd_set wfd;
        struct timeval tv = { 1, 0 };
        int sock_err = 0;
        socklen_t sock_err_len = sizeof(int);

        FD_ZERO(&wfd);
        FD_SET(net_conn->fd, &wfd);

        DBG("checking for a connection...");

        /* wait until the connect attempt finishes */
        if(select(FD_SETSIZE, NULL, &wfd, NULL, &tv) < 0) {
            if(errno == EINTR)
                continue;
            /* FIXME: should a select() failure actually be fatal? */
            return XFCE_MAILWATCH_NET_CONN_ERROR;
        }

        /* check to see if it finished, and, if so, if there was an
         * error, or if it completed successfully */
        if(!FD_ISSET(net_conn->fd, &wfd))
            continue;

        if(!getsockopt(net_conn->fd, SOL_SOCKET, SO_ERROR,
                       &sock_err, &sock_err_len)
           && !sock_err)
        {
            DBG("    connection succeeded");

            /* figure out the actual port */
            switch(addr->sa_family) {
#ifdef ENABLE_IPV6_SUPPORT
                case AF_INET6:
                {
                    struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)addr;
                    net_conn->actual_port = ntohs(addr_in6->sin6_port);
                    break;
                }
#endif
                case AF_INET:
                {
                    struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
                    net_conn->actual_port = ntohs(addr_in->sin_port);
                    break;
                }

                default:
                    g_warning("Unable to determine socket type to get real port number");
                    break;
            }

            errno = 0;
            return XFCE_MAILWATCH_NET_CONN_SUCCESS;
        } else {
            DBG("    connection failed: sock_err is (%d) %s",
                sock_err, strerror(sock_err));
            errno = sock_err;
            return XFCE_MAILWATCH_NET_CONN_ERROR;
        }
    } while(!TIMER_EXPIRED(RECV_TIMEOUT) && SHOULD_CONTINUE(net_conn));

    if(!SHOULD_CONTINUE(net_conn)) {
        if(error) {
            g_set_error(error, XFCE_MAILWATCH_ERROR,
                        XFCE_MAILWATCH_ERROR_ABORTED, _("Operation aborted"));
        }

        return XFCE_MAILWATCH_NET_CONN_FATAL;
    }

    return XFCE_MAILWATCH_NET_CONN_ERROR;
}



void
xfce_mailwatch_net_conn_init(void)
{
    static gboolean __inited = FALSE;

    if(!__inited) {
#ifdef HAVE_SSL_SUPPORT
        gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_gthread);
        gnutls_global_init();
#endif
        __inited = TRUE;
    }
}

XfceMailwatchNetConn *
xfce_mailwatch_net_conn_new(const gchar *hostname,
                            const gchar *service)
{
    XfceMailwatchNetConn *net_conn;
    
    g_return_val_if_fail(hostname && *hostname, NULL);

    net_conn = g_new0(XfceMailwatchNetConn, 1);
    net_conn->hostname = g_strdup(hostname);
    net_conn->service = service ? g_strdup(service) : NULL;
    net_conn->line_terminator = g_intern_string("\r\n");
    net_conn->fd = -1;
    net_conn->actual_port = -1;

    return net_conn;
}

void
xfce_mailwatch_net_conn_set_should_continue_func(XfceMailwatchNetConn *net_conn,
                                                 XMNCShouldContinueFunc func,
                                                 gpointer user_data)
{
    g_return_if_fail(net_conn);
    net_conn->should_continue = func;
    net_conn->should_continue_user_data = user_data;
}

gboolean
xfce_mailwatch_net_conn_should_continue(XfceMailwatchNetConn *net_conn)
{
    g_return_val_if_fail(net_conn, FALSE);
    return SHOULD_CONTINUE(net_conn);
}

void
xfce_mailwatch_net_conn_set_service(XfceMailwatchNetConn *net_conn,
                                    const gchar *service)
{
    g_return_if_fail(net_conn && net_conn->fd == -1);

    g_free(net_conn->service);
    net_conn->service = g_strdup(service);
}

void
xfce_mailwatch_net_conn_set_port(XfceMailwatchNetConn *net_conn,
                                 guint port)
{
    g_return_if_fail(net_conn && net_conn->fd == -1);
    net_conn->port = port;
}

guint
xfce_mailwatch_net_conn_get_port(XfceMailwatchNetConn *net_conn)
{
    g_return_val_if_fail(net_conn, 0);

    if(net_conn->actual_port != -1)
        return net_conn->actual_port;
    else
        return net_conn->port;
}

void
xfce_mailwatch_net_conn_set_line_terminator(XfceMailwatchNetConn *net_conn,
                                            const gchar *line_term)
{
    g_return_if_fail(net_conn && line_term && *line_term);
    net_conn->line_terminator = g_intern_string(line_term);
}

const gchar *
xfce_mailwatch_net_conn_get_line_terminator(XfceMailwatchNetConn *net_conn)
{
    g_return_val_if_fail(net_conn, NULL);
    return net_conn->line_terminator;
}

gboolean
xfce_mailwatch_net_conn_is_secure(XfceMailwatchNetConn *net_conn)
{
    g_return_val_if_fail(net_conn, FALSE);
    return net_conn->is_secure;
}

static gboolean
xfce_mailwatch_net_conn_get_addrinfo(XfceMailwatchNetConn *net_conn,
                                     struct addrinfo **addresses,
                                     GError **error)
{
    struct addrinfo hints;
    gint ret;
    gchar real_service[128];

    g_return_val_if_fail(net_conn && addresses && !*addresses
                         && (!error || !*error), FALSE);

    memset(&hints, 0, sizeof(hints));
#ifdef ENABLE_IPV6_SUPPORT
    hints.ai_family = AF_UNSPEC;
#else
    hints.ai_family = AF_INET;
#endif
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;

    /* allow setting nonstandard port */
    if(net_conn->port > 0)
        g_snprintf(real_service, sizeof(real_service), "%d", net_conn->port);
    else
        g_strlcpy(real_service, net_conn->service, sizeof(real_service));
    
    /* according to getaddrinfo(3), this should be reentrant.  however, calling
     * it from several threads often causes a crash.  backtraces show that we're
     * indeed inside getaddrinfo() in more than one thread, and I can't figure
     * out any other explanation. */
    
    xfce_mailwatch_threads_enter();
    ret = getaddrinfo(net_conn->hostname, real_service, &hints, addresses);
    xfce_mailwatch_threads_leave();
    if(ret) {
        if(error) {
            g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                        _("Could not find host \"%s\": %s"),
                        net_conn->hostname,
                        ret == EAI_SYSTEM ? strerror(errno)
                                          : gai_strerror(ret));
        }
        return FALSE;
    }
    
    return TRUE;
}

gboolean
xfce_mailwatch_net_conn_connect(XfceMailwatchNetConn *net_conn,
                                GError **error)
{
    struct addrinfo *addresses = NULL, *ai;
    
    g_return_val_if_fail(net_conn && (!error || !*error), FALSE);
    g_return_val_if_fail(net_conn->fd == -1, TRUE);

    net_conn->actual_port = -1;

    if(!xfce_mailwatch_net_conn_get_addrinfo(net_conn, &addresses, error)) {
        DBG("failed to get sockaddr");
        return FALSE;
    }
    
    for(ai = addresses; ai; ai = ai->ai_next) {
        net_conn->fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if(net_conn->fd < 0)
            continue;
        
        if(fcntl(net_conn->fd, F_SETFL,
                 fcntl(net_conn->fd, F_GETFL) | O_NONBLOCK))
        {
            g_warning("Unable to set socket to non-blocking mode. Things may not work properly from here on out.");
        }
        
        switch(xfce_mailwatch_net_conn_do_connect(net_conn, ai->ai_addr,
                                                  ai->ai_addrlen, error))
        {
            case XFCE_MAILWATCH_NET_CONN_FATAL:
                goto out_err;
            case XFCE_MAILWATCH_NET_CONN_ERROR:
                close(net_conn->fd);
                net_conn->fd = -1;
                continue;
            case XFCE_MAILWATCH_NET_CONN_SUCCESS:
                break;
        }

        switch(xfce_mailwatch_net_conn_get_connect_status(net_conn,
                                                          ai->ai_addr,
                                                          error))
        {
            case XFCE_MAILWATCH_NET_CONN_FATAL:
                goto out_err;
            case XFCE_MAILWATCH_NET_CONN_ERROR:
                close(net_conn->fd);
                net_conn->fd = -1;
                continue;
            case XFCE_MAILWATCH_NET_CONN_SUCCESS:
#if 0  /* let's use non-blocking sockets.  this can potentially hide
        * bugs in my implementation, though */
                if(fcntl(net_conn->fd, F_SETFL,
                         fcntl(net_conn->fd, F_GETFL) & ~(O_NONBLOCK)))
                {
                    g_warning("Unable to return socket to blocking mode.  Data may not be retreived correctly.");
                }
#endif
                freeaddrinfo(addresses);
                return TRUE;
        }
    }

out_err:
    
    if(net_conn->fd >= 0) {
        close(net_conn->fd);
        net_conn->fd = -1;
    }

    if(error && !*error) {
        g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                    _("Failed to connect to server \"%s\": %s"),
                    net_conn->hostname, strerror(errno));
    }

    if(addresses)
        freeaddrinfo(addresses);

    return FALSE;
}

gboolean
xfce_mailwatch_net_conn_is_connected(XfceMailwatchNetConn *net_conn)
{
    g_return_val_if_fail(net_conn, FALSE);
    return net_conn->fd != -1 ? TRUE : FALSE;
}

gboolean
xfce_mailwatch_net_conn_make_secure(XfceMailwatchNetConn *net_conn,
                                    GError **error)
{
    g_return_val_if_fail(net_conn && (!error || !*error), FALSE);
    g_return_val_if_fail(net_conn->fd != -1, FALSE);
    g_return_val_if_fail(!net_conn->is_secure, TRUE);

#ifdef HAVE_SSL_SUPPORT
    /* init the x509 cert */
    gnutls_certificate_allocate_credentials(&net_conn->gt_creds);
    gnutls_certificate_set_x509_trust_file(net_conn->gt_creds,
                                           GNUTLS_CA_FILE,
                                           GNUTLS_X509_FMT_PEM);
    
    /* init the session and set it up */
    gnutls_init(&net_conn->gt_session, GNUTLS_CLIENT);
    gnutls_priority_set_direct (net_conn->gt_session, "NORMAL", NULL); 
    gnutls_credentials_set(net_conn->gt_session, GNUTLS_CRD_CERTIFICATE,
                           net_conn->gt_creds);
    gnutls_transport_set_ptr(net_conn->gt_session,
                             (gnutls_transport_ptr_t)net_conn->fd);
#if GNUTLS_VERSION_NUMBER < 0x020c00 
    if(fcntl(net_conn->fd, F_GETFL) & O_NONBLOCK)
        gnutls_transport_set_lowat(net_conn->gt_session, 0);
#endif    

    if(!xfce_mailwatch_net_conn_tls_handshake(net_conn, error)) {
#if 0
        gnutls_bye(net_conn->gt_session, GNUTLS_SHUT_RDWR);
#endif
        gnutls_deinit(net_conn->gt_session);
        gnutls_certificate_free_credentials(net_conn->gt_creds);

        return FALSE;
    }

    net_conn->is_secure = TRUE;

    return TRUE;
#else
    if(error) {
        g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                    _("Not compiled with SSL/TLS support"));
    }
    g_critical("XfceMailwatch: TLS handshake failed: not compiled with SSL support.");
    
    return FALSE;
#endif
}

gint
xfce_mailwatch_net_conn_send_data(XfceMailwatchNetConn *net_conn,
                                  const guchar *buf,
                                  gssize buf_len,
                                  GError **error)
{
    gint bout = 0;
    TIMER_INIT;

    g_return_val_if_fail(net_conn && (!error || !*error), -1);
    g_return_val_if_fail(net_conn->fd != -1, -1);

    if(buf_len < 0)
        buf_len = strlen((const gchar *)buf);

#ifdef HAVE_SSL_SUPPORT
    if(net_conn->is_secure) {
        gint ret = 0, totallen = buf_len;
        gint bytesleft = totallen;

        while(bytesleft > 0) {
            TIMER_START;
            do {
                ret = gnutls_record_send(net_conn->gt_session,
                                         buf + totallen - bytesleft,
                                         bytesleft);

                if(ret == GNUTLS_E_REHANDSHAKE) {
                    if(!xfce_mailwatch_net_conn_tls_handshake(net_conn, error))
                        return -1;
                    ret = GNUTLS_E_AGAIN;
                }
            } while((ret == GNUTLS_E_INTERRUPTED || ret == GNUTLS_E_AGAIN)
                    && !TIMER_EXPIRED(RECV_TIMEOUT) && SHOULD_CONTINUE(net_conn));

            if(ret < 0) {
                gint code = XFCE_MAILWATCH_ERROR_FAILED;
                const gchar *reason;

                if(!SHOULD_CONTINUE(net_conn)) {
                    code = XFCE_MAILWATCH_ERROR_ABORTED;
                    reason = _("Operation aborted");
                } else if(TIMER_EXPIRED(RECV_TIMEOUT))
                    reason = strerror(ETIMEDOUT);
                else
                    reason = gnutls_strerror(ret);
                if(error) {
                    g_set_error(error, XFCE_MAILWATCH_ERROR, code,
                                _("Failed to send encrypted data: %s"),
                                reason);
                }
                DBG("gnutls_record_send() failed (%d): %s", ret, reason);
                return -1;
            } else {
                bout += ret;
                bytesleft -= ret;
            }
        }
    } else
#endif
    {
        TIMER_START;
        do {
            bout = send(net_conn->fd, buf, buf_len, MSG_NOSIGNAL);
        } while(bout < 0 && (errno == EINTR || errno == EAGAIN)
                && !TIMER_EXPIRED(RECV_TIMEOUT) && SHOULD_CONTINUE(net_conn));
    }

    if(bout < 0 && error) {
        gint code = XFCE_MAILWATCH_ERROR_FAILED;
        const gchar *reason;

        if(!SHOULD_CONTINUE(net_conn)) {
            code = XFCE_MAILWATCH_ERROR_ABORTED;
            reason = _("Operation aborted");
        } else if(errno == EINTR || errno == EAGAIN)
            reason = strerror(ETIMEDOUT);
        else
            reason = strerror(errno);

        g_set_error(error, XFCE_MAILWATCH_ERROR, code,
                    _("Failed to send data: %s"), reason);
    }

    return bout;
}

static gint
xfce_mailwatch_net_conn_recv_internal(XfceMailwatchNetConn *net_conn,
                                      guchar *buf,
                                      gsize buf_len,
                                      gboolean block,
                                      GError **error)
{
    gint ret, bin = 0, code = XFCE_MAILWATCH_ERROR_FAILED;
    const gchar *reason;
    TIMER_INIT;

    TIMER_START;
    do {
        fd_set rfd;
        struct timeval tv = { 1, 0 };

        FD_ZERO(&rfd);
        FD_SET(net_conn->fd, &rfd);
        if(!block)
            tv.tv_sec = 0;

#ifdef HAVE_SSL_SUPPORT
        /* Read data from the gnutls read buffer, first. */
        if (net_conn->is_secure
            && (ret = gnutls_record_check_pending(net_conn->gt_session)) > 0) {
            break;
        }
#endif
        ret = select(FD_SETSIZE, &rfd, NULL, NULL, &tv);
        if(ret > 0 && FD_ISSET(net_conn->fd, &rfd))
            break;
        else if(ret < 0 && errno != EINTR) {
            g_set_error(error, XFCE_MAILWATCH_ERROR,
                        XFCE_MAILWATCH_ERROR_FAILED, "%s", strerror(errno));
            return -1;
        } else if(!block)
            return 0;
    } while((ret == 0 || (ret < 0 && errno == EINTR))
            && !TIMER_EXPIRED(RECV_TIMEOUT) && SHOULD_CONTINUE(net_conn));

    if(ret < 0 && errno != EINTR) {
        if(error) {
            g_set_error(error, XFCE_MAILWATCH_ERROR,
                        XFCE_MAILWATCH_ERROR_FAILED, "%s", strerror(errno));
        }
        return -1;
    } else if(!SHOULD_CONTINUE(net_conn)) {
        if(error) {
            g_set_error(error, XFCE_MAILWATCH_ERROR,
                        XFCE_MAILWATCH_ERROR_ABORTED, "%s",
                        _("Operation aborted"));
        }
        return -1;
    } else if(TIMER_EXPIRED(RECV_TIMEOUT)) {
        if(error) {
            g_set_error(error, XFCE_MAILWATCH_ERROR,
                        XFCE_MAILWATCH_ERROR_FAILED, "%s",
                        strerror(ETIMEDOUT));
        }
        return -1;
    }

#ifdef HAVE_SSL_SUPPORT
    if(net_conn->is_secure) {
        gint gret;
        code = XFCE_MAILWATCH_ERROR_FAILED;

        TIMER_START;
        do {
            gret = gnutls_record_recv(net_conn->gt_session, buf, buf_len);

            if(gret == GNUTLS_E_REHANDSHAKE) {
                if(!xfce_mailwatch_net_conn_tls_handshake(net_conn, error))
                    return -1;
                gret = GNUTLS_E_AGAIN;
            }
        } while((gret == GNUTLS_E_INTERRUPTED || gret == GNUTLS_E_AGAIN)
                && !TIMER_EXPIRED(RECV_TIMEOUT) && SHOULD_CONTINUE(net_conn));
        
        if(gret < 0) {
            if(error) {
                if(!SHOULD_CONTINUE(net_conn)) {
                    code = XFCE_MAILWATCH_ERROR_ABORTED;
                    reason = _("Operation aborted");
                } else if(TIMER_EXPIRED(RECV_TIMEOUT))
                    reason = strerror(ETIMEDOUT);
                else
                    reason = gnutls_strerror(gret);

                g_set_error(error, XFCE_MAILWATCH_ERROR, code,
                            _("Failed to receive encrypted data: %s"),
                            reason);
            }

            bin = -1;
        } else
            bin = gret;
    } else
#endif
    {
        gint pret;
        code = XFCE_MAILWATCH_ERROR_FAILED;

        TIMER_START;
        do {
            pret = recv(net_conn->fd, buf, buf_len, MSG_NOSIGNAL);
        } while(pret < 0 && (errno == EINTR || errno == EAGAIN)
                && !TIMER_EXPIRED(RECV_TIMEOUT) && SHOULD_CONTINUE(net_conn));

        if(pret < 0) {
            if(error) {
                if(!SHOULD_CONTINUE(net_conn)) {
                    code = XFCE_MAILWATCH_ERROR_ABORTED;
                    reason = _("Operation aborted");
                } else if(TIMER_EXPIRED(RECV_TIMEOUT))
                    reason = strerror(ETIMEDOUT);
                else
                    reason = strerror(errno);

                g_set_error(error, XFCE_MAILWATCH_ERROR, code,
                            _("Failed to receive data: %s"), reason);
            }
            bin = -1;
        } else
            bin = pret;
    }

    return bin;
}

gint
xfce_mailwatch_net_conn_recv_data(XfceMailwatchNetConn *net_conn,
                                  guchar *buf,
                                  gsize buf_len,
                                  GError **error)
{
    gint bin = 0, ret;

    g_return_val_if_fail(net_conn && (!error || !*error), -1);
    g_return_val_if_fail(net_conn->fd != -1, -1);

    if(net_conn->buffer_len) {
        if(net_conn->buffer_len <= buf_len) {
            bin = net_conn->buffer_len;
            memcpy(buf, net_conn->buffer, bin);
            g_free(net_conn->buffer);
            net_conn->buffer = NULL;
            net_conn->buffer_len = 0;

            if(bin == (gint)buf_len)
                return bin;
            else {
                buf += bin;
                buf_len -= bin;
            }
        } else {
            bin = buf_len;
            net_conn->buffer_len -= bin;
            memcpy(buf, net_conn->buffer, bin);
            memmove(net_conn->buffer, net_conn->buffer + bin,
                    net_conn->buffer_len);
            net_conn->buffer = g_realloc(net_conn->buffer,
                                         net_conn->buffer_len + 1);
            net_conn->buffer[net_conn->buffer_len] = 0;

            return bin;
        }
    }

    ret = xfce_mailwatch_net_conn_recv_internal(net_conn, buf, buf_len,
                                                bin > 0 ? FALSE : TRUE,
                                                error);
    if(ret > 0)
        bin += ret;

    return bin;
}

gint
xfce_mailwatch_net_conn_recv_line(XfceMailwatchNetConn *net_conn,
                                  gchar *buf,
                                  gsize buf_len,
                                  GError **error)
{
#define BUFSTEP  1024
    gint bin;
    gchar *p = NULL;

    g_return_val_if_fail(net_conn && (!error || !*error), -1);
    g_return_val_if_fail(net_conn->fd != -1, -1);

    do {
        p = NULL;
        if(net_conn->buffer_len > 0)
            p = strstr((char *)net_conn->buffer, net_conn->line_terminator);

        if(!p) {
            net_conn->buffer = g_realloc(net_conn->buffer,
                                         net_conn->buffer_len + BUFSTEP + 1);
            bin = xfce_mailwatch_net_conn_recv_internal(net_conn,
                                                        net_conn->buffer
                                                        + net_conn->buffer_len,
                                                        BUFSTEP, TRUE, error);
            if(bin <= 0) {
                net_conn->buffer = g_realloc(net_conn->buffer,
                                             net_conn->buffer_len + 1);
                net_conn->buffer[net_conn->buffer_len] = 0;
                return bin;
            }

            net_conn->buffer_len += bin;
            net_conn->buffer[net_conn->buffer_len] = 0;
        }

        /* XXX: keep this from going too crazy */
        if(net_conn->buffer_len > (512 * 1024)) {
            if(error) {
                g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                            _("Canceling read: read too many bytes without a newline"));
            }
            return -1;
        }
    } while(!p);

    if((gint)buf_len < p - (gchar *)net_conn->buffer) {
        if(error) {
            gchar *bl = g_strdup_printf("%" G_GSIZE_FORMAT, buf_len);
            g_set_error(error, XFCE_MAILWATCH_ERROR, 0,
                        _("Buffer is not large enough to hold a full line (%s < %d)"),
                        bl, (gint)(p - (gchar *)net_conn->buffer));
            g_free(bl);
        }
        return -1;
    }

    bin = p - (gchar *)net_conn->buffer;
    memcpy(buf, net_conn->buffer, bin);
    buf[bin] = 0;

    net_conn->buffer_len -= bin + strlen(net_conn->line_terminator);
    memmove(net_conn->buffer, p + strlen(net_conn->line_terminator),
            net_conn->buffer_len);
    net_conn->buffer = g_realloc(net_conn->buffer,
                                 net_conn->buffer_len + 1);
    net_conn->buffer[net_conn->buffer_len] = 0;

    return bin;
}

void
xfce_mailwatch_net_conn_disconnect(XfceMailwatchNetConn *net_conn)
{
    g_return_if_fail(net_conn);
    g_return_if_fail(net_conn->fd != -1);

#ifdef HAVE_SSL_SUPPORT
    if(net_conn->is_secure) {
#if 0
        gnutls_bye(net_conn->gt_session, GNUTLS_SHUT_RDWR);
#endif
        gnutls_deinit(net_conn->gt_session);
        gnutls_certificate_free_credentials(net_conn->gt_creds);
        net_conn->is_secure = FALSE;
    }
#endif

    g_free(net_conn->buffer);
    net_conn->buffer = NULL;
    net_conn->buffer_len = 0;

    shutdown(net_conn->fd, SHUT_RDWR);
    close(net_conn->fd);
    net_conn->fd = -1;
    net_conn->actual_port = -1;
}

void
xfce_mailwatch_net_conn_destroy(XfceMailwatchNetConn *net_conn)
{
    g_return_if_fail(net_conn);

    if(net_conn->fd != -1)
        xfce_mailwatch_net_conn_disconnect(net_conn);

    g_free(net_conn->hostname);
    g_free(net_conn->service);
    g_free(net_conn->buffer);  /* shouldn't need this */

    g_free(net_conn);
}

