/*
 * ccache -- a fast C/C++ compiler cache
 *
 * Copyright (C) 2002-2007 Andrew Tridgell
 * Copyright (C) 2009-2011 Joel Rosdahl
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

static const char VERSION_TEXT[] =
MYNAME " version %s\n"
"\n"
"Copyright (C) 2002-2007 Andrew Tridgell\n"
"Copyright (C) 2009-2011 Joel Rosdahl\n"
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
"    -C, --clear           clear the cache completely\n"
"    -F, --max-files=N     set maximum number of files in cache to N (use 0 for\n"
"                          no limit)\n"
"    -M, --max-size=SIZE   set maximum size of cache to SIZE (use 0 for no\n"
"                          limit; available suffixes: G, M and K; default\n"
"                          suffix: G)\n"
"    -s, --show-stats      show statistics summary\n"
"    -z, --zero-stats      zero statistics counters\n"
"\n"
"    -h, --help            print this help text\n"
"    -V, --version         print version and copyright information\n"
"\n"
"See also <http://ccache.samba.org>.\n";

/* current working directory taken from $PWD, or getcwd() if $PWD is bad */
char *current_working_dir = NULL;

/* the base cache directory */
char *cache_dir = NULL;

/* the debug logfile name, if set */
char *cache_logfile = NULL;

/* base directory (from CCACHE_BASEDIR) */
char *base_dir = NULL;

/* the original argument list */
static struct args *orig_args;

/* the source file */
static char *input_file;

/* The output file being compiled to. */
static char *output_obj;

/* The path to the dependency file (implicit or specified with -MF). */
static char *output_dep;

/*
 * Name (represented as a struct file_hash) of the file containing the cached
 * object code.
 */
static struct file_hash *cached_obj_hash;

/*
 * Full path to the file containing the cached object code
 * (cachedir/a/b/cdef[...]-size.o).
 */
static char *cached_obj;

/*
 * Full path to the file containing the standard error output
 * (cachedir/a/b/cdef[...]-size.stderr).
 */
static char *cached_stderr;

/*
 * Full path to the file containing the dependency information
 * (cachedir/a/b/cdef[...]-size.d).
 */
static char *cached_dep;

/*
 * Full path to the file containing the manifest
 * (cachedir/a/b/cdef[...]-size.manifest).
 */
static char *manifest_path;

/*
 * Time of compilation. Used to see if include files have changed after
 * compilation.
 */
static time_t time_of_compilation;

/* Bitmask of SLOPPY_*. */
unsigned sloppiness = 0;

/*
 * Files included by the preprocessor and their hashes/sizes. Key: file path.
 * Value: struct file_hash.
 */
static struct hashtable *included_files;

/* is gcc being asked to output dependencies? */
static bool generating_dependencies;

/* the extension of the file (without dot) after pre-processing */
static const char *i_extension;

/* the name of the temporary pre-processor file */
static char *i_tmpfile;

/* are we compiling a .i or .ii file directly? */
static bool direct_i_file;

/* the name of the cpp stderr file */
static char *cpp_stderr;

/*
 * Full path to the statistics file in the subdirectory where the cached result
 * belongs (CCACHE_DIR/X/stats).
 */
char *stats_file = NULL;

/* can we safely use the unification hashing backend? */
static bool enable_unify;

/* should we use the direct mode? */
static bool enable_direct = true;

/*
 * Whether to enable compression of files stored in the cache. (Manifest files
 * are always compressed.)
 */
static bool enable_compression = false;

/* number of levels (1 <= nlevels <= 8) */
static int nlevels = 2;

/*
 * Whether we should use the optimization of passing the already existing
 * preprocessed source code to the compiler.
 */
static bool compile_preprocessed_source_code;

/* Whether the output is a precompiled header */
static bool output_is_precompiled_header = false;

/* Whether we should output to the real object first before saving into cache */
static bool output_to_real_object_first = false;

/*
 * Whether we are using a precompiled header (either via -include or #include).
 */
static bool using_precompiled_header = false;

/* How long (in microseconds) to wait before breaking a stale lock. */
unsigned lock_staleness_limit = 2000000;

enum fromcache_call_mode {
	FROMCACHE_DIRECT_MODE,
	FROMCACHE_CPP_MODE,
	FROMCACHE_COMPILED_MODE
};

/*
 * This is a string that identifies the current "version" of the hash sum
 * computed by ccache. If, for any reason, we want to force the hash sum to be
 * different for the same input in a new ccache version, we can just change
 * this string. A typical example would be if the format of one of the files
 * stored in the cache changes in a backwards-incompatible way.
 */
static const char HASH_PREFIX[] = "3";

static void
add_prefix(struct args *orig_args)
{
	char *env;

	env = getenv("CCACHE_PREFIX");
	if (env && strlen(env) > 0) {
		char *e;
		char *tok, *saveptr = NULL;
		struct args *prefix = args_init(0, NULL);
		int i;

		e = x_strdup(env);
		for (tok = strtok_r(e, " ", &saveptr);
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

		cc_log("Using command-line prefix %s", env);
		for (i = prefix->argc; i != 0; i--) {
			args_add_prefix(orig_args, prefix->argv[i-1]);
		}
		args_free(prefix);
	}
}

/* Something went badly wrong - just execute the real compiler. */
static void
failed(void)
{
	/* strip any local args */
	args_strip(orig_args, "--ccache-");

	/* add prefix from CCACHE_PREFIX */
	add_prefix(orig_args);

	cc_log("Failed; falling back to running the real compiler");
	cc_log_argv("Executing ", orig_args->argv);
	exitfn_call();
	execv(orig_args->argv[0], orig_args->argv);
	fatal("%s: execv returned (%s)", orig_args->argv[0], strerror(errno));
}

static void
clean_up_tmp_files()
{
	/* delete intermediate pre-processor file if needed */
	if (i_tmpfile) {
		if (!direct_i_file) {
			tmp_unlink(i_tmpfile);
		}
		free(i_tmpfile);
		i_tmpfile = NULL;
	}

	/* delete the cpp stderr file if necessary */
	if (cpp_stderr) {
		tmp_unlink(cpp_stderr);
		free(cpp_stderr);
		cpp_stderr = NULL;
	}
}

static const char *
temp_dir()
{
	static char* path = NULL;
	if (path) return path;  /* Memoize */
	path = getenv("CCACHE_TEMPDIR");
	if (!path) {
		path = format("%s/tmp", cache_dir);
	}
	return path;
}

/*
 * Transform a name to a full path into the cache directory, creating needed
 * sublevels if needed. Caller frees.
 */
static char *
get_path_in_cache(const char *name, const char *suffix)
{
	int i;
	char *path;
	char *result;

	path = x_strdup(cache_dir);
	for (i = 0; i < nlevels; ++i) {
		char *p = format("%s/%c", path, name[i]);
		free(path);
		path = p;
	}

	result = format("%s/%s%s", path, name + nlevels, suffix);
	free(path);
	return result;
}

/*
 * This function hashes an include file and stores the path and hash in the
 * global included_files variable. If the include file is a PCH, cpp_hash is
 * also updated. Takes over ownership of path.
 */
static void
remember_include_file(char *path, size_t path_len, struct mdfour *cpp_hash)
{
#ifdef _WIN32
	DWORD attributes;
#endif
	struct mdfour fhash;
	struct stat st;
	char *source = NULL;
	size_t size;
	int result;
	bool is_pch;

	if (path_len >= 2 && (path[0] == '<' && path[path_len - 1] == '>')) {
		/* Typically <built-in> or <command-line>. */
		goto ignore;
	}

	if (str_eq(path, input_file)) {
		/* Don't remember the input file. */
		goto ignore;
	}

	if (hashtable_search(included_files, path)) {
		/* Already known include file. */
		goto ignore;
	}

#ifdef _WIN32
	/* stat fails on directories on win32 */
	attributes = GetFileAttributes(path);
	if (attributes != INVALID_FILE_ATTRIBUTES &&
	    attributes & FILE_ATTRIBUTE_DIRECTORY)
		goto ignore;
#endif

	if (stat(path, &st) != 0) {
		cc_log("Failed to stat include file %s: %s", path, strerror(errno));
		goto failure;
	}
	if (S_ISDIR(st.st_mode)) {
		/* Ignore directory, typically $PWD. */
		goto ignore;
	}
	if (!S_ISREG(st.st_mode)) {
		/* Device, pipe, socket or other strange creature. */
		cc_log("Non-regular include file %s", path);
		goto failure;
	}

	/* Let's hash the include file. */
	if (!(sloppiness & SLOPPY_INCLUDE_FILE_MTIME)
	    && st.st_mtime >= time_of_compilation) {
		cc_log("Include file %s too new", path);
		goto failure;
	}

	hash_start(&fhash);

	is_pch = is_precompiled_header(path);
	if (is_pch) {
		struct file_hash pch_hash;
		if (!hash_file(&fhash, path)) {
			goto failure;
		}
		hash_result_as_bytes(&fhash, pch_hash.hash);
		pch_hash.size = fhash.totalN;
		hash_delimiter(cpp_hash, "pch_hash");
		hash_buffer(cpp_hash, pch_hash.hash, sizeof(pch_hash.hash));
	}
	if (enable_direct) {
		struct file_hash *h;

		if (!is_pch) { /* else: the file has already been hashed. */
			if (st.st_size > 0) {
				if (!read_file(path, st.st_size, &source, &size)) {
					goto failure;
				}
			} else {
				source = x_strdup("");
				size = 0;
			}

			result = hash_source_code_string(&fhash, source, size, path);
			if (result & HASH_SOURCE_CODE_ERROR
			    || result & HASH_SOURCE_CODE_FOUND_TIME) {
				goto failure;
			}
		}

		h = x_malloc(sizeof(*h));
		hash_result_as_bytes(&fhash, h->hash);
		h->size = fhash.totalN;
		hashtable_insert(included_files, path, h);
	} else {
		free(path);
	}

	free(source);
	return;

failure:
	cc_log("Disabling direct mode");
	enable_direct = false;
	/* Fall through. */
ignore:
	free(path);
	free(source);
}

/*
 * Make a relative path from current working directory to path if path is under
 * CCACHE_BASEDIR. Takes over ownership of path. Caller frees.
 */
static char *
make_relative_path(char *path)
{
	char *relpath;

	if (!base_dir || !str_startswith(path, base_dir)) {
		return path;
	}

	relpath = get_relative_path(current_working_dir, path);
	free(path);
	return relpath;
}

/*
 * This function reads and hashes a file. While doing this, it also does these
 * things:
 *
 * - Makes include file paths whose prefix is CCACHE_BASEDIR relative when
 *   computing the hash sum.
 * - Stores the paths and hashes of included files in the global variable
 *   included_files.
 */
static bool
process_preprocessed_file(struct mdfour *hash, const char *path)
{
	char *data;
	char *p, *q, *end;
	size_t size;

	if (!read_file(path, 0, &data, &size)) {
		return false;
	}

	included_files = create_hashtable(1000, hash_from_string, strings_equal);

	/* Bytes between p and q are pending to be hashed. */
	end = data + size;
	p = data;
	q = data;
	while (q < end - 7) { /* There must be at least 7 characters (# 1 "x") left
	                         to potentially find an include file path. */
		/*
		 * Check if we look at a line containing the file name of an included file.
		 * At least the following formats exist (where N is a positive integer):
		 *
		 * GCC:
		 *
		 *   # N "file"
		 *   # N "file" N
		 *   #pragma GCC pch_preprocess "file"
		 *
		 * HP's compiler:
		 *
		 *   #line N "file"
		 *
		 * AIX's compiler:
		 *
		 *   #line N "file"
		 *   #line N
		 *
		 * Note that there may be other lines starting with '#' left after
		 * preprocessing as well, for instance "#    pragma".
		 */
		if (q[0] == '#'
		        /* GCC: */
		    && ((q[1] == ' ' && q[2] >= '0' && q[2] <= '9')
		        /* GCC precompiled header: */
		        || (q[1] == 'p'
		            && str_startswith(&q[2], "ragma GCC pch_preprocess "))
		        /* HP/AIX: */
		        || (q[1] == 'l' && q[2] == 'i' && q[3] == 'n' && q[4] == 'e'
		            && q[5] == ' '))
		    && (q == data || q[-1] == '\n')) {
			char *path;

			while (q < end && *q != '"' && *q != '\n') {
				q++;
			}
			if (q < end && *q == '\n') {
				/* A newline before the quotation mark -> no match. */
				continue;
			}
			q++;
			if (q >= end) {
				cc_log("Failed to parse included file path");
				free(data);
				return false;
			}
			/* q points to the beginning of an include file path */
			hash_buffer(hash, p, q - p);
			p = q;
			while (q < end && *q != '"') {
				q++;
			}
			/* p and q span the include file path */
			path = x_strndup(p, q - p);
			path = make_relative_path(path);
			hash_string(hash, path);
			remember_include_file(path, q - p, hash);
			p = q;
		} else {
			q++;
		}
	}

	hash_buffer(hash, p, (end - p));
	free(data);
	return true;
}

/* run the real compiler and put the result in cache */
static void
to_cache(struct args *args)
{
	char *tmp_stdout, *tmp_stderr, *tmp_obj;
	struct stat st;
	int status;
	size_t added_bytes = 0;
	unsigned added_files = 0;

	if (create_parent_dirs(cached_obj) != 0) {
		cc_log("Failed to create parent directories for %s: %s",
		       cached_obj, strerror(errno));
		failed();
	}
	tmp_stdout = format("%s.tmp.stdout.%s", cached_obj, tmp_string());
	tmp_stderr = format("%s.tmp.stderr.%s", cached_obj, tmp_string());

	if (output_to_real_object_first) {
		cc_log("Outputting to final destination");
		tmp_obj = output_obj;
	} else {
		tmp_obj = format("%s.tmp.%s", cached_obj, tmp_string());
	}

	args_add(args, "-o");
	args_add(args, tmp_obj);

	/* Turn off DEPENDENCIES_OUTPUT when running cc1, because
	 * otherwise it will emit a line like
	 *
	 *  tmp.stdout.vexed.732.o: /home/mbp/.ccache/tmp.stdout.vexed.732.i
	 *
	 * unsetenv() is on BSD and Linux but not portable. */
	putenv("DEPENDENCIES_OUTPUT");

	if (compile_preprocessed_source_code) {
		args_add(args, i_tmpfile);
	} else {
		args_add(args, input_file);
	}

	cc_log("Running real compiler");
	status = execute(args->argv, tmp_stdout, tmp_stderr);
	args_pop(args, 3);

	if (stat(tmp_stdout, &st) != 0 || st.st_size != 0) {
		cc_log("Compiler produced stdout");
		stats_update(STATS_STDOUT);
		tmp_unlink(tmp_stdout);
		tmp_unlink(tmp_stderr);
		tmp_unlink(tmp_obj);
		failed();
	}
	tmp_unlink(tmp_stdout);

	/*
	 * Merge stderr from the preprocessor (if any) and stderr from the real
	 * compiler into tmp_stderr.
	 */
	if (cpp_stderr) {
		int fd_cpp_stderr;
		int fd_real_stderr;
		int fd_result;
		char *tmp_stderr2;

		tmp_stderr2 = format("%s.tmp.stderr2.%s", cached_obj, tmp_string());
		if (x_rename(tmp_stderr, tmp_stderr2)) {
			cc_log("Failed to rename %s to %s: %s", tmp_stderr, tmp_stderr2,
			       strerror(errno));
			failed();
		}
		fd_cpp_stderr = open(cpp_stderr, O_RDONLY | O_BINARY);
		if (fd_cpp_stderr == -1) {
			cc_log("Failed opening %s: %s", cpp_stderr, strerror(errno));
			failed();
		}
		fd_real_stderr = open(tmp_stderr2, O_RDONLY | O_BINARY);
		if (fd_real_stderr == -1) {
			cc_log("Failed opening %s: %s", tmp_stderr2, strerror(errno));
			failed();
		}
		fd_result = open(tmp_stderr, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
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
		int fd;
		cc_log("Compiler gave exit status %d", status);
		stats_update(STATS_STATUS);

		fd = open(tmp_stderr, O_RDONLY | O_BINARY);
		if (fd != -1) {
			if (str_eq(output_obj, "/dev/null")
			    || (! output_to_real_object_first
                                && access(tmp_obj, R_OK) == 0
			        && move_file(tmp_obj, output_obj, 0) == 0)
			    || errno == ENOENT) {
				/* we can use a quick method of getting the failed output */
				copy_fd(fd, 2);
				close(fd);
				tmp_unlink(tmp_stderr);
				exit(status);
			}
		}

		tmp_unlink(tmp_stderr);
		tmp_unlink(tmp_obj);
		failed();
	}

	if (stat(tmp_obj, &st) != 0) {
		cc_log("Compiler didn't produce an object file");
		stats_update(STATS_NOOUTPUT);
		failed();
	}
	if (st.st_size == 0) {
		cc_log("Compiler produced an empty object file");
		stats_update(STATS_EMPTYOUTPUT);
		failed();
	}

	if (stat(tmp_stderr, &st) != 0) {
		cc_log("Failed to stat %s: %s", tmp_stderr, strerror(errno));
		stats_update(STATS_ERROR);
		failed();
	}
	if (st.st_size > 0) {
		if (move_uncompressed_file(tmp_stderr, cached_stderr,
		                           enable_compression) != 0) {
			cc_log("Failed to move %s to %s: %s", tmp_stderr, cached_stderr,
			       strerror(errno));
			stats_update(STATS_ERROR);
			failed();
		}
		cc_log("Stored in cache: %s", cached_stderr);
		if (enable_compression) {
			stat(cached_stderr, &st);
		}
		added_bytes += file_size(&st);
		added_files += 1;
	} else {
		tmp_unlink(tmp_stderr);
	}

	if (output_to_real_object_first) {
		if (copy_file(tmp_obj, cached_obj, enable_compression) != 0) {
			cc_log("Failed to move %s to %s: %s", tmp_obj, cached_obj, strerror(errno));
			stats_update(STATS_ERROR);
			failed();
		}
	} else if (move_uncompressed_file(tmp_obj, cached_obj, enable_compression) != 0) {
		cc_log("Failed to move %s to %s: %s", tmp_obj, cached_obj, strerror(errno));
		stats_update(STATS_ERROR);
		failed();
	}
	cc_log("Stored in cache: %s", cached_obj);
	stat(cached_obj, &st);
	added_bytes += file_size(&st);
	added_files += 1;

	/*
	 * Do an extra stat on the potentially compressed object file for the
	 * size statistics.
	 */
	if (stat(cached_obj, &st) != 0) {
		cc_log("Failed to stat %s: %s", cached_obj, strerror(errno));
		stats_update(STATS_ERROR);
		failed();
	}

	stats_update_size(STATS_TOCACHE, added_bytes / 1024, added_files);

	/* Make sure we have a CACHEDIR.TAG
	 * This can be almost anywhere, but might as well do it near the end
	 * as if we exit early we save the stat call
	 */
	if (create_cachedirtag(cache_dir) != 0) {
		cc_log("Failed to create %s/CACHEDIR.TAG (%s)\n",
		        cache_dir, strerror(errno));
		stats_update(STATS_ERROR);
		failed();
	}

	free(tmp_obj);
	free(tmp_stderr);
	free(tmp_stdout);
}

/*
 * Find the object file name by running the compiler in preprocessor mode.
 * Returns the hash as a heap-allocated hex string.
 */
static struct file_hash *
get_object_name_from_cpp(struct args *args, struct mdfour *hash)
{
	char *input_base;
	char *tmp;
	char *path_stdout, *path_stderr;
	int status;
	struct file_hash *result;

	/* ~/hello.c -> tmp.hello.123.i
	   limit the basename to 10
	   characters in order to cope with filesystem with small
	   maximum filename length limits */
	input_base = basename(input_file);
	tmp = strchr(input_base, '.');
	if (tmp != NULL) {
		*tmp = 0;
	}
	if (strlen(input_base) > 10) {
		input_base[10] = 0;
	}

	/* now the run */
	path_stdout = format("%s/%s.tmp.%s.%s",
	                     temp_dir(), input_base, tmp_string(), i_extension);
	path_stderr = format("%s/tmp.cpp_stderr.%s", temp_dir(), tmp_string());

	if (create_parent_dirs(path_stdout) != 0) {
		char *parent = dirname(path_stdout);
		fprintf(stderr,
		        "ccache: failed to create %s (%s)\n",
		        parent, strerror(errno));
		free(parent);
		exit(1);
	}

	time_of_compilation = time(NULL);

	if (!direct_i_file) {
		/* run cpp on the input file to obtain the .i */
		args_add(args, "-E");
		args_add(args, input_file);
		status = execute(args->argv, path_stdout, path_stderr);
		args_pop(args, 2);
	} else {
		/* we are compiling a .i or .ii file - that means we
		   can skip the cpp stage and directly form the
		   correct i_tmpfile */
		path_stdout = input_file;
		if (create_empty_file(path_stderr) != 0) {
			cc_log("Failed to create %s: %s", path_stderr, strerror(errno));
			stats_update(STATS_ERROR);
			failed();
		}
		status = 0;
	}

	if (status != 0) {
		if (!direct_i_file) {
			tmp_unlink(path_stdout);
		}
		tmp_unlink(path_stderr);
		cc_log("Preprocessor gave exit status %d", status);
		stats_update(STATS_PREPROCESSOR);
		failed();
	}

	if (enable_unify) {
		/*
		 * When we are doing the unifying tricks we need to include the
		 * input file name in the hash to get the warnings right.
		 */
		hash_delimiter(hash, "unifyfilename");
		hash_string(hash, input_file);

		hash_delimiter(hash, "unifycpp");
		if (unify_hash(hash, path_stdout) != 0) {
			stats_update(STATS_ERROR);
			tmp_unlink(path_stderr);
			cc_log("Failed to unify %s", path_stdout);
			failed();
		}
	} else {
		hash_delimiter(hash, "cpp");
		if (!process_preprocessed_file(hash, path_stdout)) {
			stats_update(STATS_ERROR);
			tmp_unlink(path_stderr);
			failed();
		}
	}

	hash_delimiter(hash, "cppstderr");
	if (!hash_file(hash, path_stderr)) {
		fatal("Failed to open %s: %s", path_stderr, strerror(errno));
	}

	i_tmpfile = path_stdout;

	if (compile_preprocessed_source_code) {
		/*
		 * If we are using the CPP trick, we need to remember this
		 * stderr data and output it just before the main stderr from
		 * the compiler pass.
		 */
		cpp_stderr = path_stderr;
	} else {
		tmp_unlink(path_stderr);
		free(path_stderr);
	}

	result = x_malloc(sizeof(*result));
	hash_result_as_bytes(hash, result->hash);
	result->size = hash->totalN;
	return result;
}

static void
update_cached_result_globals(struct file_hash *hash)
{
	char *object_name;

	object_name = format_hash_as_string(hash->hash, hash->size);
	cached_obj_hash = hash;
	cached_obj = get_path_in_cache(object_name, ".o");
	cached_stderr = get_path_in_cache(object_name, ".stderr");
	cached_dep = get_path_in_cache(object_name, ".d");
	stats_file = format("%s/%c/stats", cache_dir, object_name[0]);
	free(object_name);
}

/*
 * Update a hash sum with information common for the direct and preprocessor
 * modes.
 */
static void
calculate_common_hash(struct args *args, struct mdfour *hash)
{
	struct stat st;
	const char *compilercheck;
	char *p;

	hash_string(hash, HASH_PREFIX);

	/*
	 * We have to hash the extension, as a .i file isn't treated the same
	 * by the compiler as a .ii file.
	 */
	hash_delimiter(hash, "ext");
	hash_string(hash, i_extension);

	if (stat(args->argv[0], &st) != 0) {
		cc_log("Couldn't stat compiler %s: %s", args->argv[0], strerror(errno));
		stats_update(STATS_COMPILER);
		failed();
	}

	/*
	 * Hash information about the compiler.
	 */
	compilercheck = getenv("CCACHE_COMPILERCHECK");
	if (!compilercheck) {
		compilercheck = "mtime";
	}
	if (str_eq(compilercheck, "none")) {
		/* Do nothing. */
	} else if (str_eq(compilercheck, "content")) {
		hash_delimiter(hash, "cc_content");
		hash_file(hash, args->argv[0]);
	} else if (str_eq(compilercheck, "mtime")) {
		hash_delimiter(hash, "cc_mtime");
		hash_int(hash, st.st_size);
		hash_int(hash, st.st_mtime);
	} else { /* command string */
		if (!hash_multicommand_output(hash, compilercheck, orig_args->argv[0])) {
			fatal("Failure running compiler check command: %s", compilercheck);
		}
	}

	/*
	 * Also hash the compiler name as some compilers use hard links and
	 * behave differently depending on the real name.
	 */
	hash_delimiter(hash, "cc_name");
	hash_string(hash, basename(args->argv[0]));

	/* Possibly hash the current working directory. */
	if (getenv("CCACHE_HASHDIR")) {
		char *cwd = gnu_getcwd();
		if (cwd) {
			hash_delimiter(hash, "cwd");
			hash_string(hash, cwd);
			free(cwd);
		}
	}

	p = getenv("CCACHE_EXTRAFILES");
	if (p) {
		char *path, *q, *saveptr = NULL;
		p = x_strdup(p);
		q = p;
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
}

/*
 * Update a hash sum with information specific to the direct and preprocessor
 * modes and calculate the object hash. Returns the object hash on success,
 * otherwise NULL. Caller frees.
 */
static struct file_hash *
calculate_object_hash(struct args *args, struct mdfour *hash, int direct_mode)
{
	int i;
	char *manifest_name;
	struct stat st;
	int result;
	struct file_hash *object_hash = NULL;

	/* first the arguments */
	for (i = 1; i < args->argc; i++) {
		/* -L doesn't affect compilation. */
		if (i < args->argc-1 && str_eq(args->argv[i], "-L")) {
			i++;
			continue;
		}
		if (str_startswith(args->argv[i], "-L")) {
			continue;
		}

		/* When using the preprocessor, some arguments don't contribute
		   to the hash. The theory is that these arguments will change
		   the output of -E if they are going to have any effect at
		   all. For precompiled headers this might not be the case. */
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

		if (str_startswith(args->argv[i], "--specs=") &&
		    stat(args->argv[i] + 8, &st) == 0) {
			/* If given a explicit specs file, then hash that file,
			   but don't include the path to it in the hash. */
			hash_delimiter(hash, "specs");
			if (!hash_file(hash, args->argv[i] + 8)) {
				failed();
			}
			continue;
		}

		/* All other arguments are included in the hash. */
		hash_delimiter(hash, "arg");
		hash_string(hash, args->argv[i]);
	}

	if (direct_mode) {
		if (!(sloppiness & SLOPPY_FILE_MACRO)) {
			/*
			 * The source code file or an include file may contain
			 * __FILE__, so make sure that the hash is unique for
			 * the file name.
			 */
			hash_delimiter(hash, "inputfile");
			hash_string(hash, input_file);
		}

		hash_delimiter(hash, "sourcecode");
		result = hash_source_code_file(hash, input_file);
		if (result & HASH_SOURCE_CODE_ERROR) {
			failed();
		}
		if (result & HASH_SOURCE_CODE_FOUND_TIME) {
			cc_log("Disabling direct mode");
			enable_direct = false;
			return NULL;
		}
		manifest_name = hash_result(hash);
		manifest_path = get_path_in_cache(manifest_name, ".manifest");
		free(manifest_name);
		cc_log("Looking for object file hash in %s", manifest_path);
		object_hash = manifest_get(manifest_path);
		if (object_hash) {
			cc_log("Got object file hash from manifest");
		} else {
			cc_log("Did not find object file hash in manifest");
		}
	} else {
		object_hash = get_object_name_from_cpp(args, hash);
		cc_log("Got object file hash from preprocessor");
		if (generating_dependencies) {
			cc_log("Preprocessor created %s", output_dep);
		}
	}

	return object_hash;
}

/*
 * Try to return the compile result from cache. If we can return from cache
 * then this function exits with the correct status code, otherwise it returns.
 */
static void
from_cache(enum fromcache_call_mode mode, bool put_object_in_manifest)
{
	int fd_stderr;
	int ret;
	struct stat st;
	bool produce_dep_file;

	/* the user might be disabling cache hits */
	if (mode != FROMCACHE_COMPILED_MODE && getenv("CCACHE_RECACHE")) {
		return;
	}

	/* Check if the object file is there. */
	if (stat(cached_obj, &st) != 0) {
		cc_log("Object file %s not in cache", cached_obj);
		return;
	}

	/*
	 * (If mode != FROMCACHE_DIRECT_MODE, the dependency file is created by
	 * gcc.)
	 */
	produce_dep_file = generating_dependencies && mode == FROMCACHE_DIRECT_MODE;

	/* If the dependency file should be in the cache, check that it is. */
	if (produce_dep_file && stat(cached_dep, &st) != 0) {
		cc_log("Dependency file %s missing in cache", cached_dep);
		return;
	}

	if (str_eq(output_obj, "/dev/null")) {
		ret = 0;
	} else {
		x_unlink(output_obj);
		/* only make a hardlink if the cache file is uncompressed */
		if (getenv("CCACHE_HARDLINK") && !file_is_compressed(cached_obj)) {
			ret = link(cached_obj, output_obj);
		} else {
			ret = copy_file(cached_obj, output_obj, 0);
		}
	}

	if (ret == -1) {
		if (errno == ENOENT) {
			/* Someone removed the file just before we began copying? */
			cc_log("Object file %s just disappeared from cache", cached_obj);
			stats_update(STATS_MISSING);
		} else {
			cc_log("Failed to copy/link %s to %s: %s",
			       cached_obj, output_obj, strerror(errno));
			stats_update(STATS_ERROR);
			failed();
		}
		x_unlink(output_obj);
		x_unlink(cached_stderr);
		x_unlink(cached_obj);
		x_unlink(cached_dep);
		return;
	} else {
		cc_log("Created %s from %s", output_obj, cached_obj);
	}

	if (produce_dep_file) {
		x_unlink(output_dep);
		/* only make a hardlink if the cache file is uncompressed */
		if (getenv("CCACHE_HARDLINK") && !file_is_compressed(cached_dep)) {
			ret = link(cached_dep, output_dep);
		} else {
			ret = copy_file(cached_dep, output_dep, 0);
		}
		if (ret == -1) {
			if (errno == ENOENT) {
				/*
				 * Someone removed the file just before we
				 * began copying?
				 */
				cc_log("Dependency file %s just disappeared from cache", output_obj);
				stats_update(STATS_MISSING);
			} else {
				cc_log("Failed to copy/link %s to %s: %s",
				       cached_dep, output_dep, strerror(errno));
				stats_update(STATS_ERROR);
				failed();
			}
			x_unlink(output_obj);
			x_unlink(output_dep);
			x_unlink(cached_stderr);
			x_unlink(cached_obj);
			x_unlink(cached_dep);
			return;
		} else {
			cc_log("Created %s from %s", output_dep, cached_dep);
		}
	}

	/* Update modification timestamps to save files from LRU cleanup.
	   Also gives files a sensible mtime when hard-linking. */
	update_mtime(cached_obj);
	update_mtime(cached_stderr);
	if (produce_dep_file) {
		update_mtime(cached_dep);
	}

	if (generating_dependencies && mode != FROMCACHE_DIRECT_MODE) {
		/* Store the dependency file in the cache. */
		ret = copy_file(output_dep, cached_dep, enable_compression);
		if (ret == -1) {
			cc_log("Failed to copy %s to %s: %s", output_dep, cached_dep,
			       strerror(errno));
			/* Continue despite the error. */
		} else {
			cc_log("Stored in cache: %s", cached_dep);
			stat(cached_dep, &st);
			stats_update_size(STATS_NONE, file_size(&st) / 1024, 1);
		}
	}

	/* Send the stderr, if any. */
	fd_stderr = open(cached_stderr, O_RDONLY | O_BINARY);
	if (fd_stderr != -1) {
		copy_fd(fd_stderr, 2);
		close(fd_stderr);
	}

	/* Create or update the manifest file. */
	if (enable_direct
	    && put_object_in_manifest
	    && included_files
	    && !getenv("CCACHE_READONLY")) {
		struct stat st;
		size_t old_size = 0; /* in bytes */
		if (stat(manifest_path, &st) == 0) {
			old_size = file_size(&st);
		}
		if (manifest_put(manifest_path, cached_obj_hash, included_files)) {
			cc_log("Added object file hash to %s", manifest_path);
			update_mtime(manifest_path);
			stat(manifest_path, &st);
			stats_update_size(STATS_NONE,
			                  (file_size(&st) - old_size) / 1024,
			                  old_size == 0 ? 1 : 0);
		} else {
			cc_log("Failed to add object file hash to %s", manifest_path);
		}
	}

	/* log the cache hit */
	switch (mode) {
	case FROMCACHE_DIRECT_MODE:
		cc_log("Succeded getting cached result");
		stats_update(STATS_CACHEHIT_DIR);
		break;

	case FROMCACHE_CPP_MODE:
		cc_log("Succeded getting cached result");
		stats_update(STATS_CACHEHIT_CPP);
		break;

	case FROMCACHE_COMPILED_MODE:
		/* Stats already updated in to_cache(). */
		break;
	}

	/* and exit with the right status code */
	exit(0);
}

/* find the real compiler. We just search the PATH to find a executable of the
   same name that isn't a link to ourselves */
static void
find_compiler(int argc, char **argv)
{
	char *base;
	char *path;
	char *compiler;

	orig_args = args_init(argc, argv);

	base = basename(argv[0]);

	/* we might be being invoked like "ccache gcc -c foo.c" */
	if (same_executable_name(base, MYNAME)) {
		args_remove_first(orig_args);
		free(base);
		if (is_full_path(argv[1])) {
			/* a full path was given */
			return;
		}
		base = basename(argv[1]);
	}

	/* support user override of the compiler */
	if ((path = getenv("CCACHE_CC"))) {
		base = x_strdup(path);
	}

	compiler = find_executable(base, MYNAME);

	/* can't find the compiler! */
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
	return str_eq(get_extension(path), ".gch");
}

/*
 * Process the compiler options into options suitable for passing to the
 * preprocessor and the real compiler. The preprocessor options don't include
 * -E; this is added later. Returns true on success, otherwise false.
 */
bool
cc_process_args(struct args *orig_args, struct args **preprocessor_args,
                struct args **compiler_args)
{
	int i;
	bool found_c_opt = false;
	bool found_S_opt = false;
	bool found_arch_opt = false;
	bool found_pch = false;
	bool found_fpch_preprocess = false;
	const char *explicit_language = NULL; /* As specified with -x. */
	const char *file_language;            /* As deduced from file extension. */
	const char *actual_language;          /* Language to actually use. */
	const char *input_charset = NULL;
	struct stat st;
	/* is the dependency makefile name overridden with -MF? */
	bool dependency_filename_specified = false;
	/* is the dependency makefile target name specified with -MT or -MQ? */
	bool dependency_target_specified = false;
	struct args *stripped_args = NULL, *dep_args = NULL;
	int argc = orig_args->argc;
	char **argv = orig_args->argv;
	bool result = true;

	stripped_args = args_init(0, NULL);
	dep_args = args_init(0, NULL);

	args_add(stripped_args, argv[0]);

	for (i = 1; i < argc; i++) {
		/* The user knows best: just swallow the next arg */
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

		/* Special case for -E. */
		if (str_eq(argv[i], "-E")) {
			stats_update(STATS_PREPROCESSING);
			result = false;
			goto out;
		}

		/* These are always too hard. */
		if (compopt_too_hard(argv[i])
		    || str_startswith(argv[i], "@")
		    || str_startswith(argv[i], "-fdump-")) {
			cc_log("Compiler option %s is unsupported", argv[i]);
			stats_update(STATS_UNSUPPORTED);
			result = false;
			goto out;
		}

		/* We need to output to the real object first here, otherwise
		 * runtime artifacts will be produced in the wrong place. */
		if (compopt_needs_realdir(argv[i])) {
			output_to_real_object_first = true;
			if (base_dir) {
				cc_log("Disabling base dir due to %s", argv[i]);
				base_dir = NULL;
			}
		}

		/* These are too hard in direct mode. */
		if (enable_direct) {
			if (compopt_too_hard_for_direct_mode(argv[i])) {
				cc_log("Unsupported compiler option for direct mode: %s", argv[i]);
				enable_direct = false;
			}
		}

		/* Multiple -arch options are too hard. */
		if (str_eq(argv[i], "-arch")) {
			if (found_arch_opt) {
				cc_log("More than one -arch compiler option is unsupported");
				stats_update(STATS_UNSUPPORTED);
				result = false;
				goto out;
			} else {
				found_arch_opt = true;
			}
		}

		if (str_eq(argv[i], "-fpch-preprocess")) {
			found_fpch_preprocess = true;
		}

		/* we must have -c */
		if (str_eq(argv[i], "-c")) {
			found_c_opt = true;
			continue;
		}

		/* -S changes the default extension */
		if (str_eq(argv[i], "-S")) {
			args_add(stripped_args, argv[i]);
			found_S_opt = true;
			continue;
		}

		/*
		 * Special handling for -x: remember the last specified language before the
		 * input file and strip all -x options from the arguments.
		 */
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

		/* we need to work out where the output was meant to go */
		if (str_eq(argv[i], "-o")) {
			if (i == argc-1) {
				cc_log("Missing argument to %s", argv[i]);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}
			output_obj = x_strdup(argv[i+1]);
			i++;
			continue;
		}

		/* alternate form of -o, with no space */
		if (str_startswith(argv[i], "-o")) {
			output_obj = x_strdup(&argv[i][2]);
			continue;
		}

		/* debugging is handled specially, so that we know if we
		   can strip line number info
		*/
		if (str_startswith(argv[i], "-g")) {
			args_add(stripped_args, argv[i]);
			if (enable_unify && !str_eq(argv[i], "-g0")) {
				cc_log("%s used; disabling unify mode", argv[i]);
				enable_unify = false;
			}
			if (str_eq(argv[i], "-g3")) {
				/*
				 * Fix for bug 7190 ("commandline macros (-D)
				 * have non-zero lineno when using -g3").
				 */
				cc_log("%s used; not compiling preprocessed code", argv[i]);
				compile_preprocessed_source_code = false;
			}
			continue;
		}

		/* These options require special handling, because they
		   behave differently with gcc -E, when the output
		   file is not specified. */
		if (str_eq(argv[i], "-MD") || str_eq(argv[i], "-MMD")) {
			generating_dependencies = true;
			args_add(dep_args, argv[i]);
			continue;
		}
		if (str_startswith(argv[i], "-MF")) {
			char *arg;
			dependency_filename_specified = true;
			free(output_dep);
			args_add(dep_args, argv[i]);
			if (strlen(argv[i]) == 3) {
				/* -MF arg */
				if (i >= argc - 1) {
					cc_log("Missing argument to %s", argv[i]);
					stats_update(STATS_ARGS);
					result = false;
					goto out;
				}
				arg = argv[i + 1];
				args_add(dep_args, argv[i + 1]);
				i++;
			} else {
				/* -MFarg */
				arg = &argv[i][3];
			}
			output_dep = make_relative_path(x_strdup(arg));
			continue;
		}
		if (str_startswith(argv[i], "-MQ") || str_startswith(argv[i], "-MT")) {
			args_add(dep_args, argv[i]);
			if (strlen(argv[i]) == 3) {
				/* -MQ arg or -MT arg */
				if (i >= argc - 1) {
					cc_log("Missing argument to %s", argv[i]);
					stats_update(STATS_ARGS);
					result = false;
					goto out;
				}
				args_add(dep_args, argv[i + 1]);
				i++;
				/*
				 * Yes, that's right. It's strange, but apparently, GCC behaves
				 * differently for -MT arg and -MTarg (and similar for -MQ): in the
				 * latter case, but not in the former, an implicit dependency for the
				 * object file is added to the dependency file.
				 */
				dependency_target_specified = true;
			}
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
		if (str_startswith(argv[i], "-Wp,")) {
			if (str_startswith(argv[i], "-Wp,-MD,") && !strchr(argv[i] + 8, ',')) {
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
			} else if (enable_direct) {
				/*
				 * -Wp, can be used to pass too hard options to
				 * the preprocessor. Hence, disable direct
				 * mode.
				 */
				cc_log("Unsupported compiler option for direct mode: %s", argv[i]);
				enable_direct = false;
			}
		}
		if (str_eq(argv[i], "-MP")) {
			args_add(dep_args, argv[i]);
			continue;
		}

		/* Input charset needs to be handled specially. */
		if (str_startswith(argv[i], "-finput-charset=")) {
			input_charset = argv[i];
			continue;
		}

		/*
		 * Options taking an argument that that we may want to rewrite
		 * to relative paths to get better hit rate. A secondary effect
		 * is that paths in the standard error output produced by the
		 * compiler will be normalized.
		 */
		if (compopt_takes_path(argv[i])) {
			char *relpath;
			char *pchpath;
			if (i == argc-1) {
				cc_log("Missing argument to %s", argv[i]);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}

			args_add(stripped_args, argv[i]);
			relpath = make_relative_path(x_strdup(argv[i+1]));
			args_add(stripped_args, relpath);

			/* Try to be smart about detecting precompiled headers */
			pchpath = format("%s.gch", argv[i+1]);
			if (stat(pchpath, &st) == 0) {
				cc_log("Detected use of precompiled header: %s", pchpath);
				found_pch = true;
			}

			free(pchpath);
			free(relpath);
			i++;
			continue;
		}

		/* Same as above but options with concatenated argument. */
		if (compopt_short(compopt_takes_path, argv[i])) {
			char *relpath;
			char *option;
			relpath = make_relative_path(x_strdup(argv[i] + 2));
			option = format("-%c%s", argv[i][1], relpath);
			args_add(stripped_args, option);
			free(relpath);
			free(option);
			continue;
		}

		/* options that take an argument */
		if (compopt_takes_arg(argv[i])) {
			if (i == argc-1) {
				cc_log("Missing argument to %s", argv[i]);
				stats_update(STATS_ARGS);
				result = false;
				goto out;
			}
			args_add(stripped_args, argv[i]);
			args_add(stripped_args, argv[i+1]);
			i++;
			continue;
		}

		/* other options */
		if (argv[i][0] == '-') {
			args_add(stripped_args, argv[i]);
			continue;
		}

		/* if an argument isn't a plain file then assume its
		   an option, not an input file. This allows us to
		   cope better with unusual compiler options */
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

		/* Rewrite to relative to increase hit rate. */
		input_file = make_relative_path(x_strdup(argv[i]));
	}

	if (!input_file) {
		cc_log("No input file found");
		stats_update(STATS_NOINPUT);
		result = false;
		goto out;
	}

	if (found_pch || found_fpch_preprocess) {
		using_precompiled_header = true;
		if (!(sloppiness & SLOPPY_TIME_MACROS)) {
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
		actual_language && strstr(actual_language, "-header") != NULL;

	if (!found_c_opt && !output_is_precompiled_header) {
		cc_log("No -c option found");
		/* I find that having a separate statistic for autoconf tests is useful,
		   as they are the dominant form of "called for link" in many cases */
		if (strstr(input_file, "conftest.")) {
			stats_update(STATS_CONFTEST);
		} else {
			stats_update(STATS_LINK);
		}
		result = false;
		goto out;
	}

	if (!actual_language) {
		cc_log("Unsupported source extension: %s", input_file);
		stats_update(STATS_SOURCELANG);
		result = false;
		goto out;
	}

	direct_i_file = language_is_preprocessed(actual_language);

	if (output_is_precompiled_header) {
		/* It doesn't work to create the .gch from preprocessed source. */
		cc_log("Creating precompiled header; not compiling preprocessed code");
		compile_preprocessed_source_code = false;
	}

	i_extension = getenv("CCACHE_EXTENSION");
	if (!i_extension) {
		const char *p_language = p_language_for_language(actual_language);
		i_extension = extension_for_language(p_language) + 1;
	}

	/* don't try to second guess the compilers heuristics for stdout handling */
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
			char *p;
			output_obj = basename(input_file);
			p = strrchr(output_obj, '.');
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

	/* cope with -o /dev/null */
	if (!str_eq(output_obj,"/dev/null")
	    && stat(output_obj, &st) == 0
	    && !S_ISREG(st.st_mode)) {
		cc_log("Not a regular file: %s", output_obj);
		stats_update(STATS_DEVICE);
		result = false;
		goto out;
	}

	/*
	 * Some options shouldn't be passed to the real compiler when it compiles
	 * preprocessed code:
	 *
	 * -finput-charset=XXX (otherwise conversion happens twice)
	 * -x XXX (otherwise the wrong language is selected)
	 */
	*preprocessor_args = args_copy(stripped_args);
	if (input_charset) {
		args_add(*preprocessor_args, input_charset);
	}
	if (found_pch) {
		args_add(*preprocessor_args, "-fpch-preprocess");
	}
	if (explicit_language) {
		args_add(*preprocessor_args, "-x");
		args_add(*preprocessor_args, explicit_language);
	}

	/*
	 * Add flags for dependency generation only to the preprocessor command line.
	 */
	if (generating_dependencies) {
		if (!dependency_filename_specified) {
			char *default_depfile_name;
			char *base_name;

			base_name = remove_extension(output_obj);
			default_depfile_name = format("%s.d", base_name);
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

	if (compile_preprocessed_source_code) {
		*compiler_args = args_copy(stripped_args);
		if (explicit_language) {
			/*
			 * Workaround for a bug in Apple's patched distcc -- it doesn't properly
			 * reset the language specified with -x, so if -x is given, we have to
			 * specify the preprocessed language explicitly.
			 */
			args_add(*compiler_args, "-x");
			args_add(*compiler_args, p_language_for_language(explicit_language));
		}
	} else {
		*compiler_args = args_copy(*preprocessor_args);
	}

	if (found_c_opt) {
		args_add(*compiler_args, "-c");
	}

	/*
	 * Only pass dependency arguments to the preprocesor since Intel's C++
	 * compiler doesn't produce a correct .d file when compiling preprocessed
	 * source.
	 */
	args_extend(*preprocessor_args, dep_args);

out:
	args_free(stripped_args);
	args_free(dep_args);
	return result;
}

/* Reset the global state. Used by the test suite. */
void
cc_reset(void)
{
	free(current_working_dir); current_working_dir = NULL;
	free(cache_dir); cache_dir = NULL;
	cache_logfile = NULL;
	base_dir = NULL;
	args_free(orig_args); orig_args = NULL;
	free(input_file); input_file = NULL;
	free(output_obj); output_obj = NULL;
	free(output_dep); output_dep = NULL;
	free(cached_obj_hash); cached_obj_hash = NULL;
	free(cached_obj); cached_obj = NULL;
	free(cached_stderr); cached_stderr = NULL;
	free(cached_dep); cached_dep = NULL;
	free(manifest_path); manifest_path = NULL;
	time_of_compilation = 0;
	sloppiness = false;
	if (included_files) {
		hashtable_destroy(included_files, 1); included_files = NULL;
	}
	generating_dependencies = false;
	i_extension = NULL;
	i_tmpfile = NULL;
	direct_i_file = false;
	free(cpp_stderr); cpp_stderr = NULL;
	free(stats_file); stats_file = NULL;
	enable_unify = false;
	enable_direct = true;
	enable_compression = false;
	nlevels = 2;
	compile_preprocessed_source_code = false;
	output_is_precompiled_header = false;
}

static unsigned
parse_sloppiness(char *p)
{
	unsigned result = 0;
	char *word, *q, *saveptr = NULL;

	if (!p) {
		return result;
	}
	p = x_strdup(p);
	q = p;
	while ((word = strtok_r(q, ", ", &saveptr))) {
		if (str_eq(word, "file_macro")) {
			cc_log("Being sloppy about __FILE__");
			result |= SLOPPY_FILE_MACRO;
		}
		if (str_eq(word, "include_file_mtime")) {
			cc_log("Being sloppy about include file mtime");
			result |= SLOPPY_INCLUDE_FILE_MTIME;
		}
		if (str_eq(word, "time_macros")) {
			cc_log("Being sloppy about __DATE__ and __TIME__");
			result |= SLOPPY_TIME_MACROS;
		}
		q = NULL;
	}
	free(p);
	return result;
}

/* the main ccache driver function */
static void
ccache(int argc, char *argv[])
{
	bool put_object_in_manifest = false;
	struct file_hash *object_hash;
	struct file_hash *object_hash_from_manifest = NULL;
	char *env;
	struct mdfour common_hash;
	struct mdfour direct_hash;
	struct mdfour cpp_hash;

	/* Arguments (except -E) to send to the preprocessor. */
	struct args *preprocessor_args;

	/* Arguments to send to the real compiler. */
	struct args *compiler_args;

	find_compiler(argc, argv);

	if (getenv("CCACHE_DISABLE")) {
		cc_log("ccache is disabled");
		failed();
	}

	sloppiness = parse_sloppiness(getenv("CCACHE_SLOPPINESS"));

	cc_log_argv("Command line: ", argv);
	cc_log("Hostname: %s", get_hostname());
	cc_log("Working directory: %s", current_working_dir);

	if (base_dir) {
		cc_log("Base directory: %s", base_dir);
	}

	if (getenv("CCACHE_UNIFY")) {
		cc_log("Unify mode disabled");
		enable_unify = true;
	}

	if (getenv("CCACHE_NODIRECT") || enable_unify) {
		cc_log("Direct mode disabled");
		enable_direct = false;
	}

	if (getenv("CCACHE_COMPRESS")) {
		cc_log("Compression enabled");
		enable_compression = true;
	}

	if ((env = getenv("CCACHE_NLEVELS"))) {
		nlevels = atoi(env);
		if (nlevels < 1) nlevels = 1;
		if (nlevels > 8) nlevels = 8;
	}

	if (!cc_process_args(orig_args, &preprocessor_args, &compiler_args)) {
		failed();
	}

	cc_log("Source file: %s", input_file);
	if (generating_dependencies) {
		cc_log("Dependency file: %s", output_dep);
	}
	cc_log("Object file: %s", output_obj);

	hash_start(&common_hash);
	calculate_common_hash(preprocessor_args, &common_hash);

	/* try to find the hash using the manifest */
	direct_hash = common_hash;
	if (enable_direct) {
		cc_log("Trying direct lookup");
		object_hash = calculate_object_hash(preprocessor_args, &direct_hash, 1);
		if (object_hash) {
			update_cached_result_globals(object_hash);

			/*
			 * If we can return from cache at this point then do
			 * so.
			 */
			from_cache(FROMCACHE_DIRECT_MODE, 0);

			/*
			 * Wasn't able to return from cache at this point.
			 * However, the object was already found in manifest,
			 * so don't readd it later.
			 */
			put_object_in_manifest = false;

			object_hash_from_manifest = object_hash;
		} else {
			/* Add object to manifest later. */
			put_object_in_manifest = true;
		}
	}

	/*
	 * Find the hash using the preprocessed output. Also updates
	 * included_files.
	 */
	cpp_hash = common_hash;
	cc_log("Running preprocessor");
	object_hash = calculate_object_hash(preprocessor_args, &cpp_hash, 0);
	if (!object_hash) {
		fatal("internal error: object hash from cpp returned NULL");
	}
	update_cached_result_globals(object_hash);

	if (object_hash_from_manifest
	    && !file_hashes_equal(object_hash_from_manifest, object_hash)) {
		/*
		 * The hash from manifest differs from the hash of the
		 * preprocessor output. This could be because:
		 *
		 * - The preprocessor produces different output for the same
		 *   input (not likely).
		 * - There's a bug in ccache (maybe incorrect handling of
		 *   compiler arguments).
		 * - The user has used a different CCACHE_BASEDIR (most
		 *   likely).
		 *
		 * The best thing here would probably be to remove the hash
		 * entry from the manifest. For now, we use a simpler method:
		 * just remove the manifest file.
		 */
		cc_log("Hash from manifest doesn't match preprocessor output");
		cc_log("Likely reason: different CCACHE_BASEDIRs used");
		cc_log("Removing manifest as a safety measure");
		x_unlink(manifest_path);

		put_object_in_manifest = true;
	}

	/* if we can return from cache at this point then do */
	from_cache(FROMCACHE_CPP_MODE, put_object_in_manifest);

	if (getenv("CCACHE_READONLY")) {
		cc_log("Read-only mode; running real compiler");
		failed();
	}

	/* add prefix from CCACHE_PREFIX */
	add_prefix(compiler_args);

	/* run real compiler, sending output to cache */
	to_cache(compiler_args);

	/* return from cache */
	from_cache(FROMCACHE_COMPILED_MODE, put_object_in_manifest);

	/* oh oh! */
	cc_log("Secondary from_cache failed");
	stats_update(STATS_ERROR);
	failed();
}

static void
check_cache_dir(void)
{
	if (!cache_dir) {
		fatal("Unable to determine cache directory");
	}
}

/* the main program when not doing a compile */
static int
ccache_main_options(int argc, char *argv[])
{
	int c;
	size_t v;

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
		{"show-stats",    no_argument,       0, 's'},
		{"version",       no_argument,       0, 'V'},
		{"zero-stats",    no_argument,       0, 'z'},
		{0, 0, 0, 0}
	};

	while ((c = getopt_long(argc, argv, "cChF:M:sVz", options, NULL)) != -1) {
		switch (c) {
		case DUMP_MANIFEST:
			manifest_dump(optarg, stdout);
			break;

		case 'c': /* --cleanup */
			check_cache_dir();
			cleanup_all(cache_dir);
			printf("Cleaned cache\n");
			break;

		case 'C': /* --clear */
			check_cache_dir();
			wipe_all(cache_dir);
			printf("Cleared cache\n");
			break;

		case 'h': /* --help */
			fputs(USAGE_TEXT, stdout);
			exit(0);

		case 'F': /* --max-files */
			check_cache_dir();
			v = atoi(optarg);
			if (stats_set_limits(v, -1) == 0) {
				if (v == 0) {
					printf("Unset cache file limit\n");
				} else {
					printf("Set cache file limit to %u\n", (unsigned)v);
				}
			} else {
				printf("Could not set cache file limit.\n");
				exit(1);
			}
			break;

		case 'M': /* --max-size */
			check_cache_dir();
			v = value_units(optarg);
			if (stats_set_limits(-1, v) == 0) {
				if (v == 0) {
					printf("Unset cache size limit\n");
				} else {
					char *s = format_size(v);
					printf("Set cache size limit to %s\n", s);
					free(s);
				}
			} else {
				printf("Could not set cache size limit.\n");
				exit(1);
			}
			break;

		case 's': /* --show-stats */
			check_cache_dir();
			stats_summary();
			break;

		case 'V': /* --version */
			fprintf(stdout, VERSION_TEXT, CCACHE_VERSION);
			exit(0);

		case 'z': /* --zero-stats */
			check_cache_dir();
			stats_zero();
			printf("Statistics cleared\n");
			break;

		default:
			fputs(USAGE_TEXT, stderr);
			exit(1);
		}
	}

	return 0;
}


/* Make a copy of stderr that will not be cached, so things like
   distcc can send networking errors to it. */
static void
setup_uncached_err(void)
{
	char *buf;
	int uncached_fd;

	uncached_fd = dup(2);
	if (uncached_fd == -1) {
		cc_log("dup(2) failed: %s", strerror(errno));
		failed();
	}

	/* leak a pointer to the environment */
	buf = format("UNCACHED_ERR_FD=%d", uncached_fd);

	if (putenv(buf) == -1) {
		cc_log("putenv failed: %s", strerror(errno));
		failed();
	}
}

int
ccache_main(int argc, char *argv[])
{
	char *p;
	char *program_name;

	exitfn_init();
	exitfn_add_nullary(stats_flush);
	exitfn_add_nullary(clean_up_tmp_files);

	/* check for logging early so cc_log messages start working ASAP */
	cache_logfile = getenv("CCACHE_LOGFILE");
	cc_log("=== CCACHE STARTED =========================================");

	/* the user might have set CCACHE_UMASK */
	p = getenv("CCACHE_UMASK");
	if (p) {
		mode_t mask;
		errno = 0;
		mask = strtol(p, NULL, 8);
		if (errno == 0) {
			umask(mask);
		}
	}

	current_working_dir = get_cwd();
	cache_dir = getenv("CCACHE_DIR");
	if (cache_dir) {
		cache_dir = x_strdup(cache_dir);
	} else {
		const char *home_directory = get_home_directory();
		if (home_directory) {
			cache_dir = format("%s/.ccache", home_directory);
		}
	}

	/* check if we are being invoked as "ccache" */
	program_name = basename(argv[0]);
	if (same_executable_name(program_name, MYNAME)) {
		if (argc < 2) {
			fputs(USAGE_TEXT, stderr);
			exit(1);
		}
		/* if the first argument isn't an option, then assume we are
		   being passed a compiler name and options */
		if (argv[1][0] == '-') {
			return ccache_main_options(argc, argv);
		}
	}
	free(program_name);

	check_cache_dir();

	base_dir = getenv("CCACHE_BASEDIR");
	if (base_dir && base_dir[0] != '/') {
		cc_log("Ignoring non-absolute base directory %s", base_dir);
		base_dir = NULL;
	}

	compile_preprocessed_source_code = !getenv("CCACHE_CPP2");

	setup_uncached_err();

	ccache(argc, argv);
	return 1;
}
