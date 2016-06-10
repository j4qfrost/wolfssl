/* ksdk_port.c
 *
 * Copyright (C) 2006-2016 wolfSSL Inc. All rights reserved.
 *
 * This file is part of wolfSSL.
 *
 * Contact licensing@wolfssl.com with any questions or comments.
 *
 * http://www.wolfssl.com
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

/* in case user set USE_FAST_MATH there */
#include <wolfssl/wolfcrypt/settings.h>
#ifdef NO_INLINE
#include <wolfssl/wolfcrypt/misc.h>
#else
#include <wolfcrypt/src/misc.c>
#endif

#ifdef USE_FAST_MATH

#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/tfm.h>
#include <wolfcrypt/src/asm.c> /* will define asm MACROS or C ones */

#if defined(FREESCALE_LTC_TFM)

#include "nxp/ksdk_port.h"
#include "fsl_ltc.h"
#include <stdint.h>

/* Reverse array in memory (in place) */
static void ltc_reverse_array(uint8_t *src, size_t src_len)
{
    int i;

    for (i = 0; i < src_len / 2; i++)
    {
        uint8_t tmp;

        tmp = src[i];
        src[i] = src[src_len - 1 - i];
        src[src_len - 1 - i] = tmp;
    }
}

/* same as fp_to_unsigned_bin() with fp_reverse() skipped */
static void fp_to_unsigned_lsb_bin(fp_int *a, unsigned char *b)
{
    fp_int t;

    fp_init_copy(&t, a);

    (void)fp_to_unsigned_bin_at_pos(0, &t, b);
}

static void ltc_get_lsb_bin_from_mp_int(uint8_t *dst, mp_int *A, uint16_t *psz)
{
    uint16_t sz;

    sz = mp_unsigned_bin_size(A);
    fp_to_unsigned_lsb_bin(A, dst); /* result is lsbyte at lowest addr as required by LTC */
    *psz = sz;
}

/* these function are used by wolfSSL upper layers (like RSA) */

/* c = a * b */
void fp_mul(fp_int *A, fp_int *B, fp_int *C)
{
    int szA, szB;
    szA = fp_unsigned_bin_size(A);
    szB = fp_unsigned_bin_size(B);

    /* if unsigned mul can fit into LTC PKHA let's use it, otherwise call software mul */
    if ((szA <= LTC_MAX_INT_BYTES / 2) && (szB <= LTC_MAX_INT_BYTES / 2))
    {
        int neg;

        neg = (A->sign == B->sign) ? FP_ZPOS : FP_NEG;

        /* unsigned multiply */
        uint8_t *ptrA = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);
        uint8_t *ptrB = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);
        uint8_t *ptrC = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);

        if (ptrA && ptrB && ptrC)
        {
            uint16_t sizeA, sizeB;

            ltc_get_lsb_bin_from_mp_int(ptrA, A, &sizeA);
            ltc_get_lsb_bin_from_mp_int(ptrB, B, &sizeB);
            XMEMSET(ptrC, 0xFF, LTC_MAX_INT_BYTES);

            LTC_PKHA_ModMul(LTC_BASE, ptrA, sizeA, ptrB, sizeB, ptrC, LTC_MAX_INT_BYTES, ptrB, &sizeB,
                            kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue,
                            kLTC_PKHA_TimingEqualized);

            ltc_reverse_array(ptrB, sizeB);
            mp_read_unsigned_bin(C, ptrB, sizeB);
        }

        /* fix sign */
        C->sign = neg;
        if (ptrA)
        {
            XFREE(ptrA, 0, DYNAMIC_TYPE_BIGINT);
        }
        if (ptrB)
        {
            XFREE(ptrB, 0, DYNAMIC_TYPE_BIGINT);
        }
        if (ptrC)
        {
            XFREE(ptrC, 0, DYNAMIC_TYPE_BIGINT);
        }
        return;
    }
    else
    {
        wolfcrypt_fp_mul(A, B, C);
    }
}

/* c = a mod b, 0 <= c < b  */
int fp_mod(fp_int *a, fp_int *b, fp_int *c)
{
#if defined(FREESCALE_LTC_TFM_RSA_4096_ENABLE)
    int szA, szB;
    szA = fp_unsigned_bin_size(a);
    szB = fp_unsigned_bin_size(b);
    if ((szA <= LTC_MAX_INT_BYTES) && (szB <= LTC_MAX_INT_BYTES))
    {
#endif /* FREESCALE_LTC_TFM_RSA_4096_ENABLE */
        int res = FP_OKAY;
        int neg;

        uint8_t *ptrA = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);
        uint8_t *ptrB = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);
        uint8_t *ptrC = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);

        /* get sign for the result */
        neg = (a->sign == b->sign) ? FP_ZPOS : FP_NEG;

        /* get remainder of unsigned a divided by unsigned b */
        if (ptrA && ptrB && ptrC)
        {
            uint16_t sizeA, sizeB, sizeC;

            ltc_get_lsb_bin_from_mp_int(ptrA, a, &sizeA);
            ltc_get_lsb_bin_from_mp_int(ptrB, b, &sizeB);

            if (kStatus_Success ==
                LTC_PKHA_ModRed(LTC_BASE, ptrA, sizeA, ptrB, sizeB, ptrC, &sizeC, kLTC_PKHA_IntegerArith))
            {
                ltc_reverse_array(ptrC, sizeC);
                mp_read_unsigned_bin(c, ptrC, sizeC);
            }
            else
            {
                res = FP_VAL;
            }
        }
        else
        {
            res = FP_MEM;
        }

        /* fix sign */
        c->sign = neg;

        if (ptrA)
        {
            XFREE(ptrA, 0, DYNAMIC_TYPE_BIGINT);
        }
        if (ptrB)
        {
            XFREE(ptrB, 0, DYNAMIC_TYPE_BIGINT);
        }
        if (ptrC)
        {
            XFREE(ptrC, 0, DYNAMIC_TYPE_BIGINT);
        }
        return res;
#if defined(FREESCALE_LTC_TFM_RSA_4096_ENABLE)
    }
    else
    {
        return wolfcrypt_fp_mod(a, b, c);
    }
#endif /* FREESCALE_LTC_TFM_RSA_4096_ENABLE */
}

/* c = 1/a (mod b) for odd b only */
int fp_invmod(fp_int *a, fp_int *b, fp_int *c)
{
#if defined(FREESCALE_LTC_TFM_RSA_4096_ENABLE)
    int szA, szB;
    szA = fp_unsigned_bin_size(a);
    szB = fp_unsigned_bin_size(b);
    if ((szA <= LTC_MAX_INT_BYTES) && (szB <= LTC_MAX_INT_BYTES))
    {
#endif
        int res = FP_OKAY;

        uint8_t *ptrA = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);
        uint8_t *ptrB = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);
        uint8_t *ptrC = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);

        if (ptrA && ptrB && ptrC)
        {
            uint16_t sizeA, sizeB, sizeC;

            ltc_get_lsb_bin_from_mp_int(ptrA, a, &sizeA);
            ltc_get_lsb_bin_from_mp_int(ptrB, b, &sizeB);

            if (kStatus_Success ==
                LTC_PKHA_ModInv(LTC_BASE, ptrA, sizeA, ptrB, sizeB, ptrC, &sizeC, kLTC_PKHA_IntegerArith))
            {
                ltc_reverse_array(ptrC, sizeC);
                mp_read_unsigned_bin(c, ptrC, sizeC);
            }
            else
            {
                res = FP_VAL;
            }
        }
        else
        {
            res = FP_MEM;
        }

        c->sign = a->sign;
        if (ptrA)
        {
            XFREE(ptrA, 0, DYNAMIC_TYPE_BIGINT);
        }
        if (ptrB)
        {
            XFREE(ptrB, 0, DYNAMIC_TYPE_BIGINT);
        }
        if (ptrC)
        {
            XFREE(ptrC, 0, DYNAMIC_TYPE_BIGINT);
        }
        return res;
#if defined(FREESCALE_LTC_TFM_RSA_4096_ENABLE)
    }
    else
    {
        return wolfcrypt_fp_invmod(a, b, c);
    }
#endif
}

/* d = a * b (mod c) */
int fp_mulmod(fp_int *a, fp_int *b, fp_int *c, fp_int *d)
{
#if defined(FREESCALE_LTC_TFM_RSA_4096_ENABLE)
    int szA, szB, szC;
    szA = fp_unsigned_bin_size(a);
    szB = fp_unsigned_bin_size(b);
    szC = fp_unsigned_bin_size(c);
    if ((szA <= LTC_MAX_INT_BYTES) && (szB <= LTC_MAX_INT_BYTES) && (szC <= LTC_MAX_INT_BYTES))
    {
#endif /* FREESCALE_LTC_TFM_RSA_4096_ENABLE */
        int res = FP_OKAY;
        fp_int t;

        uint8_t *ptrA = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);
        uint8_t *ptrB = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);
        uint8_t *ptrC = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);
        uint8_t *ptrD = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);

        /* if A or B is negative, substracta abs(A) or abs(B) from modulus to get positive integer representation of the
         * same number */
        fp_init(&t);
        if (a->sign)
        {
            fp_add(a, c, &t);
            fp_copy(&t, a);
        }
        if (b->sign)
        {
            fp_add(b, c, &t);
            fp_copy(&t, b);
        }

        if (ptrA && ptrB && ptrC && ptrD)
        {
            uint16_t sizeA, sizeB, sizeC, sizeD;

            ltc_get_lsb_bin_from_mp_int(ptrA, a, &sizeA);
            ltc_get_lsb_bin_from_mp_int(ptrB, b, &sizeB);
            ltc_get_lsb_bin_from_mp_int(ptrC, c, &sizeC);

            /* (A*B)mod C = ((A mod C) * (B mod C)) mod C  */
            if (LTC_PKHA_CompareBigNum(ptrA, sizeA, ptrC, sizeC) >= 0)
            {
                if (kStatus_Success !=
                    LTC_PKHA_ModRed(LTC_BASE, ptrA, sizeA, ptrC, sizeC, ptrA, &sizeA, kLTC_PKHA_IntegerArith))
                {
                    res = FP_VAL;
                }
            }
            if ((FP_OKAY == res) && (LTC_PKHA_CompareBigNum(ptrB, sizeB, ptrC, sizeC) >= 0))
            {
                if (kStatus_Success !=
                    LTC_PKHA_ModRed(LTC_BASE, ptrB, sizeB, ptrC, sizeC, ptrB, &sizeB, kLTC_PKHA_IntegerArith))
                {
                    res = FP_VAL;
                }
            }

            if (FP_OKAY == res)
            {
                if (kStatus_Success != LTC_PKHA_ModMul(LTC_BASE, ptrA, sizeA, ptrB, sizeB, ptrC, sizeC, ptrD, &sizeD,
                                                       kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue,
                                                       kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized))
                {
                    res = FP_VAL;
                }
            }

            if (FP_OKAY == res)
            {
                ltc_reverse_array(ptrD, sizeD);
                mp_read_unsigned_bin(d, ptrD, sizeD);
            }
        }
        else
        {
            res = FP_MEM;
        }

        if (ptrA)
        {
            XFREE(ptrA, 0, DYNAMIC_TYPE_BIGINT);
        }
        if (ptrB)
        {
            XFREE(ptrB, 0, DYNAMIC_TYPE_BIGINT);
        }
        if (ptrC)
        {
            XFREE(ptrC, 0, DYNAMIC_TYPE_BIGINT);
        }
        if (ptrD)
        {
            XFREE(ptrD, 0, DYNAMIC_TYPE_BIGINT);
        }
        return res;
#if defined(FREESCALE_LTC_TFM_RSA_4096_ENABLE)
    }
    else
    {
        return wolfcrypt_fp_mulmod(a, b, c, d);
    }
#endif /* FREESCALE_LTC_TFM_RSA_4096_ENABLE */
}

/* Y = G^X mod P */
int _fp_exptmod(fp_int *G, fp_int *X, fp_int *P, fp_int *Y)
{
#if defined(FREESCALE_LTC_TFM_RSA_4096_ENABLE)
    int szA, szB, szC;
    szA = fp_unsigned_bin_size(G);
    szB = fp_unsigned_bin_size(X);
    szC = fp_unsigned_bin_size(P);
    if ((szA <= LTC_MAX_INT_BYTES) && (szB <= LTC_MAX_INT_BYTES) && (szC <= LTC_MAX_INT_BYTES))
    {
#endif
        int res = FP_OKAY;
        fp_int t;

        uint16_t sizeG, sizeX, sizeP;
        uint8_t *ptrG = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);
        uint8_t *ptrX = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);
        uint8_t *ptrP = (uint8_t *)XMALLOC(LTC_MAX_INT_BYTES, 0, DYNAMIC_TYPE_BIGINT);

        /* if G is negative, add modulus to convert to positive number for LTC */
        fp_init(&t);
        if (G->sign)
        {
            fp_add(G, P, &t);
            fp_copy(&t, G);
        }

        if (ptrG && ptrX && ptrP)
        {
            ltc_get_lsb_bin_from_mp_int(ptrG, G, &sizeG);
            ltc_get_lsb_bin_from_mp_int(ptrX, X, &sizeX);
            ltc_get_lsb_bin_from_mp_int(ptrP, P, &sizeP);

            /* if number if greater that modulo, we must first reduce due to LTC requirement on modular exponentiaton */
            /* it needs number less than modulus.  */
            /* we can take advantage of modular arithmetic rule that: A^B mod C = ( (A mod C)^B ) mod C
               and so we do first (A mod N) : LTC does not give size requirement on A versus N,
               and then the modular exponentiation.
             */
            /* if G >= P then */
            if (LTC_PKHA_CompareBigNum(ptrG, sizeG, ptrP, sizeP) >= 0)
            {
                res = (int)LTC_PKHA_ModRed(LTC_BASE, ptrG, sizeG, ptrP, sizeP, ptrG, &sizeG, kLTC_PKHA_IntegerArith);

                if (res != kStatus_Success)
                {
                    res = FP_VAL;
                }
            }

            if (FP_OKAY == res)
            {
                res = (int)LTC_PKHA_ModExp(LTC_BASE, ptrG, sizeG, ptrP, sizeP, ptrX, sizeX, ptrP, &sizeP,
                                           kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);

                if (res != kStatus_Success)
                {
                    res = FP_VAL;
                }
                else
                {
                    ltc_reverse_array(ptrP, sizeP);
                    mp_read_unsigned_bin(Y, ptrP, sizeP);
                }
            }
        }
        else
        {
            res = FP_MEM;
        }

        if (ptrG)
        {
            XFREE(ptrG, 0, DYNAMIC_TYPE_BIGINT);
        }
        if (ptrX)
        {
            XFREE(ptrX, 0, DYNAMIC_TYPE_BIGINT);
        }
        if (ptrP)
        {
            XFREE(ptrP, 0, DYNAMIC_TYPE_BIGINT);
        }
        return res;
#if defined(FREESCALE_LTC_TFM_RSA_4096_ENABLE)
    }
    else
    {
        return _wolfcrypt_fp_exptmod(G, X, P, Y);
    }
#endif
}

#ifndef NO_RSA
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssl/wolfcrypt/logging.h>
int wc_RsaFunction(const byte *in, word32 inLen, byte *out, word32 *outLen, int type, RsaKey *key)
{
#define ERROR_OUT(x) \
    {                \
        ret = (x);   \
        goto done;   \
    }

    mp_int tmp;
    int ret = 0;
    word32 keyLen, len;

    if (mp_init(&tmp) != MP_OKAY)
        return MP_INIT_E;

    if (mp_read_unsigned_bin(&tmp, (byte *)in, inLen) != MP_OKAY)
        ERROR_OUT(MP_READ_E);

    if (type == RSA_PRIVATE_DECRYPT || type == RSA_PRIVATE_ENCRYPT)
    {
#define INNER_ERROR_OUT(x) \
    {                      \
        ret = (x);         \
        goto inner_done;   \
    }

        mp_int tmpa, tmpb;

        if (mp_init(&tmpa) != MP_OKAY)
            ERROR_OUT(MP_INIT_E);

        if (mp_init(&tmpb) != MP_OKAY)
        {
            mp_clear(&tmpa);
            ERROR_OUT(MP_INIT_E);
        }

        /* tmpa = tmp^dP mod p */
        if (mp_mod(&tmp, &key->p, &tmpa) != MP_OKAY)
            INNER_ERROR_OUT(MP_EXPTMOD_E);
        if (mp_exptmod(&tmpa, &key->dP, &key->p, &tmpa) != MP_OKAY)
            INNER_ERROR_OUT(MP_EXPTMOD_E);

        /* tmpb = tmp^dQ mod q */
        if (mp_mod(&tmp, &key->q, &tmpb) != MP_OKAY)
            INNER_ERROR_OUT(MP_EXPTMOD_E);
        if (mp_exptmod(&tmpb, &key->dQ, &key->q, &tmpb) != MP_OKAY)
            INNER_ERROR_OUT(MP_EXPTMOD_E);

        /* tmp = (tmpa - tmpb) * qInv (mod p) */
        if (mp_sub(&tmpa, &tmpb, &tmp) != MP_OKAY)
            INNER_ERROR_OUT(MP_SUB_E);

        if (mp_mulmod(&tmp, &key->u, &key->p, &tmp) != MP_OKAY)
            INNER_ERROR_OUT(MP_MULMOD_E);

        /* tmp = tmpb + q * tmp */
        if (mp_mul(&tmp, &key->q, &tmp) != MP_OKAY)
            INNER_ERROR_OUT(MP_MUL_E);

        if (mp_add(&tmp, &tmpb, &tmp) != MP_OKAY)
            INNER_ERROR_OUT(MP_ADD_E);

    inner_done:
        mp_clear(&tmpa);
        mp_clear(&tmpb);

        if (ret != 0)
            return ret;
    }
    else if (type == RSA_PUBLIC_ENCRYPT || type == RSA_PUBLIC_DECRYPT)
    {
#if defined(FREESCALE_LTC_TFM_RSA_4096_ENABLE)
        /* use CRT even for public key operation due to size of integer arguments
         * because up to 256 bytes results can be achieved by LTC_PKHA_2048
         */
        mp_int tmpa, tmpb;
        mp_int dP;
        mp_int dQ;
#define INNER_ERROR_OUT2(x) \
    {                       \
        ret = (x);          \
        goto inner_done;    \
    }

        if (mp_init(&tmpa) != MP_OKAY)
            ERROR_OUT(MP_INIT_E);

        if (mp_init(&tmpb) != MP_OKAY)
        {
            mp_clear(&tmpa);
            ERROR_OUT(MP_INIT_E);
        }

        if (mp_init(&dP) != MP_OKAY)
        {
            mp_clear(&tmpa);
            mp_clear(&tmpb);
            ERROR_OUT(MP_INIT_E);
        }

        if (mp_init(&dQ) != MP_OKAY)
        {
            mp_clear(&tmpa);
            mp_clear(&tmpb);
            mp_clear(&dP);
            ERROR_OUT(MP_INIT_E);
        }

        mp_sub_d(&key->p, 1, &dP); /* dP = p-1 */
        mp_mod(&key->e, &dP, &dP); /* dP = (e mod (p-1) */

        mp_sub_d(&key->q, 1, &dQ); /* dQ = q-1 */
        mp_mod(&key->e, &dQ, &dQ); /* dQ = (e mod (q-1) */

        /* tmpa = tmp^dP mod p */
        if (mp_mod(&tmp, &key->p, &tmpa) != MP_OKAY)
            INNER_ERROR_OUT2(MP_EXPTMOD_E);
        if (mp_exptmod(&tmpa, &dP, &key->p, &tmpa) != MP_OKAY)
            INNER_ERROR_OUT2(MP_EXPTMOD_E);

        /* tmpb = tmp^dQ mod q */
        if (mp_mod(&tmp, &key->q, &tmpb) != MP_OKAY)
            INNER_ERROR_OUT2(MP_EXPTMOD_E);
        if (mp_exptmod(&tmpb, &dQ, &key->q, &tmpb) != MP_OKAY)
            INNER_ERROR_OUT2(MP_EXPTMOD_E);

        /* tmp = (tmpa - tmpb) * qInv (mod p) */
        if (mp_sub(&tmpa, &tmpb, &tmp) != MP_OKAY)
            INNER_ERROR_OUT2(MP_SUB_E);

        if (mp_mulmod(&tmp, &key->u, &key->p, &tmp) != MP_OKAY)
            INNER_ERROR_OUT2(MP_MULMOD_E);

        /* tmp = tmpb + q * tmp */
        if (mp_mul(&tmp, &key->q, &tmp) != MP_OKAY)
            INNER_ERROR_OUT2(MP_MUL_E);

        if (mp_add(&tmp, &tmpb, &tmp) != MP_OKAY)
            INNER_ERROR_OUT2(MP_ADD_E);

    inner_done2:
        mp_clear(&tmpa);
        mp_clear(&tmpb);
        mp_clear(&dP);
        mp_clear(&dQ);

        if (ret != 0)
            return ret;
#else
        if (mp_exptmod(&tmp, &key->e, &key->n, &tmp) != MP_OKAY)
            ERROR_OUT(MP_EXPTMOD_E);
#endif
    }
    else
        ERROR_OUT(RSA_WRONG_TYPE_E);

    keyLen = mp_unsigned_bin_size(&key->n);
    if (keyLen > *outLen)
        ERROR_OUT(RSA_BUFFER_E);

    len = mp_unsigned_bin_size(&tmp);

    /* pad front w/ zeros to match key length */
    while (len < keyLen)
    {
        *out++ = 0x00;
        len++;
    }

    *outLen = keyLen;

    /* convert */
    if (mp_to_unsigned_bin(&tmp, out) != MP_OKAY)
        ERROR_OUT(MP_TO_E);

done:
    mp_clear(&tmp);
    if (ret == MP_EXPTMOD_E)
    {
        WOLFSSL_MSG("RSA_FUNCTION MP_EXPTMOD_E: memory/config problem");
    }
    return ret;
}
#endif /* NO_RSA */

#endif /* FREESCALE_LTC_TFM */
#endif /* USE_FAST_MATH */

#ifdef FREESCALE_LTC_ECC
#include <stdint.h>
#include "fsl_ltc.h"
#include <wolfssl/wolfcrypt/error-crypt.h>

/* convert from mp_int to LTC integer, as array of bytes of size sz.
 * if mp_int has less bytes than sz, add zero bytes at most significant byte positions.
 * This is when for example modulus is 32 bytes (P-256 curve)
 * and mp_int has only 31 bytes, we add leading zeroes
 * so that result array has 32 bytes, same as modulus (sz).
 */
static void ltc_get_from_mp_int(uint8_t *dst, mp_int *a, int sz)
{
    int szbin;
    int offset;

    /* check how many bytes are in the mp_int */
    szbin = mp_unsigned_bin_size(a);

    /* compute offset from dst */
    offset = sz - szbin;
    if (offset < 0)
        offset = 0;
    if (offset > sz)
        offset = sz;

    /* add leading zeroes */
    if (offset)
        memset(dst, 0, offset);

    /* convert mp_int to array of bytes */
    mp_to_unsigned_bin(a, dst + offset);

    /* reverse array for LTC direct use */
    ltc_reverse_array(dst, sz);
}

/* ECC specs in lsbyte at lowest address format for direct use by LTC PKHA driver functions */
#if defined(HAVE_ECC192) || defined(HAVE_ALL_CURVES)
#define ECC192
#endif
#if defined(HAVE_ECC224) || defined(HAVE_ALL_CURVES)
#define ECC224
#endif
#if !defined(NO_ECC256) || defined(HAVE_ALL_CURVES)
#define ECC256
#endif
#if defined(HAVE_ECC384) || defined(HAVE_ALL_CURVES)
#define ECC384
#endif

/* P-256 */
#ifdef ECC256
const uint8_t ltc_ecc256_modulus[32] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                        0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t ltc_ecc256_r2modn[32] = {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFB, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFF, 0xFD, 0xFF, 0xFF, 0xFF, 0x04, 0x00, 0x00, 0x00};
const uint8_t ltc_ecc256_aCurveParam[32] = {0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                            0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                            0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t ltc_ecc256_bCurveParam[32] = {0x4B, 0x60, 0xD2, 0x27, 0x3E, 0x3C, 0xCE, 0x3B, 0xF6, 0xB0, 0x53,
                                            0xCC, 0xB0, 0x06, 0x1D, 0x65, 0xBC, 0x86, 0x98, 0x76, 0x55, 0xBD,
                                            0xEB, 0xB3, 0xE7, 0x93, 0x3A, 0xAA, 0xD8, 0x35, 0xC6, 0x5A};
#endif

#ifdef ECC192
const uint8_t ltc_ecc192_modulus[24] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF,
                                        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t ltc_ecc192_r2modn[24] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
                                       0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t ltc_ecc192_aCurveParam[24] = {0xFC, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF,
                                            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t ltc_ecc192_bCurveParam[24] = {0xB1, 0xB9, 0x46, 0xC1, 0xEC, 0xDE, 0xB8, 0xFE, 0x49, 0x30, 0x24, 0x72,
                                            0xAB, 0xE9, 0xA7, 0x0F, 0xE7, 0x80, 0x9C, 0xE5, 0x19, 0x05, 0x21, 0x64};
#endif

#ifdef ECC224
const uint8_t ltc_ecc224_modulus[28] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t ltc_ecc224_r2modn[28] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                       0x00, 0x00, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                       0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00};
const uint8_t ltc_ecc224_aCurveParam[28] = {0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                            0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t ltc_ecc224_bCurveParam[28] = {0xB4, 0xFF, 0x55, 0x23, 0x43, 0x39, 0x0B, 0x27, 0xBA, 0xD8,
                                            0xBF, 0xD7, 0xB7, 0xB0, 0x44, 0x50, 0x56, 0x32, 0x41, 0xF5,
                                            0xAB, 0xB3, 0x04, 0x0C, 0x85, 0x0A, 0x05, 0xB4};
#endif

#ifdef ECC384
const uint8_t ltc_ecc384_modulus[48] = {0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
const uint8_t ltc_ecc384_r2modn[48] = {0x01, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                                       0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff,
                                       0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t ltc_ecc384_aCurveParam[48] = {0xfc, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                            0xff, 0xff, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
const uint8_t ltc_ecc384_bCurveParam[48] = {0xef, 0x2a, 0xec, 0xd3, 0xed, 0xc8, 0x85, 0x2a, 0x9d, 0xd1, 0x2e, 0x8a,
                                            0x8d, 0x39, 0x56, 0xc6, 0x5a, 0x87, 0x13, 0x50, 0x8f, 0x08, 0x14, 0x03,
                                            0x12, 0x41, 0x81, 0xfe, 0x6e, 0x9c, 0x1d, 0x18, 0x19, 0x2d, 0xf8, 0xe3,
                                            0x6b, 0x05, 0x8e, 0x98, 0xe4, 0xe7, 0x3e, 0xe2, 0xa7, 0x2f, 0x31, 0xb3};
#endif

static int ltc_get_ecc_specs(
    const uint8_t **modulus, const uint8_t **r2modn, const uint8_t **aCurveParam, const uint8_t **bCurveParam, int size)
{
    if (32 == size)
    {
        *modulus = ltc_ecc256_modulus;
        *r2modn = ltc_ecc256_r2modn;
        *aCurveParam = ltc_ecc256_aCurveParam;
        *bCurveParam = ltc_ecc256_bCurveParam;
    }
#ifdef ECC224
    else if (28 == size)
    {
        *modulus = ltc_ecc224_modulus;
        *r2modn = ltc_ecc224_r2modn;
        *aCurveParam = ltc_ecc224_aCurveParam;
        *bCurveParam = ltc_ecc224_bCurveParam;
    }
#endif
#ifdef ECC192
    else if (24 == size)
    {
        *modulus = ltc_ecc192_modulus;
        *r2modn = ltc_ecc192_r2modn;
        *aCurveParam = ltc_ecc192_aCurveParam;
        *bCurveParam = ltc_ecc192_bCurveParam;
    }
#endif
#ifdef HAVE_ECC384
    else if (48 == size)
    {
        *modulus = ltc_ecc384_modulus;
        *r2modn = ltc_ecc384_r2modn;
        *aCurveParam = ltc_ecc384_aCurveParam;
        *bCurveParam = ltc_ecc384_bCurveParam;
    }
#endif
    else
    {
        return -1;
    }
    return 0;
}

/**
   Perform a point multiplication  (timing resistant)
   k    The scalar to multiply by
   G    The base point
   R    [out] Destination for kG
   modulus  The modulus of the field the ECC curve is in
   map      Boolean whether to map back to affine or not
            (1==map, 0 == leave in projective)
   return MP_OKAY on success
*/
int wc_ecc_mulmod(mp_int *k, ecc_point *G, ecc_point *R, mp_int *modulus, int map)
{
    ltc_pkha_ecc_point_t B;
    uint8_t size;
    int szModulus;
    int szkbin;
    bool point_of_infinity;
    status_t status;

    uint8_t Gxbin[LTC_MAX_ECC_BITS / 8];
    uint8_t Gybin[LTC_MAX_ECC_BITS / 8];
    uint8_t kbin[LTC_MAX_INT_BYTES];

    const uint8_t *modbin;
    const uint8_t *aCurveParam;
    const uint8_t *bCurveParam;
    const uint8_t *r2modn;

    if (k == NULL || G == NULL || R == NULL || modulus == NULL)
        return ECC_BAD_ARG_E;

    szModulus = mp_unsigned_bin_size(modulus);
    szkbin = mp_unsigned_bin_size(k);

    ltc_get_from_mp_int(kbin, k, szkbin);
    ltc_get_from_mp_int(Gxbin, G->x, szModulus);
    ltc_get_from_mp_int(Gybin, G->y, szModulus);

    size = szModulus;
    /* find LTC friendly parameters for the selected curve */
    if (0 != ltc_get_ecc_specs(&modbin, &r2modn, &aCurveParam, &bCurveParam, size))
    {
        return ECC_BAD_ARG_E;
    }

    B.X = &Gxbin[0];
    B.Y = &Gybin[0];

    status = LTC_PKHA_ECC_PointMul(LTC_BASE, &B, kbin, szkbin, modbin, r2modn, aCurveParam, bCurveParam, size,
                                   kLTC_PKHA_TimingEqualized, kLTC_PKHA_IntegerArith, &B, &point_of_infinity);
    if (status != kStatus_Success)
        return FP_VAL;

    ltc_reverse_array(Gxbin, size);
    ltc_reverse_array(Gybin, size);
    mp_read_unsigned_bin(R->x, Gxbin, size);
    mp_read_unsigned_bin(R->y, Gybin, size);
    /* if k is negative, we compute the multiplication with abs(-k)
     * with result (x, y) and modify the result to (x, -y)
     */
    R->y->sign = k->sign;
    mp_set(R->z, 1);
    return MP_OKAY;
}

int wc_ecc_point_add(ecc_point *mG, ecc_point *mQ, ecc_point *mR, mp_int *m)
{
    int err;
    ltc_pkha_ecc_point_t A, B;
    int size;
    status_t status;

    uint8_t Gxbin[LTC_MAX_ECC_BITS / 8];
    uint8_t Gybin[LTC_MAX_ECC_BITS / 8];
    uint8_t Qxbin[LTC_MAX_ECC_BITS / 8];
    uint8_t Qybin[LTC_MAX_ECC_BITS / 8];
    const uint8_t *modbin;
    const uint8_t *aCurveParam;
    const uint8_t *bCurveParam;
    const uint8_t *r2modn;

    size = mp_unsigned_bin_size(m);
    /* find LTC friendly parameters for the selected curve */
    if (0 != ltc_get_ecc_specs(&modbin, &r2modn, &aCurveParam, &bCurveParam, size))
    {
        err = ECC_BAD_ARG_E;
    }
    else
    {
        ltc_get_from_mp_int(Gxbin, mG->x, size);
        ltc_get_from_mp_int(Gybin, mG->y, size);
        ltc_get_from_mp_int(Qxbin, mQ->x, size);
        ltc_get_from_mp_int(Qybin, mQ->y, size);

        A.X = Gxbin;
        A.Y = Gybin;

        B.X = Qxbin;
        B.Y = Qybin;

        status = LTC_PKHA_ECC_PointAdd(LTC_BASE, &A, &B, modbin, r2modn, aCurveParam, bCurveParam, size,
                                       kLTC_PKHA_IntegerArith, &A);
        if (status != kStatus_Success)
        {
            err = FP_VAL;
        }
        else
        {
            ltc_reverse_array(Gxbin, size);
            ltc_reverse_array(Gybin, size);
            mp_read_unsigned_bin(mR->x, Gxbin, size);
            mp_read_unsigned_bin(mR->y, Gybin, size);
            mp_set(mR->z, 1);
            err = MP_OKAY;
        }
    }
    return err;
}

#if defined(HAVE_ED25519) || defined(HAVE_CURVE25519)
/* Weierstrass parameters of prime 2^255 - 19 */
static const uint8_t modbin[32] = {0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                   0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f};

static const uint8_t r2mod[32] = {
    0xa4, 0x05,
}; /* precomputed R2modN for the curve25519 */

/* invThree = ModInv(3,modbin) in LSB first */
static const uint8_t invThree[32] = {0x49, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                                     0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                                     0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};

/*
 *
 * finds square root in finite field when modulus congruent to 5 modulo 8
 * this is fixed to curve25519 modulus 2^255 - 19 which is congruent to 5 modulo 8
 *
 * This function solves equation: res^2 = a mod (2^255 - 19)
 *
p = prime
p % 8 must be 5

v = ModularArithmetic.powmod(2*a, (p-5)/8, p)
i = (2*a*v**2) % p
r1 = 1*a*v*(i - 1) % p
r2 = -1*a*v*(i - 1) % p
puts "Gy=0x#{r2.to_s(16)}"
 */
status_t LTC_PKHA_Prime25519SquareRootMod(const uint8_t *A, size_t sizeA, uint8_t *res, size_t *szRes, int sign)
{
    status_t status;
    const uint8_t curve25519_param[] = {0xfd, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0f};

    uint8_t twoA[sizeof(modbin)] = {0};
    uint8_t V[sizeof(modbin)] = {0};
    uint8_t I[sizeof(modbin)] = {0};
    uint8_t VV[sizeof(modbin)] = {0};
    uint16_t szTwoA = 0;
    uint16_t szV = 0;
    uint16_t szVV = 0;
    uint16_t szI = 0;
    uint16_t szRes16 = 0;
    uint8_t one = 1;

    /* twoA = 2*A % p */
    status =
        LTC_PKHA_ModAdd(LTC_BASE, A, sizeA, A, sizeA, modbin, sizeof(modbin), twoA, &szTwoA, kLTC_PKHA_IntegerArith);

    /* V = ModularArithmetic.powmod(twoA, (p-5)/8, p) */
    if (status == kStatus_Success)
    {
        status =
            LTC_PKHA_ModExp(LTC_BASE, twoA, szTwoA, modbin, sizeof(modbin), curve25519_param, sizeof(curve25519_param),
                            V, &szV, kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);
    }

    /* VV = V*V % p */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, V, szV, V, szV, modbin, sizeof(modbin), VV, &szVV, kLTC_PKHA_IntegerArith,
                                 kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);
    }

    /* I = twoA * VV = 2*A*V*V % p */
    if (status == kStatus_Success)
    {
        status =
            LTC_PKHA_ModMul(LTC_BASE, twoA, szTwoA, VV, szVV, modbin, sizeof(modbin), I, &szI, kLTC_PKHA_IntegerArith,
                            kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);
    }

    /* I = I - 1 */
    memset(VV, 0xff, sizeof(VV)); /* just temp for maximum integer - for non-modular substract */
    if (0 <= LTC_PKHA_CompareBigNum(I, szI, &one, sizeof(one)))
    {
        if (status == kStatus_Success)
        {
            status = LTC_PKHA_ModSub1(LTC_BASE, I, szI, &one, sizeof(one), VV, sizeof(VV), I, &szI);
        }
    }
    else
    {
        if (status == kStatus_Success)
        {
            status = LTC_PKHA_ModSub1(LTC_BASE, modbin, sizeof(modbin), &one, sizeof(one), VV, sizeof(VV), I, &szI);
        }
    }

    /* res = a*v  mod p */
    status = LTC_PKHA_ModMul(LTC_BASE, A, sizeA, V, szV, modbin, sizeof(modbin), res, &szRes16, kLTC_PKHA_IntegerArith,
                             kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);

    /* res = res * (i-1) mod p */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, res, szRes16, I, szI, modbin, sizeof(modbin), res, &szRes16,
                                 kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue,
                                 kLTC_PKHA_TimingEqualized);
    }

    /* if X mod 2 != X_0 then we need the -X
     *
     * X mod 2 get from LSB bit0
     */
    if ((status == kStatus_Success) && ((bool)sign != (bool)(res[0] & 0x01u)))
    {
        status = LTC_PKHA_ModSub1(LTC_BASE, modbin, sizeof(modbin), res, szRes16, VV, sizeof(VV), res,
                                  &szRes16); /* -a = p - a */
    }

    if (status == kStatus_Success)
    {
        *szRes = szRes16;
    }

    return status;
}
#endif

#ifdef HAVE_CURVE25519

/* for LTC we need Weierstrass format of curve25519 parameters
         * these two are base point X and Y.
         * in LSB first format (native for LTC)
         */
static ECPoint ecBasePoint = {
    {0x5a, 0x24, 0xad, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
     0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0x2a},
    {0xd9, 0xd3, 0xce, 0x7e, 0xa2, 0xc5, 0xe9, 0x29, 0xb2, 0x61, 0x7c, 0x6d, 0x7e, 0x4d, 0x3d, 0x92, 0x4c, 0xd1, 0x48,
     0x77, 0x2c, 0xdd, 0x1e, 0xe0, 0xb4, 0x86, 0xa0, 0xb8, 0xa1, 0x19, 0xae, 0x20},
};

ECPoint *wc_curve25519_GetBasePoint(void)
{
    return &ecBasePoint;
}

static const uint8_t aCurveParam[CURVE25519_KEYSIZE] = {
    0x44, 0xa1, 0x14, 0x49, 0x98, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0x2a};

static const uint8_t bCurveParam[CURVE25519_KEYSIZE] = {
    0x64, 0xc8, 0x10, 0x77, 0x9c, 0x5e, 0x0b, 0x26, 0xb4, 0x97, 0xd0, 0x5e, 0x42, 0x7b, 0x09, 0xed,
    0x25, 0xb4, 0x97, 0xd0, 0x5e, 0x42, 0x7b, 0x09, 0xed, 0x25, 0xb4, 0x97, 0xd0, 0x5e, 0x42, 0x7b};

/* transform a point on Montgomery curve to a point on Weierstrass curve */
status_t LTC_PKHA_Curve25519ToWeierstrass(const ltc_pkha_ecc_point_t *ltcPointIn, ltc_pkha_ecc_point_t *ltcPointOut)
{
    /* offset X point (in Montgomery) so that it becomes Weierstrass */
    const uint8_t offset[] = {0x51, 0x24, 0xad, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
                              0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
                              0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0x2a};
    uint16_t sizeRes = 0;
    status_t status;
    status = LTC_PKHA_ModAdd(LTC_BASE, ltcPointIn->X, CURVE25519_KEYSIZE, offset, sizeof(offset), modbin,
                             CURVE25519_KEYSIZE, ltcPointOut->X, &sizeRes, kLTC_PKHA_IntegerArith);

    if (status == kStatus_Success)
    {
        if (ltcPointOut->Y != ltcPointIn->Y)
        {
            XMEMCPY(ltcPointOut->Y, ltcPointIn->Y, CURVE25519_KEYSIZE);
        }
    }

    return status;
}

/* transform a point on Weierstrass curve to a point on Montgomery curve */
status_t LTC_PKHA_WeierstrassToCurve25519(const ltc_pkha_ecc_point_t *ltcPointIn, ltc_pkha_ecc_point_t *ltcPointOut)
{
    status_t status;
    uint16_t resultSize = 0;
    uint8_t three = 0x03;

    status = LTC_PKHA_ModMul(LTC_BASE, &three, sizeof(three), ltcPointIn->X, CURVE25519_KEYSIZE, modbin,
                             CURVE25519_KEYSIZE, ltcPointOut->X, &resultSize, kLTC_PKHA_IntegerArith,
                             kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);

    if (status == kStatus_Success)
    {
        static const uint8_t A[] = {0x06, 0x6d, 0x07};
        if (LTC_PKHA_CompareBigNum(ltcPointOut->X, resultSize, A, sizeof(A)))
        {
            status = LTC_PKHA_ModSub1(LTC_BASE, ltcPointOut->X, resultSize, A, sizeof(A), modbin, CURVE25519_KEYSIZE,
                                      ltcPointOut->X, &resultSize);
        }
        else
        {
            status = LTC_PKHA_ModSub2(LTC_BASE, ltcPointOut->X, resultSize, A, sizeof(A), modbin, CURVE25519_KEYSIZE,
                                      ltcPointOut->X, &resultSize);
        }
    }

    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, invThree, CURVE25519_KEYSIZE, ltcPointOut->X, resultSize, modbin,
                                 CURVE25519_KEYSIZE, ltcPointOut->X, &resultSize, kLTC_PKHA_IntegerArith,
                                 kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);
    }

    if (status == kStatus_Success)
    {
        if (ltcPointOut->Y != ltcPointIn->Y)
        {
            XMEMCPY(ltcPointOut->Y, ltcPointIn->Y, CURVE25519_KEYSIZE);
        }
    }

    return status;
}

/* Y = square root (X^3 + 486662*X^2 + X) */
status_t LTC_PKHA_Curve25519ComputeY(ltc_pkha_ecc_point_t *ltcPoint)
{
    uint8_t three = 3;
    uint8_t A[] = {0x06, 0x6d, 0x07};
    uint8_t U[CURVE25519_KEYSIZE] = {0};
    uint8_t X2[CURVE25519_KEYSIZE] = {0};
    uint16_t sizeU = 0;
    uint16_t sizeX2 = 0;
    size_t szRes = 0;
    status_t status;

    /* X^3 */
    status = LTC_PKHA_ModExp(LTC_BASE, ltcPoint->X, CURVE25519_KEYSIZE, modbin, CURVE25519_KEYSIZE, &three, 1, U,
                             &sizeU, kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);

    /* X^2 */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, ltcPoint->X, CURVE25519_KEYSIZE, ltcPoint->X, CURVE25519_KEYSIZE, modbin,
                                 CURVE25519_KEYSIZE, X2, &sizeX2, kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue,
                                 kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);
    }

    /* 486662*X^2 */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, A, sizeof(A), X2, sizeX2, modbin, CURVE25519_KEYSIZE, X2, &sizeX2,
                                 kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue,
                                 kLTC_PKHA_TimingEqualized);
    }

    /* X^3 + 486662*X^2 */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModAdd(LTC_BASE, U, sizeU, X2, sizeX2, modbin, CURVE25519_KEYSIZE, U, &sizeU,
                                 kLTC_PKHA_IntegerArith);
    }

    /* U = X^3 + 486662*X^2 + X */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModAdd(LTC_BASE, U, sizeU, ltcPoint->X, CURVE25519_KEYSIZE, modbin, CURVE25519_KEYSIZE, U,
                                 &sizeU, kLTC_PKHA_IntegerArith);
    }

    /* Y = modular square root of U (U is Y^2) */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_Prime25519SquareRootMod(U, sizeU, ltcPoint->Y, &szRes, 1);
    }

    return status;
}

/* Q = n*P */
/* if type is set, the input point p is in Montgomery curve coordinates, so there is a map to Weierstrass curve */
/* q output point is always in Montgomery curve coordinates */
int wc_curve25519(ECPoint *q, byte *n, ECPoint *p, fsl_ltc_ecc_coordinate_system_t type)
{
    status_t status;
    ltc_pkha_ecc_point_t ltcPoint;
    ltc_pkha_ecc_point_t ltcPointOut;
    ECPoint pIn = {{0}};

    XMEMCPY(&pIn, p, sizeof(*p));
    ltcPoint.X = &pIn.point[0];
    ltcPoint.Y = &pIn.pointY[0];

    /* if input point P is on Curve25519 Montgomery curve, transform it to Weierstrass equivalent */
    if (type == kLTC_Curve25519)
    {
        LTC_PKHA_Curve25519ToWeierstrass(&ltcPoint, &ltcPoint);
    }

    ltcPointOut.X = &q->point[0];
    ltcPointOut.Y = &q->pointY[0];
    /* modbin, r2mod, aCurveParam, bCurveParam are Weierstrass equivalent with Curve25519 */
    status = LTC_PKHA_ECC_PointMul(LTC_BASE, &ltcPoint, n, CURVE25519_KEYSIZE, modbin, r2mod, aCurveParam, bCurveParam,
                                   CURVE25519_KEYSIZE, kLTC_PKHA_TimingEqualized, kLTC_PKHA_IntegerArith, &ltcPointOut,
                                   NULL);

    /* now need to map from Weierstrass form to Montgomery form */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_WeierstrassToCurve25519(&ltcPointOut, &ltcPointOut);
    }

    if (status == kStatus_Success)
        return 0;
    else
        return IS_POINT_E;
}

#endif /* HAVE_CURVE25519 */

#ifdef HAVE_ED25519
#include <wolfssl/wolfcrypt/ed25519.h>

/* a and d are Edwards curve parameters -1 and -121665/121666 prime is 2^255 - 19.
 *
 *
 *
 * https://en.wikipedia.org/wiki/Montgomery_curve#Equivalence_with_Edward_curves
 */

/* d parameter of ed25519 */
static const uint8_t d_coefEd25519[] = {0xa3, 0x78, 0x59, 0x13, 0xca, 0x4d, 0xeb, 0x75, 0xab, 0xd8, 0x41,
                                        0x41, 0x4d, 0x0a, 0x70, 0x00, 0x98, 0xe8, 0x79, 0x77, 0x79, 0x40,
                                        0xc7, 0x8c, 0x73, 0xfe, 0x6f, 0x2b, 0xee, 0x6c, 0x03, 0x52};

/* Montgomery curve parameter A for a Montgomery curve equivalent with ed25519 */
static const uint8_t A_coefEd25519[] = {0x06, 0x6d, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/* Montgomery curve parameter B for a Montgomery curve equivalent with ed25519 */
static const uint8_t B_coefEd25519[] = {0xe5, 0x92, 0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f};

/* these are pre-computed constants used in computations */

/* = 3*B */
static const uint8_t threeB_coefEd25519[] = {0xd5, 0xb8, 0xe9, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                             0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                             0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f};

/* = -A */
static const uint8_t minus_A_coefEd25519[] = {0xe7, 0x92, 0xf8, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                              0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f};

/* = 1/B */
static const uint8_t invB_coefEd25519[] = {0xc4, 0xa1, 0x29, 0x7b, 0x8d, 0x2c, 0x85, 0x22, 0xd5, 0x89, 0xaf,
                                           0xaf, 0x6c, 0xfd, 0xe3, 0xff, 0xd9, 0x85, 0x21, 0xa2, 0xe1, 0x2f,
                                           0xce, 0x1c, 0x63, 0x00, 0x24, 0x75, 0xc4, 0x24, 0x7f, 0x6b};

/* = 1/(3*B) */
static const uint8_t A_mul_invThreeB_coefEd25519[] = {0xb9, 0x3e, 0xe4, 0xad, 0xa1, 0x37, 0xa7, 0x93, 0x1c, 0xa4, 0x35,
                                                      0xe0, 0x0c, 0x57, 0xbd, 0xaa, 0x6e, 0x51, 0x94, 0x3e, 0x14, 0xe0,
                                                      0xcb, 0xec, 0xbd, 0xff, 0xe7, 0xb1, 0x27, 0x92, 0x00, 0x63};

/* Weierstrass curve parameter a for a Weierstrass curve equivalent with ed25519 */
static const uint8_t a_coefEd25519[] = {0x2d, 0x17, 0xbc, 0xf8, 0x8e, 0xe1, 0x71, 0xac, 0xf7, 0x2a, 0xa5,
                                        0x0c, 0x5d, 0xb6, 0xb8, 0x6b, 0xd6, 0x3d, 0x7b, 0x61, 0x0d, 0xe1,
                                        0x97, 0x31, 0xe6, 0xbe, 0xb9, 0xa5, 0xd3, 0xac, 0x4e, 0x5d};

/* Weierstrass curve parameter b for a Weierstrass curve equivalent with ed25519 */
static const uint8_t b_coefEd25519[] = {0xa4, 0xb2, 0x64, 0xf3, 0xc1, 0xeb, 0x04, 0x90, 0x32, 0xbc, 0x9f,
                                        0x6b, 0x97, 0x31, 0x48, 0xf5, 0xd5, 0x80, 0x57, 0x10, 0x06, 0xdb,
                                        0x0d, 0x55, 0xe0, 0xb3, 0xd0, 0xcf, 0x9b, 0xb2, 0x11, 0x1d};

/* Ed25519 basepoint B mapped to Weierstrass equivalent */
static uint8_t Wx_Ed25519[ED25519_KEY_SIZE] = {0x35, 0xef, 0x5a, 0x02, 0x9b, 0xc8, 0x55, 0xca, 0x9a, 0x7c, 0x61,
                                               0x0d, 0xdf, 0x3f, 0xc1, 0xa9, 0x18, 0x06, 0xc2, 0xf1, 0x02, 0x8f,
                                               0x0b, 0xf0, 0x39, 0x03, 0x2c, 0xd0, 0x0f, 0xdd, 0x78, 0x2a};
static uint8_t Wy_Ed25519[ED25519_KEY_SIZE] = {0x14, 0x1d, 0x2c, 0xf6, 0xf3, 0x30, 0x78, 0x9b, 0x65, 0x31, 0x71,
                                               0x80, 0x61, 0xd0, 0x6f, 0xcf, 0x23, 0x83, 0x79, 0x63, 0xa5, 0x3b,
                                               0x48, 0xbe, 0x2e, 0xa2, 0x1d, 0xc7, 0xa5, 0x44, 0xc6, 0x29};

static const ltc_pkha_ecc_point_t basepointEd25519 = {
    Wx_Ed25519, Wy_Ed25519,
};

const ltc_pkha_ecc_point_t *LTC_PKHA_Ed25519_BasePoint(void)
{
    return &basepointEd25519;
}

/* input point is on Weierstrass curve, typeOut determines the coordinates system of output point (either Weierstrass or
 * Ed25519) */
status_t LTC_PKHA_Ed25519_PointMul(const ltc_pkha_ecc_point_t *ltcPointIn,
                                   const uint8_t *N,
                                   size_t sizeN,
                                   ltc_pkha_ecc_point_t *ltcPointOut,
                                   fsl_ltc_ecc_coordinate_system_t typeOut)
{
    uint16_t szN = (uint16_t)sizeN;
    status_t status;
    /* input on W, output in W, W parameters of ECC curve are Ed25519 curve parameters mapped to Weierstrass curve */
    status =
        LTC_PKHA_ECC_PointMul(LTC_BASE, ltcPointIn, N, szN, modbin, r2mod, a_coefEd25519, b_coefEd25519,
                              ED25519_KEY_SIZE, kLTC_PKHA_TimingEqualized, kLTC_PKHA_IntegerArith, ltcPointOut, NULL);

    /* Weierstrass coordinates to Ed25519 coordinates */
    if ((status == kStatus_Success) && (typeOut == kLTC_Ed25519))
    {
        status = LTC_PKHA_WeierstrassToEd25519(ltcPointOut, ltcPointOut);
    }
    return status;
}

status_t LTC_PKHA_Ed25519ToWeierstrass(const ltc_pkha_ecc_point_t *ltcPointIn, ltc_pkha_ecc_point_t *ltcPointOut)
{
    status_t status;
    uint8_t Mx[ED25519_KEY_SIZE] = {0};
    uint8_t My[ED25519_KEY_SIZE] = {0};
    uint8_t temp[ED25519_KEY_SIZE] = {0};
    uint8_t temp2[ED25519_KEY_SIZE] = {0};
    uint8_t max[32] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const uint8_t *Ex;
    const uint8_t *Ey;
    uint8_t *Gx;
    uint8_t *Gy;
    uint16_t szMx = 0;
    uint16_t szGx = 0;
    uint16_t szMy = 0;
    uint16_t szGy = 0;
    uint16_t szTemp = 0;
    uint16_t szTemp2 = 0;
    uint8_t one = 1;

    Ex = ltcPointIn->X;
    Ey = ltcPointIn->Y;
    Gx = ltcPointOut->X;
    Gy = ltcPointOut->Y;
    /* # (Ex, Ey) on Ed (a_ed, d) to (x, y) on M (A,B)
    Mx = (1 + Ey) * ModularArithmetic.invert(1 - Ey, prime) % prime
    My = (1 + Ey) * ModularArithmetic.invert((1 - Ey)*Ex, prime) % prime */

    /* Gx = ((Mx * ModularArithmetic.invert(B, prime)) + (A * ModularArithmetic.invert(3*B, prime))) % prime
    Gy = (My * ModularArithmetic.invert(B, prime)) % prime */

    /* temp = 1 + Ey */
    status = LTC_PKHA_ModAdd(LTC_BASE, Ey, ED25519_KEY_SIZE, &one, sizeof(one), modbin, sizeof(modbin), temp, &szTemp,
                             kLTC_PKHA_IntegerArith);

    /* temp2 = 1 - Ey = 1 + (p - Ey) */
    if (status == kStatus_Success)
    {
        status =
            LTC_PKHA_ModSub1(LTC_BASE, modbin, sizeof(modbin), Ey, ED25519_KEY_SIZE, max, sizeof(max), temp2, &szTemp2);
    }
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModAdd(LTC_BASE, temp2, szTemp2, &one, sizeof(one), modbin, sizeof(modbin), temp2, &szTemp2,
                                 kLTC_PKHA_IntegerArith);
    }

    /* Mx = ModInv(temp2,prime) */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModInv(LTC_BASE, temp2, szTemp2, modbin, sizeof(modbin), Mx, &szMx, kLTC_PKHA_IntegerArith);
    }

    /* Mx = Mx * temp */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, Mx, szMx, temp, szTemp, modbin, ED25519_KEY_SIZE, Mx, &szMx,
                                 kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue,
                                 kLTC_PKHA_TimingEqualized);
    }

    /* My = temp2 * Ex */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, Ex, ED25519_KEY_SIZE, temp2, szTemp2, modbin, ED25519_KEY_SIZE, My, &szMy,
                                 kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue,
                                 kLTC_PKHA_TimingEqualized);
    }

    /* My = ModInv(My, prime) */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModInv(LTC_BASE, My, szMy, modbin, sizeof(modbin), My, &szMy, kLTC_PKHA_IntegerArith);
    }
    /* My = My * temp */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, My, szMy, temp, szTemp, modbin, ED25519_KEY_SIZE, My, &szMy,
                                 kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue,
                                 kLTC_PKHA_TimingEqualized);
    }

    /* Gx = Mx * invB_coefEd25519 + A_mul_invThreeB_coefEd25519 */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, Mx, szMx, invB_coefEd25519, sizeof(invB_coefEd25519), modbin,
                                 ED25519_KEY_SIZE, Gx, &szGx, kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue,
                                 kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);
    }
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModAdd(LTC_BASE, Gx, szGx, A_mul_invThreeB_coefEd25519, sizeof(A_mul_invThreeB_coefEd25519),
                                 modbin, sizeof(modbin), Gx, &szGx, kLTC_PKHA_IntegerArith);
    }

    /* Gy = My * invB_coefEd25519 */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, My, szMy, invB_coefEd25519, sizeof(invB_coefEd25519), modbin,
                                 ED25519_KEY_SIZE, Gy, &szGy, kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue,
                                 kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);
    }

    return status;
}

/*
# (Gx, Gy) on W to (Ex, Ey) on E
My = (B*Gy) % prime
Mx = ((3*B*Gx-A)*ModularArithmetic.invert(3, prime)) % prime
Ex = Mx*ModularArithmetic.invert(My, prime) % prime
Ey = (Mx - 1)*ModularArithmetic.invert(Mx + 1, prime) % prime
*/
status_t LTC_PKHA_WeierstrassToEd25519(const ltc_pkha_ecc_point_t *ltcPointIn, ltc_pkha_ecc_point_t *ltcPointOut)
{
    status_t status;
    uint8_t Mx[ED25519_KEY_SIZE] = {0};
    uint8_t My[ED25519_KEY_SIZE] = {0};
    uint8_t temp[ED25519_KEY_SIZE] = {0};
    const uint8_t *Gx;
    const uint8_t *Gy;
    uint8_t *Ex;
    uint8_t *Ey;
    uint16_t szMx = 0;
    uint16_t szEx = 0;
    uint16_t szMy = 0;
    uint16_t szEy = 0;
    uint16_t szTemp = 0;
    uint8_t one = 1;

    Gx = ltcPointIn->X;
    Gy = ltcPointIn->Y;
    Ex = ltcPointOut->X;
    Ey = ltcPointOut->Y;

    /* My = (B*Gy) % prime  */
    status = LTC_PKHA_ModMul(LTC_BASE, B_coefEd25519, sizeof(B_coefEd25519), Gy, ED25519_KEY_SIZE, modbin,
                             ED25519_KEY_SIZE, My, &szMy, kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue,
                             kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);

    /* temp = 3*B*Gx mod p */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, threeB_coefEd25519, sizeof(threeB_coefEd25519), Gx, ED25519_KEY_SIZE, modbin,
                                 ED25519_KEY_SIZE, temp, &szTemp, kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue,
                                 kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);
    }
    /* temp = (temp - A) mod p */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModAdd(LTC_BASE, temp, szTemp, minus_A_coefEd25519, sizeof(minus_A_coefEd25519), modbin,
                                 sizeof(modbin), temp, &szTemp, kLTC_PKHA_IntegerArith);
    }
    /* Mx = (temp/3) mod p */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, temp, szTemp, invThree, sizeof(invThree), modbin, sizeof(modbin), Mx, &szMx,
                                 kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue,
                                 kLTC_PKHA_TimingEqualized);
    }
    /* temp = 1/My mod p */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModInv(LTC_BASE, My, szMy, modbin, sizeof(modbin), temp, &szTemp, kLTC_PKHA_IntegerArith);
    }
    /* Ex = Mx * temp mod p */
    if (status == kStatus_Success)
    {
        status =
            LTC_PKHA_ModMul(LTC_BASE, temp, szTemp, Mx, szMx, modbin, sizeof(modbin), Ex, &szEx, kLTC_PKHA_IntegerArith,
                            kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);
    }

    /* temp = Mx + 1 mod p */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModAdd(LTC_BASE, Mx, szMx, &one, sizeof(one), modbin, sizeof(modbin), temp, &szTemp,
                                 kLTC_PKHA_IntegerArith);
    }
    /* temp = 1/temp mod p */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModInv(LTC_BASE, temp, szTemp, modbin, sizeof(modbin), temp, &szTemp, kLTC_PKHA_IntegerArith);
    }
    /* Mx = (Mx - 1) mod p */
    if (status == kStatus_Success)
    {
        if (LTC_PKHA_CompareBigNum(Mx, szMx, &one, sizeof(one)) >= 0)
        {
            status = LTC_PKHA_ModSub1(LTC_BASE, Mx, szMx, &one, sizeof(one), modbin, sizeof(modbin), Mx, &szMx);
        }
        else
        {
            /* Mx is zero, so it is modulus, thus we do modulus - 1 */
            XMEMCPY(Mx, modbin, sizeof(modbin));
            Mx[0]--;
        }
    }
    /* Ey = Mx * temp mod p */
    if (status == kStatus_Success)
    {
        status =
            LTC_PKHA_ModMul(LTC_BASE, temp, szTemp, Mx, szMx, modbin, sizeof(modbin), Ey, &szEy, kLTC_PKHA_IntegerArith,
                            kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);
    }

    return status;
}

status_t LTC_PKHA_Ed25519_PointDecompress(const uint8_t *pubkey, size_t pubKeySize, ltc_pkha_ecc_point_t *ltcPointOut)
{
    status_t status;
    uint8_t one = 1;

    /* pubkey contains the Y coordinate and a sign of X
     */

    /* x^2 = ((y^2 - 1) / (d*y^2 +1)) mod p */

    /* decode Y from pubkey */
    XMEMCPY(ltcPointOut->Y, pubkey, pubKeySize);
    ltcPointOut->Y[pubKeySize - 1] &= ~0x80u;
    int sign = (int)(bool)(pubkey[pubKeySize - 1] & 0x80u);

    uint8_t U[ED25519_KEY_SIZE] = {0};
    uint8_t V[ED25519_KEY_SIZE] = {0};
    uint8_t *X = ltcPointOut->X;
    uint8_t *Y = ltcPointOut->Y;
    uint16_t szU = 0;
    uint16_t szV = 0;
    size_t szRes = 0;

    /* decode X from pubkey */

    /* U = y * y mod p */
    status = LTC_PKHA_ModMul(LTC_BASE, Y, ED25519_KEY_SIZE, Y, ED25519_KEY_SIZE, modbin, ED25519_KEY_SIZE, U, &szU,
                             kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue,
                             kLTC_PKHA_TimingEqualized);
    XMEMCPY(V, U, szU);
    szV = szU;

    /* U = U - 1 = y^2 - 1 */
    if (status == kStatus_Success)
    {
        if (LTC_PKHA_CompareBigNum(U, szU, &one, sizeof(one)) >= 0)
        {
            status = LTC_PKHA_ModSub1(LTC_BASE, U, szU, &one, sizeof(one), modbin, sizeof(modbin), U, &szU);
        }
        else
        {
            /* U is zero, so it is modulus, thus we do modulus - 1 */
            XMEMCPY(U, modbin, sizeof(modbin));
            U[0]--;
        }
    }

    /* V = d*y*y + 1 */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, V, szV, d_coefEd25519, ED25519_KEY_SIZE, modbin, ED25519_KEY_SIZE, V, &szV,
                                 kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue,
                                 kLTC_PKHA_TimingEqualized);
    }

    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModAdd(LTC_BASE, V, szV, &one, sizeof(one), modbin, sizeof(modbin), V, &szV,
                                 kLTC_PKHA_IntegerArith);
    }

    /* U = U / V (mod p) */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModInv(LTC_BASE, V, szV, modbin, sizeof(modbin), V, &szV, kLTC_PKHA_IntegerArith);
    }
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, V, szV, U, szU, modbin, ED25519_KEY_SIZE, U, &szU, kLTC_PKHA_IntegerArith,
                                 kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue, kLTC_PKHA_TimingEqualized);
    }

    /* get square root */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_Prime25519SquareRootMod(U, szU, X, &szRes, sign);
    }

    return status;
}

/* LSByte first of Ed25519 parameter l = 2^252 + 27742317777372353535851937790883648493 */
static const uint8_t l_coefEdDSA[] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                      0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};

/*
Input:
  s[0]+256*s[1]+...+256^63*s[63] = s

Output:
  s[0]+256*s[1]+...+256^31*s[31] = s mod l
  where l = 2^252 + 27742317777372353535851937790883648493.
  Overwrites s in place.
*/
status_t LTC_PKHA_sc_reduce(uint8_t *a)
{
    uint16_t szA = 0;
    return LTC_PKHA_ModRed(LTC_BASE, a, 64, l_coefEdDSA, sizeof(l_coefEdDSA), a, &szA, kLTC_PKHA_IntegerArith);
}

/*
Input:
  a[0]+256*a[1]+...+256^31*a[31] = a
  b[0]+256*b[1]+...+256^31*b[31] = b
  c[0]+256*c[1]+...+256^31*c[31] = c

Output:
  s[0]+256*s[1]+...+256^31*s[31] = (ab+c) mod l
  where l = 2^252 + 27742317777372353535851937790883648493.
*/
status_t LTC_PKHA_sc_muladd(uint8_t *s, const uint8_t *a, const uint8_t *b, const uint8_t *c)
{
    uint16_t szS = 0;
    uint16_t szB = 0;
    uint8_t tempB[32] = {0};
    status_t status;

    /* Assume only b can be larger than modulus. It is called durind wc_ed25519_sign_msg() where hram (=a) and nonce(=c)
     * have been reduced by LTC_PKHA_sc_reduce()
     * Thus reducing b only.
     */
    status = LTC_PKHA_ModRed(LTC_BASE, b, 32, l_coefEdDSA, sizeof(l_coefEdDSA), tempB, &szB, kLTC_PKHA_IntegerArith);

    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModMul(LTC_BASE, a, 32, tempB, szB, l_coefEdDSA, sizeof(l_coefEdDSA), s, &szS,
                                 kLTC_PKHA_IntegerArith, kLTC_PKHA_NormalValue, kLTC_PKHA_NormalValue,
                                 kLTC_PKHA_TimingEqualized);
    }

    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModAdd(LTC_BASE, s, szS, c, 32, l_coefEdDSA, 32, s, &szS, kLTC_PKHA_IntegerArith);
    }

    return status;
}

/*
r = a * A + b * B
where A is public key point, B is basepoint
where a = a[0]+256*a[1]+...+256^31 a[31].
and b = b[0]+256*b[1]+...+256^31 b[31].
B is the Ed25519 base point (x,4/5) with x positive.
*/
status_t LTC_PKHA_SignatureForVerify(uint8_t *rcheck, const unsigned char *a, const unsigned char *b, ed25519_key *key)
{
    /* To verify a signature on a message M, first split the signature
       into two 32-octet halves.  Decode the first half as a point R,
       and the second half as an integer s, in the range 0 <= s < q.  If
       the decoding fails, the signature is invalid. */

    /* Check the group equation 8s B = 8 R + 8k A. */

    /*
       Uses a fast single-signature verification SB = R + H(R,A,M)A becomes
       SB - H(R,A,M)A saving decompression of R
    */
    uint8_t X0[ED25519_PUB_KEY_SIZE] = {0};
    uint8_t X1[ED25519_PUB_KEY_SIZE] = {0};
    uint8_t Y0[ED25519_PUB_KEY_SIZE] = {0};
    uint8_t Y1[ED25519_PUB_KEY_SIZE] = {0};
    uint8_t max[32] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    ltc_pkha_ecc_point_t ltc0;
    ltc_pkha_ecc_point_t ltc1;
    ltc_pkha_ecc_point_t pubKey;
    status_t status;

    ltc0.X = X0;
    ltc1.X = X1;
    ltc0.Y = Y0;
    ltc1.Y = Y1;
    pubKey.X = key->pointX;
    pubKey.Y = key->pointY;

    /* ltc0 = b*B */
    status = LTC_PKHA_Ed25519_PointMul(LTC_PKHA_Ed25519_BasePoint(), b, ED25519_KEY_SIZE, &ltc0,
                                       kLTC_Weierstrass /* result in W */);

    /* ltc1 = a*A */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_Ed25519ToWeierstrass(&pubKey, &ltc1);
    }
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_Ed25519_PointMul(&ltc1, a, ED25519_KEY_SIZE, &ltc1, kLTC_Weierstrass /* result in W */);
    }

    /* The equality for the negative of a point P, in affine coordinates, is -P = -(x,y) = (x, -y) */
    uint16_t szY = 32;

    /* R = b*B - a*A */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ModSub1(LTC_BASE, modbin, sizeof(modbin), ltc1.Y, szY, max, sizeof(max), ltc1.Y, &szY);
    }
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_ECC_PointAdd(LTC_BASE, &ltc0, &ltc1, modbin, r2mod, a_coefEd25519, b_coefEd25519,
                                       ED25519_KEY_SIZE, kLTC_PKHA_IntegerArith, &ltc0);
    }
    /* map to Ed25519 */
    if (status == kStatus_Success)
    {
        status = LTC_PKHA_WeierstrassToEd25519(&ltc0, &ltc0);
    }
    if (((uint32_t)ltc0.X[0]) & 0x01u)
    {
        ltc0.Y[ED25519_KEY_SIZE - 1] |= 0x80u;
    }

    XMEMCPY(rcheck, ltc0.Y, ED25519_KEY_SIZE);
    return status;
}

status_t LTC_PKHA_Ed25519_Compress(const ltc_pkha_ecc_point_t *ltcPointIn, uint8_t *p)
{
    /* compress */
    /* get sign of X per https://tools.ietf.org/html/draft-josefsson-eddsa-ed25519-02
     * To form the encoding of the point, copy the least
       significant bit of the x-coordinate to the most significant bit of
       the final octet
     */
    XMEMCPY(p, ltcPointIn->Y, ED25519_KEY_SIZE);
    if (((uint32_t)ltcPointIn->X[0]) & 0x01u)
    {
        p[ED25519_KEY_SIZE - 1] |= 0x80u;
    }
    return kStatus_Success;
}

#endif

#endif /* FREESCALE_LTC_ECC */
