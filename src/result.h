#ifndef RESULT_H
#define RESULT_H

#include "conf.h"

#define RESULT_VERSION 1

struct filelist *filelist_init(void);
int filelist_add(struct filelist *c, const char *path, const char *suffix);
void filelist_free(struct filelist *c);

bool result_get(const char *path, struct filelist *list);
bool result_put(const char *path, struct filelist *list, int compression_level);
bool result_dump(const char *path, FILE *stream);

#endif
