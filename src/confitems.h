#ifndef CONFITEMS_H
#define CONFITEMS_H

#include "system.h"

typedef bool (*conf_item_parser)(const char *str, void *result, char **errmsg);
typedef bool (*conf_item_verifier)(const void *value, char **errmsg);
typedef char *(*conf_item_formatter)(const void *value);

struct conf_item {
	const char *name;
	size_t number;
	size_t offset;
	conf_item_parser parser;
	conf_item_formatter formatter;
	conf_item_verifier verifier;
};

bool confitem_parse_bool(const char *str, void *result, char **errmsg);
char *confitem_format_bool(const void *value);

bool confitem_parse_env_string(const char *str, void *result, char **errmsg);
char *confitem_format_env_string(const void *value);

bool confitem_parse_double(const char *str, void *result, char **errmsg);
char *confitem_format_double(const void *value);

bool confitem_parse_size(const char *str, void *result, char **errmsg);
char *confitem_format_size(const void *value);

bool confitem_parse_sloppiness(const char *str, void *result, char **errmsg);
char *confitem_format_sloppiness(const void *value);

bool confitem_parse_string(const char *str, void *result, char **errmsg);
char *confitem_format_string(const void *value);

bool confitem_parse_umask(const char *str, void *result, char **errmsg);
char *confitem_format_umask(const void *value);

bool confitem_parse_unsigned(const char *str, void *result, char **errmsg);
char *confitem_format_unsigned(const void *value);

bool confitem_verify_absolute_path(const void *value, char **errmsg);
bool confitem_verify_dir_levels(const void *value, char **errmsg);

const struct conf_item *confitems_get(const char *str, size_t len);
size_t confitems_count(void);

#endif
