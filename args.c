#include "ccache.h"

ARGS *args_init(void)
{
	ARGS *args;
	args = malloc(sizeof(ARGS));
	args->argc = 0;
	args->argv = malloc(sizeof(char *));
	args->argv[0] = NULL;
	return args;
}


void args_add(ARGS *args, const char *s)
{
	args->argv = realloc(args->argv, (args->argc + 2) * sizeof(char *));
	args->argv[args->argc] = strdup(s);
	args->argc++;
	args->argv[args->argc] = NULL;
}

