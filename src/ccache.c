// ccache -- a fast C/C++ compiler cache
//
// Copyright (C) 2002-2007 Andrew Tridgell
// Copyright (C) 2009-2020 Joel Rosdahl
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
#include "hash.h"
#include "hashtable.h"
#include "hashtable_itr.h"
#include "hashutil.h"
#include "language.h"
#include "manifest.h"

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

// Global variables used by other compilation units.
extern struct conf *conf;
extern char *primary_config_path;
extern char *secondary_config_path;
extern char *current_working_dir;
extern char *stats_file;
extern unsigned lock_staleness_limit;

static const char VERSION_TEXT[] =
	MYNAME " version %s\n"
	"\n"
	"Copyright (C) 2002-2007 Andrew Tridgell\n"
	"Copyright (C) 2009-2020 Joel Rosdahl\n"
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
	"Common options:\n"
	"    -c, --cleanup             delete old files and recalculate size counters\n"
	"                              (normally not needed as this is done\n"
	"                              automatically)\n"
	"    -C, --clear               clear the cache completely (except configuration)\n"
	"    -F, --max-files=N         set maximum number of files in cache to N (use 0\n"
	"                              for no limit)\n"
	"    -M, --max-size=SIZE       set maximum size of cache to SIZE (use 0 for no\n"
	"                              limit); available suffixes: k, M, G, T (decimal)\n"
	"                              and Ki, Mi, Gi, Ti (binary); default suffix: G\n"
	"    -p, --show-config         show current configuration options in\n"
	"                              human-readable format\n"
	"    -s, --show-stats          show summary of configuration and statistics\n"
	"                              counters in human-readable format\n"
	"    -z, --zero-stats          zero statistics counters\n"
	"\n"
	"    -h, --help                print this help text\n"
	"    -V, --version             print version and copyright information\n"
	"\n"
	"Options for scripting or debugging:\n"
	"        --dump-manifest=PATH  dump manifest file at PATH in text format\n"
	"    -k, --get-config=K        print the value of configuration key K\n"
	"        --hash-file=PATH      print the hash (<MD4>-<size>) of the file at PATH\n"
	"        --print-stats         print statistics counter IDs and corresponding\n"
	"                              values in machine-parsable format\n"
	"    -o, --set-config=K=V      set configuration item K to value V\n"
	"\n"
	"See also <https://ccache.dev>.\n";

static const char *preproc_envvars[] = {
	"CPATH",
	"C_INCLUDE_PATH",
	"CPLUS_INCLUDE_PATH",
	"OBJC_INCLUDE_PATH",
	"OBJCPLUS_INCLUDE_PATH", // clang
	NULL
};

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

// Argument list to add to compiler invocation in depend mode.
static struct args *depend_extra_args;

// The source file.
static char *input_file;

// The output file being compiled to.
static char *output_obj;

// The path to the dependency file (implicit or specified with -MF).
static char *output_dep;

// The path to the coverage file (implicit when using -ftest-coverage).
static char *output_cov;

// The path to the stack usage (implicit when using -fstack-usage).
static char *output_su;

// Diagnostic generation information (clang). Contains pathname if not NULL.
static char *output_dia;

// Split dwarf information (GCC 4.8 and up). Contains pathname if not NULL.
static char *output_dwo;

// Language to use for the compilation target (see language.c).
static const char *actual_language;

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

// Is the compiler being asked to output debug info on level 3?
static bool generating_debuginfo_level_3;

// Is the compiler being asked to output dependencies?
static bool generating_dependencies;

// Is the compiler being asked to output coverage?
static bool generating_coverage;

// Is the compiler being asked to output stack usage?
static bool generating_stackusage;

// Us the compiler being asked to generate diagnostics
// (--serialize-diagnostics)?
static bool generating_diagnostics;

// Is the compiler being asked to separate dwarf debug info into a separate
// file (-gsplit-dwarf)"?
static bool using_split_dwarf;

// Relocating debuginfo in the format old=new.
static char **debug_prefix_maps = NULL;

// Size of debug_prefix_maps list.
static size_t debug_prefix_maps_len = 0;

// Is the compiler being asked to output coverage data (.gcda) at runtime?
static bool profile_arcs;

// The name of the temporary preprocessed file.
static char *i_tmpfile;

// Are we compiling a .i or .ii file directly?
static bool direct_i_file;

// The name of the cpp stderr file.
static char *cpp_stderr;

// Full path to the statistics file in the subdirectory where the cached result
// belongs (<cache_dir>/<x>/stats).
char *stats_file = NULL;

// The stats file to use for the manifest.
static char *manifest_stats_file;

// Whether the output is a precompiled header.
bool output_is_precompiled_header = false;

// Compiler guessing is currently only based on the compiler name, so nothing
// should hard-depend on it if possible.
enum guessed_compiler guessed_compiler = GUESSED_UNKNOWN;

// Profile generation / usage information.
static char *profile_path = NULL; // directory (GCC/Clang) or file (Clang)
static bool profile_use = false;
static bool profile_generate = false;

// Sanitize blacklist
static char **sanitize_blacklists = NULL;

// Size of sanitize_blacklists
static size_t sanitize_blacklists_len = 0;

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

// How often (in seconds) to scan $CCACHE_DIR/tmp for left-over temporary
// files.
static const int k_tempdir_cleanup_interval = 2 * 24 * 60 * 60; // 2 days

#ifndef _WIN32
static sigset_t fatal_signal_set;

// PID of currently executing compiler that we have started, if any. 0 means no
// ongoing compilation.
static pid_t compiler_pid = 0;
#endif

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

// Compiler in depend mode is invoked with the original arguments.
// Collect extra arguments that should be added.
static void
add_extra_arg(const char *arg)
{
	if (conf->depend_mode) {
		if (depend_extra_args == NULL) {
			depend_extra_args = args_init(0, NULL);
		}
		args_add(depend_extra_args, arg);
	}
}

static void failed(void) ATTR_NORETURN;

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
	if (x_stat(conf->cache_dir, &st) != 0
	    || st.st_mtime + k_tempdir_cleanup_interval >= now) {
		// No cleanup needed.
		return;
	}

	update_mtime(conf->cache_dir);

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
		if (x_lstat(path, &st) == 0
		    && st.st_mtime + k_tempdir_cleanup_interval < now) {
			tmp_unlink(path);
		}
		free(path);
	}

	closedir(dir);
}

static void
fclose_exitfn(void *context)
{
	fclose((FILE *)context);
}

static void
dump_debug_log_buffer_exitfn(void *context)
{
	if (!conf->debug) {
		return;
	}

	char *path = format("%s.ccache-log", (const char *)context);
	cc_dump_debug_log_buffer(path);
	free(path);
}

static void
init_hash_debug(struct hash *hash, const char *obj_path, char type,
                const char *section_name, FILE *debug_text_file)
{
	if (!conf->debug) {
		return;
	}

	char *path = format("%s.ccache-input-%c", obj_path, type);
	FILE *debug_binary_file = fopen(path, "wb");
	if (debug_binary_file) {
		hash_enable_debug(hash, section_name, debug_binary_file, debug_text_file);
		exitfn_add(fclose_exitfn, debug_binary_file);
	} else {
		cc_log("Failed to open %s: %s", path, strerror(errno));
	}
	free(path);
}

static enum guessed_compiler
guess_compiler(const char *path)
{
	char *name = basename(path);
	enum guessed_compiler result = GUESSED_UNKNOWN;
	if (strstr(name, "clang")) {
		result = GUESSED_CLANG;
	} else if (strstr(name, "gcc") || strstr(name, "g++")) {
		result = GUESSED_GCC;
	} else if (strstr(name, "nvcc")) {
		result = GUESSED_NVCC;
	} else if (str_eq(name, "pump") || str_eq(name, "distcc-pump")) {
		result = GUESSED_PUMP;
	}
	free(name);
	return result;
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
			stats_update(STATS_ERROR);
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
remember_include_file(char *path, struct hash *cpp_hash, bool system,
                      struct hash *depend_mode_hash)
{
	struct hash *fhash = NULL;

	size_t path_len = strlen(path);
	if (path_len >= 2 && (path[0] == '<' && path[path_len - 1] == '>')) {
		// Typically <built-in> or <command-line>.
		goto out;
	}

	if (str_eq(path, input_file)) {
		// Don't remember the input file.
		goto out;
	}

	if (system && (conf->sloppiness & SLOPPY_SYSTEM_HEADERS)) {
		// Don't remember this system header.
		goto out;
	}

	if (hashtable_search(included_files, path)) {
		// Already known include file.
		goto out;
	}

#ifdef _WIN32
	// stat fails on directories on win32.
	DWORD attributes = GetFileAttributes(path);
	if (attributes != INVALID_FILE_ATTRIBUTES &&
	    attributes & FILE_ATTRIBUTE_DIRECTORY) {
		goto out;
	}
#endif

	struct stat st;
	if (x_stat(path, &st) != 0) {
		goto failure;
	}
	if (S_ISDIR(st.st_mode)) {
		// Ignore directory, typically $PWD.
		goto out;
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
			goto out;
		}
	}

	// The comparison using >= is intentional, due to a possible race between
	// starting compilation and writing the include file. See also the notes
	// under "Performance" in doc/MANUAL.adoc.
	if (!(conf->sloppiness & SLOPPY_INCLUDE_FILE_MTIME)
	    && st.st_mtime >= time_of_compilation) {
		cc_log("Include file %s too new", path);
		goto failure;
	}

	// The same >= logic as above applies to the change time of the file.
	if (!(conf->sloppiness & SLOPPY_INCLUDE_FILE_CTIME)
	    && st.st_ctime >= time_of_compilation) {
		cc_log("Include file %s ctime too new", path);
		goto failure;
	}

	// Let's hash the include file content.
	fhash = hash_init();

	bool is_pch = is_precompiled_header(path);
	if (is_pch) {
		if (!included_pch_file) {
			cc_log("Detected use of precompiled header: %s", path);
		}
		bool using_pch_sum = false;
		if (conf->pch_external_checksum) {
			// hash pch.sum instead of pch when it exists
			// to prevent hashing a very large .pch file every time
			char *pch_sum_path = format("%s.sum", path);
			if (x_stat(pch_sum_path, &st) == 0) {
				char *old_path = path;
				path = pch_sum_path;
				pch_sum_path = old_path;
				using_pch_sum = true;
				cc_log("Using pch.sum file %s", path);
			}
			free(pch_sum_path);
		}

		if (!hash_file(fhash, path)) {
			goto failure;
		}
		hash_delimiter(cpp_hash, using_pch_sum ? "pch_sum_hash" : "pch_hash");
		char *pch_hash_result = hash_result(fhash);
		hash_string(cpp_hash, pch_hash_result);
		free(pch_hash_result);
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

			int result = hash_source_code_string(conf, fhash, source, size, path);
			free(source);
			if (result & HASH_SOURCE_CODE_ERROR
			    || result & HASH_SOURCE_CODE_FOUND_TIME) {
				goto failure;
			}
		}

		struct file_hash *h = x_malloc(sizeof(*h));
		hash_result_as_bytes(fhash, h->hash);
		h->size = hash_input_size(fhash);
		hashtable_insert(included_files, path, h);
		path = NULL; // Ownership transferred to included_files.

		if (depend_mode_hash) {
			hash_delimiter(depend_mode_hash, "include");
			char *result = format_hash_as_string(h->hash, h->size);
			hash_string(depend_mode_hash, result);
			free(result);
		}
	}

	goto out;

failure:
	if (conf->direct_mode) {
		cc_log("Disabling direct mode");
		conf->direct_mode = false;
	}
	// Fall through.
out:
	hash_free(fhash);
	free(path);
}

static void
print_included_files(FILE *fp)
{
	struct hashtable_itr *iter = hashtable_iterator(included_files);
	do {
		char *path = hashtable_iterator_key(iter);
		fprintf(fp, "%s\n", path);
	} while (hashtable_iterator_advance(iter));
}

// Make a relative path from current working directory to path if path is under
// the base directory. Takes over ownership of path. Caller frees.
static char *
make_relative_path(char *path)
{
	if (str_eq(conf->base_dir, "") || !str_startswith(path, conf->base_dir)) {
		return path;
	}

#ifdef _WIN32
	if (path[0] == '/') {
		char *p = NULL;
		if (isalpha(path[1]) && path[2] == '/') {
			// Transform /c/path... to c:/path...
			p = format("%c:/%s", path[1], &path[3]);
		} else {
			p = x_strdup(path+1); // Skip leading slash.
		}
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
		// find the nearest existing directory in path
		while (stat(dir, &st) != 0) {
			char *parent_dir = dirname(dir);
			free(dir);
			dir = parent_dir;
		}

		// suffix is the remaining of the path, skip the first delimiter
		size_t dir_len = strlen(dir);
		if (path[dir_len] == '/' || path[dir_len] == '\\') {
			dir_len++;
		}
		path_suffix = x_strdup(&path[dir_len]);
		char *p = path;
		path = dir;
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

static void
init_included_files_table(void)
{
	// (This function may be called multiple times if several -arch options are
	// used.)
	if (!included_files) {
		included_files = create_hashtable(1000, hash_from_string, strings_equal);
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
process_preprocessed_file(struct hash *hash, const char *path, bool pump)
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

	init_included_files_table();

	char *cwd = get_cwd();

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
					hash_string_buffer(hash, p, q - p);
					while (q < end && *q != '\n') {
						q++;
					}
					q++;
					p = q;
					continue;
				} else if (str_startswith(q, "# 32 \"<command-line>\" 2\n")) {
					// Bogus wrong line with #32, instead of regular #1: Replace the line
					// number with the usual one.
					hash_string_buffer(hash, p, q - p);
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
				free(cwd);
				return false;
			}
			// q points to the beginning of an include file path
			hash_string_buffer(hash, p, q - p);
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
			}
			if (should_hash_inc_path) {
				hash_string_buffer(hash, inc_path, strlen(inc_path));
			}

			remember_include_file(inc_path, hash, system, NULL);
			p = q; // Everything of interest between p and q has been hashed now.
		} else if (q[0] == '.' && q[1] == 'i' && q[2] == 'n' && q[3] == 'c'
		           && q[4] == 'b' && q[5] == 'i' && q[6] == 'n') {
			// An assembler .inc bin (without the space) statement, which could be
			// part of inline assembly, refers to an external file. If the file
			// changes, the hash should change as well, but finding out what file to
			// hash is too hard for ccache, so just bail out.
			cc_log("Found unsupported .inc" "bin directive in source code");
			stats_update(STATS_UNSUPPORTED_DIRECTIVE);
			failed();
		} else if (pump && strncmp(q, "_________", 9) == 0) {
			// Unfortunately the distcc-pump wrapper outputs standard output lines:
			// __________Using distcc-pump from /usr/bin
			// __________Using # distcc servers in pump mode
			// __________Shutting down distcc-pump include server
			while (q < end && *q != '\n') {
				q++;
			}
			if (*q == '\n') {
				q++;
			}
			p = q;
			continue;
		} else {
			q++;
		}
	}

	hash_string_buffer(hash, p, (end - p));
	free(data);
	free(cwd);

	// Explicitly check the .gch/.pch/.pth file as Clang does not include any
	// mention of it in the preprocessed output.
	if (included_pch_file) {
		char *pch_path = x_strdup(included_pch_file);
		pch_path = make_relative_path(pch_path);
		hash_string(hash, pch_path);
		remember_include_file(pch_path, hash, false, NULL);
	}

	bool debug_included = getenv("CCACHE_DEBUG_INCLUDED");
	if (debug_included) {
		print_included_files(stdout);
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
		tmp_unlink(tmp_file);
	}
	free(tmp_file);
}

// Extract the used includes from the dependency file. Note that we cannot
// distinguish system headers from other includes here.
static struct file_hash *
object_hash_from_depfile(const char *depfile, struct hash *hash)
{
	FILE *f = fopen(depfile, "r");
	if (!f) {
		cc_log("Cannot open dependency file %s: %s", depfile, strerror(errno));
		return NULL;
	}

	init_included_files_table();

	char buf[10000];
	while (fgets(buf, sizeof(buf), f) && !ferror(f)) {
		char *saveptr;
		char *token;
		for (token = strtok_r(buf, " \t\n", &saveptr);
		     token;
		     token = strtok_r(NULL, " \t\n", &saveptr)) {
			if (str_endswith(token, ":") || str_eq(token, "\\")) {
				continue;
			}
			if (!has_absolute_include_headers) {
				has_absolute_include_headers = is_absolute_path(token);
			}
			char *path = make_relative_path(x_strdup(token));
			remember_include_file(path, hash, false, hash);
		}
	}

	fclose(f);

	// Explicitly check the .gch/.pch/.pth file as it may not be mentioned in the
	// dependencies output.
	if (included_pch_file) {
		char *pch_path = x_strdup(included_pch_file);
		pch_path = make_relative_path(pch_path);
		hash_string(hash, pch_path);
		remember_include_file(pch_path, hash, false, NULL);
	}

	bool debug_included = getenv("CCACHE_DEBUG_INCLUDED");
	if (debug_included) {
		print_included_files(stdout);
	}

	struct file_hash *result = x_malloc(sizeof(*result));
	hash_result_as_bytes(hash, result->hash);
	result->size = hash_input_size(hash);
	return result;
}

// Helper function for put_file_in_cache and move_file_to_cache_same_fs.
static void
do_copy_or_move_file_to_cache(const char *source, const char *dest, bool copy,
                              bool attempt_link)
{
	assert(!conf->read_only);
	assert(!conf->read_only_direct);

	struct stat orig_dest_st;
	bool orig_dest_existed = stat(dest, &orig_dest_st) == 0;
	int compression_level = conf->compression ? conf->compression_level : 0;
	bool do_move = !copy && !conf->compression;
	bool do_link = attempt_link && copy && conf->hard_link && !conf->compression;

	if (do_move) {
		move_uncompressed_file(source, dest, compression_level);
	} else {
		if (do_link) {
			x_unlink(dest);
			int ret = link(source, dest);
			if (ret != 0 && errno == ENOENT) {
				create_parent_dirs(dest);
				ret = link(source, dest);
			}
			if (ret != 0) {
				cc_log("Failed to link %s to %s: %s", source, dest, strerror(errno));
				cc_log("Falling back to copying");
				do_link = false;
			}
		}
		if (!do_link) {
			int ret = copy_file(source, dest, compression_level, true);
			if (ret != 0) {
				cc_log("Failed to copy %s to %s: %s", source, dest, strerror(errno));
				stats_update(STATS_ERROR);
				failed();
			}
		}
	}

	if (!copy && conf->compression) {
		// We fell back to copying since dest should be compressed, so clean up.
		x_unlink(source);
	}

	cc_log("Stored in cache: %s -> %s (%s)",
	       source,
	       dest,
	       do_move ? "moved" : (do_link ? "linked" : "copied"));

	struct stat st;
	if (x_stat(dest, &st) != 0) {
		stats_update(STATS_ERROR);
		failed();
	}
	stats_update_size(
		stats_file,
		file_size(&st) - (orig_dest_existed ? file_size(&orig_dest_st) : 0),
		orig_dest_existed ? 0 : 1);
}

// Copy or link a file into the cache.
//
// dest must be a path in the cache (see get_path_in_cache). source does not
// have to be on the same file system as dest.
//
// An attempt will be made to hard link source to dest if conf->hard_link is
// true and conf->compression is false, otherwise copy. dest will be compressed
// if conf->compression is true.
static void
put_file_in_cache(const char *source, const char *dest)
{
	do_copy_or_move_file_to_cache(source, dest, true, true);
}

// Copy a file to the cache.
//
// dest must be a path in the cache (see get_path_in_cache). source does not
// have to be on the same file system as dest.
static void
copy_file_to_cache(const char *source, const char *dest)
{
	do_copy_or_move_file_to_cache(source, dest, true, false);
}

// Move a file into the cache.
//
// dest must be a path in the cache (see get_path_in_cache). source must be on
// the same file system as dest. dest will be compressed if conf->compression
// is true.
static void
move_file_to_cache_same_fs(const char *source, const char *dest)
{
	do_copy_or_move_file_to_cache(source, dest, false, true);
}

// Helper function for get_file_from_cache and copy_file_from_cache.
static void
do_copy_or_link_file_from_cache(const char *source, const char *dest, bool copy)
{
	if (str_eq(dest, "/dev/null")) {
		cc_log("Skipping copy from %s to %s", source, dest);
		return;
	}

	int ret;
	bool do_link = !copy && conf->hard_link && !file_is_compressed(source);
	if (do_link) {
		x_unlink(dest);
		ret = link(source, dest);
	} else {
		ret = copy_file(source, dest, 0, false);
	}

	if (ret == -1) {
		if (errno == ENOENT || errno == ESTALE) {
			cc_log("File missing in cache: %s", source);
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
		x_unlink(cached_cov);
		x_unlink(cached_su);
		x_unlink(cached_dia);
		x_unlink(cached_dwo);

		failed();
	}

	cc_log("Created from cache: %s -> %s", source, dest);
}

// Copy or link a file from the cache.
//
// source must be a path in the cache (see get_path_in_cache). dest does not
// have to be on the same file system as source.
//
// An attempt will be made to hard link source to dest if conf->hard_link is
// true and conf->compression is false, otherwise copy. dest will be compressed
// if conf->compression is true.
static void
get_file_from_cache(const char *source, const char *dest)
{
	do_copy_or_link_file_from_cache(source, dest, false);
}

// Copy a file from the cache.
static void
copy_file_from_cache(const char *source, const char *dest)
{
	do_copy_or_link_file_from_cache(source, dest, true);
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
static void
update_manifest_file(void)
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

	// See comment in get_file_hash_index for why saving of timestamps is forced
	// for precompiled headers.
	bool save_timestamp =
		(conf->sloppiness & SLOPPY_FILE_STAT_MATCHES)
		|| output_is_precompiled_header;

	MTR_BEGIN("manifest", "manifest_put");
	if (manifest_put(manifest_path, cached_obj_hash, included_files,
	                 save_timestamp)) {
		cc_log("Added object file hash to %s", manifest_path);
		if (x_stat(manifest_path, &st) == 0) {
			stats_update_size(
				manifest_stats_file,
				file_size(&st) - old_size,
				old_size == 0 ? 1 : 0);
		}
	} else {
		cc_log("Failed to add object file hash to %s", manifest_path);
	}
	MTR_END("manifest", "manifest_put");
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
	cached_dwo = get_path_in_cache(object_name, ".dwo");

	stats_file = format("%s/%c/stats", conf->cache_dir, object_name[0]);
	free(object_name);
}

// Run the real compiler and put the result in cache.
static void
to_cache(struct args *args, struct hash *depend_mode_hash)
{
	args_add(args, "-o");
	args_add(args, output_obj);

	if (conf->hard_link && !str_eq(output_obj, "/dev/null")) {
		// This is a workaround for https://bugs.llvm.org/show_bug.cgi?id=39782.
		x_unlink(output_obj);
	}

	if (generating_diagnostics) {
		args_add(args, "--serialize-diagnostics");
		args_add(args, output_dia);
	}

	// Turn off DEPENDENCIES_OUTPUT when running cc1, because otherwise it will
	// emit a line like this:
	//
	//   tmp.stdout.vexed.732.o: /home/mbp/.ccache/tmp.stdout.vexed.732.i
	x_unsetenv("DEPENDENCIES_OUTPUT");
	x_unsetenv("SUNPRO_DEPENDENCIES");

	if (conf->run_second_cpp) {
		args_add(args, input_file);
	} else {
		args_add(args, i_tmpfile);
	}

	cc_log("Running real compiler");
	MTR_BEGIN("execute", "compiler");
	char *tmp_stdout;
	int tmp_stdout_fd;
	char *tmp_stderr;
	int tmp_stderr_fd;
	int status;
	if (!conf->depend_mode) {
		tmp_stdout = format("%s.tmp.stdout", cached_obj);
		tmp_stdout_fd = create_tmp_fd(&tmp_stdout);
		add_pending_tmp_file(tmp_stdout);

		tmp_stderr = format("%s.tmp.stderr", cached_obj);
		tmp_stderr_fd = create_tmp_fd(&tmp_stderr);
		add_pending_tmp_file(tmp_stderr);

		status = execute(args->argv, tmp_stdout_fd, tmp_stderr_fd, &compiler_pid);
		args_pop(args, 3);
	} else {
		// The cached object path is not known yet, use temporary files.
		tmp_stdout = format("%s/tmp.stdout", temp_dir());
		tmp_stdout_fd = create_tmp_fd(&tmp_stdout);
		add_pending_tmp_file(tmp_stdout);

		tmp_stderr = format("%s/tmp.stderr", temp_dir());
		tmp_stderr_fd = create_tmp_fd(&tmp_stderr);
		add_pending_tmp_file(tmp_stderr);

		// Use the original arguments (including dependency options) in depend
		// mode.
		assert(orig_args);
		struct args *depend_mode_args = args_copy(orig_args);
		args_strip(depend_mode_args, "--ccache-");
		if (depend_extra_args) {
			args_extend(depend_mode_args, depend_extra_args);
		}
		add_prefix(depend_mode_args, conf->prefix_command);

		time_of_compilation = time(NULL);
		status = execute(
			depend_mode_args->argv, tmp_stdout_fd, tmp_stderr_fd, &compiler_pid);
		args_free(depend_mode_args);
	}
	MTR_END("execute", "compiler");

	struct stat st;
	if (x_stat(tmp_stdout, &st) != 0) {
		// The stdout file was removed - cleanup in progress? Better bail out.
		stats_update(STATS_MISSING);
		tmp_unlink(tmp_stdout);
		tmp_unlink(tmp_stderr);
		failed();
	}

	// distcc-pump outputs lines like this:
	// __________Using # distcc servers in pump mode
	if (st.st_size != 0 && guessed_compiler != GUESSED_PUMP) {
		cc_log("Compiler produced stdout");
		stats_update(STATS_STDOUT);
		tmp_unlink(tmp_stdout);
		tmp_unlink(tmp_stderr);
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
			stats_update(STATS_ERROR);
			failed();
		}

		int fd_cpp_stderr = open(cpp_stderr, O_RDONLY | O_BINARY);
		if (fd_cpp_stderr == -1) {
			cc_log("Failed opening %s: %s", cpp_stderr, strerror(errno));
			stats_update(STATS_ERROR);
			failed();
		}

		int fd_real_stderr = open(tmp_stderr2, O_RDONLY | O_BINARY);
		if (fd_real_stderr == -1) {
			cc_log("Failed opening %s: %s", tmp_stderr2, strerror(errno));
			stats_update(STATS_ERROR);
			failed();
		}

		int fd_result =
			open(tmp_stderr, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
		if (fd_result == -1) {
			cc_log("Failed opening %s: %s", tmp_stderr, strerror(errno));
			stats_update(STATS_ERROR);
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
		failed();
	}

	if (conf->depend_mode) {
		struct file_hash *object_hash =
			object_hash_from_depfile(output_dep, depend_mode_hash);
		if (!object_hash) {
			stats_update(STATS_ERROR);
			failed();
		}
		update_cached_result_globals(object_hash);
	}

	bool produce_dep_file =
		generating_dependencies && !str_eq(output_dep, "/dev/null");

	if (produce_dep_file) {
		use_relative_paths_in_depfile(output_dep);
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

	if (x_stat(tmp_stderr, &st) != 0) {
		stats_update(STATS_ERROR);
		failed();
	}
	if (st.st_size > 0) {
		if (!conf->depend_mode) {
			move_file_to_cache_same_fs(tmp_stderr, cached_stderr);
		} else {
			put_file_in_cache(tmp_stderr, cached_stderr);
		}
	} else if (conf->recache) {
		// If recaching, we need to remove any previous .stderr.
		x_unlink(cached_stderr);
	}
	if (st.st_size == 0 || conf->depend_mode) {
		tmp_unlink(tmp_stderr);
	}

	MTR_BEGIN("file", "file_put");

	put_file_in_cache(output_obj, cached_obj);
	if (produce_dep_file) {
		copy_file_to_cache(output_dep, cached_dep);
	}
	if (generating_coverage) {
		copy_file_to_cache(output_cov, cached_cov);
	}
	if (generating_stackusage) {
		copy_file_to_cache(output_su, cached_su);
	}
	if (generating_diagnostics) {
		copy_file_to_cache(output_dia, cached_dia);
	}
	if (using_split_dwarf) {
		copy_file_to_cache(output_dwo, cached_dwo);
	}

	MTR_END("file", "file_put");

	stats_update(STATS_CACHEMISS);

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
}

// Find the object file name by running the compiler in preprocessor mode.
// Returns the hash as a heap-allocated hex string.
static struct file_hash *
get_object_name_from_cpp(struct args *args, struct hash *hash)
{
	time_of_compilation = time(NULL);

	char *path_stderr = NULL;
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
		free(input_base);
		int path_stdout_fd = create_tmp_fd(&path_stdout);
		add_pending_tmp_file(path_stdout);

		path_stderr = format("%s/tmp.cpp_stderr", temp_dir());
		int path_stderr_fd = create_tmp_fd(&path_stderr);
		add_pending_tmp_file(path_stderr);

		int args_added = 2;
		args_add(args, "-E");
		if (conf->keep_comments_cpp) {
			args_add(args, "-C");
			args_added = 3;
		}
		args_add(args, input_file);
		add_prefix(args, conf->prefix_command_cpp);
		cc_log("Running preprocessor");
		MTR_BEGIN("execute", "preprocessor");
		status = execute(args->argv, path_stdout_fd, path_stderr_fd, &compiler_pid);
		MTR_END("execute", "preprocessor");
		args_pop(args, args_added);
	}

	if (status != 0) {
		cc_log("Preprocessor gave exit status %d", status);
		stats_update(STATS_PREPROCESSOR);
		failed();
	}

	hash_delimiter(hash, "cpp");
	if (!process_preprocessed_file(hash, path_stdout,
	                               guessed_compiler == GUESSED_PUMP)) {
		stats_update(STATS_ERROR);
		failed();
	}

	hash_delimiter(hash, "cppstderr");
	if (!direct_i_file && !hash_file(hash, path_stderr)) {
		// Somebody removed the temporary file?
		stats_update(STATS_ERROR);
		cc_log("Failed to open %s: %s", path_stderr, strerror(errno));
		failed();
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
	result->size = hash_input_size(hash);
	return result;
}

// Hash mtime or content of a file, or the output of a command, according to
// the CCACHE_COMPILERCHECK setting.
static void
hash_compiler(struct hash *hash, struct stat *st, const char *path,
              bool allow_command)
{
	if (str_eq(conf->compiler_check, "none")) {
		// Do nothing.
	} else if (str_eq(conf->compiler_check, "mtime")) {
		hash_delimiter(hash, "cc_mtime");
		hash_int(hash, st->st_size);
		hash_int(hash, st->st_mtime);
	} else if (str_startswith(conf->compiler_check, "string:")) {
		hash_delimiter(hash, "cc_hash");
		hash_string(hash, conf->compiler_check + strlen("string:"));
	} else if (str_eq(conf->compiler_check, "content") || !allow_command) {
		hash_delimiter(hash, "cc_content");
		hash_file(hash, path);
	} else { // command string
		bool ok = hash_multicommand_output(
			hash, conf->compiler_check, orig_args->argv[0]);
		if (!ok) {
			fatal("Failure running compiler check command: %s", conf->compiler_check);
		}
	}
}

// Hash the host compiler(s) invoked by nvcc.
//
// If ccbin_st and ccbin are set, they refer to a directory or compiler set
// with -ccbin/--compiler-bindir. If they are NULL, the compilers are looked up
// in PATH instead.
static void
hash_nvcc_host_compiler(struct hash *hash, struct stat *ccbin_st,
                        const char *ccbin)
{
	// From <http://docs.nvidia.com/cuda/cuda-compiler-driver-nvcc/index.html>:
	//
	//   "[...] Specify the directory in which the compiler executable resides.
	//   The host compiler executable name can be also specified to ensure that
	//   the correct host compiler is selected."
	//
	// and
	//
	//   "On all platforms, the default host compiler executable (gcc and g++ on
	//   Linux, clang and clang++ on Mac OS X, and cl.exe on Windows) found in
	//   the current execution search path will be used".

	if (!ccbin || S_ISDIR(ccbin_st->st_mode)) {
#if defined(__APPLE__)
		const char *compilers[] = {"clang", "clang++"};
#elif defined(_WIN32)
		const char *compilers[] = {"cl.exe"};
#else
		const char *compilers[] = {"gcc", "g++"};
#endif
		for (size_t i = 0; i < ARRAY_SIZE(compilers); i++) {
			if (ccbin) {
				char *path = format("%s/%s", ccbin, compilers[i]);
				struct stat st;
				if (stat(path, &st) == 0) {
					hash_compiler(hash, &st, path, false);
				}
				free(path);
			} else {
				char *path = find_executable(compilers[i], MYNAME);
				if (path) {
					struct stat st;
					x_stat(path, &st);
					hash_compiler(hash, &st, ccbin, false);
					free(path);
				}
			}
		}
	} else {
		hash_compiler(hash, ccbin_st, ccbin, false);
	}
}

// Update a hash sum with information common for the direct and preprocessor
// modes.
static void
calculate_common_hash(struct args *args, struct hash *hash)
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
	char *base = basename(args->argv[0]);
	hash_string(hash, base);
	free(base);

	if (!(conf->sloppiness & SLOPPY_LOCALE)) {
		// Hash environment variables that may affect localization of compiler
		// warning messages.
		const char *envvars[] = {
			"LANG",
			"LC_ALL",
			"LC_CTYPE",
			"LC_MESSAGES",
			NULL
		};
		for (const char **p = envvars; *p; ++p) {
			char *v = getenv(*p);
			if (v) {
				hash_delimiter(hash, *p);
				hash_string(hash, v);
			}
		}
	}

	// Possibly hash the current working directory.
	if (generating_debuginfo && conf->hash_dir) {
		char *cwd = get_cwd();
		for (size_t i = 0; i < debug_prefix_maps_len; i++) {
			char *map = debug_prefix_maps[i];
			char *sep = strchr(map, '=');
			if (sep) {
				char *old = x_strndup(map, sep - map);
				char *new = x_strdup(sep + 1);
				cc_log("Relocating debuginfo CWD %s from %s to %s", cwd, old, new);
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
			cc_log("Hashing CWD %s", cwd);
			hash_delimiter(hash, "cwd");
			hash_string(hash, cwd);
			free(cwd);
		}
	}

	if (using_split_dwarf) {
		// When using -gsplit-dwarf, object files include a link to the
		// corresponding .dwo file based on the target object filename, so we need
		// to include the target filename in the hash to avoid handing out an
		// object file with an incorrect .dwo link.
		hash_delimiter(hash, "filename");
		hash_string(hash, basename(output_obj));
	}

	// Possibly hash the coverage data file path.
	if (generating_coverage && profile_arcs) {
		char *dir = dirname(output_obj);
		if (profile_path) {
			dir = x_strdup(profile_path);
		} else {
			char *real_dir = x_realpath(dir);
			free(dir);
			dir = real_dir;
		}
		if (dir) {
			char *base_name = basename(output_obj);
			char *p = remove_extension(base_name);
			free(base_name);
			char *gcda_path = format("%s/%s.gcda", dir, p);
			cc_log("Hashing coverage path %s", gcda_path);
			free(p);
			hash_delimiter(hash, "gcda");
			hash_string(hash, gcda_path);
			free(dir);
		}
	}

	// Possibly hash the sanitize blacklist file path.
	for (size_t i = 0; i < sanitize_blacklists_len; i++) {
		char *sanitize_blacklist = sanitize_blacklists[i];
		cc_log("Hashing sanitize blacklist %s", sanitize_blacklist);
		hash_delimiter(hash, "sanitizeblacklist");
		if (!hash_file(hash, sanitize_blacklist)) {
			stats_update(STATS_BADEXTRAFILE);
			failed();
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
	if (guessed_compiler == GUESSED_GCC) {
		const char *gcc_colors = getenv("GCC_COLORS");
		if (gcc_colors) {
			hash_delimiter(hash, "gcccolors");
			hash_string(hash, gcc_colors);
		}
	}
}

static bool
hash_profile_data_file(const char *path, struct hash *hash)
{
	assert(path);

	char *base_name = remove_extension(output_obj);
	char *hashified_cwd = get_cwd();
	for (char *p = hashified_cwd; *p; ++p) {
		if (*p == '/') {
			*p = '#';
		}
	}
	char *paths_to_try[] = {
		// -fprofile-use[=dir]/-fbranch-probabilities (GCC <9)
		format("%s/%s.gcda", path, base_name),
		// -fprofile-use[=dir]/-fbranch-probabilities (GCC >=9)
		format("%s/%s#%s.gcda", path, hashified_cwd, base_name),
		// -fprofile(-instr|-sample)-use=file (Clang), -fauto-profile=file (GCC >=5)
		x_strdup(path),
		// -fprofile(-instr|-sample)-use=dir (Clang)
		format("%s/default.profdata", path),
		// -fauto-profile (GCC >=5)
		x_strdup("fbdata.afdo"), // -fprofile-dir is not used
		NULL
	};
	free(hashified_cwd);
	free(base_name);

	bool found = false;
	for (char **p = paths_to_try; *p; ++p) {
		cc_log("Checking for profile data file %s", *p);
		struct stat st;
		if (stat(*p, &st) == 0 && !S_ISDIR(st.st_mode)) {
			cc_log("Adding profile data %s to the hash", *p);
			hash_delimiter(hash, "-fprofile-use");
			if (hash_file(hash, *p)) {
				found = true;
			}
		}
		free(*p);
	}

	return found;
}

// Update a hash sum with information specific to the direct and preprocessor
// modes and calculate the object hash. Returns the object hash on success,
// otherwise NULL. Caller frees.
static struct file_hash *
calculate_object_hash(struct args *args, struct args *preprocessor_args,
                      struct hash *hash, int direct_mode)
{
	bool found_ccbin = false;

	if (direct_mode) {
		hash_delimiter(hash, "manifest version");
		hash_int(hash, MANIFEST_VERSION);
	}

	// clang will emit warnings for unused linker flags, so we shouldn't skip
	// those arguments.
	int is_clang =
		guessed_compiler == GUESSED_CLANG || guessed_compiler == GUESSED_UNKNOWN;

	// First the arguments.
	for (int i = 1; i < args->argc; i++) {
		// -L doesn't affect compilation (except for clang).
		if (i < args->argc-1 && str_eq(args->argv[i], "-L") && !is_clang) {
			i++;
			continue;
		}
		if (str_startswith(args->argv[i], "-L") && !is_clang) {
			continue;
		}

		// -Wl,... doesn't affect compilation (except for clang).
		if (str_startswith(args->argv[i], "-Wl,") && !is_clang) {
			continue;
		}

		// The -fdebug-prefix-map option may be used in combination with
		// CCACHE_BASEDIR to reuse results across different directories. Skip using
		// the value of the option from hashing but still hash the existence of the
		// option.
		if (str_startswith(args->argv[i], "-fdebug-prefix-map=")) {
			hash_delimiter(hash, "arg");
			hash_string(hash, "-fdebug-prefix-map=");
			continue;
		}
		if (str_startswith(args->argv[i], "-ffile-prefix-map=")) {
			hash_delimiter(hash, "arg");
			hash_string(hash, "-ffile-prefix-map=");
			continue;
		}
		if (str_startswith(args->argv[i], "-fmacro-prefix-map=")) {
			hash_delimiter(hash, "arg");
			hash_string(hash, "-fmacro-prefix-map=");
			continue;
		}

		// When using the preprocessor, some arguments don't contribute to the
		// hash. The theory is that these arguments will change the output of -E if
		// they are going to have any effect at all. For precompiled headers this
		// might not be the case.
		if (!direct_mode && !output_is_precompiled_header
		    && !using_precompiled_header) {
			if (compopt_affects_cpp(args->argv[i])) {
				if (compopt_takes_arg(args->argv[i])) {
					i++;
				}
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
					hash_string_buffer(hash, args->argv[i], 8);
					continue;
				} else if (str_startswith(args->argv[i], "-Wp,-MMD,")
				           && !strchr(args->argv[i] + 9, ',')) {
					hash_string_buffer(hash, args->argv[i], 9);
					continue;
				}
			} else if (str_startswith(args->argv[i], "-MF")) {
				// In either case, hash the "-MF" part.
				hash_delimiter(hash, "arg");
				hash_string_buffer(hash, args->argv[i], 3);

				if (!str_eq(output_dep, "/dev/null")) {
					bool separate_argument = (strlen(args->argv[i]) == 3);
					if (separate_argument) {
						// Next argument is dependency name, so skip it.
						i++;
					}
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

		if ((str_eq(args->argv[i], "-ccbin")
		     || str_eq(args->argv[i], "--compiler-bindir"))
		    && i + 1 < args->argc
		    && stat(args->argv[i+1], &st) == 0) {
			found_ccbin = true;
			hash_delimiter(hash, "ccbin");
			hash_nvcc_host_compiler(hash, &st, args->argv[i+1]);
			i++;
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

	// Make results with dependency file /dev/null different from those without
	// it.
	if (generating_dependencies && str_eq(output_dep, "/dev/null")) {
		hash_delimiter(hash, "/dev/null dependency file");
	}

	if (!found_ccbin && str_eq(actual_language, "cu")) {
		hash_nvcc_host_compiler(hash, NULL, NULL);
	}

	// For profile generation (-fprofile(-instr)-generate[=path])
	// - hash profile path
	//
	// For profile usage (-fprofile(-instr|-sample)-use, -fbranch-probabilities):
	// - hash profile data
	//
	// The profile directory can be specified as an argument to
	// -fprofile(-instr)-generate=, -fprofile(-instr|-sample)-use= or
	// --fprofile-dir=.
	if (profile_generate) {
		assert(profile_path);
		cc_log("Adding profile directory %s to our hash", profile_path);
		hash_delimiter(hash, "-fprofile-dir");
		hash_string(hash, profile_path);
	}

	if (profile_use && !hash_profile_data_file(profile_path, hash)) {
		cc_log("No profile data file found");
		stats_update(STATS_NOINPUT);
		failed();
	}

	// Adding -arch to hash since cpp output is affected.
	for (size_t i = 0; i < arch_args_size; ++i) {
		hash_delimiter(hash, "-arch");
		hash_string(hash, arch_args[i]);
	}

	struct file_hash *object_hash = NULL;
	if (direct_mode) {
		// Hash environment variables that affect the preprocessor output.
		for (const char **p = preproc_envvars; *p; ++p) {
			char *v = getenv(*p);
			if (v) {
				hash_delimiter(hash, *p);
				hash_string(hash, v);
			}
		}

		// Make sure that the direct mode hash is unique for the input file path.
		// If this would not be the case:
		//
		// * An false cache hit may be produced. Scenario:
		//   - a/r.h exists.
		//   - a/x.c has #include "r.h".
		//   - b/x.c is identical to a/x.c.
		//   - Compiling a/x.c records a/r.h in the manifest.
		//   - Compiling b/x.c results in a false cache hit since a/x.c and b/x.c
		//     share manifests and a/r.h exists.
		// * The expansion of __FILE__ may be incorrect.
		hash_delimiter(hash, "inputfile");
		hash_string(hash, input_file);

		hash_delimiter(hash, "sourcecode");
		int result = hash_source_code_file(conf, hash, input_file);
		if (result & HASH_SOURCE_CODE_ERROR) {
			stats_update(STATS_ERROR);
			failed();
		}
		if (result & HASH_SOURCE_CODE_FOUND_TIME) {
			cc_log("Disabling direct mode");
			conf->direct_mode = false;
			return NULL;
		}

		char *manifest_name = hash_result(hash);
		manifest_path = get_path_in_cache(manifest_name, ".manifest");
		manifest_stats_file =
			format("%s/%c/stats", conf->cache_dir, manifest_name[0]);
		free(manifest_name);

		cc_log("Looking for object file hash in %s", manifest_path);
		MTR_BEGIN("manifest", "manifest_get");
		object_hash = manifest_get(conf, manifest_path);
		MTR_END("manifest", "manifest_get");
		if (object_hash) {
			cc_log("Got object file hash from manifest");
			update_mtime(manifest_path);
		} else {
			cc_log("Did not find object file hash in manifest");
		}
	} else {
		assert(preprocessor_args);
		if (arch_args_size == 0) {
			object_hash = get_object_name_from_cpp(preprocessor_args, hash);
			cc_log("Got object file hash from preprocessor");
		} else {
			args_add(preprocessor_args, "-arch");
			for (size_t i = 0; i < arch_args_size; ++i) {
				args_add(preprocessor_args, arch_args[i]);
				object_hash = get_object_name_from_cpp(preprocessor_args, hash);
				cc_log("Got object file hash from preprocessor with -arch %s",
				       arch_args[i]);
				if (i != arch_args_size - 1) {
					free(object_hash);
					object_hash = NULL;
				}
				args_pop(preprocessor_args, 1);
			}
			args_pop(preprocessor_args, 1);
		}
		if (generating_dependencies) {
			// Nothing is actually created with -MF /dev/null
			if (!str_eq(output_dep, "/dev/null")) {
				cc_log("Preprocessor created %s", output_dep);
			}
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

	// If we're using Clang, we can't trust a precompiled header object based on
	// running the preprocessor since clang will produce a fatal error when the
	// precompiled header is used and one of the included files has an updated
	// timestamp:
	//
	//     file 'foo.h' has been modified since the precompiled header 'foo.pch'
	//     was built
	if ((guessed_compiler == GUESSED_CLANG || guessed_compiler == GUESSED_UNKNOWN)
	    && output_is_precompiled_header
	    && mode == FROMCACHE_CPP_MODE) {
		cc_log("Not considering cached precompiled header in preprocessor mode");
		return;
	}

	// Occasionally, e.g. on hard reset, our cache ends up as just filesystem
	// meta-data with no content. Catch an easy case of this.
	struct stat st;
	if (stat(cached_obj, &st) != 0) {
		cc_log("Object file %s not in cache", cached_obj);
		return;
	}
	if (st.st_size == 0) {
		cc_log("Invalid (empty) object file %s in cache", cached_obj);
		x_unlink(cached_obj);
		return;
	}

	MTR_BEGIN("cache", "from_cache");

	// (If mode != FROMCACHE_DIRECT_MODE, the dependency file is created by gcc.)
	bool produce_dep_file =
		generating_dependencies && mode == FROMCACHE_DIRECT_MODE
		&& !str_eq(output_dep, "/dev/null");

	MTR_BEGIN("file", "file_get");

	// Get result from cache.
	if (!str_eq(output_obj, "/dev/null")) {
		get_file_from_cache(cached_obj, output_obj);
		if (using_split_dwarf) {
			copy_file_from_cache(cached_dwo, output_dwo);
		}
	}
	if (produce_dep_file) {
		// Never hardlink the .d file since automake fails to move a foo.d.tmp file
		// to foo.d if they have the same i-node.
		copy_file_from_cache(cached_dep, output_dep);
	}
	if (generating_coverage) {
		copy_file_from_cache(cached_cov, output_cov);
	}
	if (generating_stackusage) {
		copy_file_from_cache(cached_su, output_su);
	}
	if (generating_diagnostics) {
		copy_file_from_cache(cached_dia, output_dia);
	}

	MTR_END("file", "file_get");

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
	if (generating_diagnostics) {
		update_mtime(cached_dia);
	}
	if (cached_dwo) {
		update_mtime(cached_dwo);
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

	MTR_END("cache", "from_cache");

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
	const char *ext = get_extension(path);
	char *dir = dirname(path);
	const char *dir_ext = get_extension(dir);
	bool result =
		str_eq(ext, ".gch")
		|| str_eq(ext, ".pch")
		|| str_eq(ext, ".pth")
		|| str_eq(dir_ext, ".gch"); // See "Precompiled Headers" in GCC docs.
	free(dir);
	return result;
}

static bool
color_output_possible(void)
{
	const char *term_env = getenv("TERM");
	return isatty(STDERR_FILENO) && term_env && strcasecmp(term_env, "DUMB") != 0;
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

static bool
process_profiling_option(const char *arg)
{
	char *new_profile_path = NULL;
	bool new_profile_use = false;

	if (str_startswith(arg, "-fprofile-dir=")) {
		new_profile_path = x_strdup(strchr(arg, '=') + 1);
	} else if (str_eq(arg, "-fprofile-generate")
		         || str_eq(arg, "-fprofile-instr-generate")) {
		profile_generate = true;
		if (guessed_compiler == GUESSED_CLANG) {
			new_profile_path = x_strdup(".");
		} else {
			// GCC uses $PWD/$(basename $obj).
			new_profile_path = get_cwd();
		}
	} else if (str_startswith(arg, "-fprofile-generate=")
		         || str_startswith(arg, "-fprofile-instr-generate=")) {
		profile_generate = true;
		new_profile_path = x_strdup(strchr(arg, '=') + 1);
	} else if (str_eq(arg, "-fprofile-use")
	           || str_eq(arg, "-fprofile-instr-use")
	           || str_eq(arg, "-fprofile-sample-use")
	           || str_eq(arg, "-fbranch-probabilities")
	           || str_eq(arg, "-fauto-profile")) {
		new_profile_use = true;
		if (!profile_path) {
			new_profile_path = x_strdup(".");
		}
	} else if (str_startswith(arg, "-fprofile-use=")
		         || str_startswith(arg, "-fprofile-instr-use=")
		         || str_startswith(arg, "-fprofile-sample-use=")
		         || str_startswith(arg, "-fauto-profile=")) {
		new_profile_use = true;
		new_profile_path = x_strdup(strchr(arg, '=') + 1);
	} else if (str_eq(arg, "-fprofile-correction")
	           || str_eq(arg, "-fprofile-reorder-functions")
	           || str_eq(arg, "-fprofile-sample-accurate")
	           || str_eq(arg, "-fprofile-values")) {
		return true;
	} else {
		cc_log("Unknown profiling option: %s", arg);
		stats_update(STATS_UNSUPPORTED_OPTION);
		return false;
	}

	if (new_profile_use) {
		if (profile_use) {
			free(new_profile_path);
			cc_log("Multiple profiling options not supported");
			stats_update(STATS_UNSUPPORTED_OPTION);
			return false;
		}
		profile_use = true;
	}

	if (new_profile_path) {
		free(profile_path);
		profile_path = new_profile_path;
		cc_log("Set profile directory to %s", profile_path);
	}

	if (profile_generate && profile_use) {
		// Too hard to figure out what the compiler will do.
		cc_log("Both generating and using profile info, giving up");
		stats_update(STATS_UNSUPPORTED_OPTION);
		return false;
	}

	return true;
}

// Process the compiler options into options suitable for passing to the
// preprocessor and the real compiler. preprocessor_args doesn't include -E;
// this is added later. extra_args_to_hash are the arguments that are not
// included in preprocessor_args but that should be included in the hash.
//
// Returns true on success, otherwise false.
bool
cc_process_args(struct args *args,
                struct args **preprocessor_args,
                struct args **extra_args_to_hash,
                struct args **compiler_args)
{
	bool found_c_opt = false;
	bool found_dc_opt = false;
	bool found_S_opt = false;
	bool found_pch = false;
	bool found_fpch_preprocess = false;
	const char *explicit_language = NULL; // As specified with -x.
	const char *file_language;            // As deduced from file extension.
	const char *input_charset = NULL;

	// Is the dependency makefile name overridden with -MF?
	bool dependency_filename_specified = false;

	// Is the dependency makefile target name specified with -MT or -MQ?
	bool dependency_target_specified = false;

	// Is the dependency target name implicitly specified using
	// DEPENDENCIES_OUTPUT or SUNPRO_DEPENDENCIES?
	bool dependency_implicit_target_specified = false;

	// expanded_args is a copy of the original arguments given to the compiler
	// but with arguments from @file and similar constructs expanded. It's only
	// used as a temporary data structure to loop over.
	struct args *expanded_args = args_copy(args);

	// common_args contains all original arguments except:
	// * those that never should be passed to the preprocessor,
	// * those that only should be passed to the preprocessor (if run_second_cpp
	//   is false), and
	// * dependency options (like -MD and friends).
	struct args *common_args = args_init(0, NULL);

	// cpp_args contains arguments that were not added to common_args, i.e. those
	// that should only be passed to the preprocessor if run_second_cpp is false.
	// If run_second_cpp is true, they will be passed to the compiler as well.
	struct args *cpp_args = args_init(0, NULL);

	// dep_args contains dependency options like -MD. They are only passed to the
	// preprocessor, never to the compiler.
	struct args *dep_args = args_init(0, NULL);

	// compiler_only_args contains arguments that should only be passed to the
	// compiler, not the preprocessor.
	struct args *compiler_only_args = args_init(0, NULL);

	bool found_color_diagnostics = false;
	bool found_directives_only = false;
	bool found_rewrite_includes = false;

	int argc = expanded_args->argc;
	char **argv = expanded_args->argv;
	args_add(common_args, argv[0]);

	bool result = true;
	for (int i = 1; i < argc; i++) {
		// The user knows best: just swallow the next arg.
		if (str_eq(argv[i], "--ccache-skip")) {
			i++;
			if (i == argc) {
				cc_log("--ccache-skip lacks an argument");
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}
			args_add(common_args, argv[i]);
			continue;
		}

		// Special case for -E.
		if (str_eq(argv[i], "-E")) {
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
		if (guessed_compiler == GUESSED_NVCC
		    && (str_eq(argv[i], "-optf") || str_eq(argv[i], "--options-file"))) {
			if (i == argc - 1) {
				cc_log("Expected argument after %s", argv[i]);
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
		if (compopt_too_hard(argv[i])
		    || str_startswith(argv[i], "-fdump-")
		    || str_startswith(argv[i], "-MJ")) {
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
			cc_log("Unsupported compiler option: %s", argv[i]);
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

		// Handle options that should not be passed to the preprocessor.
		if (compopt_affects_comp(argv[i])) {
			args_add(compiler_only_args, argv[i]);
			if (compopt_takes_arg(argv[i])
			    || (guessed_compiler == GUESSED_NVCC && str_eq(argv[i], "-Werror"))) {
				if (i == argc - 1) {
					cc_log("Missing argument to %s", argv[i]);
					stats_update(STATS_ARGS);
					result = false;
					goto out;
				}
				args_add(compiler_only_args, argv[i + 1]);
				++i;
			}
			continue;
		}
		if (compopt_prefix_affects_comp(argv[i])) {
			args_add(compiler_only_args, argv[i]);
			continue;
		}

		if (str_eq(argv[i], "-fpch-preprocess")
		    || str_eq(argv[i], "-emit-pch")
		    || str_eq(argv[i], "-emit-pth")) {
			found_fpch_preprocess = true;
		}

		// We must have -c.
		if (str_eq(argv[i], "-c")) {
			found_c_opt = true;
			continue;
		}

		// when using nvcc with separable compilation, -dc implies -c
		if ((str_eq(argv[i], "-dc") || str_eq(argv[i], "--device-c"))
		    && guessed_compiler == GUESSED_NVCC) {
			found_dc_opt = true;
			continue;
		}

		// -S changes the default extension.
		if (str_eq(argv[i], "-S")) {
			args_add(common_args, argv[i]);
			found_S_opt = true;
			continue;
		}

		if (strlen(argv[i]) >= 3
		    && str_startswith(argv[i], "-x")
		    && !islower(argv[i][2])) {
			// -xCODE (where CODE can be e.g. Host or CORE-AVX2, always starting with
			// an uppercase letter) is an ordinary Intel compiler option, not a
			// language specification. (GCC's "-x" language argument is always
			// lowercase.)
			args_add(common_args, argv[i]);
			continue;
		}

		// Special handling for -x: remember the last specified language before the
		// input file and strip all -x options from the arguments.
		if (str_eq(argv[i], "-x")) {
			if (i == argc - 1) {
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
			if (i == argc - 1) {
				cc_log("Missing argument to %s", argv[i]);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}
			output_obj = make_relative_path(x_strdup(argv[i+1]));
			i++;
			continue;
		}

		// Alternate form of -o with no space. Nvcc does not support this.
		if (str_startswith(argv[i], "-o") && guessed_compiler != GUESSED_NVCC) {
			output_obj = make_relative_path(x_strdup(&argv[i][2]));
			continue;
		}

		if (str_startswith(argv[i], "-fdebug-prefix-map=")
		    || str_startswith(argv[i], "-ffile-prefix-map=")) {
			debug_prefix_maps = x_realloc(
				debug_prefix_maps,
				(debug_prefix_maps_len + 1) * sizeof(char *));
			debug_prefix_maps[debug_prefix_maps_len++] =
				x_strdup(&argv[i][argv[i][2] == 'f' ? 18 : 19]);
			args_add(common_args, argv[i]);
			continue;
		}

		// Debugging is handled specially, so that we know if we can strip line
		// number info.
		if (str_startswith(argv[i], "-g")) {
			args_add(common_args, argv[i]);

			if (str_startswith(argv[i], "-gdwarf")) {
				// Selection of DWARF format (-gdwarf or -gdwarf-<version>) enables
				// debug info on level 2.
				generating_debuginfo = true;
				continue;
			}

			if (str_startswith(argv[i], "-gz")) {
				// -gz[=type] neither disables nor enables debug info.
				continue;
			}

			char last_char = argv[i][strlen(argv[i]) - 1];
			if (last_char == '0') {
				// "-g0", "-ggdb0" or similar: All debug information disabled.
				// "-gsplit-dwarf" is still in effect if given previously, though.
				generating_debuginfo = false;
				generating_debuginfo_level_3 = false;
			} else {
				generating_debuginfo = true;
				if (last_char == '3') {
					generating_debuginfo_level_3 = true;
				}
				if (str_eq(argv[i], "-gsplit-dwarf")) {
					using_split_dwarf = true;
				}
			}
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
				if (i == argc - 1) {
					cc_log("Missing argument to %s", argv[i]);
					stats_update(STATS_ARGS);
					result = false;
					goto out;
				}
				arg = argv[i + 1];
				i++;
			} else {
				// -MFarg or -MF=arg (EDG-based compilers)
				arg = &argv[i][3];
				if (arg[0] == '=') {
					++arg;
				}
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
				if (i == argc - 1) {
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
			args_add(common_args, argv[i]);
			continue;
		}
		if (str_eq(argv[i], "-ftest-coverage")) {
			generating_coverage = true;
			args_add(common_args, argv[i]);
			continue;
		}
		if (str_eq(argv[i], "-fstack-usage")) {
			generating_stackusage = true;
			args_add(common_args, argv[i]);
			continue;
		}
		if (str_eq(argv[i], "--coverage") // = -fprofile-arcs -ftest-coverage
		    || str_eq(argv[i], "-coverage")) { // Undocumented but still works.
			profile_arcs = true;
			generating_coverage = true;
			args_add(common_args, argv[i]);
			continue;
		}
		if (str_startswith(argv[i], "-fprofile-")
		    || str_startswith(argv[i], "-fauto-profile")
		    || str_eq(argv[i], "-fbranch-probabilities")) {
			if (process_profiling_option(argv[i])) {
				args_add(common_args, argv[i]);
				continue;
			} else {
				result = false;
				goto out;
			}
		}
		if (str_startswith(argv[i], "-fsanitize-blacklist=")) {
			sanitize_blacklists = x_realloc(
				sanitize_blacklists,
				(sanitize_blacklists_len + 1) * sizeof(char *));
			sanitize_blacklists[sanitize_blacklists_len++] = x_strdup(argv[i] + 21);
			args_add(common_args, argv[i]);
			continue;
		}
		if (str_startswith(argv[i], "--sysroot=")) {
			char *relpath = make_relative_path(x_strdup(argv[i] + 10));
			char *option = format("--sysroot=%s", relpath);
			args_add(common_args, option);
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
			args_add(common_args, argv[i]);
			char *relpath = make_relative_path(x_strdup(argv[i+1]));
			args_add(common_args, relpath);
			i++;
			free(relpath);
			continue;
		}
		// Alternate form of specifying target without =
		if (str_eq(argv[i], "-target")) {
			if (i == argc-1) {
				cc_log("Missing argument to %s", argv[i]);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}
			args_add(common_args, argv[i]);
			args_add(common_args, argv[i+1]);
			i++;
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
			if (i == argc - 1) {
				cc_log("Missing argument to %s", argv[i]);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}
			generating_diagnostics = true;
			output_dia = make_relative_path(x_strdup(argv[i+1]));
			i++;
			continue;
		}

		if (str_eq(argv[i], "-fcolor-diagnostics")
		    || str_eq(argv[i], "-fno-color-diagnostics")
		    || str_eq(argv[i], "-fdiagnostics-color")
		    || str_eq(argv[i], "-fdiagnostics-color=always")
		    || str_eq(argv[i], "-fno-diagnostics-color")
		    || str_eq(argv[i], "-fdiagnostics-color=never")) {
			args_add(common_args, argv[i]);
			found_color_diagnostics = true;
			continue;
		}
		if (str_eq(argv[i], "-fdiagnostics-color=auto")) {
			if (color_output_possible()) {
				// Output is redirected, so color output must be forced.
				args_add(common_args, "-fdiagnostics-color=always");
				add_extra_arg("-fdiagnostics-color=always");
				cc_log("Automatically forcing colors");
			} else {
				args_add(common_args, argv[i]);
			}
			found_color_diagnostics = true;
			continue;
		}

		// GCC
		if (str_eq(argv[i], "-fdirectives-only")) {
			found_directives_only = true;
			continue;
		}
		// Clang
		if (str_eq(argv[i], "-frewrite-includes")) {
			found_rewrite_includes = true;
			continue;
		}

		if (conf->sloppiness & SLOPPY_CLANG_INDEX_STORE
		    && str_eq(argv[i], "-index-store-path")) {
			// Xcode 9 or later calls Clang with this option. The given path includes
			// a UUID that might lead to cache misses, especially when cache is
			// shared among multiple users.
			i++;
			if (i <= argc - 1) {
				cc_log("Skipping argument -index-store-path %s", argv[i]);
			}
			continue;
		}

		// Options taking an argument that we may want to rewrite to relative paths
		// to get better hit rate. A secondary effect is that paths in the standard
		// error output produced by the compiler will be normalized.
		if (compopt_takes_path(argv[i])) {
			if (i == argc - 1) {
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
				args_add(common_args, argv[i]);
				args_add(common_args, relpath);
			}
			free(relpath);

			i++;
			continue;
		}

		// Same as above but options with concatenated argument beginning with a
		// slash.
		if (argv[i][0] == '-') {
			char *slash_pos = strchr(argv[i], '/');
			if (slash_pos) {
				char *option = x_strndup(argv[i], slash_pos - argv[i]);
				if (compopt_takes_concat_arg(option) && compopt_takes_path(option)) {
					char *relpath = make_relative_path(x_strdup(slash_pos));
					char *new_option = format("%s%s", option, relpath);
					if (compopt_affects_cpp(option)) {
						args_add(cpp_args, new_option);
					} else {
						args_add(common_args, new_option);
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
			if (i == argc - 1) {
				cc_log("Missing argument to %s", argv[i]);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}

			if (compopt_affects_cpp(argv[i])) {
				args_add(cpp_args, argv[i]);
				args_add(cpp_args, argv[i+1]);
			} else {
				args_add(common_args, argv[i]);
				args_add(common_args, argv[i+1]);
			}

			i++;
			continue;
		}

		// Other options.
		if (argv[i][0] == '-') {
			if (compopt_affects_cpp(argv[i])
			    || compopt_prefix_affects_cpp(argv[i])) {
				args_add(cpp_args, argv[i]);
			} else {
				args_add(common_args, argv[i]);
			}
			continue;
		}

		// If an argument isn't a plain file then assume its an option, not an
		// input file. This allows us to cope better with unusual compiler options.
		//
		// Note that "/dev/null" is an exception that is sometimes used as an input
		// file when code is testing compiler flags.
		struct stat st;
		if (!str_eq(argv[i], "/dev/null")
		    && (stat(argv[i], &st) != 0 || !S_ISREG(st.st_mode))) {
			cc_log("%s is not a regular file, not considering as input file",
			       argv[i]);
			args_add(common_args, argv[i]);
			continue;
		}

		if (input_file) {
			if (language_for_file(argv[i])) {
				cc_log("Multiple input files: %s and %s", input_file, argv[i]);
				stats_update(STATS_MULTIPLE);
			} else if (!found_c_opt && !found_dc_opt) {
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

	if (generating_debuginfo_level_3 && !conf->run_second_cpp) {
		cc_log("Generating debug info level 3; not compiling preprocessed code");
		conf->run_second_cpp = true;
	}

	// See <http://gcc.gnu.org/onlinedocs/cpp/Environment-Variables.html>.
	// Contrary to what the documentation seems to imply the compiler still
	// creates object files with these defined (confirmed with GCC 8.2.1), i.e.
	// they work as -MMD/-MD, not -MM/-M. These environment variables do nothing
	// on Clang.
	char *dependencies_env = getenv("DEPENDENCIES_OUTPUT");
	bool using_sunpro_dependencies = false;
	if (!dependencies_env) {
		dependencies_env = getenv("SUNPRO_DEPENDENCIES");
		using_sunpro_dependencies = true;
	}
	if (dependencies_env) {
		generating_dependencies = true;
		dependency_filename_specified = true;
		char *saveptr = NULL;
		char *abspath_file = strtok_r(dependencies_env, " ", &saveptr);

		free(output_dep);
		output_dep = make_relative_path(x_strdup(abspath_file));

		// Specifying target object is optional.
		char *abspath_obj = strtok_r(NULL, " ", &saveptr);
		if (abspath_obj) {
			// It's the "file target" form.

			dependency_target_specified = true;
			char *relpath_obj = make_relative_path(x_strdup(abspath_obj));
			// Ensure compiler gets relative path.
			char *relpath_both = format("%s %s", output_dep, relpath_obj);
			if (using_sunpro_dependencies) {
				x_setenv("SUNPRO_DEPENDENCIES", relpath_both);
			} else {
				x_setenv("DEPENDENCIES_OUTPUT", relpath_both);
			}
			free(relpath_obj);
			free(relpath_both);
		} else {
			// It's the "file" form.

			dependency_implicit_target_specified = true;
			// Ensure compiler gets relative path.
			if (using_sunpro_dependencies) {
				x_setenv("SUNPRO_DEPENDENCIES", output_dep);
			} else {
				x_setenv("DEPENDENCIES_OUTPUT", output_dep);
			}
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
		actual_language = x_strdup(explicit_language);
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

	if (!found_c_opt && !found_dc_opt && !found_S_opt) {
		if (output_is_precompiled_header) {
			args_add(common_args, "-c");
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

	if (!conf->run_second_cpp && str_eq(actual_language, "cu")) {
		cc_log("Using CUDA compiler; not compiling preprocessed code");
		conf->run_second_cpp = true;
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
			char extension = found_S_opt ? 's' : 'o';
			output_obj = basename(input_file);
			char *p = strrchr(output_obj, '.');
			if (!p) {
				reformat(&output_obj, "%s.%c", output_obj, extension);
			} else if (!p[1]) {
				reformat(&output_obj, "%s%c", output_obj, extension);
			} else {
				p[1] = extension;
				p[2] = 0;
			}
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

	// Cope with -o /dev/null.
	struct stat st;
	if (!str_eq(output_obj, "/dev/null")
	    && stat(output_obj, &st) == 0
	    && !S_ISREG(st.st_mode)) {
		cc_log("Not a regular file: %s", output_obj);
		stats_update(STATS_BADOUTPUTFILE);
		result = false;
		goto out;
	}

	char *output_dir = dirname(output_obj);
	if (stat(output_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		cc_log("Directory does not exist: %s", output_dir);
		stats_update(STATS_BADOUTPUTFILE);
		result = false;
		free(output_dir);
		goto out;
	}
	free(output_dir);

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
		if (guessed_compiler == GUESSED_CLANG) {
			if (!str_eq(actual_language, "assembler")) {
				args_add(common_args, "-fcolor-diagnostics");
				add_extra_arg("-fcolor-diagnostics");
				cc_log("Automatically enabling colors");
			}
		} else if (guessed_compiler == GUESSED_GCC) {
			// GCC has it since 4.9, but that'd require detecting what GCC version is
			// used for the actual compile. However it requires also GCC_COLORS to be
			// set (and not empty), so use that for detecting if GCC would use
			// colors.
			if (getenv("GCC_COLORS") && getenv("GCC_COLORS")[0] != '\0') {
				args_add(common_args, "-fdiagnostics-color");
				add_extra_arg("-fdiagnostics-color");
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

		if (!dependency_target_specified
		    && !dependency_implicit_target_specified) {
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

	*compiler_args = args_copy(common_args);
	args_extend(*compiler_args, compiler_only_args);

	if (conf->run_second_cpp) {
		args_extend(*compiler_args, cpp_args);
	} else if (found_directives_only || found_rewrite_includes) {
		// Need to pass the macros and any other preprocessor directives again.
		args_extend(*compiler_args, cpp_args);
		if (found_directives_only) {
			args_add(cpp_args, "-fdirectives-only");
			// The preprocessed source code still needs some more preprocessing.
			args_add(*compiler_args, "-fpreprocessed");
			args_add(*compiler_args, "-fdirectives-only");
		}
		if (found_rewrite_includes) {
			args_add(cpp_args, "-frewrite-includes");
			// The preprocessed source code still needs some more preprocessing.
			args_add(*compiler_args, "-x");
			args_add(*compiler_args, actual_language);
		}
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

	if (found_dc_opt) {
		args_add(*compiler_args, "-dc");
	}

	for (size_t i = 0; i < arch_args_size; ++i) {
		args_add(*compiler_args, "-arch");
		args_add(*compiler_args, arch_args[i]);
	}

	// Only pass dependency arguments to the preprocessor since Intel's C++
	// compiler doesn't produce a correct .d file when compiling preprocessed
	// source.
	args_extend(cpp_args, dep_args);

	*preprocessor_args = args_copy(common_args);
	args_extend(*preprocessor_args, cpp_args);

	if (extra_args_to_hash) {
		*extra_args_to_hash = compiler_only_args;
	}

out:
	args_free(expanded_args);
	args_free(common_args);
	args_free(dep_args);
	args_free(cpp_args);
	return result;
}

static void
create_initial_config_file(const char *path)
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

#ifdef MTR_ENABLED
static void *trace_id;
static const char *trace_file;

static void
trace_init(const char *json)
{
	trace_file = json;
	mtr_init(json);
	char *s = format("%f", time_seconds());
	MTR_INSTANT_C("", "", "time", s);
}

static void
trace_start(const char *tracefile)
{
	trace_file = tracefile;
	MTR_META_PROCESS_NAME(MYNAME);
	trace_id = (void *) ((long) getpid());
	MTR_START("program", "ccache", trace_id);
}

static void
trace_stop(void)
{
	const char *json = format("%s%s", output_obj, ".ccache-trace");
	MTR_FINISH("program", "ccache", trace_id);
	mtr_flush();
	mtr_shutdown();
	move_file(trace_file, json, 0);
}

static const char *
tmpdir()
{
#ifndef _WIN32
	const char *tmpdir = getenv("TMPDIR");
	if (tmpdir != NULL) {
		return tmpdir;
	}
#else
	static char dirbuf[PATH_MAX];
	DWORD retval = GetTempPath(PATH_MAX, dirbuf);
	if (retval > 0 && retval < PATH_MAX) {
		return dirbuf;
	}
#endif
	return "/tmp";
}

#endif // MTR_ENABLED

// Read config file(s), populate variables, create configuration file in cache
// directory if missing, etc.
static void
initialize(void)
{
	char *tracefile = getenv("CCACHE_INTERNAL_TRACE");
	if (tracefile != NULL) {
#ifdef MTR_ENABLED
		// We don't have any conf yet, so we can't use temp_dir() here.
		tracefile = format("%s/trace.%d.json", tmpdir(), (int)getpid());

		trace_init(tracefile);
#endif
	}

	conf_free(conf);
	MTR_BEGIN("config", "conf_create");
	conf = conf_create();
	MTR_END("config", "conf_create");

	char *errmsg;
	char *p = getenv("CCACHE_CONFIGPATH");
	if (p) {
		primary_config_path = x_strdup(p);
	} else {
		secondary_config_path = format("%s/ccache.conf", TO_STRING(SYSCONFDIR));
		MTR_BEGIN("config", "conf_read_secondary");
		if (!conf_read(conf, secondary_config_path, &errmsg)) {
			if (errno == 0) {
				// We could read the file but it contained errors.
				fatal("%s", errmsg);
			}
			// A missing config file in SYSCONFDIR is OK.
			free(errmsg);
		}
		MTR_END("config", "conf_read_secondary");

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
	MTR_BEGIN("config", "conf_read_primary");
	if (!conf_read(conf, primary_config_path, &errmsg)) {
		if (errno == 0) {
			// We could read the file but it contained errors.
			fatal("%s", errmsg);
		}
		if (!conf->disable) {
			should_create_initial_config = true;
		}
	}
	MTR_END("config", "conf_read_primary");

	MTR_BEGIN("config", "conf_update_from_environment");
	if (!conf_update_from_environment(conf, &errmsg)) {
		fatal("%s", errmsg);
	}
	MTR_END("config", "conf_update_from_environment");

	if (should_create_initial_config) {
		create_initial_config_file(primary_config_path);
	}

	exitfn_init();
	exitfn_add_nullary(stats_flush);
	exitfn_add_nullary(clean_up_pending_tmp_files);

	cc_log("=== CCACHE %s STARTED =========================================",
	       CCACHE_VERSION);

	if (conf->umask != UINT_MAX) {
		umask(conf->umask);
	}

	if (tracefile != NULL) {
#ifdef MTR_ENABLED
		trace_start(tracefile);
		exitfn_add_nullary(trace_stop);
#else
		cc_log("Error: tracing is not enabled!");
#endif
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
	for (size_t i = 0; i < debug_prefix_maps_len; i++) {
		free(debug_prefix_maps[i]);
		debug_prefix_maps[i] = NULL;
	}
	free(debug_prefix_maps); debug_prefix_maps = NULL;
	debug_prefix_maps_len = 0;
	free(profile_path); profile_path = NULL;
	profile_use = false;
	profile_generate = false;
	for (size_t i = 0; i < sanitize_blacklists_len; i++) {
		free(sanitize_blacklists[i]);
		sanitize_blacklists[i] = NULL;
	}
	free(sanitize_blacklists); sanitize_blacklists = NULL;
	sanitize_blacklists_len = 0;
	free(included_pch_file); included_pch_file = NULL;
	args_free(orig_args); orig_args = NULL;
	args_free(depend_extra_args); depend_extra_args = NULL;
	free(input_file); input_file = NULL;
	free(output_obj); output_obj = NULL;
	free(output_dep); output_dep = NULL;
	free(output_cov); output_cov = NULL;
	free(output_su); output_su = NULL;
	free(output_dia); output_dia = NULL;
	free(output_dwo); output_dwo = NULL;
	free(cached_obj_hash); cached_obj_hash = NULL;
	free(cached_stderr); cached_stderr = NULL;
	free(cached_obj); cached_obj = NULL;
	free(cached_dep); cached_dep = NULL;
	free(cached_cov); cached_cov = NULL;
	free(cached_su); cached_su = NULL;
	free(cached_dia); cached_dia = NULL;
	free(cached_dwo); cached_dwo = NULL;
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
	generating_debuginfo_level_3 = false;
	generating_dependencies = false;
	generating_coverage = false;
	generating_stackusage = false;
	profile_arcs = false;
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
set_up_uncached_err(void)
{
	int uncached_fd = dup(2); // The file descriptor is intentionally leaked.
	if (uncached_fd == -1) {
		cc_log("dup(2) failed: %s", strerror(errno));
		stats_update(STATS_ERROR);
		failed();
	}

	// Leak a pointer to the environment.
	char *buf = format("UNCACHED_ERR_FD=%d", uncached_fd);
	if (putenv(buf) == -1) {
		cc_log("putenv failed: %s", strerror(errno));
		stats_update(STATS_ERROR);
		failed();
	}
}

static void
configuration_logger(const char *descr, const char *origin, void *context)
{
	(void)context;
	cc_bulklog("Config: (%s) %s", origin, descr);
}

static void ccache(int argc, char *argv[]) ATTR_NORETURN;

// The main ccache driver function.
static void
ccache(int argc, char *argv[])
{
#ifndef _WIN32
	set_up_signal_handlers();
#endif

	// Needed for portability when using localtime_r.
	tzset();

	orig_args = args_init(argc, argv);

	initialize();
	MTR_BEGIN("main", "find_compiler");
	find_compiler(argv);
	MTR_END("main", "find_compiler");

	MTR_BEGIN("main", "clean_up_internal_tempdir");
	if (str_eq(conf->temporary_dir, "")) {
		clean_up_internal_tempdir();
	}
	MTR_END("main", "clean_up_internal_tempdir");

	if (!str_eq(conf->log_file, "") || conf->debug) {
		conf_print_items(conf, configuration_logger, NULL);
	}

	if (conf->disable) {
		cc_log("ccache is disabled");
		stats_update(STATS_CACHEMISS); // Dummy to trigger stats_flush.
		failed();
	}

	MTR_BEGIN("main", "set_up_uncached_err");
	set_up_uncached_err();
	MTR_END("main", "set_up_uncached_err");

	cc_log_argv("Command line: ", argv);
	cc_log("Hostname: %s", get_hostname());
	cc_log("Working directory: %s", get_current_working_dir());

    // Since some environment variables can affect preprocessing
    // and make logs unreplayable, this is an outlet
    // See https://gcc.gnu.org/onlinedocs/gcc/Environment-Variables.html
	for(const char **p = preproc_envvars ; *p ; ++p) {
        char *v = getenv(*p);
        if (v) {
            cc_log("Environment variable affects preprocessing: %s='%s'"
                , *p, v);
        }
    }

	conf->limit_multiple = MIN(MAX(conf->limit_multiple, 0.0), 1.0);

	MTR_BEGIN("main", "guess_compiler");
	guessed_compiler = guess_compiler(orig_args->argv[0]);
	MTR_END("main", "guess_compiler");

	// Arguments (except -E) to send to the preprocessor.
	struct args *preprocessor_args;
	// Arguments not sent to the preprocessor but that should be part of the
	// hash.
	struct args *extra_args_to_hash;
	// Arguments to send to the real compiler.
	struct args *compiler_args;
	MTR_BEGIN("main", "process_args");
	if (!cc_process_args(
	      orig_args, &preprocessor_args, &extra_args_to_hash, &compiler_args)) {
		failed(); // stats_update is called in cc_process_args.
	}
	MTR_END("main", "process_args");

	if (conf->depend_mode
	    && (!generating_dependencies || str_eq(output_dep, "/dev/null")
	        || !conf->run_second_cpp)) {
		cc_log("Disabling depend mode");
		conf->depend_mode = false;
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
	if (generating_diagnostics) {
		cc_log("Diagnostics file: %s", output_dia);
	}
	if (output_dwo) {
		cc_log("Split dwarf file: %s", output_dwo);
	}

	cc_log("Object file: %s", output_obj);
	MTR_META_THREAD_NAME(output_obj);

	// Need to dump log buffer as the last exit function to not lose any logs.
	exitfn_add_last(dump_debug_log_buffer_exitfn, output_obj);

	FILE *debug_text_file = NULL;
	if (conf->debug) {
		char *path = format("%s.ccache-input-text", output_obj);
		debug_text_file = fopen(path, "w");
		if (debug_text_file) {
			exitfn_add(fclose_exitfn, debug_text_file);
		} else {
			cc_log("Failed to open %s: %s", path, strerror(errno));
		}
		free(path);
	}

	struct hash *common_hash = hash_init();
	init_hash_debug(common_hash, output_obj, 'c', "COMMON", debug_text_file);

	MTR_BEGIN("hash", "common_hash");
	calculate_common_hash(preprocessor_args, common_hash);
	MTR_END("hash", "common_hash");

	// Try to find the hash using the manifest.
	struct hash *direct_hash = hash_copy(common_hash);
	init_hash_debug(
		direct_hash, output_obj, 'd', "DIRECT MODE", debug_text_file);

	struct args *args_to_hash = args_copy(preprocessor_args);
	args_extend(args_to_hash, extra_args_to_hash);

	bool put_object_in_manifest = false;
	struct file_hash *object_hash = NULL;
	struct file_hash *object_hash_from_manifest = NULL;
	if (conf->direct_mode) {
		cc_log("Trying direct lookup");
		MTR_BEGIN("hash", "direct_hash");
		object_hash = calculate_object_hash(args_to_hash, NULL, direct_hash, 1);
		MTR_END("hash", "direct_hash");
		if (object_hash) {
			update_cached_result_globals(object_hash);

			// If we can return from cache at this point then do so.
			from_cache(FROMCACHE_DIRECT_MODE, 0);

			// Wasn't able to return from cache at this point. However, the object
			// was already found in manifest, so don't re-add it later.
			put_object_in_manifest = false;

			object_hash_from_manifest = object_hash;
		} else {
			// Add object to manifest later.
			put_object_in_manifest = true;
		}
	}

	if (conf->read_only_direct) {
		cc_log("Read-only direct mode; running real compiler");
		stats_update(STATS_CACHEMISS);
		failed();
	}

	if (!conf->depend_mode) {
		// Find the hash using the preprocessed output. Also updates
		// included_files.
		struct hash *cpp_hash = hash_copy(common_hash);
		init_hash_debug(
			cpp_hash, output_obj, 'p', "PREPROCESSOR MODE", debug_text_file);

		MTR_BEGIN("hash", "cpp_hash");
		object_hash = calculate_object_hash(
			args_to_hash, preprocessor_args, cpp_hash, 0);
		MTR_END("hash", "cpp_hash");
		if (!object_hash) {
			fatal("internal error: object hash from cpp returned NULL");
		}
		update_cached_result_globals(object_hash);

		if (object_hash_from_manifest
		    && !file_hashes_equal(object_hash_from_manifest, object_hash)) {
			// The hash from manifest differs from the hash of the preprocessor
			// output. This could be because:
			//
			// - The preprocessor produces different output for the same input (not
			//   likely).
			// - There's a bug in ccache (maybe incorrect handling of compiler
			//   arguments).
			// - The user has used a different CCACHE_BASEDIR (most likely).
			//
			// The best thing here would probably be to remove the hash entry from
			// the manifest. For now, we use a simpler method: just remove the
			// manifest file.
			cc_log("Hash from manifest doesn't match preprocessor output");
			cc_log("Likely reason: different CCACHE_BASEDIRs used");
			cc_log("Removing manifest as a safety measure");
			x_unlink(manifest_path);

			put_object_in_manifest = true;
		}

		// If we can return from cache at this point then do.
		from_cache(FROMCACHE_CPP_MODE, put_object_in_manifest);
	}

	if (conf->read_only) {
		cc_log("Read-only mode; running real compiler");
		stats_update(STATS_CACHEMISS);
		failed();
	}

	add_prefix(compiler_args, conf->prefix_command);

	// In depend_mode, extend the direct hash.
	struct hash *depend_mode_hash = conf->depend_mode ? direct_hash : NULL;

	// Run real compiler, sending output to cache.
	MTR_BEGIN("cache", "to_cache");
	to_cache(compiler_args, depend_mode_hash);
	MTR_END("cache", "to_cache");

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
		DUMP_MANIFEST,
		HASH_FILE,
		PRINT_STATS,
	};
	static const struct option options[] = {
		{"cleanup",       no_argument,       0, 'c'},
		{"clear",         no_argument,       0, 'C'},
		{"dump-manifest", required_argument, 0, DUMP_MANIFEST},
		{"get-config",    required_argument, 0, 'k'},
		{"hash-file",     required_argument, 0, HASH_FILE},
		{"help",          no_argument,       0, 'h'},
		{"max-files",     required_argument, 0, 'F'},
		{"max-size",      required_argument, 0, 'M'},
		{"print-stats",   no_argument,       0, PRINT_STATS},
		{"set-config",    required_argument, 0, 'o'},
		{"show-config",   no_argument,       0, 'p'},
		{"show-stats",    no_argument,       0, 's'},
		{"version",       no_argument,       0, 'V'},
		{"zero-stats",    no_argument,       0, 'z'},
		{0, 0, 0, 0}
	};

	int c;
	while ((c = getopt_long(argc, argv, "cCk:hF:M:po:sVz", options, NULL))
	       != -1) {
		switch (c) {
		case DUMP_MANIFEST:
			initialize();
			manifest_dump(optarg, stdout);
			break;

		case HASH_FILE:
		{
			initialize();
			struct hash *hash = hash_init();
			if (str_eq(optarg, "-")) {
				hash_fd(hash, STDIN_FILENO);
			} else {
				hash_file(hash, optarg);
			}
			char *result = hash_result(hash);
			puts(result);
			free(result);
			hash_free(hash);
			break;
		}

		case PRINT_STATS:
			initialize();
			stats_print();
			break;

		case 'c': // --cleanup
			initialize();
			clean_up_all(conf);
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

		case 'k': // --get-config
		{
			initialize();
			char *errmsg;
			if (!conf_print_value(conf, optarg, stdout, &errmsg)) {
				fatal("%s", errmsg);
			}
		}
		break;

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

		case 'p': // --show-config
			initialize();
			conf_print_items(conf, configuration_printer, stdout);
			break;

		case 's': // --show-stats
			initialize();
			stats_summary();
			break;

		case 'V': // --version
			fprintf(stdout, VERSION_TEXT, CCACHE_VERSION);
			x_exit(0);

		case 'z': // --zero-stats
			initialize();
			stats_zero();
			printf("Statistics zeroed\n");
			break;

		default:
			fputs(USAGE_TEXT, stderr);
			x_exit(1);
		}
	}

	return 0;
}

int ccache_main(int argc, char *argv[]);

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
}
