#include "ccache.h"

#ifdef HAVE_LIBMEMCACHED

#include <libmemcached/memcached.h>
#include <netinet/in.h>

/* status variables for memcached */
static memcached_st *memc;
static char *current_cache = NULL;
static int current_length = 0;

int memccached_init(char *conf)
{
    memc = memcached(conf, strlen(conf));
    if (memc == NULL) {
        char errorbuf[1024];
        libmemcached_check_configuration(conf, strlen(conf), errorbuf, 1024);
        cc_log("Problem creating memcached with conf %s:\n%s\n", conf, errorbuf);
        return -1;
    }
    /* Consistent hashing delivers better distribution and allows servers to be added
       to the cluster with minimal cache losses */
    memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_DISTRIBUTION, MEMCACHED_DISTRIBUTION_CONSISTENT);
    return 0;
}
/* blob format for storing:

    char magic[4]; # 'CCH1', might change for other version of ccache
                   # ccache will erase the blob in memcached if wrong magic
    uint32_t obj_len; # network endian
    char *obj[obj_len];
    uint32_t stderr_len; # network endian
    char *stderr[stderr_len];
    uint32_t dia_len; # network endian
    char *dia[dia_len];
    uint32_t dep_len; # network endian
    char *dep[dep_len];

*/
int memccached_store(const char *key,
                   const char *obj, const char *stderr, const char *dia, const char *dep,
                   size_t obj_len, size_t stderr_len, size_t dia_len, size_t dep_len)
{
    size_t buf_len = 4 + 4*4 + obj_len + stderr_len + dia_len + dep_len;
    char *buf = malloc(buf_len);
    char *ptr;
    memcached_return_t mret;

    if (buf == NULL) {
        cc_log("unable to allocate %u bytes memory from memcache value", (unsigned int)buf_len);
        return -1;
    }
    memcpy(buf, MEMCCACHE_MAGIC, 4);
    ptr = buf + 4;

#define PROCESS_ONE_BUFFER(src_ptr, src_len)     \
    do {                                         \
        *((uint32_t*)ptr) = htonl(src_len);      \
        ptr += 4;                                \
        if (src_len)                             \
            memcpy(ptr, src_ptr, src_len);       \
        ptr += src_len;                          \
    } while(0)

    PROCESS_ONE_BUFFER(obj, obj_len);
    PROCESS_ONE_BUFFER(stderr, stderr_len);
    PROCESS_ONE_BUFFER(dia, dia_len);
    PROCESS_ONE_BUFFER(dep, dep_len);

#undef PROCESS_ONE_BUFFER

    mret = memcached_set(memc, key, strlen(key),
                         buf, buf_len, 0, 0);

    if (mret != MEMCACHED_SUCCESS) {
        cc_log("Failed to move %s to memcached: %s", key,
               memcached_strerror(memc, mret));
        return -1;
    }
    current_cache = buf;
    current_length = buf_len;
    return 0;
}

static void *memccached_prune(const char *key)
{
    cc_log("key from memcached has wrong data %s: pruning...", key);
    /* don't really care whether delete failed */
    memcached_delete(memc, key, strlen(key), 0);
    return NULL;
}
void *memccached_get(const char *key,
                   char **obj, char **stderr, char **dia, char **dep,
                   size_t *obj_len, size_t *stderr_len, size_t *dia_len, size_t *dep_len)
{
    memcached_return_t mret;
    char *value, *ptr;
    size_t value_l;
    /* micro optimization: for the second from_cache, we don't hit memcached
       and just reuse the data that we just sent
       I don't understand anyway the need for a second from_cache ;-/
       */
    if (current_cache){
        value = current_cache;
        value_l = current_length;
        current_cache = NULL;
    } else {
        value = memcached_get(memc, key, strlen(key), &value_l,
                          NULL/*flags*/, &mret);
    }
    if (value == NULL) {
            cc_log("Failed to get key from memcached %s: %s", key,
                   memcached_strerror(memc, mret));
            return NULL;
    }
    if (value_l < 20 || memcmp(value, MEMCCACHE_MAGIC, 4) != 0) {
        cc_log("wrong magic or length %s: %d", value, (int)value_l);
        free(value);
        return memccached_prune(key);
    }
    ptr = value;
    /* skip the magic */
    ptr+=4; value_l-=4;

#define PROCESS_ONE_BUFFER(dst_ptr, dst_len)     \
    do {                                         \
        if (value_l < 4) {                       \
            free(value);                         \
            cc_log("no more buffer for %s: %d",  \
                   #dst_ptr, (int)value_l);      \
            return memccached_prune(key);        \
        }                                        \
        dst_len = ntohl(*((uint32_t*)ptr));      \
        ptr += 4; value_l-=4;                    \
        if (value_l < dst_len) {                 \
            cc_log("no more buffer for %s: %d %d",\
                 #dst_ptr, (int)value_l, (int) dst_len);\
            free(value);                         \
            return memccached_prune(key);        \
        }                                        \
        dst_ptr = ptr;                           \
        ptr += dst_len; value_l-=dst_len;        \
    } while(0)

    PROCESS_ONE_BUFFER(*obj, *obj_len);
    PROCESS_ONE_BUFFER(*stderr, *stderr_len);
    PROCESS_ONE_BUFFER(*dia, *dia_len);
    PROCESS_ONE_BUFFER(*dep, *dep_len);

#undef PROCESS_ONE_BUFFER

   return value; /* caller must free this when done with the ptrs */
}
void memccached_free(void *blob)
{
    free(blob);
}
int memccached_release(void)
{
    memcached_free(memc);
    return 1;
}

#endif // HAVE_LIBMEMCACHED
