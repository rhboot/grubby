/* adler32.c -- compute the Adler-32 checksum of a data stream
 * Copyright (C) 1995-2004 Mark Adler
 *
 * License:

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

  Jean-loup Gailly        Mark Adler
  jloup@gzip.org          madler@alumni.caltech.edu

 *
 * This version has had A32_ prepended to "private" tokens.
 */

#ifndef NASH_ADLER32_H
#define NASH_ADLER32_H 1

#define A32_BASE 65521UL    /* largest prime smaller than 65536 */
#define A32_NMAX 5552
/* NMAX is the largest n such that 255n(n+1)/2 + (n+1)(BASE-1) <= 2^32-1 */

#define A32_DO1(buf,i)  {adler += (buf)[i]; sum2 += adler;}
#define A32_DO2(buf,i)  A32_DO1(buf,i); A32_DO1(buf,i+1);
#define A32_DO4(buf,i)  A32_DO2(buf,i); A32_DO2(buf,i+2);
#define A32_DO8(buf,i)  A32_DO4(buf,i); A32_DO4(buf,i+4);
#define A32_DO16(buf)   A32_DO8(buf,0); A32_DO8(buf,8);

#define A32_MOD(a) ((a) %= A32_BASE)
#define A32_MOD4(a) ((a) %= A32_BASE)

/* ========================================================================= */
uint32_t adler32(uint32_t adler, const char *buf, size_t len)
{
    unsigned long sum2;
    unsigned n;

    /* split Adler-32 into component sums */
    sum2 = (adler >> 16) & 0xffff;
    adler &= 0xffff;

    /* in case user likes doing a byte at a time, keep it fast */
    if (len == 1) {
        adler += buf[0];
        if (adler >= A32_BASE)
            adler -= A32_BASE;
        sum2 += adler;
        if (sum2 >= A32_BASE)
            sum2 -= A32_BASE;
        return adler | (sum2 << 16);
    }

    /* initial Adler-32 value (deferred check for len == 1 speed) */
    if (buf == NULL)
        return 1L;

    /* in case short lengths are provided, keep it somewhat fast */
    if (len < 16) {
        while (len--) {
            adler += *buf++;
            sum2 += adler;
        }
        if (adler >= A32_BASE)
            adler -= A32_BASE;
        A32_MOD4(sum2);             /* only added so many A32_BASE's */
        return adler | (sum2 << 16);
    }

    /* do length NMAX blocks -- requires just one modulo operation */
    while (len >= A32_NMAX) {
        len -= A32_NMAX;
        n = A32_NMAX / 16;          /* NMAX is divisible by 16 */
        do {
            A32_DO16(buf);          /* 16 sums unrolled */
            buf += 16;
        } while (--n);
        A32_MOD(adler);
        A32_MOD(sum2);
    }

    /* do remaining bytes (less than NMAX, still just one modulo) */
    if (len) {                  /* avoid modulos if none remaining */
        while (len >= 16) {
            len -= 16;
            A32_DO16(buf);
            buf += 16;
        }
        while (len--) {
            adler += *buf++;
            sum2 += adler;
        }
        A32_MOD(adler);
        A32_MOD(sum2);
    }

    /* return recombined sums */
    return adler | (sum2 << 16);
}

/* ========================================================================= */
uint32_t adler32_combine(uint32_t adler1, uint32_t adler2, off_t len2)
{
    unsigned long sum1;
    unsigned long sum2;
    unsigned rem;

    /* the derivation of this formula is left as an exercise for the reader */
    rem = (unsigned)(len2 % A32_BASE);
    sum1 = adler1 & 0xffff;
    sum2 = rem * sum1;
    A32_MOD(sum2);
    sum1 += (adler2 & 0xffff) + A32_BASE - 1;
    sum2 += ((adler1 >> 16) & 0xffff) + ((adler2 >> 16) & 0xffff) + A32_BASE - rem;
    if (sum1 > A32_BASE) sum1 -= A32_BASE;
    if (sum1 > A32_BASE) sum1 -= A32_BASE;
    if (sum2 > (A32_BASE << 1)) sum2 -= (A32_BASE << 1);
    if (sum2 > A32_BASE) sum2 -= A32_BASE;
    return sum1 | (sum2 << 16);
}

#undef A32_BASE
#undef A32_NMAX
#undef A32_DO1
#undef A32_DO2
#undef A32_DO4
#undef A32_DO8
#undef A32_DO16

#undef A32_MOD
#undef A32_MOD4

#endif /* NASH_ADLER32_H */
