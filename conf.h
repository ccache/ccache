#ifndef CONF_H
#define CONF_H

#include "system.h"

struct conf {
	char *base_dir;
	char *cache_dir;
	unsigned cache_dir_levels;
	char *compiler;
	char *compiler_check;
	bool compression;
	unsigned compression_level;
	char *cpp_extension;
	bool direct_mode;
	bool disable;
	char *extra_files_to_hash;
	bool hard_link;
	bool hash_dir;
	char *ignore_headers_in_manifest;
	char *log_file;
	unsigned max_files;
	uint64_t max_size;
	char *memcached_conf;
	bool memcached_only;
	char *path;
	char *prefix_command;
	bool read_only;
	bool read_only_direct;
	bool read_only_memcached;
	bool recache;
	bool run_second_cpp;
	unsigned sloppiness;
	bool stats;
	char *temporary_dir;
	unsigned umask;
	bool unify;

	const char **item_origins;
};

struct conf *conf_create(void);
void conf_free(struct conf *conf);
bool conf_read(struct conf *conf, const char *path, char **errmsg);
bool conf_update_from_environment(struct conf *conf, char **errmsg);
bool conf_set_value_in_file(const char *path, const char *key,
                            const char *value, char **errmsg);
bool conf_print_items(struct conf *conf,
                      void (*printer)(const char *descr, const char *origin,
                                      void *context),
                      void *context);

#endif
