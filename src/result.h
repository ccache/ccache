#ifndef RESULT_H
#define RESULT_H

#include "conf.h"

#define RESULT_VERSION 1

struct filelist *create_empty_filelist(void);
int add_file_to_filelist(struct filelist *c, const char *path, const char *suffix);
void free_filelist(struct filelist *c);

bool cache_get(const char *cache_path, struct filelist *list);
bool cache_put(const char *cache_path, struct filelist *list, int compression_level);
bool cache_dump(const char *cache_path, FILE *stream);

#endif
