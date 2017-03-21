// ccache -- a fast C/C++ compiler cache
//
// Copyright (C) 2002-2007 Andrew Tridgell
// Copyright (C) 2009-2017 Joel Rosdahl
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
#include "compopt.h"
#ifdef HAVE_GETOPT_LONG
#include <getopt.h>
#else
#include "getopt_long.h"
#endif
#include "hashtable.h"
#include "hashtable_itr.h"
#include "hashutil.h"
#include "language.h"
#include "manifest.h"

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

static const char VERSION_TEXT[] =
	MYNAME " version %s\n"
	"\n"
	"Copyright (C) 2002-2007 Andrew Tridgell\n"
	"Copyright (C) 2009-2017 Joel Rosdahl\n"
	"\n"
	"This program is free software; you can redistribute it and/or modify it under\n"
	"the terms of the GNU General Public License as published by the Free Software\n"
	"Foundation; either version 3 of the License, or (at your option) any later\n"
	"version.\n";

static const char USAGE_TEXT[] =
	"Usage:\n"
	"    " MYNAME " [options]\n"
	"    " MYNAME " compiler [compiler options]\n"
	"    compiler [compiler options]          (via symbolic link)\n"
	"\n"
	"Options:\n"
	"    -c, --cleanup         delete old files and recalculate size counters\n"
	"                          (normally not needed as this is done automatically)\n"
	"    -C, --clear           clear the cache completely (except configuration)\n"
	"    -F, --max-files=N     set maximum number of files in cache to N (use 0 for\n"
	"                          no limit)\n"
	"    -M, --max-size=SIZE   set maximum size of cache to SIZE (use 0 for no\n"
	"                          limit); available suffixes: k, M, G, T (decimal) and\n"
	"                          Ki, Mi, Gi, Ti (binary); default suffix: G\n"
	"    -o, --set-config=K=V  set configuration key K to value V\n"
	"    -p, --print-config    print current configuration options\n"
	"    -s, --show-stats      show statistics summary\n"
	"    -z, --zero-stats      zero statistics counters\n"
	"\n"
	"    -h, --help            print this help text\n"
	"    -V, --version         print version and copyright information\n"
	"\n"
	"See also <https://ccache.samba.org>.\n";

// Global configuration data.
struct conf *conf = NULL;

// Where to write configuration changes.
char *primary_config_path = NULL;

// Secondary, read-only configuration file (if any).
char *secondary_config_path = NULL;

// Current working directory taken from $PWD, or getcwd() if $PWD is bad.
char *current_working_dir = NULL;

// The original argument list.
static struct args *orig_args;

// The source file.
static char *input_file;

// The output file being compiled to.
static char *output_obj;

// The path to the dependency file (implicit or specified with -MF).
static char *output_dep;

// The path to the coverage file (implicit when using -ftest-coverage).
static char *output_cov;

// The path to the stack usage (implicit when using -fstack-usage).
static char *output_su = NULL;

// Diagnostic generation information (clang). Contains pathname if not NULL.
static char *output_dia = NULL;

// Split dwarf information (GCC 4.8 andup). Contains pathname if not NULL.
static char *output_dwo = NULL;

// Array for storing -arch options.
#define MAX_ARCH_ARGS 10
static size_t arch_args_size = 0;
static char *arch_args[MAX_ARCH_ARGS] = {NULL};

// Name (represented as a struct file_hash) of the file containing the cached
// object code.
static struct file_hash *cached_obj_hash;

// Full path to the file containing the cached object code
// (cachedir/a/b/cdef[...]-size.o).
static char *cached_obj;

// Full path to the file containing the standard error output
// (cachedir/a/b/cdef[...]-size.stderr).
static char *cached_stderr;

// Full path to the file containing the dependency information
// (cachedir/a/b/cdef[...]-size.d).
static char *cached_dep;

// Full path to the file containing the coverage information
// (cachedir/a/b/cdef[...]-size.gcno).
static char *cached_cov;

// Full path to the file containing the stack usage
// (cachedir/a/b/cdef[...]-size.su).
static char *cached_su;

// Full path to the file containing the diagnostic information (for clang)
// (cachedir/a/b/cdef[...]-size.dia).
static char *cached_dia;

// Full path to the file containing the split dwarf (for GCC 4.8 and above)
// (cachedir/a/b/cdef[...]-size.dwo).
//
// Contains NULL if -gsplit-dwarf is not given.
static char *cached_dwo;

// using_split_dwarf is true if "-gsplit-dwarf" is given to the compiler (GCC
// 4.8 and up).
bool using_split_dwarf = false;

// Full path to the file containing the manifest
// (cachedir/a/b/cdef[...]-size.manifest).
static char *manifest_path;

// Time of compilation. Used to see if include files have changed after
// compilation.
time_t time_of_compilation;

// Files included by the preprocessor and their hashes/sizes. Key: file path.
// Value: struct file_hash.
static struct hashtable *included_files = NULL;

// Uses absolute path for some include files.
static bool has_absolute_include_headers = false;

// List of headers to ignore.
static char **ignore_headers;

// Size of headers to ignore list.
static size_t ignore_headers_len;

// Is the compiler being asked to output debug info?
static bool generating_debuginfo;

// Is the compiler being asked to output dependencies?
static bool generating_dependencies;

// Is the compiler being asked to output coverage?
static bool generating_coverage;

// Is the compiler being asked to output stack usage?
static bool generating_stackusage;

// Relocating debuginfo in the format old=new.
static char *debug_prefix_map = NULL;

// Is the compiler being asked to output coverage data (.gcda) at runtime?
static bool profile_arcs;

// Name of the custom profile directory (default: object dirname).
static char *profile_dir;

// The name of the temporary preprocessed file.
static char *i_tmpfile;

// Are we compiling a .i or .ii file directly?
static bool direct_i_file;

// The name of the cpp stderr file.
static char *cpp_stderr;

// Full path to the statistics file in the subdirectory where the cached result
// belongs (<cache_dir>/<x>/stats).
char *stats_file = NULL;

// Whether the output is a precompiled header.
static bool output_is_precompiled_header = false;

// Profile generation / usage information.
static char *profile_dir = NULL;
static bool profile_use = false;
static bool profile_generate = false;

// Whether we are using a precompiled header (either via -include, #include or
// clang's -include-pch or -include-pth).
static bool using_precompiled_header = false;

// The .gch/.pch/.pth file used for compilation.
static char *included_pch_file = NULL;

// How long (in microseconds) to wait before breaking a stale lock.
unsigned lock_staleness_limit = 2000000;

enum fromcache_call_mode {
	FROMCACHE_DIRECT_MODE,
	FROMCACHE_CPP_MODE
};

struct pending_tmp_file {
	char *path;
	struct pending_tmp_file *next;
};

// Temporary files to remove at program exit.
static struct pending_tmp_file *pending_tmp_files = NULL;

#ifndef _WIN32
static sigset_t fatal_signal_set;

// PID of currently executing compiler that we have started, if any. 0 means no
// ongoing compilation.
static pid_t compiler_pid = 0;
#endif

// Note that these compiler checks are unreliable, so nothing should
// hard-depend on them.

static bool
compiler_is_clang(struct args *args)
{
	char *name = basename(args->argv[0]);
	bool result = strstr(name, "clang") != NULL;
	free(name);
	return result;
}

static bool
compiler_is_gcc(struct args *args)
{
	char *name = basename(args->argv[0]);
	bool result = strstr(name, "gcc") || strstr(name, "g++");
	free(name);
	return result;
}

static bool
compiler_is_msvc(struct args *args)
{
	char *name = basename(args->argv[0]);
	bool result = str_eq(name, "cl") || str_eq(name, "cl.exe");
	free(name);
	return result;
}

// This is a string that identifies the current "version" of the hash sum
// computed by ccache. If, for any reason, we want to force the hash sum to be
// different for the same input in a new ccache version, we can just change
// this string. A typical example would be if the format of one of the files
// stored in the cache changes in a backwards-incompatible way.
static const char HASH_PREFIX[] = "3";

static void
add_prefix(struct args *args, char *prefix_command)
{
	if (str_eq(prefix_command, "")) {
		return;
	}

	struct args *prefix = args_init(0, NULL);
	char *e = x_strdup(prefix_command);
	char *saveptr = NULL;
	for (char *tok = strtok_r(e, " ", &saveptr);
	     tok;
	     tok = strtok_r(NULL, " ", &saveptr)) {
		char *p;

		p = find_executable(tok, MYNAME);
		if (!p) {
			fatal("%s: %s", tok, strerror(errno));
		}

		args_add(prefix, p);
		free(p);
	}
	free(e);

	cc_log("Using command-line prefix %s", prefix_command);
	for (int i = prefix->argc; i != 0; i--) {
		args_add_prefix(args, prefix->argv[i-1]);
	}
	args_free(prefix);
}

// Something went badly wrong - just execute the real compiler.
static void
failed(void)
{
	assert(orig_args);

	args_strip(orig_args, "--ccache-");
	add_prefix(orig_args, conf->prefix_command);

	cc_log("Failed; falling back to running the real compiler");
	cc_log_argv("Executing ", orig_args->argv);
	exitfn_call();
	execv(orig_args->argv[0], orig_args->argv);
	fatal("execv of %s failed: %s", orig_args->argv[0], strerror(errno));
}

static const char *
temp_dir()
{
	static char *path = NULL;
	if (path) {
		return path; // Memoize
	}
	path = conf->temporary_dir;
	if (str_eq(path, "")) {
		path = format("%s/tmp", conf->cache_dir);
	}
	return path;
}

void
block_signals(void)
{
#ifndef _WIN32
	sigprocmask(SIG_BLOCK, &fatal_signal_set, NULL);
#endif
}

void
unblock_signals(void)
{
#ifndef _WIN32
	sigset_t empty;
	sigemptyset(&empty);
	sigprocmask(SIG_SETMASK, &empty, NULL);
#endif
}

static void
add_pending_tmp_file(const char *path)
{
	block_signals();
	struct pending_tmp_file *e = x_malloc(sizeof(*e));
	e->path = x_strdup(path);
	e->next = pending_tmp_files;
	pending_tmp_files = e;
	unblock_signals();
}

static void
do_clean_up_pending_tmp_files(void)
{
	struct pending_tmp_file *p = pending_tmp_files;
	while (p) {
		// Can't call tmp_unlink here since its cc_log calls aren't signal safe.
		unlink(p->path);
		p = p->next;
		// Leak p->path and p here because clean_up_pending_tmp_files needs to be
		// signal safe.
	}
}

static void
clean_up_pending_tmp_files(void)
{
	block_signals();
	do_clean_up_pending_tmp_files();
	unblock_signals();
}

#ifndef _WIN32
static void
signal_handler(int signum)
{
	// Unregister handler for this signal so that we can send the signal to
	// ourselves at the end of the handler.
	signal(signum, SIG_DFL);

	// If ccache was killed explicitly, then bring the compiler subprocess (if
	// any) with us as well.
	if (signum == SIGTERM
	    && compiler_pid != 0
	    && waitpid(compiler_pid, NULL, WNOHANG) == 0) {
		kill(compiler_pid, signum);
	}

	do_clean_up_pending_tmp_files();

	if (compiler_pid != 0) {
		// Wait for compiler subprocess to exit before we snuff it.
		waitpid(compiler_pid, NULL, 0);
	}

	// Resend signal to ourselves to exit properly after returning from the
	// handler.
	kill(getpid(), signum);
}

static void
register_signal_handler(int signum)
{
	struct sigaction act;
	memset(&act, 0, sizeof(act));
	act.sa_handler = signal_handler;
	act.sa_mask = fatal_signal_set;
#ifdef SA_RESTART
	act.sa_flags = SA_RESTART;
#endif
	sigaction(signum, &act, NULL);
}

static void
set_up_signal_handlers(void)
{
	sigemptyset(&fatal_signal_set);
	sigaddset(&fatal_signal_set, SIGINT);
	sigaddset(&fatal_signal_set, SIGTERM);
#ifdef SIGHUP
	sigaddset(&fatal_signal_set, SIGHUP);
#endif
#ifdef SIGQUIT
	sigaddset(&fatal_signal_set, SIGQUIT);
#endif

	register_signal_handler(SIGINT);
	register_signal_handler(SIGTERM);
#ifdef SIGHUP
	register_signal_handler(SIGHUP);
#endif
#ifdef SIGQUIT
	register_signal_handler(SIGQUIT);
#endif
}
#endif // _WIN32

static void
clean_up_internal_tempdir(void)
{
	time_t now = time(NULL);
	struct stat st;
	if (x_stat(conf->cache_dir, &st) != 0 || st.st_mtime + 3600 >= now) {
		// No cleanup needed.
		return;
	}

	update_mtime(conf->cache_dir);

#ifdef _WIN32
	WIN32_FIND_DATA data;
	HANDLE hFind = FindFirstFile(temp_dir(), &data);
	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			if (str_eq(data.cFileName, ".") || str_eq(data.cFileName, "..")) {
				continue;
			}
			char *path = format("%s/%s", temp_dir(), data.cFileName);
			if (x_lstat(path, &st) == 0 && st.st_mtime + 3600 < now) {
				tmp_unlink(path);
			}
			free(path);
		} while (FindNextFile(hFind, &data));
		FindClose(hFind);
	}
#else
	DIR *dir = opendir(temp_dir());
	if (!dir) {
		return;
	}

	struct dirent *entry;
	while ((entry = readdir(dir))) {
		if (str_eq(entry->d_name, ".") || str_eq(entry->d_name, "..")) {
			continue;
		}

		char *path = format("%s/%s", temp_dir(), entry->d_name);
		if (x_lstat(path, &st) == 0 && st.st_mtime + 3600 < now) {
			tmp_unlink(path);
		}
		free(path);
	}

	closedir(dir);
#endif
}

static char *
get_current_working_dir(void)
{
	if (!current_working_dir) {
		char *cwd = get_cwd();
		if (cwd) {
			current_working_dir = x_realpath(cwd);
			free(cwd);
		}
		if (!current_working_dir) {
			cc_log("Unable to determine current working directory: %s",
			       strerror(errno));
			failed();
		}
	}
	return current_working_dir;
}

// Transform a name to a full path into the cache directory, creating needed
// sublevels if needed. Caller frees.
static char *
get_path_in_cache(const char *name, const char *suffix)
{
	char *path = x_strdup(conf->cache_dir);
	for (unsigned i = 0; i < conf->cache_dir_levels; ++i) {
		char *p = format("%s/%c", path, name[i]);
		free(path);
		path = p;
	}

	char *result =
		format("%s/%s%s", path, name + conf->cache_dir_levels, suffix);
	free(path);
	return result;
}

// This function hashes an include file and stores the path and hash in the
// global included_files variable. If the include file is a PCH, cpp_hash is
// also updated. Takes over ownership of path.
static void
remember_include_file(char *path, struct mdfour *cpp_hash, bool system)
{
	size_t path_len = strlen(path);
	if (path_len >= 2 && (path[0] == '<' && path[path_len - 1] == '>')) {
		// Typically <built-in> or <command-line>.
		goto ignore;
	}

	if (str_eq(path, input_file)) {
		// Don't remember the input file.
		goto ignore;
	}

	if (system && (conf->sloppiness & SLOPPY_NO_SYSTEM_HEADERS)) {
		// Don't remember this system header.
		goto ignore;
	}

	if (hashtable_search(included_files, path)) {
		// Already known include file.
		goto ignore;
	}

#ifdef _WIN32
	// stat fails on directories on win32.
	DWORD attributes = GetFileAttributes(path);
	if (attributes != INVALID_FILE_ATTRIBUTES &&
	    attributes & FILE_ATTRIBUTE_DIRECTORY) {
		goto ignore;
	}
#endif

	struct stat st;
	if (x_stat(path, &st) != 0) {
		goto failure;
	}
	if (S_ISDIR(st.st_mode)) {
		// Ignore directory, typically $PWD.
		goto ignore;
	}
	if (!S_ISREG(st.st_mode)) {
		// Device, pipe, socket or other strange creature.
		cc_log("Non-regular include file %s", path);
		goto failure;
	}

	// Canonicalize path for comparison; clang uses ./header.h.
	char *canonical = path;
	size_t canonical_len = path_len;
	if (canonical[0] == '.' && canonical[1] == '/') {
		canonical += 2;
		canonical_len -= 2;
	}

	for (size_t i = 0; i < ignore_headers_len; i++) {
		char *ignore = ignore_headers[i];
		size_t ignore_len = strlen(ignore);
		if (ignore_len > canonical_len) {
			continue;
		}
		if (strncmp(canonical, ignore, ignore_len) == 0
		    && (ignore[ignore_len-1] == DIR_DELIM_CH
		        || canonical[ignore_len] == DIR_DELIM_CH
		        || canonical[ignore_len] == '\0')) {
			goto ignore;
		}
	}

	if (!(conf->sloppiness & SLOPPY_INCLUDE_FILE_MTIME)
	    && st.st_mtime >= time_of_compilation) {
		cc_log("Include file %s too new", path);
		goto failure;
	}

	if (!(conf->sloppiness & SLOPPY_INCLUDE_FILE_CTIME)
	    && st.st_ctime >= time_of_compilation) {
		cc_log("Include file %s ctime too new", path);
		goto failure;
	}

	// Let's hash the include file content.
	struct mdfour fhash;
	hash_start(&fhash);

	bool is_pch = is_precompiled_header(path);
	if (is_pch) {
		if (!hash_file(&fhash, path)) {
			goto failure;
		}
		struct file_hash pch_hash;
		hash_result_as_bytes(&fhash, pch_hash.hash);
		pch_hash.size = (uint32_t)fhash.totalN;
		hash_delimiter(cpp_hash, "pch_hash");
		hash_buffer(cpp_hash, pch_hash.hash, sizeof(pch_hash.hash));
	}

	if (conf->direct_mode) {
		if (!is_pch) { // else: the file has already been hashed.
			char *source = NULL;
			size_t size;
			if (st.st_size > 0) {
				if (!read_file(path, st.st_size, &source, &size)) {
					goto failure;
				}
			} else {
				source = x_strdup("");
				size = 0;
			}

			int result = hash_source_code_string(conf, &fhash, source, size, path);
			free(source);
			if (result & HASH_SOURCE_CODE_ERROR
			    || result & HASH_SOURCE_CODE_FOUND_TIME) {
				goto failure;
			}
		}

		struct file_hash *h = x_malloc(sizeof(*h));
		hash_result_as_bytes(&fhash, h->hash);
		h->size = (uint32_t)fhash.totalN;
		hashtable_insert(included_files, path, h);
	} else {
		free(path);
	}

	return;

failure:
	if (conf->direct_mode) {
		cc_log("Disabling direct mode");
		conf->direct_mode = false;
	}
	// Fall through.
ignore:
	free(path);
}

// Make a relative path from current working directory to path if path is under
// the base directory. Takes over ownership of path. Caller frees.
char *
make_relative_path(char *path)
{
	if (str_eq(conf->base_dir, "") || !str_startswith(path, conf->base_dir)) {
		return path;
	}

#ifdef _WIN32
	if (path[0] == '/') {
		char *p = x_strdup(path+1); // Skip leading slash.
		free(path);
		path = p;
	}
#endif

	// x_realpath only works for existing paths, so if path doesn't exist, try
	// dirname(path) and assemble the path afterwards. We only bother to try
	// canonicalizing one of these two paths since a compiler path argument
	// typically only makes sense if path or dirname(path) exists.
	char *path_suffix = NULL;
	struct stat st;
	if (stat(path, &st) != 0) {
		// path doesn't exist.
		char *dir = dirname(path);
		if (stat(dir, &st) != 0) {
			// And neither does its parent directory, so no action to take.
			free(dir);
			return path;
		}
		free(dir);
		path_suffix = basename(path);
		char *p = path;
		path = dirname(path);
		free(p);
	}

	char *canon_path = x_realpath(path);
	if (canon_path) {
		free(path);
		char *relpath = get_relative_path(get_current_working_dir(), canon_path);
		free(canon_path);
		if (path_suffix) {
			path = format("%s/%s", relpath, path_suffix);
			free(relpath);
			free(path_suffix);
			return path;
		} else {
			return relpath;
		}
	} else {
		// path doesn't exist, so leave it as it is.
		free(path_suffix);
		return path;
	}
}

// This function reads and hashes a file. While doing this, it also does these
// things:
//
// - Makes include file paths for which the base directory is a prefix relative
//   when computing the hash sum.
// - Stores the paths and hashes of included files in the global variable
//   included_files.
static bool
process_preprocessed_file(struct mdfour *hash, const char *path)
{
	char *data;
	size_t size;
	if (!read_file(path, 0, &data, &size)) {
		return false;
	}

	ignore_headers = NULL;
	ignore_headers_len = 0;
	if (!str_eq(conf->ignore_headers_in_manifest, "")) {
		char *header, *p, *q, *saveptr = NULL;
		p = x_strdup(conf->ignore_headers_in_manifest);
		q = p;
		while ((header = strtok_r(q, PATH_DELIM, &saveptr))) {
			ignore_headers = x_realloc(ignore_headers,
			                           (ignore_headers_len+1) * sizeof(char *));
			ignore_headers[ignore_headers_len++] = x_strdup(header);
			q = NULL;
		}
		free(p);
	}

	if (!included_files) {
		included_files = create_hashtable(1000, hash_from_string, strings_equal);
	}

	// Bytes between p and q are pending to be hashed.
	char *p = data;
	char *q = data;
	char *end = data + size;

	// There must be at least 7 characters (# 1 "x") left to potentially find an
	// include file path.
	while (q < end - 7) {
		// Check if we look at a line containing the file name of an included file.
		// At least the following formats exist (where N is a positive integer):
		//
		// GCC:
		//
		//   # N "file"
		//   # N "file" N
		//   #pragma GCC pch_preprocess "file"
		//
		// HP's compiler:
		//
		//   #line N "file"
		//
		// AIX's compiler:
		//
		//   #line N "file"
		//   #line N
		//
		// Note that there may be other lines starting with '#' left after
		// preprocessing as well, for instance "#    pragma".
		if (q[0] == '#'
		    // GCC:
		    && ((q[1] == ' ' && q[2] >= '0' && q[2] <= '9')
		        // GCC precompiled header:
		        || (q[1] == 'p'
		            && str_startswith(&q[2], "ragma GCC pch_preprocess "))
		        // HP/AIX:
		        || (q[1] == 'l' && q[2] == 'i' && q[3] == 'n' && q[4] == 'e'
		            && q[5] == ' '))
		    && (q == data || q[-1] == '\n')) {
			// Workarounds for preprocessor linemarker bugs in GCC version 6.
			if (q[2] == '3') {
				if (str_startswith(q, "# 31 \"<command-line>\"\n")) {
					// Bogus extra line with #31, after the regular #1: Ignore the whole
					// line, and continue parsing.
					hash_buffer(hash, p, q - p);
					while (q < end && *q != '\n') {
						q++;
					}
					q++;
					p = q;
					continue;
				} else if (str_startswith(q, "# 32 \"<command-line>\" 2\n")) {
					// Bogus wrong line with #32, instead of regular #1: Replace the line
					// number with the usual one.
					hash_buffer(hash, p, q - p);
					q += 1;
					q[0] = '#';
					q[1] = ' ';
					q[2] = '1';
					p = q;
				}
			}

			while (q < end && *q != '"' && *q != '\n') {
				q++;
			}
			if (q < end && *q == '\n') {
				// A newline before the quotation mark -> no match.
				continue;
			}
			q++;
			if (q >= end) {
				cc_log("Failed to parse included file path");
				free(data);
				return false;
			}
			// q points to the beginning of an include file path
			hash_buffer(hash, p, q - p);
			p = q;
			while (q < end && *q != '"') {
				q++;
			}
			// Look for preprocessor flags, after the "filename".
			bool system = false;
			char *r = q + 1;
			while (r < end && *r != '\n') {
				if (*r == '3') { // System header.
					system = true;
				}
				r++;
			}
			// p and q span the include file path.
			char *inc_path = x_strndup(p, q - p);
			if (!has_absolute_include_headers) {
				has_absolute_include_headers = is_absolute_path(inc_path);
			}
			inc_path = make_relative_path(inc_path);

			bool should_hash_inc_path = true;
			if (!conf->hash_dir) {
				char *cwd = gnu_getcwd();
				if (str_startswith(inc_path, cwd) && str_endswith(inc_path, "//")) {
					// When compiling with -g or similar, GCC adds the absolute path to
					// CWD like this:
					//
					//   # 1 "CWD//"
					//
					// If the user has opted out of including the CWD in the hash, don't
					// hash it. See also how debug_prefix_map is handled.
					should_hash_inc_path = false;
				}
				free(cwd);
			}
			if (should_hash_inc_path) {
				hash_string(hash, inc_path);
			}

			remember_include_file(inc_path, hash, system);
			p = q; // Everything of interest between p and q has been hashed now.
		} else if (q[0] == '.' && q[1] == 'i' && q[2] == 'n' && q[3] == 'c'
		           && q[4] == 'b' && q[5] == 'i' && q[6] == 'n') {
			// An assembler .incbin statement (which could be part of inline
			// assembly) refers to an external file. If the file changes, the hash
			// should change as well, but finding out what file to hash is too hard
			// for ccache, so just bail out.
			cc_log("Found unsupported .inc" "bin directive in source code %s", path);
			stats_update(STATS_UNSUPPORTED_DIRECTIVE);
			failed();
		} else {
			q++;
		}
	}

	hash_buffer(hash, p, (end - p));
	free(data);

	// Explicitly check the .gch/.pch/.pth file, Clang does not include any
	// mention of it in the preprocessed output.
	if (included_pch_file) {
		char *path = x_strdup(included_pch_file);
		path = make_relative_path(path);
		hash_string(hash, path);
		remember_include_file(path, hash, false);
	}

	return true;
}

// Replace absolute paths with relative paths in the provided dependency file.
static void
use_relative_paths_in_depfile(const char *depfile)
{
	if (str_eq(conf->base_dir, "")) {
		cc_log("Base dir not set, skip using relative paths");
		return; // nothing to do
	}
	if (!has_absolute_include_headers) {
		cc_log("No absolute path for included files found, skip using relative"
		       " paths");
		return; // nothing to do
	}

	FILE *f;
	f = fopen(depfile, "r");
	if (!f) {
		cc_log("Cannot open dependency file: %s (%s)", depfile, strerror(errno));
		return;
	}

	char *tmp_file = format("%s.tmp", depfile);
	FILE *tmpf = create_tmp_file(&tmp_file, "w");

	bool result = false;
	char buf[10000];
	while (fgets(buf, sizeof(buf), f) && !ferror(tmpf)) {
		char *saveptr;
		char *token = strtok_r(buf, " \t", &saveptr);
		while (token) {
			char *relpath;
			if (is_absolute_path(token) && str_startswith(token, conf->base_dir)) {
				relpath = make_relative_path(x_strdup(token));
				result = true;
			} else {
				relpath = token;
			}
			if (token != buf) { // This is a dependency file.
				fputc(' ', tmpf);
			}
			fputs(relpath, tmpf);
			if (relpath != token) {
				free(relpath);
			}
			token = strtok_r(NULL, " \t", &saveptr);
		}
	}

	if (ferror(f)) {
		cc_log("Error reading dependency file: %s, skip relative path usage",
		       depfile);
		result = false;
		goto out;
	}
	if (ferror(tmpf)) {
		cc_log("Error writing temporary dependency file: %s, skip relative path"
		       " usage", tmp_file);
		result = false;
		goto out;
	}

out:
	fclose(tmpf);
	fclose(f);
	if (result) {
		if (x_rename(tmp_file, depfile) != 0) {
			cc_log("Error renaming dependency file: %s -> %s (%s), skip relative"
			       " path usage", tmp_file, depfile, strerror(errno));
			result = false;
		} else {
			cc_log("Renamed dependency file: %s -> %s", tmp_file, depfile);
		}
	}
	if (!result) {
		cc_log("Removing temporary dependency file: %s", tmp_file);
		x_unlink(tmp_file);
	}
	free(tmp_file);
}

// Copy or link a file to the cache.
static void
put_file_in_cache(const char *source, const char *dest)
{
	assert(!conf->read_only);
	assert(!conf->read_only_direct);

	bool do_link = conf->hard_link && !conf->compression;
	if (do_link) {
		x_unlink(dest);
		int ret = link(source, dest);
		if (ret != 0) {
			cc_log("Failed to link %s to %s: %s", source, dest, strerror(errno));
			cc_log("Falling back to copying");
			do_link = false;
		}
	}
	if (!do_link) {
		int ret = copy_file(
			source, dest, conf->compression ? conf->compression_level : 0);
		if (ret != 0) {
			cc_log("Failed to copy %s to %s: %s", source, dest, strerror(errno));
			stats_update(STATS_ERROR);
			failed();
		}
	}

	cc_log("Stored in cache: %s -> %s", source, dest);

	struct stat st;
	if (x_stat(dest, &st) != 0) {
		stats_update(STATS_ERROR);
		failed();
	}
	stats_update_size(file_size(&st), 1);
}

// Copy or link a file from the cache.
static void
get_file_from_cache(const char *source, const char *dest)
{
	int ret;
	bool do_link = conf->hard_link && !file_is_compressed(source);
	if (do_link) {
		x_unlink(dest);
		ret = link(source, dest);
	} else {
		ret = copy_file(source, dest, 0);
	}

	if (ret == -1) {
		if (errno == ENOENT || errno == ESTALE) {
			// Someone removed the file just before we began copying?
			cc_log("Cache file %s just disappeared from cache", source);
			stats_update(STATS_MISSING);
		} else {
			cc_log("Failed to %s %s to %s: %s",
			       do_link ? "link" : "copy",
			       source,
			       dest,
			       strerror(errno));
			stats_update(STATS_ERROR);
		}

		// If there was trouble getting a file from the cached result, wipe the
		// whole cached result for consistency.
		x_unlink(cached_stderr);
		x_unlink(cached_obj);
		x_unlink(cached_dep);
		x_unlink(cached_su);
		x_unlink(cached_dia);

		failed();
	}

	cc_log("Created from cache: %s -> %s", source, dest);
}

// Send cached stderr, if any, to stderr.
static void
send_cached_stderr(void)
{
	int fd_stderr = open(cached_stderr, O_RDONLY | O_BINARY);
	if (fd_stderr != -1) {
		copy_fd(fd_stderr, 2);
		close(fd_stderr);
	}
}

// Create or update the manifest file.
void update_manifest_file(void)
{
	if (!conf->direct_mode
	    || !included_files
	    || conf->read_only
	    || conf->read_only_direct) {
		return;
	}

	struct stat st;
	size_t old_size = 0; // in bytes
	if (stat(manifest_path, &st) == 0) {
		old_size = file_size(&st);
	}
	if (manifest_put(manifest_path, cached_obj_hash, included_files)) {
		cc_log("Added object file hash to %s", manifest_path);
		update_mtime(manifest_path);
		if (x_stat(manifest_path, &st) == 0) {
			stats_update_size(file_size(&st) - old_size, old_size == 0 ? 1 : 0);
		}
	} else {
		cc_log("Failed to add object file hash to %s", manifest_path);
	}
}

// Run the real compiler and put the result in cache.
static void
to_cache(struct args *args)
{
	char *tmp_stdout = format("%s.tmp.stdout", cached_obj);
	int tmp_stdout_fd = create_tmp_fd(&tmp_stdout);
	char *tmp_stderr = format("%s.tmp.stderr", cached_obj);
	int tmp_stderr_fd = create_tmp_fd(&tmp_stderr);

	char *tmp_cov;
	if (generating_coverage) {
		char *tmp_aux;
		// GCC has some funny rule about max extension length.
		if (strlen(get_extension(output_obj)) < 6) {
			tmp_aux = remove_extension(output_obj);
		} else {
			tmp_aux = x_strdup(output_obj);
		}
		tmp_cov = format("%s.gcno", tmp_aux);
		free(tmp_aux);
	} else {
		tmp_cov = NULL;
	}

	char *tmp_su;
	if (generating_stackusage) {
		char *tmp_aux;
		// GCC has some funny rule about max extension length.
		if (strlen(get_extension(output_obj)) < 6) {
			tmp_aux = remove_extension(output_obj);
		} else {
			tmp_aux = x_strdup(output_obj);
		}
		tmp_su = format("%s.su", tmp_aux);
		free(tmp_aux);
	} else {
		tmp_su = NULL;
	}

	// GCC (at least 4.8 and 4.9) forms the .dwo file name by removing everything
	// after (and including) the last "." from the object file name and then
	// appending ".dwo".
	char *tmp_dwo = NULL;
	if (using_split_dwarf) {
		char *base_name = remove_extension(output_obj);
		tmp_dwo = format("%s.dwo", base_name);
		free(base_name);
	}

	if (compiler_is_msvc(args)) {
		char *fo = format("-Fo%s", output_obj);
		args_add(args, fo);
		free(fo);
	} else {
		args_add(args, "-o");
		args_add(args, output_obj);
	}

	if (output_dia) {
		args_add(args, "--serialize-diagnostics");
		args_add(args, output_dia);
	}

	// Turn off DEPENDENCIES_OUTPUT when running cc1, because otherwise it will
	// emit a line like this:
	//
	//   tmp.stdout.vexed.732.o: /home/mbp/.ccache/tmp.stdout.vexed.732.i
	x_unsetenv("DEPENDENCIES_OUTPUT");

	if (conf->run_second_cpp) {
		args_add(args, input_file);
	} else {
		args_add(args, i_tmpfile);
	}

	cc_log("Running real compiler");
	int status =
		execute(args->argv, tmp_stdout_fd, tmp_stderr_fd, &compiler_pid);
	args_pop(args, 3);

	struct stat st;
	if (x_stat(tmp_stdout, &st) != 0) {
		// The stdout file was removed - cleanup in progress? Better bail out.
		stats_update(STATS_MISSING);
		tmp_unlink(tmp_stdout);
		tmp_unlink(tmp_stderr);
		if (tmp_cov) {
			tmp_unlink(tmp_cov);
		}
		if (tmp_su) {
			tmp_unlink(tmp_su);
		}
		if (tmp_dwo) {
			tmp_unlink(tmp_dwo);
		}
		failed();
	}

	// MSVC compiler always print the input file name to stdout,
	// plus parts of the warnings/error messages.
	// So we have to fusion that into stderr...
	if (compiler_is_msvc(args)) {
		char *tmp_stderr2 = format("%s.2", tmp_stderr);
		if (x_rename(tmp_stderr, tmp_stderr2)) {
			cc_log("Failed to rename %s to %s: %s", tmp_stderr, tmp_stderr2,
			       strerror(errno));
			failed();
		}

		int fd_result =
			open(tmp_stderr, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
		if (fd_result == -1) {
			cc_log("Failed opening %s: %s", tmp_stderr, strerror(errno));
			failed();
		}

		int fd_stdout = open(tmp_stdout, O_RDONLY | O_BINARY);
		if (fd_stdout == -1) {
			cc_log("Failed opening %s: %s", tmp_stdout, strerror(errno));
			failed();
		}
		copy_fd(fd_stdout, fd_result);
		close(fd_stdout);

		int fd_stderr = open(tmp_stderr2, O_RDONLY | O_BINARY);
		if (fd_stderr == -1) {
			cc_log("Failed opening %s: %s", tmp_stderr2, strerror(errno));
			failed();
		}
		copy_fd(fd_stderr, fd_result);
		close(fd_stderr);

		close(fd_result);
		tmp_unlink(tmp_stderr2);
		free(tmp_stderr2);

		st.st_size = 0;
	}

	if (st.st_size > 0) {
		cc_log("Compiler produced stdout");
		stats_update(STATS_STDOUT);
		tmp_unlink(tmp_stdout);
		tmp_unlink(tmp_stderr);
		if (tmp_cov) {
			tmp_unlink(tmp_cov);
		}
		if (tmp_su) {
			tmp_unlink(tmp_su);
		}
		if (tmp_dwo) {
			tmp_unlink(tmp_dwo);
		}
		failed();
	}
	tmp_unlink(tmp_stdout);

	// Merge stderr from the preprocessor (if any) and stderr from the real
	// compiler into tmp_stderr.
	if (cpp_stderr) {
		char *tmp_stderr2 = format("%s.2", tmp_stderr);
		if (x_rename(tmp_stderr, tmp_stderr2)) {
			cc_log("Failed to rename %s to %s: %s", tmp_stderr, tmp_stderr2,
			       strerror(errno));
			failed();
		}

		int fd_cpp_stderr = open(cpp_stderr, O_RDONLY | O_BINARY);
		if (fd_cpp_stderr == -1) {
			cc_log("Failed opening %s: %s", cpp_stderr, strerror(errno));
			failed();
		}

		int fd_real_stderr = open(tmp_stderr2, O_RDONLY | O_BINARY);
		if (fd_real_stderr == -1) {
			cc_log("Failed opening %s: %s", tmp_stderr2, strerror(errno));
			failed();
		}

		int fd_result =
			open(tmp_stderr, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
		if (fd_result == -1) {
			cc_log("Failed opening %s: %s", tmp_stderr, strerror(errno));
			failed();
		}

		copy_fd(fd_cpp_stderr, fd_result);
		copy_fd(fd_real_stderr, fd_result);
		close(fd_cpp_stderr);
		close(fd_real_stderr);
		close(fd_result);
		tmp_unlink(tmp_stderr2);
		free(tmp_stderr2);
	}

	if (status != 0) {
		cc_log("Compiler gave exit status %d", status);
		stats_update(STATS_STATUS);

		int fd = open(tmp_stderr, O_RDONLY | O_BINARY);
		if (fd != -1) {
			// We can output stderr immediately instead of rerunning the compiler.
			copy_fd(fd, 2);
			close(fd);
			tmp_unlink(tmp_stderr);

			x_exit(status);
		}

		tmp_unlink(tmp_stderr);
		if (tmp_cov) {
			tmp_unlink(tmp_cov);
		}
		if (tmp_su) {
			tmp_unlink(tmp_su);
		}
		if (tmp_dwo) {
			tmp_unlink(tmp_dwo);
		}

		failed();
	}

	if (stat(output_obj, &st) != 0) {
		cc_log("Compiler didn't produce an object file");
		stats_update(STATS_NOOUTPUT);
		failed();
	}
	if (st.st_size == 0) {
		cc_log("Compiler produced an empty object file");
		stats_update(STATS_EMPTYOUTPUT);
		failed();
	}

	if (using_split_dwarf && tmp_dwo) {
		if (stat(tmp_dwo, &st) != 0) {
			cc_log("Compiler didn't produce a split dwarf file");
			stats_update(STATS_NOOUTPUT);
			failed();
		}
		if (st.st_size == 0) {
			cc_log("Compiler produced an empty split dwarf file");
			stats_update(STATS_EMPTYOUTPUT);
			failed();
		}
	}

	if (x_stat(tmp_stderr, &st) != 0) {
		stats_update(STATS_ERROR);
		failed();
	}
	if (st.st_size > 0) {
		if (move_uncompressed_file(
					tmp_stderr, cached_stderr,
					conf->compression ? conf->compression_level : 0) != 0) {
			cc_log("Failed to move %s to %s: %s", tmp_stderr, cached_stderr,
			       strerror(errno));
			stats_update(STATS_ERROR);
			failed();
		}
		cc_log("Stored in cache: %s", cached_stderr);
		if (!conf->compression
		    // If the file was compressed, obtain the size again:
		    || x_stat(cached_stderr, &st) == 0) {
			stats_update_size(file_size(&st), 1);
		}
	} else {
		tmp_unlink(tmp_stderr);
		if (conf->recache) {
			// If recaching, we need to remove any previous .stderr.
			x_unlink(cached_stderr);
		}
	}

	if (generating_coverage && tmp_cov) {
		// GCC won't generate notes if there is no code.
		if (stat(tmp_cov, &st) != 0 && errno == ENOENT) {
			FILE *f = fopen(cached_cov, "wb");
			cc_log("Creating placeholder: %s", cached_cov);
			if (!f) {
				cc_log("Failed to create %s: %s", cached_cov, strerror(errno));
				stats_update(STATS_ERROR);
				failed();
			}
			fclose(f);
			stats_update_size(0, 1);
		} else {
			put_file_in_cache(tmp_cov, cached_cov);
		}
	}

	if (generating_stackusage && tmp_su) {
		// GCC won't generate notes if there is no code.
		if (stat(tmp_su, &st) != 0 && errno == ENOENT) {
			FILE *f = fopen(cached_su, "wb");
			cc_log("Creating placeholder: %s", cached_su);
			if (!f) {
				cc_log("Failed to create %s: %s", cached_su, strerror(errno));
				stats_update(STATS_ERROR);
				failed();
			}
			fclose(f);
			stats_update_size(0, 1);
		} else {
			put_file_in_cache(tmp_su, cached_su);
		}
	}

	if (output_dia) {
		if (x_stat(output_dia, &st) != 0) {
			stats_update(STATS_ERROR);
			failed();
		}
		if (st.st_size > 0) {
			put_file_in_cache(output_dia, cached_dia);
		}
	}

	put_file_in_cache(output_obj, cached_obj);

	if (using_split_dwarf) {
		assert(tmp_dwo);
		assert(cached_dwo);
		put_file_in_cache(tmp_dwo, cached_dwo);
	}

	if (generating_dependencies) {
		use_relative_paths_in_depfile(output_dep);
		put_file_in_cache(output_dep, cached_dep);
	}

	if (output_su) {
		put_file_in_cache(output_su, cached_su);
	}

	stats_update(STATS_TOCACHE);

	// Make sure we have a CACHEDIR.TAG in the cache part of cache_dir. This can
	// be done almost anywhere, but we might as well do it near the end as we
	// save the stat call if we exit early.
	{
		char *first_level_dir = dirname(stats_file);
		if (create_cachedirtag(first_level_dir) != 0) {
			cc_log("Failed to create %s/CACHEDIR.TAG (%s)\n",
			       first_level_dir, strerror(errno));
			stats_update(STATS_ERROR);
			failed();
		}
		free(first_level_dir);

		// Remove any CACHEDIR.TAG on the cache_dir level where it was located in
		// previous ccache versions.
		if (getpid() % 1000 == 0) {
			char *path = format("%s/CACHEDIR.TAG", conf->cache_dir);
			x_unlink(path);
			free(path);
		}
	}

	// Everything OK.
	send_cached_stderr();
	update_manifest_file();

	free(tmp_stderr);
	free(tmp_stdout);
	free(tmp_cov);
	free(tmp_su);
	free(tmp_dwo);
}

// Find the object file name by running the compiler in preprocessor mode.
// Returns the hash as a heap-allocated hex string.
static struct file_hash *
get_object_name_from_cpp(struct args *args, struct mdfour *hash)
{
	time_of_compilation = time(NULL);

	char *path_stderr = format("%s/tmp.cpp_stderr", temp_dir());
	int path_stderr_fd = create_tmp_fd(&path_stderr);
	add_pending_tmp_file(path_stderr);

	char *path_stdout;
	int status;
	if (direct_i_file) {
		// We are compiling a .i or .ii file - that means we can skip the cpp stage
		// and directly form the correct i_tmpfile.
		path_stdout = input_file;
		status = 0;
	} else {
		// Run cpp on the input file to obtain the .i.

		// Limit the basename to 10 characters in order to cope with filesystem with
		// small maximum filename length limits.
		char *input_base = basename(input_file);
		char *tmp = strchr(input_base, '.');
		if (tmp) {
			*tmp = 0;
		}
		if (strlen(input_base) > 10) {
			input_base[10] = 0;
		}

		path_stdout = format("%s/%s.stdout", temp_dir(), input_base);
		int path_stdout_fd = create_tmp_fd(&path_stdout);
		add_pending_tmp_file(path_stdout);

		int args_added = 2;
		args_add(args, "-E");
		if (conf->keep_comments_cpp) {
			args_add(args, "-C");
			args_added = 3;
		}
		args_add(args, input_file);
		add_prefix(args, conf->prefix_command_cpp);
		cc_log("Running preprocessor");
		status = execute(args->argv, path_stdout_fd, path_stderr_fd, &compiler_pid);
		args_pop(args, args_added);
	}

	if (status != 0) {
		cc_log("Preprocessor gave exit status %d", status);
		stats_update(STATS_PREPROCESSOR);
		failed();
	}

	if (conf->unify) {
		// When we are doing the unifying tricks we need to include the input file
		// name in the hash to get the warnings right.
		hash_delimiter(hash, "unifyfilename");
		hash_string(hash, input_file);

		hash_delimiter(hash, "unifycpp");
		if (unify_hash(hash, path_stdout) != 0) {
			stats_update(STATS_ERROR);
			cc_log("Failed to unify %s", path_stdout);
			failed();
		}
	} else {
		hash_delimiter(hash, "cpp");
		if (!process_preprocessed_file(hash, path_stdout)) {
			stats_update(STATS_ERROR);
			failed();
		}
	}

	hash_delimiter(hash, "cppstderr");
	if (!hash_file(hash, path_stderr)) {
		fatal("Failed to open %s: %s", path_stderr, strerror(errno));
	}

	if (direct_i_file) {
		i_tmpfile = input_file;
	} else {
		// i_tmpfile needs the proper cpp_extension for the compiler to do its
		// thing correctly
		i_tmpfile = format("%s.%s", path_stdout, conf->cpp_extension);
		x_rename(path_stdout, i_tmpfile);
		add_pending_tmp_file(i_tmpfile);
	}

	if (conf->run_second_cpp) {
		free(path_stderr);
	} else {
		// If we are using the CPP trick, we need to remember this stderr data and
		// output it just before the main stderr from the compiler pass.
		cpp_stderr = path_stderr;
		hash_delimiter(hash, "runsecondcpp");
		hash_string(hash, "false");
	}

	struct file_hash *result = x_malloc(sizeof(*result));
	hash_result_as_bytes(hash, result->hash);
	result->size = (int)hash->totalN;
	return result;
}

static void
update_cached_result_globals(struct file_hash *hash)
{
	char *object_name = format_hash_as_string(hash->hash, hash->size);
	cached_obj_hash = hash;
	cached_obj = get_path_in_cache(object_name, ".o");
	cached_stderr = get_path_in_cache(object_name, ".stderr");
	cached_dep = get_path_in_cache(object_name, ".d");
	cached_cov = get_path_in_cache(object_name, ".gcno");
	cached_su = get_path_in_cache(object_name, ".su");
	cached_dia = get_path_in_cache(object_name, ".dia");

	if (using_split_dwarf) {
		cached_dwo = get_path_in_cache(object_name, ".dwo");
	} else {
		cached_dwo = NULL;
	}

	stats_file = format("%s/%c/stats", conf->cache_dir, object_name[0]);
	free(object_name);
}

// Hash mtime or content of a file, or the output of a command, according to
// the CCACHE_COMPILERCHECK setting.
static void
hash_compiler(struct mdfour *hash, struct stat *st, const char *path,
              bool allow_command)
{
	if (str_eq(conf->compiler_check, "none")) {
		// Do nothing.
	} else if (str_eq(conf->compiler_check, "mtime")) {
		hash_delimiter(hash, "cc_mtime");
		hash_int(hash, st->st_size);
		hash_int(hash, (int)st->st_mtime);
	} else if (str_startswith(conf->compiler_check, "string:")) {
		hash_delimiter(hash, "cc_hash");
		hash_string(hash, conf->compiler_check + strlen("string:"));
	} else if (str_eq(conf->compiler_check, "content") || !allow_command) {
		hash_delimiter(hash, "cc_content");
		hash_file(hash, path);
	} else { // command string
		if (!hash_multicommand_output(
					hash, conf->compiler_check, orig_args->argv[0])) {
			fatal("Failure running compiler check command: %s", conf->compiler_check);
		}
	}
}

// Update a hash sum with information common for the direct and preprocessor
// modes.
static void
calculate_common_hash(struct args *args, struct mdfour *hash)
{
	hash_string(hash, HASH_PREFIX);

	// We have to hash the extension, as a .i file isn't treated the same by the
	// compiler as a .ii file.
	hash_delimiter(hash, "ext");
	hash_string(hash, conf->cpp_extension);

#ifdef _WIN32
	const char *ext = strrchr(args->argv[0], '.');
	char full_path_win_ext[MAX_PATH + 1] = {0};
	add_exe_ext_if_no_to_fullpath(full_path_win_ext, MAX_PATH, ext,
	                              args->argv[0]);
	const char *full_path = full_path_win_ext;
#else
	const char *full_path = args->argv[0];
#endif

	struct stat st;
	if (x_stat(full_path, &st) != 0) {
		stats_update(STATS_COMPILER);
		failed();
	}

	// Hash information about the compiler.
	hash_compiler(hash, &st, args->argv[0], true);

	// Also hash the compiler name as some compilers use hard links and behave
	// differently depending on the real name.
	hash_delimiter(hash, "cc_name");
	char *p = basename(args->argv[0]);
	hash_string(hash, p);
	free(p);

	// Possibly hash the current working directory.
	if (generating_debuginfo && conf->hash_dir) {
		char *cwd = gnu_getcwd();
		if (debug_prefix_map) {
			char *map = debug_prefix_map;
			char *sep = strchr(map, '=');
			if (sep) {
				char *old = x_strndup(map, sep - map);
				char *new = x_strdup(sep + 1);
				cc_log("Relocating debuginfo cwd %s, from %s to %s", cwd, old, new);
				if (str_startswith(cwd, old)) {
					char *dir = format("%s%s", new, cwd + strlen(old));
					free(cwd);
					cwd = dir;
				}
				free(old);
				free(new);
			}
		}
		if (cwd) {
			hash_delimiter(hash, "cwd");
			hash_string(hash, cwd);
			free(cwd);
		}
	}

	// Possibly hash the coverage data file path.
	if (generating_coverage && profile_arcs) {
		char *dir = dirname(output_obj);
		if (profile_dir) {
			dir = x_strdup(profile_dir);
		} else {
			char *real_dir = x_realpath(dir);
			free(dir);
			dir = real_dir;
		}
		if (dir) {
			char *base_name = basename(output_obj);
			p = remove_extension(base_name);
			free(base_name);
			char *gcda_path = format("%s/%s.gcda", dir, p);
			cc_log("Hashing coverage path %s", gcda_path);
			free(p);
			hash_delimiter(hash, "gcda");
			hash_string(hash, gcda_path);
			free(dir);
		}
	}

	if (!str_eq(conf->extra_files_to_hash, "")) {
		char *p = x_strdup(conf->extra_files_to_hash);
		char *q = p;
		char *path;
		char *saveptr = NULL;
		while ((path = strtok_r(q, PATH_DELIM, &saveptr))) {
			cc_log("Hashing extra file %s", path);
			hash_delimiter(hash, "extrafile");
			if (!hash_file(hash, path)) {
				stats_update(STATS_BADEXTRAFILE);
				failed();
			}
			q = NULL;
		}
		free(p);
	}

	// Possibly hash GCC_COLORS (for color diagnostics).
	if (compiler_is_gcc(args)) {
		const char *gcc_colors = getenv("GCC_COLORS");
		if (gcc_colors) {
			hash_delimiter(hash, "gcccolors");
			hash_string(hash, gcc_colors);
		}
	}
}

// Update a hash sum with information specific to the direct and preprocessor
// modes and calculate the object hash. Returns the object hash on success,
// otherwise NULL. Caller frees.
static struct file_hash *
calculate_object_hash(struct args *args, struct mdfour *hash, int direct_mode)
{
	if (direct_mode) {
		hash_delimiter(hash, "manifest version");
		hash_int(hash, MANIFEST_VERSION);
	}

	// First the arguments.
	for (int i = 1; i < args->argc; i++) {
		// -L doesn't affect compilation.
		if (i < args->argc-1 && str_eq(args->argv[i], "-L")) {
			i++;
			continue;
		}
		if (str_startswith(args->argv[i], "-L")) {
			continue;
		}

		// -Wl,... doesn't affect compilation.
		if (str_startswith(args->argv[i], "-Wl,")) {
			continue;
		}

		// The -fdebug-prefix-map option may be used in combination with
		// CCACHE_BASEDIR to reuse results across different directories. Skip it
		// from hashing.
		if (str_startswith(args->argv[i], "-fdebug-prefix-map=")) {
			continue;
		}

		// When using the preprocessor, some arguments don't contribute to the
		// hash. The theory is that these arguments will change the output of -E if
		// they are going to have any effect at all. For precompiled headers this
		// might not be the case.
		if (!direct_mode && !output_is_precompiled_header
		    && !using_precompiled_header) {
			if (compopt_affects_cpp(args->argv[i])) {
				i++;
				continue;
			}
			if (compopt_short(compopt_affects_cpp, args->argv[i])) {
				continue;
			}
		}

		// If we're generating dependencies, we make sure to skip the filename of
		// the dependency file, since it doesn't impact the output.
		if (generating_dependencies) {
			if (str_startswith(args->argv[i], "-Wp,")) {
				if (str_startswith(args->argv[i], "-Wp,-MD,")
				    && !strchr(args->argv[i] + 8, ',')) {
					hash_string_length(hash, args->argv[i], 8);
					continue;
				} else if (str_startswith(args->argv[i], "-Wp,-MMD,")
				           && !strchr(args->argv[i] + 9, ',')) {
					hash_string_length(hash, args->argv[i], 9);
					continue;
				}
			} else if (str_startswith(args->argv[i], "-MF")) {
				// In either case, hash the "-MF" part.
				hash_string_length(hash, args->argv[i], 3);

				bool separate_argument = (strlen(args->argv[i]) == 3);
				if (separate_argument) {
					// Next argument is dependency name, so skip it.
					i++;
				}
				continue;
			}
		}

		char *p = NULL;
		if (str_startswith(args->argv[i], "-specs=")) {
			p = args->argv[i] + 7;
		} else if (str_startswith(args->argv[i], "--specs=")) {
			p = args->argv[i] + 8;
		}

		struct stat st;
		if (p && x_stat(p, &st) == 0) {
			// If given an explicit specs file, then hash that file, but don't
			// include the path to it in the hash.
			hash_delimiter(hash, "specs");
			hash_compiler(hash, &st, p, false);
			continue;
		}

		if (str_startswith(args->argv[i], "-fplugin=")
		    && x_stat(args->argv[i] + 9, &st) == 0) {
			hash_delimiter(hash, "plugin");
			hash_compiler(hash, &st, args->argv[i] + 9, false);
			continue;
		}

		if (str_eq(args->argv[i], "-Xclang")
		    && i + 3 < args->argc
		    && str_eq(args->argv[i+1], "-load")
		    && str_eq(args->argv[i+2], "-Xclang")
		    && x_stat(args->argv[i+3], &st) == 0) {
			hash_delimiter(hash, "plugin");
			hash_compiler(hash, &st, args->argv[i+3], false);
			i += 3;
			continue;
		}

		// All other arguments are included in the hash.
		hash_delimiter(hash, "arg");
		hash_string(hash, args->argv[i]);
		if (i + 1 < args->argc && compopt_takes_arg(args->argv[i])) {
			i++;
			hash_delimiter(hash, "arg");
			hash_string(hash, args->argv[i]);
		}
	}

	// For profile generation (-fprofile-arcs, -fprofile-generate):
	// - hash profile directory
	//
	// For profile usage (-fprofile-use):
	// - hash profile data
	//
	// -fbranch-probabilities and -fvpt usage is covered by
	// -fprofile-generate/-fprofile-use.
	//
	// The profile directory can be specified as an argument to
	// -fprofile-generate=, -fprofile-use= or -fprofile-dir=.
	if (profile_generate) {
		if (!profile_dir) {
			profile_dir = get_cwd();
		}
		cc_log("Adding profile directory %s to our hash", profile_dir);
		hash_delimiter(hash, "-fprofile-dir");
		hash_string(hash, profile_dir);
	}

	if (profile_use) {
		// Calculate gcda name.
		if (!profile_dir) {
			profile_dir = get_cwd();
		}
		char *base_name = remove_extension(output_obj);
		char *gcda_name = format("%s/%s.gcda", profile_dir, base_name);
		cc_log("Adding profile data %s to our hash", gcda_name);
		// Add the gcda to our hash.
		hash_delimiter(hash, "-fprofile-use");
		hash_file(hash, gcda_name);
		free(base_name);
		free(gcda_name);
	}

	// Adding -arch to hash since cpp output is affected.
	for (size_t i = 0; i < arch_args_size; ++i) {
		hash_delimiter(hash, "-arch");
		hash_string(hash, arch_args[i]);
	}

	struct file_hash *object_hash = NULL;
	if (direct_mode) {
		// Hash environment variables that affect the preprocessor output.
		const char *envvars[] = {
			"CPATH",
			"C_INCLUDE_PATH",
			"CPLUS_INCLUDE_PATH",
			"OBJC_INCLUDE_PATH",
			"OBJCPLUS_INCLUDE_PATH", // clang
			NULL
		};
		for (const char **p = envvars; *p; ++p) {
			char *v = getenv(*p);
			if (v) {
				hash_delimiter(hash, *p);
				hash_string(hash, v);
			}
		}

		if (!(conf->sloppiness & SLOPPY_FILE_MACRO)) {
			// The source code file or an include file may contain __FILE__, so make
			// sure that the hash is unique for the file name.
			hash_delimiter(hash, "inputfile");
			hash_string(hash, input_file);
		}

		hash_delimiter(hash, "sourcecode");
		int result = hash_source_code_file(conf, hash, input_file);
		if (result & HASH_SOURCE_CODE_ERROR) {
			failed();
		}
		if (result & HASH_SOURCE_CODE_FOUND_TIME) {
			cc_log("Disabling direct mode");
			conf->direct_mode = false;
			return NULL;
		}
		char *manifest_name = hash_result(hash);
		manifest_path = get_path_in_cache(manifest_name, ".manifest");
		free(manifest_name);
		cc_log("Looking for object file hash in %s", manifest_path);
		object_hash = manifest_get(conf, manifest_path);
		if (object_hash) {
			cc_log("Got object file hash from manifest");
		} else {
			cc_log("Did not find object file hash in manifest");
		}
	} else {
		if (arch_args_size == 0) {
			object_hash = get_object_name_from_cpp(args, hash);
			cc_log("Got object file hash from preprocessor");
		} else {
			args_add(args, "-arch");
			for (size_t i = 0; i < arch_args_size; ++i) {
				args_add(args, arch_args[i]);
				object_hash = get_object_name_from_cpp(args, hash);
				cc_log("Got object file hash from preprocessor with -arch %s",
				       arch_args[i]);
				if (i != arch_args_size - 1) {
					free(object_hash);
					object_hash = NULL;
				}
				args_pop(args, 1);
			}
			args_pop(args, 1);
		}
		if (generating_dependencies) {
			cc_log("Preprocessor created %s", output_dep);
		}
	}

	return object_hash;
}

// Try to return the compile result from cache. If we can return from cache
// then this function exits with the correct status code, otherwise it returns.
static void
from_cache(enum fromcache_call_mode mode, bool put_object_in_manifest)
{
	// The user might be disabling cache hits.
	if (conf->recache) {
		return;
	}

	struct stat st;
	if (stat(cached_obj, &st) != 0) {
		cc_log("Object file %s not in cache", cached_obj);
		return;
	}

	// Check if the diagnostic file is there.
	if (output_dia && stat(cached_dia, &st) != 0) {
		cc_log("Diagnostic file %s not in cache", cached_dia);
		return;
	}

	// Occasionally, e.g. on hard reset, our cache ends up as just filesystem
	// meta-data with no content. Catch an easy case of this.
	if (st.st_size == 0) {
		cc_log("Invalid (empty) object file %s in cache", cached_obj);
		x_unlink(cached_obj);
		return;
	}

	if (using_split_dwarf && !generating_dependencies) {
		assert(output_dwo);
	}
	if (output_dwo) {
		assert(cached_dwo);
		if (stat(cached_dwo, &st) != 0) {
			cc_log("Split dwarf file %s not in cache", cached_dwo);
			return;
		}
		if (st.st_size == 0) {
			cc_log("Invalid (empty) dwo file %s in cache", cached_dwo);
			x_unlink(cached_dwo);
			x_unlink(cached_obj); // To really invalidate.
			return;
		}
	}

	// (If mode != FROMCACHE_DIRECT_MODE, the dependency file is created by gcc.)
	bool produce_dep_file =
		generating_dependencies && mode == FROMCACHE_DIRECT_MODE;

	// If the dependency file should be in the cache, check that it is.
	if (produce_dep_file && stat(cached_dep, &st) != 0) {
		cc_log("Dependency file %s missing in cache", cached_dep);
		return;
	}

	// Copy object file from cache. Do so also for FissionDwarf file, cached_dwo,
	// when -gsplit-dwarf is specified.
	if (!str_eq(output_obj, "/dev/null")) {
		get_file_from_cache(cached_obj, output_obj);
		if (using_split_dwarf) {
			assert(output_dwo);
			get_file_from_cache(cached_dwo, output_dwo);
		}
	}
	if (produce_dep_file) {
		get_file_from_cache(cached_dep, output_dep);
	}
	if (generating_coverage && stat(cached_cov, &st) == 0 && st.st_size > 0) {
		// The compiler won't generate notes if there is no code
		get_file_from_cache(cached_cov, output_cov);
	}
	if (generating_stackusage && stat(cached_su, &st) == 0 && st.st_size > 0) {
		// The compiler won't generate notes if there is no code
		get_file_from_cache(cached_su, output_su);
	}
	if (output_dia) {
		get_file_from_cache(cached_dia, output_dia);
	}

	// Update modification timestamps to save files from LRU cleanup. Also gives
	// files a sensible mtime when hard-linking.
	update_mtime(cached_obj);
	update_mtime(cached_stderr);
	if (produce_dep_file) {
		update_mtime(cached_dep);
	}
	if (generating_coverage) {
		update_mtime(cached_cov);
	}
	if (generating_stackusage) {
		update_mtime(cached_su);
	}
	if (output_dia) {
		update_mtime(cached_dia);
	}
	if (cached_dwo) {
		update_mtime(cached_dwo);
	}

	if (generating_dependencies && mode == FROMCACHE_CPP_MODE
	    && !conf->read_only && !conf->read_only_direct) {
		put_file_in_cache(output_dep, cached_dep);
	}

	send_cached_stderr();

	if (put_object_in_manifest) {
		update_manifest_file();
	}

	// Log the cache hit.
	switch (mode) {
	case FROMCACHE_DIRECT_MODE:
		cc_log("Succeeded getting cached result");
		stats_update(STATS_CACHEHIT_DIR);
		break;

	case FROMCACHE_CPP_MODE:
		cc_log("Succeeded getting cached result");
		stats_update(STATS_CACHEHIT_CPP);
		break;
	}

	// And exit with the right status code.
	x_exit(0);
}

// Find the real compiler. We just search the PATH to find an executable of the
// same name that isn't a link to ourselves.
static void
find_compiler(char **argv)
{
	// We might be being invoked like "ccache gcc -c foo.c".
	char *base = basename(argv[0]);
	if (same_executable_name(base, MYNAME)) {
		args_remove_first(orig_args);
		free(base);
		if (is_full_path(orig_args->argv[0])) {
			// A full path was given.
			return;
		}
		base = basename(orig_args->argv[0]);
	}

	// Support user override of the compiler.
	if (!str_eq(conf->compiler, "")) {
		base = conf->compiler;
	}

	char *compiler = find_executable(base, MYNAME);
	if (!compiler) {
		stats_update(STATS_COMPILER);
		fatal("Could not find compiler \"%s\" in PATH", base);
	}
	if (str_eq(compiler, argv[0])) {
		fatal("Recursive invocation (the name of the ccache binary must be \"%s\")",
		      MYNAME);
	}
	orig_args->argv[0] = compiler;
}

bool
is_precompiled_header(const char *path)
{
	return str_eq(get_extension(path), ".gch")
	       || str_eq(get_extension(path), ".pch")
	       || str_eq(get_extension(path), ".pth");
}

static bool
color_output_possible(void)
{
#ifdef _WIN32
	return false;
#else
	const char *term_env = getenv("TERM");
	return isatty(STDERR_FILENO) && term_env && strcasecmp(term_env, "DUMB") != 0;
#endif
}

static bool
detect_pch(const char *option, const char *arg, bool *found_pch)
{
	struct stat st;

	// Try to be smart about detecting precompiled headers.
	char *pch_file = NULL;
	if (str_eq(option, "-include-pch") || str_eq(option, "-include-pth")) {
		if (stat(arg, &st) == 0) {
			cc_log("Detected use of precompiled header: %s", arg);
			pch_file = x_strdup(arg);
		}
	} else {
		char *gchpath = format("%s.gch", arg);
		if (stat(gchpath, &st) == 0) {
			cc_log("Detected use of precompiled header: %s", gchpath);
			pch_file = x_strdup(gchpath);
		} else {
			char *pchpath = format("%s.pch", arg);
			if (stat(pchpath, &st) == 0) {
				cc_log("Detected use of precompiled header: %s", pchpath);
				pch_file = x_strdup(pchpath);
			} else {
				// clang may use pretokenized headers.
				char *pthpath = format("%s.pth", arg);
				if (stat(pthpath, &st) == 0) {
					cc_log("Detected use of pretokenized header: %s", pthpath);
					pch_file = x_strdup(pthpath);
				}
				free(pthpath);
			}
			free(pchpath);
		}
		free(gchpath);
	}

	if (pch_file) {
		if (included_pch_file) {
			cc_log("Multiple precompiled headers used: %s and %s\n",
			       included_pch_file, pch_file);
			stats_update(STATS_ARGS);
			return false;
		}
		included_pch_file = pch_file;
		*found_pch = true;
	}
	return true;
}

// Process the compiler options into options suitable for passing to the
// preprocessor and the real compiler. The preprocessor options don't include
// -E; this is added later. Returns true on success, otherwise false.
bool
cc_process_args(struct args *args, struct args **preprocessor_args,
                struct args **compiler_args)
{
	bool found_c_opt = false;
	bool found_S_opt = false;
	bool found_pch = false;
	bool found_fpch_preprocess = false;
	const char *explicit_language = NULL; // As specified with -x.
	const char *file_language;            // As deduced from file extension.
	const char *actual_language;          // Language to actually use.
	const char *input_charset = NULL;
	// Is the dependency makefile name overridden with -MF?
	bool dependency_filename_specified = false;
	// Is the dependency makefile target name specified with -MT or -MQ?
	bool dependency_target_specified = false;
	// expanded_args is a copy of the original arguments given to the compiler
	// but with arguments from @file and similar constructs expanded. It's only
	// used as a temporary data structure to loop over.
	struct args *expanded_args = args_copy(args);
	// stripped_args essentially contains all original arguments except those
	// that only should be passed to the preprocessor (if run_second_cpp is
	// false) and except dependency options (like -MD and friends).
	struct args *stripped_args = args_init(0, NULL);
	// cpp_args contains arguments that were not added to stripped_args, i.e.
	// those that should only be passed to the preprocessor if run_second_cpp is
	// false. If run_second_cpp is true, they will be passed to the compiler as
	// well.
	struct args *cpp_args = args_init(0, NULL);
	// dep_args contains dependency options like -MD. They only passed to the
	// preprocessor, never to the compiler.
	struct args *dep_args = args_init(0, NULL);

	bool found_color_diagnostics = false;
	int debug_level = 0;
	const char *debug_argument = NULL;

	int argc = expanded_args->argc;
	char **argv = expanded_args->argv;
	args_add(stripped_args, argv[0]);

	bool result = true;
	for (int i = 1; i < argc; i++) {
		// The user knows best: just swallow the next arg.
		if (str_eq(argv[i], "--ccache-skip")) {
			i++;
			if (i == argc) {
				cc_log("--ccache-skip lacks an argument");
				result = false;
				goto out;
			}
			args_add(stripped_args, argv[i]);
			continue;
		}

		// Special case for -E.
		if (str_eq(argv[i], "-E") || str_eq(argv[i], "/E")) {
			stats_update(STATS_PREPROCESSING);
			result = false;
			goto out;
		}

		// Handle "@file" argument.
		if (str_startswith(argv[i], "@") || str_startswith(argv[i], "-@")) {
			char *argpath = argv[i] + 1;

			if (argpath[-1] == '-') {
				++argpath;
			}
			struct args *file_args = args_init_from_gcc_atfile(argpath);
			if (!file_args) {
				cc_log("Couldn't read arg file %s", argpath);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}

			args_insert(expanded_args, i, file_args, true);
			argc = expanded_args->argc;
			argv = expanded_args->argv;
			i--;
			continue;
		}

		// Handle cuda "-optf" and "--options-file" argument.
		if (str_eq(argv[i], "-optf") || str_eq(argv[i], "--options-file")) {
			if (i > argc) {
				cc_log("Expected argument after -optf/--options-file");
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}
			++i;

			// Argument is a comma-separated list of files.
			char *str_start = argv[i];
			char *str_end = strchr(str_start, ',');
			int index = i + 1;

			if (!str_end) {
				str_end = str_start + strlen(str_start);
			}

			while (str_end) {
				*str_end = '\0';
				struct args *file_args = args_init_from_gcc_atfile(str_start);
				if (!file_args) {
					cc_log("Couldn't read cuda options file %s", str_start);
					stats_update(STATS_ARGS);
					result = false;
					goto out;
				}

				int new_index = file_args->argc + index;
				args_insert(expanded_args, index, file_args, false);
				index = new_index;
				str_start = str_end;
				str_end = strchr(str_start, ',');
			}

			argc = expanded_args->argc;
			argv = expanded_args->argv;
			continue;
		}

		// These are always too hard.
		if (compopt_too_hard(argv[i]) || str_startswith(argv[i], "-fdump-")) {
			cc_log("Compiler option %s is unsupported", argv[i]);
			stats_update(STATS_UNSUPPORTED_OPTION);
			result = false;
			goto out;
		}

		// These are too hard in direct mode.
		if (conf->direct_mode && compopt_too_hard_for_direct_mode(argv[i])) {
			cc_log("Unsupported compiler option for direct mode: %s", argv[i]);
			conf->direct_mode = false;
		}

		// -Xarch_* options are too hard.
		if (str_startswith(argv[i], "-Xarch_")) {
			cc_log("Unsupported compiler option :%s", argv[i]);
			stats_update(STATS_UNSUPPORTED_OPTION);
			result = false;
			goto out;
		}

		// Handle -arch options.
		if (str_eq(argv[i], "-arch")) {
			if (arch_args_size == MAX_ARCH_ARGS - 1) {
				cc_log("Too many -arch compiler options; ccache supports at most %d",
				       MAX_ARCH_ARGS);
				stats_update(STATS_UNSUPPORTED_OPTION);
				result = false;
				goto out;
			}

			++i;
			arch_args[arch_args_size] = x_strdup(argv[i]); // It will leak.
			++arch_args_size;
			if (arch_args_size == 2) {
				conf->run_second_cpp = true;
			}
			continue;
		}

		if (str_eq(argv[i], "-fpch-preprocess")
		    || str_eq(argv[i], "-emit-pch")
		    || str_eq(argv[i], "-emit-pth")) {
			found_fpch_preprocess = true;
		}

		// We must have -c.
		if (str_eq(argv[i], "-c") || str_eq(argv[i], "/c")) {
			found_c_opt = true;
			continue;
		}

		// -S changes the default extension.
		if (str_eq(argv[i], "-S")) {
			args_add(stripped_args, argv[i]);
			found_S_opt = true;
			continue;
		}

		// Special handling for -x: remember the last specified language before the
		// input file and strip all -x options from the arguments.
		if (str_eq(argv[i], "-x")) {
			if (i == argc-1) {
				cc_log("Missing argument to %s", argv[i]);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}
			if (!input_file) {
				explicit_language = argv[i+1];
			}
			i++;
			continue;
		}
		if (str_startswith(argv[i], "-x")) {
			if (!input_file) {
				explicit_language = &argv[i][2];
			}
			continue;
		}

		// We need to work out where the output was meant to go.
		if (str_eq(argv[i], "-o")) {
			if (i == argc-1) {
				cc_log("Missing argument to %s", argv[i]);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}
			output_obj = make_relative_path(x_strdup(argv[i+1]));
			i++;
			continue;
		}

		// Alternate form of -o/-Fo with no space.
		if (compiler_is_msvc(args)) {
			if (str_startswith(argv[i], "-Fo")) {
				output_obj = make_relative_path(x_strdup(&argv[i][3]));
				continue;
			}
		} else {
			if (str_startswith(argv[i], "-o")) {
				output_obj = make_relative_path(x_strdup(&argv[i][2]));
				continue;
			}
		}

		if (str_eq(argv[i], "-gsplit-dwarf")) {
			cc_log("Enabling caching of dwarf files since -gsplit-dwarf is used");
			using_split_dwarf = true;
			args_add(stripped_args, argv[i]);
			continue;
		}
		if (str_startswith(argv[i], "-fdebug-prefix-map=")) {
			debug_prefix_map = x_strdup(argv[i] + 19);
			args_add(stripped_args, argv[i]);
			continue;
		}

		// Debugging is handled specially, so that we know if we can strip line
		// number info.
		if (str_startswith(argv[i], "-g") && !compiler_is_msvc(args)) {
			const char *pLevel = argv[i] + 2;
			if (str_startswith(argv[i], "-ggdb")) {
				pLevel = argv[i] + 5;
			} else if (str_startswith(argv[i], "-gstabs")) {
				pLevel = argv[i] + 7;
			} else if (str_startswith(argv[i], "-gcoff")) {
				pLevel = argv[i] + 6;
			} else if (str_startswith(argv[i], "-gxcoff")) {
				pLevel = argv[i] + 7;
			} else if (str_startswith(argv[i], "-gvms")) {
				pLevel = argv[i] + 5;
			}

			// Deduce level from argument, default is 2.
			int foundlevel = -1;
			if (pLevel[0] == '\0') {
				foundlevel = 2;
			} else if (pLevel[0] >= '0' && pLevel[0] <= '9') {
				foundlevel = atoi(pLevel);
			}

			if (foundlevel >= 0) {
				debug_level = foundlevel;
				debug_argument = argv[i];
				continue;
			}
		}

		if ((str_startswith(argv[i],
		                    "-O") ||
		     str_startswith(argv[i], "/O")) && compiler_is_msvc(args)) {
			debug_level = 1;
			debug_argument = argv[i];
			continue;
		}

		// These options require special handling, because they behave differently
		// with gcc -E, when the output file is not specified.
		if (str_eq(argv[i], "-MD") || str_eq(argv[i], "-MMD")) {
			generating_dependencies = true;
			args_add(dep_args, argv[i]);
			continue;
		}
		if (str_startswith(argv[i], "-MF")) {
			dependency_filename_specified = true;
			free(output_dep);

			char *arg;
			bool separate_argument = (strlen(argv[i]) == 3);
			if (separate_argument) {
				// -MF arg
				if (i >= argc - 1) {
					cc_log("Missing argument to %s", argv[i]);
					stats_update(STATS_ARGS);
					result = false;
					goto out;
				}
				arg = argv[i + 1];
				i++;
			} else {
				// -MFarg
				arg = &argv[i][3];
			}
			output_dep = make_relative_path(x_strdup(arg));
			// Keep the format of the args the same.
			if (separate_argument) {
				args_add(dep_args, "-MF");
				args_add(dep_args, output_dep);
			} else {
				char *option = format("-MF%s", output_dep);
				args_add(dep_args, option);
				free(option);
			}
			continue;
		}
		if (str_startswith(argv[i], "-MQ") || str_startswith(argv[i], "-MT")) {
			dependency_target_specified = true;

			char *relpath;
			if (strlen(argv[i]) == 3) {
				// -MQ arg or -MT arg
				if (i >= argc - 1) {
					cc_log("Missing argument to %s", argv[i]);
					stats_update(STATS_ARGS);
					result = false;
					goto out;
				}
				args_add(dep_args, argv[i]);
				relpath = make_relative_path(x_strdup(argv[i + 1]));
				args_add(dep_args, relpath);
				free(relpath);
				i++;
			} else {
				char *arg_opt = x_strndup(argv[i], 3);
				relpath = make_relative_path(x_strdup(argv[i] + 3));
				char *option = format("%s%s", arg_opt, relpath);
				args_add(dep_args, option);
				free(arg_opt);
				free(relpath);
				free(option);
			}
			continue;
		}
		if (str_eq(argv[i], "-fprofile-arcs")) {
			profile_arcs = true;
			args_add(stripped_args, argv[i]);
			continue;
		}
		if (str_eq(argv[i], "-ftest-coverage")) {
			generating_coverage = true;
			args_add(stripped_args, argv[i]);
			continue;
		}
		if (str_eq(argv[i], "-fstack-usage")) {
			generating_stackusage = true;
			args_add(stripped_args, argv[i]);
			continue;
		}
		if (str_eq(argv[i], "--coverage") // = -fprofile-arcs -ftest-coverage
		    || str_eq(argv[i], "-coverage")) { // Undocumented but still works.
			profile_arcs = true;
			generating_coverage = true;
			args_add(stripped_args, argv[i]);
			continue;
		}
		if (str_startswith(argv[i], "-fprofile-dir=")) {
			profile_dir = x_strdup(argv[i] + 14);
			args_add(stripped_args, argv[i]);
			continue;
		}
		if (str_startswith(argv[i], "--sysroot=")) {
			char *relpath = make_relative_path(x_strdup(argv[i] + 10));
			char *option = format("--sysroot=%s", relpath);
			args_add(stripped_args, option);
			free(relpath);
			free(option);
			continue;
		}
		// Alternate form of specifying sysroot without =
		if (str_eq(argv[i], "--sysroot")) {
			if (i == argc-1) {
				cc_log("Missing argument to %s", argv[i]);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}
			args_add(stripped_args, argv[i]);
			char *relpath = make_relative_path(x_strdup(argv[i+1]));
			args_add(stripped_args, relpath);
			i++;
			free(relpath);
			continue;
		}
		if (str_startswith(argv[i], "-Wp,")) {
			if (str_eq(argv[i], "-Wp,-P")
			    || strstr(argv[i], ",-P,")
			    || str_endswith(argv[i], ",-P")) {
				// -P removes preprocessor information in such a way that the object
				// file from compiling the preprocessed file will not be equal to the
				// object file produced when compiling without ccache.
				cc_log("Too hard option -Wp,-P detected");
				stats_update(STATS_UNSUPPORTED_OPTION);
				failed();
			} else if (str_startswith(argv[i], "-Wp,-MD,")
			           && !strchr(argv[i] + 8, ',')) {
				generating_dependencies = true;
				dependency_filename_specified = true;
				free(output_dep);
				output_dep = make_relative_path(x_strdup(argv[i] + 8));
				args_add(dep_args, argv[i]);
				continue;
			} else if (str_startswith(argv[i], "-Wp,-MMD,")
			           && !strchr(argv[i] + 9, ',')) {
				generating_dependencies = true;
				dependency_filename_specified = true;
				free(output_dep);
				output_dep = make_relative_path(x_strdup(argv[i] + 9));
				args_add(dep_args, argv[i]);
				continue;
			} else if (str_startswith(argv[i], "-Wp,-D")
			           && !strchr(argv[i] + 6, ',')) {
				// Treat it like -D.
				args_add(cpp_args, argv[i] + 4);
				continue;
			} else if (str_eq(argv[i], "-Wp,-MP")
			           || (strlen(argv[i]) > 8
			               && str_startswith(argv[i], "-Wp,-M")
			               && argv[i][7] == ','
			               && (argv[i][6] == 'F'
			                   || argv[i][6] == 'Q'
			                   || argv[i][6] == 'T')
			               && !strchr(argv[i] + 8, ','))) {
				// TODO: Make argument to MF/MQ/MT relative.
				args_add(dep_args, argv[i]);
				continue;
			} else if (conf->direct_mode) {
				// -Wp, can be used to pass too hard options to the preprocessor.
				// Hence, disable direct mode.
				cc_log("Unsupported compiler option for direct mode: %s", argv[i]);
				conf->direct_mode = false;
			}

			// Any other -Wp,* arguments are only relevant for the preprocessor.
			args_add(cpp_args, argv[i]);
			continue;
		}
		if (str_eq(argv[i], "-MP")) {
			args_add(dep_args, argv[i]);
			continue;
		}

		// Input charset needs to be handled specially.
		if (str_startswith(argv[i], "-finput-charset=")) {
			input_charset = argv[i];
			continue;
		}

		if (str_eq(argv[i], "--serialize-diagnostics")) {
			if (i >= argc - 1) {
				cc_log("Missing argument to %s", argv[i]);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}
			output_dia = make_relative_path(x_strdup(argv[i+1]));
			i++;
			continue;
		}

		if (str_startswith(argv[i], "-fprofile-")) {
			char *arg = x_strdup(argv[i]);
			const char *arg_profile_dir = strchr(argv[i], '=');
			if (arg_profile_dir) {
				// Convert to absolute path.
				char *dir = x_realpath(arg_profile_dir + 1);
				if (!dir) {
					// Directory doesn't exist.
					dir = x_strdup(arg_profile_dir + 1);
				}

				// We can get a better hit rate by using the real path here.
				free(arg);
				char *option = x_strndup(argv[i], arg_profile_dir - argv[i]);
				arg = format("%s=%s", option, dir);
				cc_log("Rewriting %s to %s", argv[i], arg);
				free(option);
				free(dir);
			}

			bool supported_profile_option = false;
			if (str_startswith(argv[i], "-fprofile-generate")
			    || str_eq(argv[i], "-fprofile-arcs")) {
				profile_generate = true;
				supported_profile_option = true;
			} else if (str_startswith(argv[i], "-fprofile-use")
			           || str_eq(argv[i], "-fbranch-probabilities")) {
				profile_use = true;
				supported_profile_option = true;
			} else if (str_eq(argv[i], "-fprofile-dir")) {
				supported_profile_option = true;
			}

			if (supported_profile_option) {
				args_add(stripped_args, arg);
				free(arg);

				// If the profile directory has already been set, give up... Hard to
				// know what the user means, and what the compiler will do.
				if (arg_profile_dir && profile_dir) {
					cc_log("Profile directory already set; giving up");
					result = false;
					goto out;
				} else if (arg_profile_dir) {
					cc_log("Setting profile directory to %s", profile_dir);
					profile_dir = x_strdup(arg_profile_dir);
				}
				continue;
			}
			cc_log("Unknown profile option: %s", argv[i]);
			free(arg);
		}

		if (str_eq(argv[i], "-fcolor-diagnostics")
		    || str_eq(argv[i], "-fno-color-diagnostics")
		    || str_eq(argv[i], "-fdiagnostics-color")
		    || str_eq(argv[i], "-fdiagnostics-color=always")
		    || str_eq(argv[i], "-fno-diagnostics-color")
		    || str_eq(argv[i], "-fdiagnostics-color=never")) {
			args_add(stripped_args, argv[i]);
			found_color_diagnostics = true;
			continue;
		}
		if (str_eq(argv[i], "-fdiagnostics-color=auto")) {
			if (color_output_possible()) {
				// Output is redirected, so color output must be forced.
				args_add(stripped_args, "-fdiagnostics-color=always");
				cc_log("Automatically forcing colors");
			} else {
				args_add(stripped_args, argv[i]);
			}
			found_color_diagnostics = true;
			continue;
		}

		// Options taking an argument that we may want to rewrite to relative paths
		// to get better hit rate. A secondary effect is that paths in the standard
		// error output produced by the compiler will be normalized.
		if (compopt_takes_path(argv[i])) {
			if (i == argc-1) {
				cc_log("Missing argument to %s", argv[i]);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}

			if (!detect_pch(argv[i], argv[i+1], &found_pch)) {
				result = false;
				goto out;
			}

			char *relpath = make_relative_path(x_strdup(argv[i+1]));
			if (compopt_affects_cpp(argv[i])) {
				args_add(cpp_args, argv[i]);
				args_add(cpp_args, relpath);
			} else {
				args_add(stripped_args, argv[i]);
				args_add(stripped_args, relpath);
			}
			free(relpath);

			i++;
			continue;
		}

		// Same as above but options with concatenated argument beginning with a
		// slash.
		if (argv[i][0] == '-' || (compiler_is_msvc(args) && argv[i][0] == '/')) {
			char *slash_pos = strchr(argv[i]+1, '/');
			if (slash_pos) {
				char *option = x_strndup(argv[i], slash_pos - argv[i]);
				if (compopt_takes_concat_arg(option) && compopt_takes_path(option)) {
					char *relpath = make_relative_path(x_strdup(slash_pos));
					char *new_option = format("%s%s", option, relpath);
					if (compopt_affects_cpp(option)) {
						args_add(cpp_args, new_option);
					} else {
						args_add(stripped_args, new_option);
					}
					free(new_option);
					free(relpath);
					free(option);
					continue;
				} else {
					free(option);
				}
			}
		}

		// Options that take an argument.
		if (compopt_takes_arg(argv[i])) {
			if (i == argc-1) {
				cc_log("Missing argument to %s", argv[i]);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}

			if (compopt_affects_cpp(argv[i])) {
				args_add(cpp_args, argv[i]);
				args_add(cpp_args, argv[i+1]);
			} else {
				args_add(stripped_args, argv[i]);
				args_add(stripped_args, argv[i+1]);
			}

			i++;
			continue;
		}

		// Other options.
		if (argv[i][0] == '-' || (compiler_is_msvc(args) && argv[i][0] == '/')) {
			if (compopt_affects_cpp(argv[i])
			    || compopt_prefix_affects_cpp(argv[i])) {
				args_add(cpp_args, argv[i]);
			} else {
				args_add(stripped_args, argv[i]);
			}
			continue;
		}

		// If an argument isn't a plain file then assume its an option, not an
		// input file. This allows us to cope better with unusual compiler options.
		struct stat st;
		if (stat(argv[i], &st) != 0 || !S_ISREG(st.st_mode)) {
			cc_log("%s is not a regular file, not considering as input file",
			       argv[i]);
			args_add(stripped_args, argv[i]);
			continue;
		}

		if (input_file) {
			if (language_for_file(argv[i])) {
				cc_log("Multiple input files: %s and %s", input_file, argv[i]);
				stats_update(STATS_MULTIPLE);
			} else if (!found_c_opt) {
				cc_log("Called for link with %s", argv[i]);
				if (strstr(argv[i], "conftest.")) {
					stats_update(STATS_CONFTEST);
				} else {
					stats_update(STATS_LINK);
				}
			} else {
				cc_log("Unsupported source extension: %s", argv[i]);
				stats_update(STATS_SOURCELANG);
			}
			result = false;
			goto out;
		}

		// The source code file path gets put into the notes.
		if (generating_coverage) {
			input_file = x_strdup(argv[i]);
			continue;
		}

		if (is_symlink(argv[i])) {
			// Don't rewrite source file path if it's a symlink since
			// make_relative_path resolves symlinks using realpath(3) and this leads
			// to potentially choosing incorrect relative header files. See the
			// "symlink to source file" test.
			input_file = x_strdup(argv[i]);
		} else {
			// Rewrite to relative to increase hit rate.
			input_file = make_relative_path(x_strdup(argv[i]));
		}
	} // for

	if (debug_level > 0) {
		generating_debuginfo = true;
		args_add(stripped_args, debug_argument);
		if (conf->unify) {
			cc_log("%s used; disabling unify mode", debug_argument);
			conf->unify = false;
		}
		if (debug_level >= 3 && !conf->run_second_cpp) {
			cc_log("%s used; not compiling preprocessed code", debug_argument);
			conf->run_second_cpp = true;
		}
	}

	if (found_S_opt) {
		// Even if -gsplit-dwarf is given, the .dwo file is not generated when -S
		// is also given.
		using_split_dwarf = false;
		cc_log("Disabling caching of dwarf files since -S is used");
	}

	if (!input_file) {
		cc_log("No input file found");
		stats_update(STATS_NOINPUT);
		result = false;
		goto out;
	}

	if (found_pch || found_fpch_preprocess) {
		using_precompiled_header = true;
		if (!(conf->sloppiness & SLOPPY_TIME_MACROS)) {
			cc_log("You have to specify \"time_macros\" sloppiness when using"
			       " precompiled headers to get direct hits");
			cc_log("Disabling direct mode");
			stats_update(STATS_CANTUSEPCH);
			result = false;
			goto out;
		}
	}

	if (explicit_language && str_eq(explicit_language, "none")) {
		explicit_language = NULL;
	}
	file_language = language_for_file(input_file);
	if (explicit_language) {
		if (!language_is_supported(explicit_language)) {
			cc_log("Unsupported language: %s", explicit_language);
			stats_update(STATS_SOURCELANG);
			result = false;
			goto out;
		}
		actual_language = explicit_language;
	} else {
		actual_language = file_language;
	}

	output_is_precompiled_header =
		actual_language && strstr(actual_language, "-header");

	if (output_is_precompiled_header
	    && !(conf->sloppiness & SLOPPY_PCH_DEFINES)) {
		cc_log("You have to specify \"pch_defines,time_macros\" sloppiness when"
		       " creating precompiled headers");
		stats_update(STATS_CANTUSEPCH);
		result = false;
		goto out;
	}

	if (!found_c_opt && !found_S_opt) {
		if (output_is_precompiled_header) {
			args_add(stripped_args, "-c");
		} else {
			cc_log("No -c option found");
			// I find that having a separate statistic for autoconf tests is useful,
			// as they are the dominant form of "called for link" in many cases.
			if (strstr(input_file, "conftest.")) {
				stats_update(STATS_CONFTEST);
			} else {
				stats_update(STATS_LINK);
			}
			result = false;
			goto out;
		}
	}

	if (!actual_language) {
		cc_log("Unsupported source extension: %s", input_file);
		stats_update(STATS_SOURCELANG);
		result = false;
		goto out;
	}

	direct_i_file = language_is_preprocessed(actual_language);

	if (output_is_precompiled_header && !conf->run_second_cpp) {
		// It doesn't work to create the .gch from preprocessed source.
		cc_log("Creating precompiled header; not compiling preprocessed code");
		conf->run_second_cpp = true;
	}

	if (str_eq(conf->cpp_extension, "")) {
		const char *p_language = p_language_for_language(actual_language);
		free(conf->cpp_extension);
		conf->cpp_extension = x_strdup(extension_for_language(p_language) + 1);
	}

	// Don't try to second guess the compilers heuristics for stdout handling.
	if (output_obj && str_eq(output_obj, "-")) {
		stats_update(STATS_OUTSTDOUT);
		cc_log("Output file is -");
		result = false;
		goto out;
	}

	if (!output_obj) {
		if (output_is_precompiled_header) {
			output_obj = format("%s.gch", input_file);
		} else {
			output_obj = basename(input_file);
			char *p = strrchr(output_obj, '.');
			if (!p || !p[1]) {
				cc_log("Badly formed object filename");
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}
			p[1] = found_S_opt ? 's' : 'o';
			p[2] = 0;
		}
	}

	if (using_split_dwarf) {
		char *p = strrchr(output_obj, '.');
		if (!p || !p[1]) {
			cc_log("Badly formed object filename");
			stats_update(STATS_ARGS);
			result = false;
			goto out;
		}

		char *base_name = remove_extension(output_obj);
		output_dwo = format("%s.dwo", base_name);
		free(base_name);
	}

	// Cope with -FoDebug/ directory name
	if (input_file
	    && compiler_is_msvc(args)
	    && output_obj &&
	    (str_endswith(output_obj, "/") || str_endswith(output_obj, "\\"))) {
		char *base = basename(input_file);
		char *next = remove_extension(base);
		char *obj = format("%s%s.obj", output_obj, next);
		free(output_obj); output_obj = obj;
		free(next);
		free(base);
	}

	// Cope with -o /dev/null.
	struct stat st;
	if (output_obj
	    && !str_eq(output_obj, "/dev/null")
	    && stat(output_obj, &st) == 0
	    && !S_ISREG(st.st_mode)) {
		cc_log("Not a regular file: %s", output_obj);
		stats_update(STATS_DEVICE);
		result = false;
		goto out;
	}

	// Some options shouldn't be passed to the real compiler when it compiles
	// preprocessed code:
	//
	// -finput-charset=XXX (otherwise conversion happens twice)
	// -x XXX (otherwise the wrong language is selected)
	if (input_charset) {
		args_add(cpp_args, input_charset);
	}
	if (found_pch) {
		args_add(cpp_args, "-fpch-preprocess");
	}
	if (explicit_language) {
		args_add(cpp_args, "-x");
		args_add(cpp_args, explicit_language);
	}

	// Since output is redirected, compilers will not color their output by
	// default, so force it explicitly if it would be otherwise done.
	if (!found_color_diagnostics && color_output_possible()) {
		if (compiler_is_clang(args)) {
			if (!str_eq(actual_language, "assembler")) {
				args_add(stripped_args, "-fcolor-diagnostics");
				cc_log("Automatically enabling colors");
			}
		} else if (compiler_is_gcc(args)) {
			// GCC has it since 4.9, but that'd require detecting what GCC version is
			// used for the actual compile. However it requires also GCC_COLORS to be
			// set (and not empty), so use that for detecting if GCC would use
			// colors.
			if (getenv("GCC_COLORS") && getenv("GCC_COLORS")[0] != '\0') {
				args_add(stripped_args, "-fdiagnostics-color");
				cc_log("Automatically enabling colors");
			}
		}
	}

	// Add flags for dependency generation only to the preprocessor command line.
	if (generating_dependencies) {
		if (!dependency_filename_specified) {
			char *base_name = remove_extension(output_obj);
			char *default_depfile_name = format("%s.d", base_name);
			free(base_name);
			args_add(dep_args, "-MF");
			args_add(dep_args, default_depfile_name);
			output_dep = make_relative_path(x_strdup(default_depfile_name));
		}

		if (!dependency_target_specified) {
			args_add(dep_args, "-MQ");
			args_add(dep_args, output_obj);
		}
	}
	if (generating_coverage) {
		char *base_name = remove_extension(output_obj);
		char *default_covfile_name = format("%s.gcno", base_name);
		free(base_name);
		output_cov = make_relative_path(default_covfile_name);
	}
	if (generating_stackusage) {
		char *base_name = remove_extension(output_obj);
		char *default_sufile_name = format("%s.su", base_name);
		free(base_name);
		output_su = make_relative_path(default_sufile_name);
	}

	*compiler_args = args_copy(stripped_args);
	if (conf->run_second_cpp) {
		args_extend(*compiler_args, cpp_args);
	} else if (explicit_language) {
		// Workaround for a bug in Apple's patched distcc -- it doesn't properly
		// reset the language specified with -x, so if -x is given, we have to
		// specify the preprocessed language explicitly.
		args_add(*compiler_args, "-x");
		args_add(*compiler_args, p_language_for_language(explicit_language));
	}

	if (found_c_opt) {
		args_add(*compiler_args, "-c");
	}

	for (size_t i = 0; i < arch_args_size; ++i) {
		args_add(*compiler_args, "-arch");
		args_add(*compiler_args, arch_args[i]);
	}

	// Only pass dependency arguments to the preprocesor since Intel's C++
	// compiler doesn't produce a correct .d file when compiling preprocessed
	// source.
	args_extend(cpp_args, dep_args);

	*preprocessor_args = args_copy(stripped_args);
	args_extend(*preprocessor_args, cpp_args);

out:
	args_free(expanded_args);
	args_free(stripped_args);
	args_free(dep_args);
	args_free(cpp_args);
	return result;
}

static void
create_initial_config_file(struct conf *conf, const char *path)
{
	if (create_parent_dirs(path) != 0) {
		return;
	}

	unsigned max_files;
	uint64_t max_size;
	char *stats_dir = format("%s/0", conf->cache_dir);
	struct stat st;
	if (stat(stats_dir, &st) == 0) {
		stats_get_obsolete_limits(stats_dir, &max_files, &max_size);
		// STATS_MAXFILES and STATS_MAXSIZE was stored for each top directory.
		max_files *= 16;
		max_size *= 16;
	} else {
		max_files = 0;
		max_size = conf->max_size;
	}
	free(stats_dir);

	FILE *f = fopen(path, "w");
	if (!f) {
		return;
	}
	if (max_files != 0) {
		fprintf(f, "max_files = %u\n", max_files);
		conf->max_files = max_files;
	}
	if (max_size != 0) {
		char *size = format_parsable_size_with_suffix(max_size);
		fprintf(f, "max_size = %s\n", size);
		free(size);
		conf->max_size = max_size;
	}
	fclose(f);
}

// Read config file(s), populate variables, create configuration file in cache
// directory if missing, etc.
static void
initialize(void)
{
	conf_free(conf);
	conf = conf_create();

	char *errmsg;
	struct stat st;
	char *p = getenv("CCACHE_CONFIGPATH");
	if (p) {
		primary_config_path = x_strdup(p);
	} else {
		secondary_config_path = format("%s/ccache.conf", TO_STRING(SYSCONFDIR));
		if (!conf_read(conf, secondary_config_path, &errmsg)) {
			if (stat(secondary_config_path, &st) == 0) {
				warn("%s", errmsg);
			}
			// Missing config file in SYSCONFDIR is OK.
			free(errmsg);
		}

		if (str_eq(conf->cache_dir, "")) {
			fatal("configuration setting \"cache_dir\" must not be the empty string");
		}
		if ((p = getenv("CCACHE_DIR"))) {
			free(conf->cache_dir);
			conf->cache_dir = strdup(p);
		}
		if (str_eq(conf->cache_dir, "")) {
			fatal("CCACHE_DIR must not be the empty string");
		}

		primary_config_path = format("%s/ccache.conf", conf->cache_dir);
	}

	bool should_create_initial_config = false;
	if (!conf_read(conf, primary_config_path, &errmsg)) {
		if (stat(primary_config_path, &st) == 0) {
			warn("%s", errmsg);
		}
		should_create_initial_config = true;
	}

	if (!conf_update_from_environment(conf, &errmsg)) {
		warn("%s", errmsg);
	}

	if (conf->disable) {
		should_create_initial_config = false;
	}

	if (should_create_initial_config) {
		create_initial_config_file(conf, primary_config_path);
	}

	exitfn_init();
	exitfn_add_nullary(stats_flush);
	exitfn_add_nullary(clean_up_pending_tmp_files);

	cc_log("=== CCACHE %s STARTED =========================================",
	       CCACHE_VERSION);

	if (conf->umask != UINT_MAX) {
		umask(conf->umask);
	}
}

// Reset the global state. Used by the test suite.
void
cc_reset(void)
{
	conf_free(conf); conf = NULL;
	free(primary_config_path); primary_config_path = NULL;
	free(secondary_config_path); secondary_config_path = NULL;
	free(current_working_dir); current_working_dir = NULL;
	free(debug_prefix_map); debug_prefix_map = NULL;
	free(profile_dir); profile_dir = NULL;
	free(included_pch_file); included_pch_file = NULL;
	args_free(orig_args); orig_args = NULL;
	free(input_file); input_file = NULL;
	free(output_obj); output_obj = NULL;
	free(output_dwo); output_dwo = NULL;
	free(output_dep); output_dep = NULL;
	free(output_cov); output_cov = NULL;
	free(output_su); output_su = NULL;
	free(output_dia); output_dia = NULL;
	free(cached_obj_hash); cached_obj_hash = NULL;
	free(cached_obj); cached_obj = NULL;
	free(cached_dwo); cached_dwo = NULL;
	free(cached_stderr); cached_stderr = NULL;
	free(cached_dep); cached_dep = NULL;
	free(cached_cov); cached_cov = NULL;
	free(cached_su); cached_su = NULL;
	free(cached_dia); cached_dia = NULL;
	free(manifest_path); manifest_path = NULL;
	time_of_compilation = 0;
	for (size_t i = 0; i < ignore_headers_len; i++) {
		free(ignore_headers[i]);
		ignore_headers[i] = NULL;
	}
	free(ignore_headers); ignore_headers = NULL;
	ignore_headers_len = 0;
	if (included_files) {
		hashtable_destroy(included_files, 1); included_files = NULL;
	}
	has_absolute_include_headers = false;
	generating_debuginfo = false;
	generating_dependencies = false;
	generating_coverage = false;
	generating_stackusage = false;
	profile_arcs = false;
	free(profile_dir); profile_dir = NULL;
	i_tmpfile = NULL;
	direct_i_file = false;
	free(cpp_stderr); cpp_stderr = NULL;
	free(stats_file); stats_file = NULL;
	output_is_precompiled_header = false;

	conf = conf_create();
	using_split_dwarf = false;
}

// Make a copy of stderr that will not be cached, so things like distcc can
// send networking errors to it.
static void
setup_uncached_err(void)
{
	int uncached_fd = dup(2);
	if (uncached_fd == -1) {
		cc_log("dup(2) failed: %s", strerror(errno));
		failed();
	}

	// Leak a pointer to the environment.
	char *buf = format("UNCACHED_ERR_FD=%d", uncached_fd);
	if (putenv(buf) == -1) {
		cc_log("putenv failed: %s", strerror(errno));
		failed();
	}
}

static void
configuration_logger(const char *descr, const char *origin, void *context)
{
	(void)context;
	cc_bulklog("Config: (%s) %s", origin, descr);
}

// The main ccache driver function.
static void
ccache(int argc, char *argv[])
{
#ifndef _WIN32
	set_up_signal_handlers();
#endif

	orig_args = args_init(argc, argv);

	initialize();
	find_compiler(argv);

	if (str_eq(conf->temporary_dir, "")) {
		clean_up_internal_tempdir();
	}

	if (!str_eq(conf->log_file, "")) {
		conf_print_items(conf, configuration_logger, NULL);
	}

	if (conf->disable) {
		cc_log("ccache is disabled");
		failed();
	}

	setup_uncached_err();

	cc_log_argv("Command line: ", argv);
	cc_log("Hostname: %s", get_hostname());
	cc_log("Working directory: %s", get_current_working_dir());

	if (conf->unify) {
		cc_log("Direct mode disabled because unify mode is enabled");
		conf->direct_mode = false;
	}

	conf->limit_multiple = MIN(MAX(conf->limit_multiple, 0.0f), 1.0f);

	// Arguments (except -E) to send to the preprocessor.
	struct args *preprocessor_args;
	// Arguments to send to the real compiler.
	struct args *compiler_args;
	if (!cc_process_args(orig_args, &preprocessor_args, &compiler_args)) {
		failed();
	}

	cc_log("Source file: %s", input_file);
	if (generating_dependencies) {
		cc_log("Dependency file: %s", output_dep);
	}
	if (generating_coverage) {
		cc_log("Coverage file: %s", output_cov);
	}
	if (generating_stackusage) {
		cc_log("Stack usage file: %s", output_su);
	}
	if (output_dia) {
		cc_log("Diagnostic file: %s", output_dia);
	}

	if (using_split_dwarf) {
		if (!generating_dependencies) {
			assert(output_dwo);
		}
	} else {
		assert(!output_dwo);
	}

	if (output_dwo) {
		cc_log("Split dwarf file: %s", output_dwo);
	}

	cc_log("Object file: %s", output_obj);

	struct mdfour common_hash;
	hash_start(&common_hash);
	calculate_common_hash(preprocessor_args, &common_hash);

	// Try to find the hash using the manifest.
	struct mdfour direct_hash = common_hash;
	bool put_object_in_manifest = false;
	struct file_hash *object_hash = NULL;
	struct file_hash *object_hash_from_manifest = NULL;
	if (conf->direct_mode) {
		cc_log("Trying direct lookup");
		object_hash = calculate_object_hash(preprocessor_args, &direct_hash, 1);
		if (object_hash) {
			update_cached_result_globals(object_hash);

			// If we can return from cache at this point then do so.
			from_cache(FROMCACHE_DIRECT_MODE, 0);

			// Wasn't able to return from cache at this point. However, the object
			// was already found in manifest, so don't readd it later.
			put_object_in_manifest = false;

			object_hash_from_manifest = object_hash;
		} else {
			// Add object to manifest later.
			put_object_in_manifest = true;
		}
	}

	if (conf->read_only_direct) {
		cc_log("Read-only direct mode; running real compiler");
		failed();
	}

	// Find the hash using the preprocessed output. Also updates included_files.
	struct mdfour cpp_hash = common_hash;
	object_hash = calculate_object_hash(preprocessor_args, &cpp_hash, 0);
	if (!object_hash) {
		fatal("internal error: object hash from cpp returned NULL");
	}
	update_cached_result_globals(object_hash);

	if (object_hash_from_manifest
	    && !file_hashes_equal(object_hash_from_manifest, object_hash)) {
		// The hash from manifest differs from the hash of the preprocessor output.
		// This could be because:
		//
		// - The preprocessor produces different output for the same input (not
		//   likely).
		// - There's a bug in ccache (maybe incorrect handling of compiler
		//   arguments).
		// - The user has used a different CCACHE_BASEDIR (most likely).
		//
		// The best thing here would probably be to remove the hash entry from the
		// manifest. For now, we use a simpler method: just remove the manifest
		// file.
		cc_log("Hash from manifest doesn't match preprocessor output");
		cc_log("Likely reason: different CCACHE_BASEDIRs used");
		cc_log("Removing manifest as a safety measure");
		x_unlink(manifest_path);

		put_object_in_manifest = true;
	}

	// If we can return from cache at this point then do.
	from_cache(FROMCACHE_CPP_MODE, put_object_in_manifest);

	if (conf->read_only) {
		cc_log("Read-only mode; running real compiler");
		failed();
	}

	add_prefix(compiler_args, conf->prefix_command);

	// Run real compiler, sending output to cache.
	to_cache(compiler_args);

	x_exit(0);
}

static void
configuration_printer(const char *descr, const char *origin, void *context)
{
	assert(context);
	fprintf(context, "(%s) %s\n", origin, descr);
}

// The main program when not doing a compile.
static int
ccache_main_options(int argc, char *argv[])
{
	enum longopts {
		DUMP_MANIFEST
	};
	static const struct option options[] = {
		{"cleanup",       no_argument,       0, 'c'},
		{"clear",         no_argument,       0, 'C'},
		{"dump-manifest", required_argument, 0, DUMP_MANIFEST},
		{"help",          no_argument,       0, 'h'},
		{"max-files",     required_argument, 0, 'F'},
		{"max-size",      required_argument, 0, 'M'},
		{"set-config",    required_argument, 0, 'o'},
		{"print-config",  no_argument,       0, 'p'},
		{"show-stats",    no_argument,       0, 's'},
		{"version",       no_argument,       0, 'V'},
		{"zero-stats",    no_argument,       0, 'z'},
		{0, 0, 0, 0}
	};

	int c;
	while ((c = getopt_long(argc, argv, "cChF:M:o:psVz", options, NULL)) != -1) {
		switch (c) {
		case DUMP_MANIFEST:
			manifest_dump(optarg, stdout);
			break;

		case 'c': // --cleanup
			initialize();
			cleanup_all(conf);
			printf("Cleaned cache\n");
			break;

		case 'C': // --clear
			initialize();
			wipe_all(conf);
			printf("Cleared cache\n");
			break;

		case 'h': // --help
			fputs(USAGE_TEXT, stdout);
			x_exit(0);

		case 'F': // --max-files
		{
			initialize();
			char *errmsg;
			if (conf_set_value_in_file(primary_config_path, "max_files", optarg,
			                           &errmsg)) {
				unsigned files = atoi(optarg);
				if (files == 0) {
					printf("Unset cache file limit\n");
				} else {
					printf("Set cache file limit to %u\n", files);
				}
			} else {
				fatal("could not set cache file limit: %s", errmsg);
			}
		}
		break;

		case 'M': // --max-size
		{
			initialize();
			uint64_t size;
			if (!parse_size_with_suffix(optarg, &size)) {
				fatal("invalid size: %s", optarg);
			}
			char *errmsg;
			if (conf_set_value_in_file(primary_config_path, "max_size", optarg,
			                           &errmsg)) {
				if (size == 0) {
					printf("Unset cache size limit\n");
				} else {
					char *s = format_human_readable_size(size);
					printf("Set cache size limit to %s\n", s);
					free(s);
				}
			} else {
				fatal("could not set cache size limit: %s", errmsg);
			}
		}
		break;

		case 'o': // --set-config
		{
			initialize();
			char *p = strchr(optarg, '=');
			if (!p) {
				fatal("missing equal sign in \"%s\"", optarg);
			}
			char *key = x_strndup(optarg, p - optarg);
			char *value = p + 1;
			char *errmsg;
			if (!conf_set_value_in_file(primary_config_path, key, value, &errmsg)) {
				fatal("%s", errmsg);
			}
			free(key);
		}
		break;

		case 'p': // --print-config
			initialize();
			conf_print_items(conf, configuration_printer, stdout);
			break;

		case 's': // --show-stats
			initialize();
			stats_summary(conf);
			break;

		case 'V': // --version
			fprintf(stdout, VERSION_TEXT, CCACHE_VERSION);
			x_exit(0);

		case 'z': // --zero-stats
			initialize();
			stats_zero();
			printf("Statistics cleared\n");
			break;

		default:
			fputs(USAGE_TEXT, stderr);
			x_exit(1);
		}
	}

	return 0;
}

int
ccache_main(int argc, char *argv[])
{
	// Check if we are being invoked as "ccache".
	char *program_name = basename(argv[0]);
	if (same_executable_name(program_name, MYNAME)) {
		if (argc < 2) {
			fputs(USAGE_TEXT, stderr);
			x_exit(1);
		}
		// If the first argument isn't an option, then assume we are being passed a
		// compiler name and options.
		if (argv[1][0] == '-') {
			return ccache_main_options(argc, argv);
		}
	}
	free(program_name);

	ccache(argc, argv);
	return 1;
}
