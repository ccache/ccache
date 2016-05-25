/*
 * Copyright (C) 2010-2016 Joel Rosdahl
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

/*
 * Supported file extensions and corresponding languages (as in parameter to
 * the -x option).
 */
static const struct {
	const char *extension;
	const char *language;
} extensions[] = {
	{".c",   "c"},
	{".C",   "c++"},
	{".cc",  "c++"},
	{".CC",  "c++"},
	{".cp",  "c++"},
	{".CP",  "c++"},
	{".cpp", "c++"},
	{".CPP", "c++"},
	{".cxx", "c++"},
	{".CXX", "c++"},
	{".c++", "c++"},
	{".C++", "c++"},
	{".m",   "objective-c"},
	{".M",   "objective-c++"},
	{".mm",  "objective-c++"},
	{".sx",  "assembler-with-cpp"},
	{".S",   "assembler-with-cpp"},
	/* Preprocessed: */
	{".i",   "cpp-output"},
	{".ii",  "c++-cpp-output"},
	{".mi",  "objective-c-cpp-output"},
	{".mii", "objective-c++-cpp-output"},
	{".s",   "assembler"},
	/* Header file (for precompilation): */
	{".h",   "c-header"},
	{".H",   "c++-header"},
	{".h++", "c++-header"},
	{".H++", "c++-header"},
	{".hh",  "c++-header"},
	{".HH",  "c++-header"},
	{".hp",  "c++-header"},
	{".HP",  "c++-header"},
	{".hpp", "c++-header"},
	{".HPP", "c++-header"},
	{".hxx", "c++-header"},
	{".HXX", "c++-header"},
	{".tcc", "c++-header"},
	{".TCC", "c++-header"},
	{".cu",  "cuda"},
	{".ic",  "cuda-output"},
	/* Fixed form Fortran without preprocessing */
	{".f",   "f77"},
	{".for", "f77"},
	{".ftn", "f77"},
	/* Fixed form Fortran with traditional preprocessing */
	{".F",   "f77-cpp-input"},
	{".FOR", "f77-cpp-input"},
	{".fpp", "f77-cpp-input"},
	{".FPP", "f77-cpp-input"},
	{".FTN", "f77-cpp-input"},
	/* Free form Fortran without preprocessing */
	/* could generate modules, ignore for now!
	{".f90", "f95"},
	{".f95", "f95"},
	{".f03", "f95"},
	{".f08", "f95"},
	*/
	/* Free form Fortran with traditional preprocessing */
  /* could generate modules, ignore for now!
	{".F90", "f95-cpp-input"},
	{".F95", "f95-cpp-input"},
	{".F03", "f95-cpp-input"},
	{".F08", "f95-cpp-input"},
	*/
	{NULL,  NULL}
};

/*
 * Supported languages and corresponding preprocessed languages.
 */
static const struct {
	const char *language;
	const char *p_language;
} languages[] = {
	{"c",                        "cpp-output"},
	{"cpp-output",               "cpp-output"},
	{"c-header",                 "cpp-output"},
	{"c++",                      "c++-cpp-output"},
	{"c++-cpp-output",           "c++-cpp-output"},
	{"c++-header",               "c++-cpp-output"},
	{"objective-c",              "objective-c-cpp-output"},
	{"objective-c-header",       "objective-c-cpp-output"},
	{"objc-cpp-output",          "objective-c-cpp-output"},
	{"objective-c-cpp-output",   "objective-c-cpp-output"},
	{"objective-c++",            "objective-c++-cpp-output"},
	{"objc++-cpp-output",        "objective-c++-cpp-output"},
	{"objective-c++-header",     "objective-c++-cpp-output"},
	{"objective-c++-cpp-output", "objective-c++-cpp-output"},
	{"cuda",                     "cuda-output"},
	{"assembler-with-cpp",       "assembler"},
	{"assembler",                "assembler"},
	{"f77-cpp-input",            "f77"},
	{"f77",                      "f77"},
	/* could generate module files, ignore for now!
	{"f95-cpp-input",            "f95"},
	{"f95",                      "f95"},
	*/
	{NULL,  NULL}
};

/*
 * Guess the language of a file based on its extension. Returns NULL if the
 * extension is unknown.
 */
const char *
language_for_file(const char *fname)
{
	int i;
	const char *p;

	p = get_extension(fname);
	for (i = 0; extensions[i].extension; i++) {
		if (str_eq(p, extensions[i].extension)) {
			return extensions[i].language;
		}
	}
	return NULL;
}

/*
 * Return the preprocessed language for a given language, or NULL if unknown.
 */
const char *
p_language_for_language(const char *language)
{
	int i;

	if (!language) {
		return NULL;
	}
	for (i = 0; languages[i].language; ++i) {
		if (str_eq(language, languages[i].language)) {
			return languages[i].p_language;
		}
	}
	return NULL;
}

/*
 * Return the default file extension (including dot) for a language, or NULL if
 * unknown.
 */
const char *
extension_for_language(const char *language)
{
	int i;

	if (!language) {
		return NULL;
	}
	for (i = 0; extensions[i].extension; i++) {
		if (str_eq(language, extensions[i].language)) {
			return extensions[i].extension;
		}
	}
	return NULL;
}

bool
language_is_supported(const char *language)
{
	return p_language_for_language(language) != NULL;
}

bool
language_is_preprocessed(const char *language)
{
	return str_eq(language, p_language_for_language(language));
}
