#ifndef CONFITEMS_H
#define CONFITEMS_H

#include "system.h"

typedef bool (*conf_item_parser)(const char *str, void *result, char **errmsg);
typedef bool (*conf_item_verifier)(void *value, char **errmsg);
typedef char *(*conf_item_formatter)(void *value);

struct conf_item {
	const char *name;
	size_t number;
	size_t offset;
	conf_item_parser parser;
	conf_item_formatter formatter;
	conf_item_verifier verifier;
};

bool confitem_parse_bool(const char *str, void *result, char **errmsg);
char *confitem_format_bool(void *value);

bool confitem_parse_env_string(const char *str, void *result, char **errmsg);
char *confitem_format_env_string(void *value);

bool confitem_parse_float(const char *str, void *result, char **errmsg);
char *confitem_format_float(void *value);

bool confitem_parse_size(const char *str, void *result, char **errmsg);
char *confitem_format_size(void *value);

bool confitem_parse_sloppiness(const char *str, void *result, char **errmsg);
char *confitem_format_sloppiness(void *value);

bool confitem_parse_string(const char *str, void *result, char **errmsg);
char *confitem_format_string(void *value);

bool confitem_parse_umask(const char *str, void *result, char **errmsg);
char *confitem_format_umask(void *value);

bool confitem_parse_unsigned(const char *str, void *result, char **errmsg);
char *confitem_format_unsigned(void *value);

bool confitem_verify_absolute_path(void *value, char **errmsg);
bool confitem_verify_dir_levels(void *value, char **errmsg);

const struct conf_item *confitems_get(const char *str, size_t len);
size_t confitems_count(void);

#endif
