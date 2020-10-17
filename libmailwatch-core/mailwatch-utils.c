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

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_SSL_SUPPORT
#include <gcrypt.h>
#endif

#include <libxfce4util/libxfce4util.h>

#include "mailwatch-utils.h"
#include "mailwatch-common.h"
#include "mailwatch.h"

#ifdef HAVE_SSL_SUPPORT
/* assumes |dest| is allocated 2x |src_len| */
static void
bin2hex(gchar *dest,
        const guchar *src,
        gsize src_len)
{
    static const gchar hexdigits[] = "0123456789abcdef";

    if(!src_len)
        return;

    while(src_len-- > 0) {
        *dest++ = hexdigits[(*src >> 4) & 0xf];
        *dest++ = hexdigits[*src & 0xf];
        src++;
    }
}
#endif

gchar *
xfce_mailwatch_cram_md5(const gchar *username,
                        const gchar *password,
                        const gchar *challenge_base64)
{
#ifdef HAVE_SSL_SUPPORT
    gchar challenge[2048];
    gsize len, username_len;
    gcry_md_hd_t hmac_md5;
    gchar *response, *response_base64 = NULL;
    
    g_return_val_if_fail(username && *username && password && *password
                         && challenge_base64 && *challenge_base64, NULL);

    len = xfce_mailwatch_base64_decode(challenge_base64, (guchar *)challenge,
                                       sizeof(challenge) - 1);
    if(len <= 0)
        return NULL;
    challenge[len] = 0;
    DBG("challenge is \"%s\"\n", challenge);

    if(gcry_md_open(&hmac_md5, GCRY_MD_MD5, GCRY_MD_FLAG_HMAC) != GPG_ERR_NO_ERROR)
        return NULL;
    gcry_md_setkey(hmac_md5, password, strlen(password));
    gcry_md_write(hmac_md5, challenge, len);
    gcry_md_final(hmac_md5);

    username_len = strlen(username);
    /* username + a space + MD5 in hex + null */
    response = g_malloc0(username_len + 1
                         + gcry_md_get_algo_dlen(GCRY_MD_MD5)*2 + 1);
    strcpy(response, username);
    response[username_len] = ' ';
    bin2hex(response + username_len + 1, gcry_md_read(hmac_md5, GCRY_MD_MD5),
            gcry_md_get_algo_dlen(GCRY_MD_MD5));

    gcry_md_close(hmac_md5);

    DBG("response before base64: %s\n", response);
    if(xfce_mailwatch_base64_encode((guchar *)response, strlen(response),
                                    &response_base64) <= 0)
    {
        g_free(response_base64);
        response_base64 = NULL;
    }

    g_free(response);

    return response_base64;
#else
    g_warning("CRAM-MD5 computation unavailable: libmailwatch was not compiled with gnutls support.");
    return NULL;
#endif
}


/*
 * The following Base64 code is provided under the following license:
 *
 * Copyright (c) 1995 - 1999 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Modified slightly by Brian Tarricone <bjt23@cornell.edu> to use g_malloc()
 * and glib primitive types, and to add buffer overrun checking.
 */

static const gchar base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

gint
xfce_mailwatch_base64_encode(const guint8 *data,
                             gsize size,
                             gchar **str)
{
  gchar *s, *p;
  guint i;
  gint c;
  const guchar *q;

  p = s = (gchar *)g_malloc(size*4/3+4);
  q = (const guchar *)data;
  i=0;
  for(i = 0; i < size;){
    c=q[i++];
    c*=256;
    if(i < size)
      c+=q[i];
    i++;
    c*=256;
    if(i < size)
      c+=q[i];
    i++;
    p[0]=base64[(c&0x00fc0000) >> 18];
    p[1]=base64[(c&0x0003f000) >> 12];
    p[2]=base64[(c&0x00000fc0) >> 6];
    p[3]=base64[(c&0x0000003f) >> 0];
    if(i > size)
      p[3]='=';
    if(i > size+1)
      p[2]='=';
    p+=4;
  }
  *p=0;
  *str = s;
  return strlen(s);
}

static
int pos(gchar c)
{
  gchar *p;
  for(p = (gchar *)base64; *p; p++)
    if(*p == c)
      return p - base64;
  return -1;
}

gint
xfce_mailwatch_base64_decode(const gchar *str,
                             guint8 *data,
                             gsize size)
{
  const char *p;
  unsigned char *q;
  int c;
  int x;
  int done = 0;
  
  q=(unsigned char*)data;
  for(p=str; *p && !done; p+=4){
    x = pos(p[0]);
    if(x >= 0)
      c = x;
    else{
      done = 3;
      break;
    }
    c*=64;
    
    x = pos(p[1]);
    if(x >= 0)
      c += x;
    else
      return -1;
    c*=64;
    
    if(p[2] == '=')
      done++;
    else{
      x = pos(p[2]);
      if(x >= 0)
	c += x;
      else
	return -1;
    }
    c*=64;
    
    if(p[3] == '=')
      done++;
    else{
      if(done)
	return -1;
      x = pos(p[3]);
      if(x >= 0)
	c += x;
      else
	return -1;
    }
    if(done < 3) {
      if(!size)
        return -1;
      *q++=(c&0x00ff0000)>>16;
      --size;
    }
      
    if(done < 2) {
      if(!size)
        return -1;
      *q++=(c&0x0000ff00)>>8;
      --size;
    }

    if(done < 1) {
      if(!size)
          return -1;
      *q++=(c&0x000000ff)>>0;
      --size;
    }
  }
  return q - (unsigned char*)data;
}

/*****
 * End Base64 code.  Don't put other stuff under here.
 *****/
