#ifndef TEST_UTIL_H
#define TEST_UTIL_H

bool path_exists(const char *path);
bool is_symlink(const char *path);
void create_file(const char *path, const char *content);

#endif
