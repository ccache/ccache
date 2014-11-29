#include "ccache.h"

/*
 * Tuning parameters
 *
 * Unaligned memory access is automatically enabled for "common" CPU, such as x86.
 * For others CPU, the compiler will be more cautious, and insert extra code to ensure aligned access is respected.
 * If you know your target CPU supports unaligned memory access, you want to force this option manually to improve performance.
 * You can also enable this parameter if you know your input data will always be aligned (boundaries of 4, for unsigned).
 */
#if defined(__ARM_FEATURE_UNALIGNED) || defined(__i386) || defined(_M_IX86) || defined(__x86_64__) || defined(_M_X64)
#  define xxh_USE_UNALIGNED_ACCESS 1
#endif

/*
 * xxh_ACCEPT_NULL_INPUT_POINTER :
 * If the input pointer is a null pointer, xxhash default behavior is to trigger a memory access error, since it is a bad pointer.
 * When this option is enabled, xxhash output for null input pointers will be the same as a null-length input.
 * This option has a very small performance cost (only measurable on small inputs).
 * By default, this option is disabled. To enable it, uncomment below define :
 */
#define xxh_ACCEPT_NULL_INPUT_POINTER 0

/*
 * Basic Types
 */
typedef uint64_t U64;

#if defined(__GNUC__)  && !defined(xxh_USE_UNALIGNED_ACCESS)
#  define _PACKED __attribute__ ((packed))
#else
#  define _PACKED
#endif

#if !defined(xxh_USE_UNALIGNED_ACCESS) && !defined(__GNUC__)
#  ifdef __IBMC__
#    pragma pack(1)
#  else
#    pragma pack(push, 1)
#  endif
#endif

typedef struct _unsigned_S
{
    unsigned v;
} _PACKED unsigned_S;
typedef struct _U64_S
{
    U64 v;
} _PACKED U64_S;

#if !defined(xxh_USE_UNALIGNED_ACCESS) && !defined(__GNUC__)
#  pragma pack(pop)
#endif

#define A32(x) (((unsigned_S *)(x))->v)
#define A64(x) (((U64_S *)(x))->v)


//***************************************
// Compiler-specific Functions and Macros
//***************************************
#define xxh_rotl32(x,r) ((x << r) | (x >> (32 - r)))
#define xxh_rotl64(x,r) ((x << r) | (x >> (64 - r)))

#define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)
#if GCC_VERSION >= 403
#  define xxh_swap32 __builtin_bswap32
#  define xxh_swap64 __builtin_bswap64
#else
static inline unsigned xxh_swap32 (unsigned x)
{
    return  ((x << 24) & 0xff000000 ) |
            ((x <<  8) & 0x00ff0000 ) |
            ((x >>  8) & 0x0000ff00 ) |
            ((x >> 24) & 0x000000ff );
}
static inline U64 xxh_swap64 (U64 x)
{
    return  ((x << 56) & 0xff00000000000000ULL) |
            ((x << 40) & 0x00ff000000000000ULL) |
            ((x << 24) & 0x0000ff0000000000ULL) |
            ((x << 8)  & 0x000000ff00000000ULL) |
            ((x >> 8)  & 0x00000000ff000000ULL) |
            ((x >> 24) & 0x0000000000ff0000ULL) |
            ((x >> 40) & 0x000000000000ff00ULL) |
            ((x >> 56) & 0x00000000000000ffULL);
}
#endif


//**************************************
// Constants
//**************************************
#define PRIME32_1   2654435761U
#define PRIME32_2   2246822519U
#define PRIME32_3   3266489917U
#define PRIME32_4    668265263U
#define PRIME32_5    374761393U

#define PRIME64_1 11400714785074694791ULL
#define PRIME64_2 14029467366897019727ULL
#define PRIME64_3  1609587929392839161ULL
#define PRIME64_4  9650029242287828579ULL
#define PRIME64_5  2870177450012600261ULL

/*
 * Architecture Macros
 */
enum xxh_endianess{ xxh_bigEndian=0, xxh_littleEndian=1 };
#ifndef xxh_CPU_LITTLE_ENDIAN   // It is possible to define xxh_CPU_LITTLE_ENDIAN externally, for example using a compiler switch
static const int one = 1;
#   define xxh_CPU_LITTLE_ENDIAN   (*(char*)(&one))
#endif


/*
 * Memory reads
 */
enum xxh_alignment { xxh_aligned, xxh_unaligned };

static inline unsigned
xxh_readLE32_align(const unsigned* ptr, enum xxh_endianess endian, enum xxh_alignment align)
{
    if (align==xxh_unaligned)
        return endian==xxh_littleEndian ? A32(ptr) : xxh_swap32(A32(ptr));
    return endian==xxh_littleEndian ? *ptr : xxh_swap32(*ptr);
}

static inline unsigned
xxh_readLE32(const unsigned* ptr, enum xxh_endianess endian)
{
    return xxh_readLE32_align(ptr, endian, xxh_unaligned);
}

static inline U64
xxh_readLE64_align(const U64* ptr, enum xxh_endianess endian, enum xxh_alignment align)
{
    if (align==xxh_unaligned)
        return endian==xxh_littleEndian ? A64(ptr) : xxh_swap64(A64(ptr));
    return endian==xxh_littleEndian ? *ptr : xxh_swap64(*ptr);
}

static inline U64
xxh_readLE64(const U64* ptr, enum xxh_endianess endian)
{
    return xxh_readLE64_align(ptr, endian, xxh_unaligned);
}


/*
 * Simple Hash Functions
 */
static inline unsigned
xxh32_endian_align(const void* input, size_t len, unsigned seed, enum xxh_endianess endian, enum xxh_alignment align)
{
    const unsigned char* p = (const unsigned char*)input;
    const unsigned char* bEnd = p + len;
    unsigned h32;
#define xxh_get32bits(p) xxh_readLE32_align((const unsigned*)p, endian, align)

#ifdef xxh_ACCEPT_NULL_INPUT_POINTER
    if (!p) {
        len=0;
        bEnd=p=(const unsigned char*)(size_t)16;
    }
#endif

    if (len>=16) {
        const unsigned char* const limit = bEnd - 16;
        unsigned v1 = seed + PRIME32_1 + PRIME32_2;
        unsigned v2 = seed + PRIME32_2;
        unsigned v3 = seed + 0;
        unsigned v4 = seed - PRIME32_1;

        do {
            v1 += xxh_get32bits(p) * PRIME32_2;
            v1 = xxh_rotl32(v1, 13);
            v1 *= PRIME32_1;
            p+=4;
            v2 += xxh_get32bits(p) * PRIME32_2;
            v2 = xxh_rotl32(v2, 13);
            v2 *= PRIME32_1;
            p+=4;
            v3 += xxh_get32bits(p) * PRIME32_2;
            v3 = xxh_rotl32(v3, 13);
            v3 *= PRIME32_1;
            p+=4;
            v4 += xxh_get32bits(p) * PRIME32_2;
            v4 = xxh_rotl32(v4, 13);
            v4 *= PRIME32_1;
            p+=4;
        } while (p<=limit);

        h32 = xxh_rotl32(v1, 1) + xxh_rotl32(v2, 7) + xxh_rotl32(v3, 12) + xxh_rotl32(v4, 18);
    } else {
        h32  = seed + PRIME32_5;
    }

    h32 += (unsigned) len;

    while (p+4<=bEnd) {
        h32 += xxh_get32bits(p) * PRIME32_3;
        h32  = xxh_rotl32(h32, 17) * PRIME32_4 ;
        p+=4;
    }

    while (p<bEnd) {
        h32 += (*p) * PRIME32_5;
        h32 = xxh_rotl32(h32, 11) * PRIME32_1 ;
        p++;
    }

    h32 ^= h32 >> 15;
    h32 *= PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= PRIME32_3;
    h32 ^= h32 >> 16;

    return h32;
}


unsigned int
xxh32 (const void* input, size_t len, unsigned seed)
{
    enum xxh_endianess endian_detected = (enum xxh_endianess)xxh_CPU_LITTLE_ENDIAN;

#  if !defined(xxh_USE_UNALIGNED_ACCESS)
    if ((((size_t)input) & 3) == 0)   // Input is aligned, let's leverage the speed advantage
    {
        if ((endian_detected==xxh_littleEndian) || xxh_FORCE_NATIVE_FORMAT)
            return xxh32_endian_align(input, len, seed, xxh_littleEndian, xxh_aligned);
        return xxh32_endian_align(input, len, seed, xxh_bigEndian, xxh_aligned);
    }
#  endif

    if (endian_detected==xxh_littleEndian)
        return xxh32_endian_align(input, len, seed, xxh_littleEndian, xxh_unaligned);
    return xxh32_endian_align(input, len, seed, xxh_bigEndian, xxh_unaligned);
}

static inline U64
xxh64_endian_align(const void* input, size_t len, U64 seed, enum xxh_endianess endian, enum xxh_alignment align)
{
    const unsigned char* p = (const unsigned char*)input;
    const unsigned char* bEnd = p + len;
    U64 h64;
#define xxh_get64bits(p) xxh_readLE64_align((const U64*)p, endian, align)

#ifdef xxh_ACCEPT_NULL_INPUT_POINTER
    if (!p) {
        len=0;
        bEnd=p=(const unsigned char*)(size_t)32;
    }
#endif

    if (len>=32) {
        const unsigned char* const limit = bEnd - 32;
        U64 v1 = seed + PRIME64_1 + PRIME64_2;
        U64 v2 = seed + PRIME64_2;
        U64 v3 = seed + 0;
        U64 v4 = seed - PRIME64_1;

        do {
            v1 += xxh_get64bits(p) * PRIME64_2;
            p+=8;
            v1 = xxh_rotl64(v1, 31);
            v1 *= PRIME64_1;
            v2 += xxh_get64bits(p) * PRIME64_2;
            p+=8;
            v2 = xxh_rotl64(v2, 31);
            v2 *= PRIME64_1;
            v3 += xxh_get64bits(p) * PRIME64_2;
            p+=8;
            v3 = xxh_rotl64(v3, 31);
            v3 *= PRIME64_1;
            v4 += xxh_get64bits(p) * PRIME64_2;
            p+=8;
            v4 = xxh_rotl64(v4, 31);
            v4 *= PRIME64_1;
        } while (p<=limit);

        h64 = xxh_rotl64(v1, 1) + xxh_rotl64(v2, 7) + xxh_rotl64(v3, 12) + xxh_rotl64(v4, 18);

        v1 *= PRIME64_2;
        v1 = xxh_rotl64(v1, 31);
        v1 *= PRIME64_1;
        h64 ^= v1;
        h64 = h64 * PRIME64_1 + PRIME64_4;

        v2 *= PRIME64_2;
        v2 = xxh_rotl64(v2, 31);
        v2 *= PRIME64_1;
        h64 ^= v2;
        h64 = h64 * PRIME64_1 + PRIME64_4;

        v3 *= PRIME64_2;
        v3 = xxh_rotl64(v3, 31);
        v3 *= PRIME64_1;
        h64 ^= v3;
        h64 = h64 * PRIME64_1 + PRIME64_4;

        v4 *= PRIME64_2;
        v4 = xxh_rotl64(v4, 31);
        v4 *= PRIME64_1;
        h64 ^= v4;
        h64 = h64 * PRIME64_1 + PRIME64_4;
    } else {
        h64  = seed + PRIME64_5;
    }

    h64 += (U64) len;

    while (p+8<=bEnd) {
        U64 k1 = xxh_get64bits(p);
        k1 *= PRIME64_2;
        k1 = xxh_rotl64(k1,31);
        k1 *= PRIME64_1;
        h64 ^= k1;
        h64 = xxh_rotl64(h64,27) * PRIME64_1 + PRIME64_4;
        p+=8;
    }

    if (p+4<=bEnd) {
        h64 ^= (U64)(xxh_get32bits(p)) * PRIME64_1;
        h64 = xxh_rotl64(h64, 23) * PRIME64_2 + PRIME64_3;
        p+=4;
    }

    while (p<bEnd) {
        h64 ^= (*p) * PRIME64_5;
        h64 = xxh_rotl64(h64, 11) * PRIME64_1;
        p++;
    }

    h64 ^= h64 >> 33;
    h64 *= PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= PRIME64_3;
    h64 ^= h64 >> 32;

    return h64;
}


unsigned long long
xxh64 (const void* input, size_t len, unsigned long long seed)
{
    enum xxh_endianess endian_detected = (enum xxh_endianess)xxh_CPU_LITTLE_ENDIAN;

#  if !defined(xxh_USE_UNALIGNED_ACCESS)
    if ((((size_t)input) & 7)==0)   // Input is aligned, let's leverage the speed advantage
    {
        if ((endian_detected==xxh_littleEndian) || xxh_FORCE_NATIVE_FORMAT)
            return xxh64_endian_align(input, len, seed, xxh_littleEndian, xxh_aligned);
        return xxh64_endian_align(input, len, seed, xxh_bigEndian, xxh_aligned);
    }
#  endif

    if (endian_detected==xxh_littleEndian)
        return xxh64_endian_align(input, len, seed, xxh_littleEndian, xxh_unaligned);
    return xxh64_endian_align(input, len, seed, xxh_bigEndian, xxh_unaligned);
}

/*** Hash feed ***/

int
xxh32_reset(struct xxh32_state_t* state, unsigned seed)
{
    state->seed = seed;
    state->v1 = seed + PRIME32_1 + PRIME32_2;
    state->v2 = seed + PRIME32_2;
    state->v3 = seed + 0;
    state->v4 = seed - PRIME32_1;
    state->total_len = 0;
    state->memsize = 0;
    return 0;
}

int
xxh64_reset(struct xxh64_state_t* state, unsigned long long seed)
{
    state->seed = seed;
    state->v1 = seed + PRIME64_1 + PRIME64_2;
    state->v2 = seed + PRIME64_2;
    state->v3 = seed + 0;
    state->v4 = seed - PRIME64_1;
    state->total_len = 0;
    state->memsize = 0;
    return 0;
}


static inline int
xxh32_update_endian (struct xxh32_state_t* state, const void* input, size_t len, enum xxh_endianess endian)
{
    const unsigned char* p = (const unsigned char*)input;
    const unsigned char* const bEnd = p + len;

#ifdef xxh_ACCEPT_NULL_INPUT_POINTER
    if (!input) return 1;
#endif

    state->total_len += len;

    if (state->memsize + len < 16)   // fill in tmp buffer
    {
        memcpy(state->memory + state->memsize, input, len);
        state->memsize += (unsigned)len;
        return 0;
    }

    if (state->memsize)   // some data left from previous update
    {
        memcpy(state->memory + state->memsize, input, 16-state->memsize);
        {
            const unsigned* p32 = (const unsigned*)state->memory;
            state->v1 += xxh_readLE32(p32, endian) * PRIME32_2;
            state->v1 = xxh_rotl32(state->v1, 13);
            state->v1 *= PRIME32_1;
            p32++;
            state->v2 += xxh_readLE32(p32, endian) * PRIME32_2;
            state->v2 = xxh_rotl32(state->v2, 13);
            state->v2 *= PRIME32_1;
            p32++;
            state->v3 += xxh_readLE32(p32, endian) * PRIME32_2;
            state->v3 = xxh_rotl32(state->v3, 13);
            state->v3 *= PRIME32_1;
            p32++;
            state->v4 += xxh_readLE32(p32, endian) * PRIME32_2;
            state->v4 = xxh_rotl32(state->v4, 13);
            state->v4 *= PRIME32_1;
            p32++;
        }
        p += 16-state->memsize;
        state->memsize = 0;
    }

    if (p <= bEnd-16) {
        const unsigned char* const limit = bEnd - 16;
        unsigned v1 = state->v1;
        unsigned v2 = state->v2;
        unsigned v3 = state->v3;
        unsigned v4 = state->v4;

        do {
            v1 += xxh_readLE32((const unsigned*)p, endian) * PRIME32_2;
            v1 = xxh_rotl32(v1, 13);
            v1 *= PRIME32_1;
            p+=4;
            v2 += xxh_readLE32((const unsigned*)p, endian) * PRIME32_2;
            v2 = xxh_rotl32(v2, 13);
            v2 *= PRIME32_1;
            p+=4;
            v3 += xxh_readLE32((const unsigned*)p, endian) * PRIME32_2;
            v3 = xxh_rotl32(v3, 13);
            v3 *= PRIME32_1;
            p+=4;
            v4 += xxh_readLE32((const unsigned*)p, endian) * PRIME32_2;
            v4 = xxh_rotl32(v4, 13);
            v4 *= PRIME32_1;
            p+=4;
        } while (p<=limit);

        state->v1 = v1;
        state->v2 = v2;
        state->v3 = v3;
        state->v4 = v4;
    }

    if (p < bEnd) {
        memcpy(state->memory, p, bEnd-p);
        state->memsize = (int)(bEnd-p);
    }

    return 0;
}

int
xxh32_update (struct xxh32_state_t* state_in, const void* input, size_t len)
{
    enum xxh_endianess endian_detected = (enum xxh_endianess)xxh_CPU_LITTLE_ENDIAN;

    if (endian_detected==xxh_littleEndian)
        return xxh32_update_endian(state_in, input, len, xxh_littleEndian);
    return xxh32_update_endian(state_in, input, len, xxh_bigEndian);
}



static inline
unsigned xxh32_digest_endian (const struct xxh32_state_t* state, enum xxh_endianess endian)
{
    const unsigned char * p = (const unsigned char*)state->memory;
    unsigned char* bEnd = (unsigned char*)state->memory + state->memsize;
    unsigned h32;

    if (state->total_len >= 16) {
        h32 = xxh_rotl32(state->v1, 1) + xxh_rotl32(state->v2, 7) + xxh_rotl32(state->v3, 12) + xxh_rotl32(state->v4, 18);
    } else {
        h32  = state->seed + PRIME32_5;
    }

    h32 += (unsigned) state->total_len;

    while (p+4<=bEnd) {
        h32 += xxh_readLE32((const unsigned*)p, endian) * PRIME32_3;
        h32  = xxh_rotl32(h32, 17) * PRIME32_4;
        p+=4;
    }

    while (p<bEnd) {
        h32 += (*p) * PRIME32_5;
        h32 = xxh_rotl32(h32, 11) * PRIME32_1;
        p++;
    }

    h32 ^= h32 >> 15;
    h32 *= PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= PRIME32_3;
    h32 ^= h32 >> 16;

    return h32;
}


unsigned
xxh32_digest (const struct xxh32_state_t* state_in)
{
    enum xxh_endianess endian_detected = (enum xxh_endianess)xxh_CPU_LITTLE_ENDIAN;

    if (endian_detected==xxh_littleEndian)
        return xxh32_digest_endian(state_in, xxh_littleEndian);
    return xxh32_digest_endian(state_in, xxh_bigEndian);
}


static inline int
xxh64_update_endian (struct xxh64_state_t* state, const void* input, size_t len, enum xxh_endianess endian)
{
    const unsigned char* p = (const unsigned char*)input;
    const unsigned char* const bEnd = p + len;

#ifdef xxh_ACCEPT_NULL_INPUT_POINTER
    if (!input) return 1;
#endif

    state->total_len += len;

    if (state->memsize + len < 32)   // fill in tmp buffer
    {
        memcpy(state->memory + state->memsize, input, len);
        state->memsize += (unsigned)len;
        return 0;
    }

    if (state->memsize)   // some data left from previous update
    {
        memcpy(state->memory + state->memsize, input, 32-state->memsize);
        {
            const U64* p64 = (const U64*)state->memory;
            state->v1 += xxh_readLE64(p64, endian) * PRIME64_2;
            state->v1 = xxh_rotl64(state->v1, 31);
            state->v1 *= PRIME64_1;
            p64++;
            state->v2 += xxh_readLE64(p64, endian) * PRIME64_2;
            state->v2 = xxh_rotl64(state->v2, 31);
            state->v2 *= PRIME64_1;
            p64++;
            state->v3 += xxh_readLE64(p64, endian) * PRIME64_2;
            state->v3 = xxh_rotl64(state->v3, 31);
            state->v3 *= PRIME64_1;
            p64++;
            state->v4 += xxh_readLE64(p64, endian) * PRIME64_2;
            state->v4 = xxh_rotl64(state->v4, 31);
            state->v4 *= PRIME64_1;
            p64++;
        }
        p += 32-state->memsize;
        state->memsize = 0;
    }

    if (p+32 <= bEnd) {
        const unsigned char* const limit = bEnd - 32;
        U64 v1 = state->v1;
        U64 v2 = state->v2;
        U64 v3 = state->v3;
        U64 v4 = state->v4;

        do {
            v1 += xxh_readLE64((const U64*)p, endian) * PRIME64_2;
            v1 = xxh_rotl64(v1, 31);
            v1 *= PRIME64_1;
            p+=8;
            v2 += xxh_readLE64((const U64*)p, endian) * PRIME64_2;
            v2 = xxh_rotl64(v2, 31);
            v2 *= PRIME64_1;
            p+=8;
            v3 += xxh_readLE64((const U64*)p, endian) * PRIME64_2;
            v3 = xxh_rotl64(v3, 31);
            v3 *= PRIME64_1;
            p+=8;
            v4 += xxh_readLE64((const U64*)p, endian) * PRIME64_2;
            v4 = xxh_rotl64(v4, 31);
            v4 *= PRIME64_1;
            p+=8;
        } while (p<=limit);

        state->v1 = v1;
        state->v2 = v2;
        state->v3 = v3;
        state->v4 = v4;
    }

    if (p < bEnd) {
        memcpy(state->memory, p, bEnd-p);
        state->memsize = (int)(bEnd-p);
    }

    return 0;
}

int
xxh64_update (struct xxh64_state_t* state_in, const void* input, size_t len)
{
    enum xxh_endianess endian_detected = (enum xxh_endianess)xxh_CPU_LITTLE_ENDIAN;

    if (endian_detected==xxh_littleEndian)
        return xxh64_update_endian(state_in, input, len, xxh_littleEndian);
    return xxh64_update_endian(state_in, input, len, xxh_bigEndian);
}



static inline U64
xxh64_digest_endian (const struct xxh64_state_t* state, enum xxh_endianess endian)
{
    const unsigned char * p = (const unsigned char*)state->memory;
    unsigned char* bEnd = (unsigned char*)state->memory + state->memsize;
    U64 h64;

    if (state->total_len >= 32) {
        U64 v1 = state->v1;
        U64 v2 = state->v2;
        U64 v3 = state->v3;
        U64 v4 = state->v4;

        h64 = xxh_rotl64(v1, 1) + xxh_rotl64(v2, 7) + xxh_rotl64(v3, 12) + xxh_rotl64(v4, 18);

        v1 *= PRIME64_2;
        v1 = xxh_rotl64(v1, 31);
        v1 *= PRIME64_1;
        h64 ^= v1;
        h64 = h64*PRIME64_1 + PRIME64_4;

        v2 *= PRIME64_2;
        v2 = xxh_rotl64(v2, 31);
        v2 *= PRIME64_1;
        h64 ^= v2;
        h64 = h64*PRIME64_1 + PRIME64_4;

        v3 *= PRIME64_2;
        v3 = xxh_rotl64(v3, 31);
        v3 *= PRIME64_1;
        h64 ^= v3;
        h64 = h64*PRIME64_1 + PRIME64_4;

        v4 *= PRIME64_2;
        v4 = xxh_rotl64(v4, 31);
        v4 *= PRIME64_1;
        h64 ^= v4;
        h64 = h64*PRIME64_1 + PRIME64_4;
    } else {
        h64  = state->seed + PRIME64_5;
    }

    h64 += (U64) state->total_len;

    while (p+8<=bEnd) {
        U64 k1 = xxh_readLE64((const U64*)p, endian);
        k1 *= PRIME64_2;
        k1 = xxh_rotl64(k1,31);
        k1 *= PRIME64_1;
        h64 ^= k1;
        h64 = xxh_rotl64(h64,27) * PRIME64_1 + PRIME64_4;
        p+=8;
    }

    if (p+4<=bEnd) {
        h64 ^= (U64)(xxh_readLE32((const unsigned*)p, endian)) * PRIME64_1;
        h64 = xxh_rotl64(h64, 23) * PRIME64_2 + PRIME64_3;
        p+=4;
    }

    while (p<bEnd) {
        h64 ^= (*p) * PRIME64_5;
        h64 = xxh_rotl64(h64, 11) * PRIME64_1;
        p++;
    }

    h64 ^= h64 >> 33;
    h64 *= PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= PRIME64_3;
    h64 ^= h64 >> 32;

    return h64;
}


unsigned long long
xxh64_digest (const struct xxh64_state_t* state_in)
{
    enum xxh_endianess endian_detected = (enum xxh_endianess)xxh_CPU_LITTLE_ENDIAN;

    if (endian_detected==xxh_littleEndian)
        return xxh64_digest_endian(state_in, xxh_littleEndian);
    return xxh64_digest_endian(state_in, xxh_bigEndian);
}
