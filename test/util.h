#ifndef TEST_UTIL_H
#define TEST_UTIL_H

int path_exists(const char *path);
int is_symlink(const char *path);
void create_file(const char *path, const char *content);
char *read_file(const char *path);

#endif
