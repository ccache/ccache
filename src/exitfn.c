// Copyright (C) 2010-2018 Joel Rosdahl and other contributors
//
// See doc/AUTHORS.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "ccache.h"

struct exit_function {
	void (*function)(void *);
	void *context;
	struct exit_function *next;
};

struct nullary_exit_function {
	void (*function)(void);
};

static struct exit_function *exit_functions;

static void
call_nullary_exit_function(void *context)
{
	struct nullary_exit_function *p = (struct nullary_exit_function *)context;
	p->function();
	free(p);
}

// Initialize exit functions. Must be called once before exitfn_add* are used.
void
exitfn_init(void)
{
	if (atexit(exitfn_call) != 0) {
		fatal("atexit failed: %s", strerror(errno));
	}
}

// Add a nullary function to be called when ccache exits. Functions are called
// in reverse order.
void
exitfn_add_nullary(void (*function)(void))
{
	struct nullary_exit_function *p = x_malloc(sizeof(*p));
	p->function = function;
	exitfn_add(call_nullary_exit_function, p);
}

// Add a function to be called with a context parameter when ccache exits.
// Functions are called in LIFO order except when added via exitfn_add_last.
void
exitfn_add(void (*function)(void *), void *context)
{
	struct exit_function *p = x_malloc(sizeof(*p));
	p->function = function;
	p->context = context;
	p->next = exit_functions;
	exit_functions = p;
}

// Add a function to be called with a context parameter when ccache exits. In
// contrast to exitfn_add, exitfn_add_last sets up the function to be called
// last.
void
exitfn_add_last(void (*function)(void *), void *context)
{
	struct exit_function *p = x_malloc(sizeof(*p));
	p->function = function;
	p->context = context;
	p->next = NULL;

	struct exit_function **q = &exit_functions;
	while (*q) {
		q = &(*q)->next;
	}
	*q = p;
}

// Call added functions.
void
exitfn_call(void)
{
	struct exit_function *p = exit_functions;
	exit_functions = NULL;
	while (p) {
		p->function(p->context);
		struct exit_function *q = p;
		p = p->next;
		free(q);
	}
}
