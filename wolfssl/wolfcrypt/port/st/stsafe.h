/* stsafe.h
 *
 * Copyright (C) 2006-2018 wolfSSL Inc.
 *
 * This file is part of wolfSSL.
 *
 * wolfSSL is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * wolfSSL is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#ifndef _WOLFPORT_STSAFE_H_
#define _WOLFPORT_STSAFE_H_

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/ecc.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

#ifdef WOLFSSL_STSAFEA100

#include "stsafe_interface.h"

#ifndef STSAFE_MAX_KEY_LEN
    #define STSAFE_MAX_KEY_LEN ((uint32_t)48) /* for up to 384-bit keys */
#endif
#ifndef STSAFE_MAX_PUBKEY_RAW_LEN
    #define STSAFE_MAX_PUBKEY_RAW_LEN ((uint32_t)STSAFE_MAX_KEY_LEN * 2) /* x/y */
#endif
#ifndef STSAFE_MAX_SIG_LEN
    #define STSAFE_MAX_SIG_LEN ((uint32_t)STSAFE_MAX_KEY_LEN * 2) /* r/s */
#endif

WOLFSSL_API int SSL_STSAFE_LoadDeviceCertificate(byte** pRawCertificate,
    word32* pRawCertificateLen);

#ifdef HAVE_PK_CALLBACKS
WOLFSSL_API int SSL_STSAFE_VerifyPeerCertCb(WOLFSSL* ssl,
   const unsigned char* sig, unsigned int sigSz,
   const unsigned char* hash, unsigned int hashSz,
   const unsigned char* keyDer, unsigned int keySz,
   int* result, void* ctx);
WOLFSSL_API int SSL_STSAFE_SignCertificateCb(WOLFSSL* ssl, 
    const byte* in, word32 inSz,
    byte* out, word32* outSz, 
    const byte* key, word32 keySz, void* ctx);
WOLFSSL_API int SSL_STSAFE_SharedSecretCb(WOLFSSL* ssl, 
    ecc_key* otherKey,
    unsigned char* pubKeyDer, unsigned int* pubKeySz,
    unsigned char* out, unsigned int* outlen,
    int side, void* ctx);
#endif

#endif /* WOLFSSL_STSAFEA100 */

#endif /* _WOLFPORT_STSAFE_H_ */