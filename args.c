/*
 * Copyright (C) 2002 Andrew Tridgell
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "ccache.h"

#include <stdlib.h>
#include <string.h>

ARGS *args_init(int init_argc, char **init_args)
{
	ARGS *args;
	int i;
	args = (ARGS *)x_malloc(sizeof(ARGS));
	args->argc = 0;
	args->argv = (char **)x_malloc(sizeof(char *));
	args->argv[0] = NULL;
	for (i=0;i<init_argc;i++) {
		args_add(args, init_args[i]);
	}
	return args;
}


void args_add(ARGS *args, const char *s)
{
	args->argv = (char**)x_realloc(args->argv, (args->argc + 2) * sizeof(char *));
	args->argv[args->argc] = x_strdup(s);
	args->argc++;
	args->argv[args->argc] = NULL;
}

/* pop the last element off the args list */
void args_pop(ARGS *args, int n)
{
	while (n--) {
		args->argc--;
		free(args->argv[args->argc]);
		args->argv[args->argc] = NULL;
	}
}

/* remove the first element of the argument list */
void args_remove_first(ARGS *args)
{
	free(args->argv[0]);
	memmove(&args->argv[0],
		&args->argv[1],
		args->argc * sizeof(args->argv[0]));
	args->argc--;
}

/* add an argument into the front of the argument list */
void args_add_prefix(ARGS *args, const char *s)
{
	args->argv = (char**)x_realloc(args->argv, (args->argc + 2) * sizeof(char *));
	memmove(&args->argv[1], &args->argv[0],
		(args->argc+1) * sizeof(args->argv[0]));
	args->argv[0] = x_strdup(s);
	args->argc++;
}

/* strip any arguments beginning with the specified prefix */
void args_strip(ARGS *args, const char *prefix)
{
	int i;
	for (i=0; i<args->argc; ) {
		if (strncmp(args->argv[i], prefix, strlen(prefix)) == 0) {
			free(args->argv[i]);
			memmove(&args->argv[i],
				&args->argv[i+1],
				args->argc * sizeof(args->argv[i]));
			args->argc--;
		} else {
			i++;
		}
	}
}
