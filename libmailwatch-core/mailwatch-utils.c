/*
 *  xfce4-mailwatch-plugin - a mail notification applet for the xfce4 panel
 *  Copyright (c) 2005-2008 Brian Tarricone <bjt23@cornell.edu>
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
 *
 *  The Base64 encoding functionalty at the bottom of this file is
 *  released under different terms.  See the copyright/licensing block
 *  below for details.
 */

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

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#include <libxfce4util/libxfce4util.h>

#include "mailwatch-utils.h"

gchar *
xfce_mailwatch_cram_md5(const gchar *username,
                        const gchar *password,
                        const gchar *challenge_base64)
{
    guchar *challenge;
    gsize challenge_len;
    gchar *response, *response_base64 = NULL;
    gchar *digest;

    g_return_val_if_fail(username && *username && password && *password
                         && challenge_base64 && *challenge_base64, NULL);

    challenge = g_base64_decode(challenge_base64, &challenge_len);
    if(!challenge)
        return NULL;

    digest = g_compute_hmac_for_data(G_CHECKSUM_MD5,
                                     (guchar *)password, strlen(password),
                                     challenge, challenge_len);

    response = g_strdup_printf("%s %s", username, digest);

    DBG("response before base64: %s\n", response);
    response_base64 = g_base64_encode((guchar *)response, strlen(response));

    g_free(response);
    g_free(digest);
    g_free(challenge);

    return response_base64;
}
