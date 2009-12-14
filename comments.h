#ifndef COMMENTS_H
#define COMMENTS_H

#include "mdfour.h"

void hash_string_ignoring_comments(
	struct mdfour *hash, const char *str, size_t len);
int hash_file_ignoring_comments(struct mdfour *hash, const char *path);

#endif
