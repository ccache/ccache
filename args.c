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

struct args *
args_init(int init_argc, char **init_args)
{
	struct args *args;
	int i;
	args = (struct args *)x_malloc(sizeof(struct args));
	args->argc = 0;
	args->argv = (char **)x_malloc(sizeof(char *));
	args->argv[0] = NULL;
	for (i=0;i<init_argc;i++) {
		args_add(args, init_args[i]);
	}
	return args;
}

struct args *
args_init_from_string(const char *command)
{
	struct args *args;
	char *p = x_strdup(command);
	char *q = p;
	char *word;

	args = args_init(0, NULL);
	while ((word = strtok(q, " "))) {
		args_add(args, word);
		q = NULL;
	}

	free(p);
	return args;
}

struct args *
args_copy(struct args *args)
{
	return args_init(args->argc, args->argv);
}

void
args_free(struct args *args)
{
	int i;
	for (i = 0; i < args->argc; ++i) {
		if (args->argv[i]) {
			free(args->argv[i]);
		}
	}
	free(args->argv);
	free(args);
}

void
args_add(struct args *args, const char *s)
{
	args->argv = (char**)x_realloc(args->argv, (args->argc + 2) * sizeof(char *));
	args->argv[args->argc] = x_strdup(s);
	args->argc++;
	args->argv[args->argc] = NULL;
}

/* pop the last element off the args list */
void
args_pop(struct args *args, int n)
{
	while (n--) {
		args->argc--;
		free(args->argv[args->argc]);
		args->argv[args->argc] = NULL;
	}
}

/* remove the first element of the argument list */
void
args_remove_first(struct args *args)
{
	free(args->argv[0]);
	memmove(&args->argv[0],
		&args->argv[1],
		args->argc * sizeof(args->argv[0]));
	args->argc--;
}

/* add an argument into the front of the argument list */
void
args_add_prefix(struct args *args, const char *s)
{
	args->argv = (char**)x_realloc(args->argv, (args->argc + 2) * sizeof(char *));
	memmove(&args->argv[1], &args->argv[0],
		(args->argc+1) * sizeof(args->argv[0]));
	args->argv[0] = x_strdup(s);
	args->argc++;
}

/* strip any arguments beginning with the specified prefix */
void
args_strip(struct args *args, const char *prefix)
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

/*
 * Format args to a space-separated string. Does not quote spaces. Caller
 * frees.
 */
char *
args_to_string(struct args *args)
{
	char *result;
	char **p;
	unsigned size = 0;
	int pos;
	for (p = args->argv; *p; p++) {
		size += strlen(*p) + 1;
	}
	result = x_malloc(size + 1);
	pos = 0;
	for (p = args->argv; *p; p++) {
		pos += sprintf(&result[pos], "%s ", *p);
	}
	result[pos - 1] = '\0';
	return result;
}

/* Returns 1 if args1 equals args2, else 0. */
int
args_equal(struct args *args1, struct args *args2)
{
	int i;
	if (args1->argc != args2->argc) {
		return 0;
	}
	for (i = 0; i < args1->argc; i++) {
		if (strcmp(args1->argv[i], args2->argv[i]) != 0) {
			return 0;
		}
	}
	return 1;
}

