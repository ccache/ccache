#include <stddef.h>   /* size_t */

unsigned int       xxh32 (const void* input, size_t length, unsigned seed);
unsigned long long xxh64 (const void* input, size_t length, unsigned long long seed);

/*****************************
   Advanced Hash Functions
*****************************/
/*** Allocation ***/
struct xxh32_state_t
{
        unsigned long long total_len;
        unsigned seed;
        unsigned v1;
        unsigned v2;
        unsigned v3;
        unsigned v4;
        unsigned memsize;
        char memory[16];
};

struct xxh64_state_t
{
        unsigned long long total_len;
        unsigned long long seed;
        unsigned long long v1;
        unsigned long long v2;
        unsigned long long v3;
        unsigned long long v4;
        unsigned memsize;
        char memory[32];
};

struct xxh32_state_t* xxh32_createState(void);
int  xxh32_freeState(struct xxh32_state_t* statePtr);

struct xxh64_state_t* xxh64_createState(void);
int  xxh64_freeState(struct xxh64_state_t* statePtr);

int xxh32_reset  (struct xxh32_state_t* statePtr, unsigned seed);
int xxh32_update (struct xxh32_state_t* statePtr, const void* input, size_t length);
unsigned int  xxh32_digest (const struct xxh32_state_t* statePtr);

int      xxh64_reset  (struct xxh64_state_t* statePtr, unsigned long long seed);
int      xxh64_update (struct xxh64_state_t* statePtr, const void* input, size_t length);
unsigned long long xxh64_digest (const struct xxh64_state_t* statePtr);
