#ifndef RESULT_H
#define RESULT_H

#include "conf.h"

extern const char RESULT_MAGIC[4];
#define RESULT_VERSION 1

struct result_files;

struct result_files *result_files_init(void);
void result_files_add(
	struct result_files *c, const char *path, const char *suffix);
void result_files_free(struct result_files *c);

bool result_get(const char *path, struct result_files *list);
bool result_put(const char *path, struct result_files *list);
bool result_dump(const char *path, FILE *stream);

#endif
