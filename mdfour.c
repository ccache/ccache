// Copyright (C) 1997-1998 Andrew Tridgell
// Copyright (C) 2009-2016 Joel Rosdahl
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "ccache.h"

// NOTE: This code makes no attempt to be fast!

static struct mdfour *m;

#define MASK32 (0xffffffff)

#define F(X, Y, Z) ((((X)&(Y)) | ((~(X))&(Z))))
#define G(X, Y, Z) ((((X)&(Y)) | ((X)&(Z)) | ((Y)&(Z))))
#define H(X, Y, Z) (((X)^(Y)^(Z)))
#define lshift(x, s) (((((x)<<(s))&MASK32) | (((x)>>(32-(s)))&MASK32)))

#define ROUND1(a, b, c, d, k, s) \
	a = lshift((a + F(b, c, d) + M[k])&MASK32, s)
#define ROUND2(a, b, c, d, k, s) \
	a = lshift((a + G(b, c, d) + M[k] + 0x5A827999)&MASK32, s)
#define ROUND3(a, b, c, d, k, s) \
	a = lshift((a + H(b, c, d) + M[k] + 0x6ED9EBA1)&MASK32, s)

// This applies md4 to 64 byte chunks.
static void
mdfour64(uint32_t *M)
{
	uint32_t AA, BB, CC, DD;
	uint32_t A, B, C, D;

	A = m->A;
	B = m->B;
	C = m->C;
	D = m->D;
	AA = A;
	BB = B;
	CC = C;
	DD = D;

	ROUND1(A, B, C, D,  0,  3);  ROUND1(D, A, B, C,  1,  7);
	ROUND1(C, D, A, B,  2, 11);  ROUND1(B, C, D, A,  3, 19);
	ROUND1(A, B, C, D,  4,  3);  ROUND1(D, A, B, C,  5,  7);
	ROUND1(C, D, A, B,  6, 11);  ROUND1(B, C, D, A,  7, 19);
	ROUND1(A, B, C, D,  8,  3);  ROUND1(D, A, B, C,  9,  7);
	ROUND1(C, D, A, B, 10, 11);  ROUND1(B, C, D, A, 11, 19);
	ROUND1(A, B, C, D, 12,  3);  ROUND1(D, A, B, C, 13,  7);
	ROUND1(C, D, A, B, 14, 11);  ROUND1(B, C, D, A, 15, 19);


	ROUND2(A, B, C, D,  0,  3);  ROUND2(D, A, B, C,  4,  5);
	ROUND2(C, D, A, B,  8,  9);  ROUND2(B, C, D, A, 12, 13);
	ROUND2(A, B, C, D,  1,  3);  ROUND2(D, A, B, C,  5,  5);
	ROUND2(C, D, A, B,  9,  9);  ROUND2(B, C, D, A, 13, 13);
	ROUND2(A, B, C, D,  2,  3);  ROUND2(D, A, B, C,  6,  5);
	ROUND2(C, D, A, B, 10,  9);  ROUND2(B, C, D, A, 14, 13);
	ROUND2(A, B, C, D,  3,  3);  ROUND2(D, A, B, C,  7,  5);
	ROUND2(C, D, A, B, 11,  9);  ROUND2(B, C, D, A, 15, 13);

	ROUND3(A, B, C, D,  0,  3);  ROUND3(D, A, B, C,  8,  9);
	ROUND3(C, D, A, B,  4, 11);  ROUND3(B, C, D, A, 12, 15);
	ROUND3(A, B, C, D,  2,  3);  ROUND3(D, A, B, C, 10,  9);
	ROUND3(C, D, A, B,  6, 11);  ROUND3(B, C, D, A, 14, 15);
	ROUND3(A, B, C, D,  1,  3);  ROUND3(D, A, B, C,  9,  9);
	ROUND3(C, D, A, B,  5, 11);  ROUND3(B, C, D, A, 13, 15);
	ROUND3(A, B, C, D,  3,  3);  ROUND3(D, A, B, C, 11,  9);
	ROUND3(C, D, A, B,  7, 11);  ROUND3(B, C, D, A, 15, 15);

	A += AA;
	B += BB;
	C += CC;
	D += DD;

	A &= MASK32;
	B &= MASK32;
	C &= MASK32;
	D &= MASK32;

	m->A = A;
	m->B = B;
	m->C = C;
	m->D = D;
}

static void
copy64(uint32_t *M, const unsigned char *in)
{
#ifdef WORDS_BIGENDIAN
	for (int i = 0; i < 16; i++) {
		M[i] = (in[i*4+3]<<24) | (in[i*4+2]<<16) |
		       (in[i*4+1]<<8) | (in[i*4+0]<<0);
	}
#else
	memcpy(M, in, 16*4);
#endif
}

static void
copy4(unsigned char *out, uint32_t x)
{
#ifdef WORDS_BIGENDIAN
	out[0] = x&0xFF;
	out[1] = (x>>8)&0xFF;
	out[2] = (x>>16)&0xFF;
	out[3] = (x>>24)&0xFF;
#else
	memcpy(out, &x, 4);
#endif
}

void
mdfour_begin(struct mdfour *md)
{
	md->A = 0x67452301;
	md->B = 0xefcdab89;
	md->C = 0x98badcfe;
	md->D = 0x10325476;
	md->totalN = 0;
	md->tail_len = 0;
	md->finalized = 0;
}

static
void mdfour_tail(const unsigned char *in, size_t n)
{
	m->totalN += n;
	uint32_t b = (uint32_t)m->totalN * 8;
	unsigned char buf[128] = { 0 };
	uint32_t M[16];
	if (n) {
		memcpy(buf, in, n);
	}
	buf[n] = 0x80;

	if (n <= 55) {
		copy4(buf+56, b);
		copy64(M, buf);
		mdfour64(M);
	} else {
		copy4(buf+120, b);
		copy64(M, buf);
		mdfour64(M);
		copy64(M, buf+64);
		mdfour64(M);
	}
}

void
mdfour_update(struct mdfour *md, const unsigned char *in, size_t n)
{
#ifdef CCACHE_DEBUG_HASH
	if (getenv("CCACHE_DEBUG_HASH")) {
		FILE *f = fopen("ccache-debug-hash.bin", "a");
		fwrite(in, 1, n, f);
		fclose(f);
	}
#endif

	m = md;

	if (!in) {
		if (!md->finalized) {
			mdfour_tail(md->tail, md->tail_len);
			md->finalized = 1;
		}
		return;
	}

	uint32_t M[16];
	if (md->tail_len) {
		size_t len = 64 - md->tail_len;
		if (len > n) {
			len = n;
		}
		memcpy(md->tail+md->tail_len, in, len);
		md->tail_len += len;
		n -= len;
		in += len;
		if (md->tail_len == 64) {
			copy64(M, md->tail);
			mdfour64(M);
			m->totalN += 64;
			md->tail_len = 0;
		}
	}

	while (n >= 64) {
		copy64(M, in);
		mdfour64(M);
		in += 64;
		n -= 64;
		m->totalN += 64;
	}

	if (n) {
		memcpy(md->tail, in, n);
		md->tail_len = n;
	}
}

void
mdfour_result(struct mdfour *md, unsigned char *out)
{
	copy4(out, md->A);
	copy4(out+4, md->B);
	copy4(out+8, md->C);
	copy4(out+12, md->D);
}
