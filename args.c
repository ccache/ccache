/*
  convenient routines for argument list handling

   Copyright (C) Andrew Tridgell 2002
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

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

void args_pop(ARGS *args, int n)
{
	while (n--) {
		args->argc--;
		free(args->argv[args->argc]);
		args->argv[args->argc] = NULL;
	}
}
