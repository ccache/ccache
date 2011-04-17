/*
 * Copyright (C) 2010 Joel Rosdahl
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
#include "compopt.h"

#define TOO_HARD         (1 << 0)
#define TOO_HARD_DIRECT  (1 << 1)
#define TAKES_ARG        (1 << 2)
#define TAKES_CONCAT_ARG (1 << 3)
#define TAKES_PATH       (1 << 4)
#define AFFECTS_CPP      (1 << 5)

struct compopt {
	const char *name;
	int type;
};

static const struct compopt compopts[] = {
	{"--coverage",      TOO_HARD},
	{"--param",         TAKES_ARG},
	{"-A",              TAKES_ARG},
	{"-D",              AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG},
	{"-E",              TOO_HARD},
	{"-F",              AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
	{"-G",              TAKES_ARG},
	{"-I",              AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
	{"-L",              TAKES_ARG},
	{"-M",              TOO_HARD},
	{"-MF",             TAKES_ARG},
	{"-MM",             TOO_HARD},
	{"-MQ",             TAKES_ARG},
	{"-MT",             TAKES_ARG},
	{"-U",              AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG},
	{"-V",              TAKES_ARG},
	{"-Xassembler",     TAKES_ARG},
	{"-Xlinker",        TAKES_ARG},
	{"-Xpreprocessor",  TOO_HARD_DIRECT | TAKES_ARG},
	{"-aux-info",       TAKES_ARG},
	{"-b",              TAKES_ARG},
	{"-fbranch-probabilities", TOO_HARD},
	{"-fprofile-arcs",  TOO_HARD},
	{"-fprofile-generate", TOO_HARD},
	{"-fprofile-use",   TOO_HARD},
	{"-frepo",          TOO_HARD},
	{"-ftest-coverage", TOO_HARD},
	{"-idirafter",      AFFECTS_CPP | TAKES_ARG | TAKES_PATH},
	{"-iframework",     AFFECTS_CPP | TAKES_ARG | TAKES_CONCAT_ARG | TAKES_PATH},
	{"-imacros",        AFFECTS_CPP | TAKES_ARG | TAKES_PATH},
	{"-imultilib",      AFFECTS_CPP | TAKES_ARG | TAKES_PATH},
	{"-include",        AFFECTS_CPP | TAKES_ARG | TAKES_PATH},
	{"-install_name",   TAKES_ARG}, /* Darwin linker option */
	{"-iprefix",        AFFECTS_CPP | TAKES_ARG | TAKES_PATH},
	{"-iquote",         AFFECTS_CPP | TAKES_ARG | TAKES_PATH},
	{"-isysroot",       AFFECTS_CPP | TAKES_ARG | TAKES_PATH},
	{"-isystem",        AFFECTS_CPP | TAKES_ARG | TAKES_PATH},
	{"-iwithprefix",    AFFECTS_CPP | TAKES_ARG | TAKES_PATH},
	{"-iwithprefixbefore", AFFECTS_CPP | TAKES_ARG | TAKES_PATH},
	{"-nostdinc",       AFFECTS_CPP},
	{"-nostdinc++",     AFFECTS_CPP},
	{"-save-temps",     TOO_HARD},
	{"-u",              TAKES_ARG},
};

static int
compare_compopts(const void *key1, const void *key2)
{
	const struct compopt *opt1 = (const struct compopt *)key1;
	const struct compopt *opt2 = (const struct compopt *)key2;
	return strcmp(opt1->name, opt2->name);
}

static const struct compopt *
find(const char *option)
{
	struct compopt key;
	key.name = option;
	return bsearch(
		&key, compopts, sizeof(compopts) / sizeof(compopts[0]),
		sizeof(compopts[0]), compare_compopts);
}

/* Runs fn on the first two characters of option. */
bool
compopt_short(bool (*fn)(const char *), const char *option)
{
	char *short_opt = x_strndup(option, 2);
	bool retval = fn(short_opt);
	free(short_opt);
	return retval;
}

/* For test purposes. */
bool
compopt_verify_sortedness(void)
{
	size_t i;
	for (i = 1; i < sizeof(compopts)/sizeof(compopts[0]); i++) {
		if (strcmp(compopts[i-1].name, compopts[i].name) >= 0) {
			fprintf(stderr,
			        "compopt_verify_sortedness: %s >= %s\n",
			        compopts[i-1].name,
			        compopts[i].name);
			return false;
		}
	}
	return true;
}

bool
compopt_affects_cpp(const char *option)
{
	const struct compopt *co = find(option);
	return co && (co->type & AFFECTS_CPP);
}

bool
compopt_too_hard(const char *option)
{
	const struct compopt *co = find(option);
	return co && (co->type & TOO_HARD);
}

bool
compopt_too_hard_for_direct_mode(const char *option)
{
	const struct compopt *co = find(option);
	return co && (co->type & TOO_HARD_DIRECT);
}

bool
compopt_takes_path(const char *option)
{
	const struct compopt *co = find(option);
	return co && (co->type & TAKES_PATH);
}

bool
compopt_takes_arg(const char *option)
{
	const struct compopt *co = find(option);
	return co && (co->type & TAKES_ARG);
}
