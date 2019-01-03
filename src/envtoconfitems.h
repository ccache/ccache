#ifndef ENVTOCONFITEMS_H
#define ENVTOCONFITEMS_H

#include "system.h"

struct env_to_conf_item {
	const char *env_name;
	const char *conf_name;
};

const struct env_to_conf_item *envtoconfitems_get(const char *str, size_t len);
size_t envtoconfitems_count(void);

#endif
