/*
  a re-implementation of the compilercache scripts in C

  The idea is based on the shell-script compilercache by Erik Thiele <erikyyy@erikyyy.de>

   Copyright (C) Andrew Tridgell 2002
   Copyright (C) Martin Pool 2003
   Copyright (C) Joel Rosdahl 2009

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
#include "hashtable.h"
#include "hashtable_itr.h"
#include "hashutil.h"
#include "manifest.h"

#include <getopt.h>

/* current working directory taken from $PWD, or getcwd() if $PWD is bad */
char *current_working_dir;

/* the base cache directory */
char *cache_dir = NULL;

/* the directory for temporary files */
char *temp_dir = NULL;

/* the debug logfile name, if set */
char *cache_logfile = NULL;

/* base directory (from CCACHE_BASEDIR) */
char *base_dir;

/* the argument list after processing */
static ARGS *stripped_args;

/* the original argument list */
static ARGS *orig_args;

/* the output filename being compiled to */
static char *output_file;

/* the source file */
static char *input_file;

/*
 * the hash of the file containing the cached object code (abcdef[...]-size)
 */
struct file_hash *object_hash;

/*
 * the name of the file containing the cached object code (abcdef[...]-size)
 */
static char *object_name;

/*
 * the full path of the file containing the cached object code
 * (cachedir/a/b/cdef[...]-size)
 */
static char *object_path;

/* the name of the manifest file without the extension (abcdef[...]-size) */
static char *manifest_name;

/*
 * the full path of the file containing the manifest
 * (cachedir/a/b/cdef[...]-size.manifest)
 */
static char *manifest_path;

/*
 * Time of compilation. Used to see if include files have changed after
 * compilation.
 */
static time_t time_of_compilation;

/*
 * Files included by the preprocessor and their hashes/sizes. Key: file path.
 * Value: struct file_hash.
 */
static struct hashtable *included_files;

/* is gcc being asked to output dependencies? */
static int generating_dependencies;

/* the path to the dependency file (implicit or specified with -MF) */
static char *dependency_path;

/* the extension of the file after pre-processing */
static const char *i_extension;

/* the name of the temporary pre-processor file */
static char *i_tmpfile;

/* are we compiling a .i or .ii file directly? */
static int direct_i_file;

/* the name of the cpp stderr file */
static char *cpp_stderr;

/* the name of the statistics file */
char *stats_file = NULL;

/* can we safely use the unification hashing backend? */
static int enable_unify;

/* should we use the direct mode? */
static int enable_direct = 1;

/* a list of supported file extensions, and the equivalent
   extension for code that has been through the pre-processor
*/
static struct {
	char *extension;
	char *i_extension;
} extensions[] = {
	{"c", "i"},
	{"C", "ii"},
	{"m", "mi"},
	{"cc", "ii"},
	{"CC", "ii"},
	{"cpp", "ii"},
	{"CPP", "ii"},
	{"cxx", "ii"},
	{"CXX", "ii"},
	{"c++", "ii"},
	{"C++", "ii"},
	{"i", "i"},
	{"ii", "ii"},
	{NULL, NULL}};

enum fromcache_call_mode {
	FROMCACHE_DIRECT_MODE,
	FROMCACHE_CPP_MODE,
	FROMCACHE_COMPILED_MODE
};

enum findhash_call_mode {
	FINDHASH_DIRECT_MODE,
	FINDHASH_CPP_MODE
};

/*
  something went badly wrong - just execute the real compiler
*/
static void failed(void)
{
	char *e;

	/* delete intermediate pre-processor file if needed */
	if (i_tmpfile) {
		if (!direct_i_file) {
			unlink(i_tmpfile);
		}
		free(i_tmpfile);
		i_tmpfile = NULL;
	}

	/* delete the cpp stderr file if necessary */
	if (cpp_stderr) {
		unlink(cpp_stderr);
		free(cpp_stderr);
		cpp_stderr = NULL;
	}

	/* strip any local args */
	args_strip(orig_args, "--ccache-");

	if ((e=getenv("CCACHE_PREFIX"))) {
		char *p = find_executable(e, MYNAME);
		if (!p) {
			perror(e);
			exit(1);
		}
		args_add_prefix(orig_args, p);
	}

	cc_log("Failed; falling back to running the real compiler\n");
	execv(orig_args->argv[0], orig_args->argv);
	cc_log("execv returned (%s)!\n", strerror(errno));
	perror(orig_args->argv[0]);
	exit(1);
}

char *format_file_hash(struct file_hash *file_hash)
{
	char *ret;
	int i;

	ret = x_malloc(53);
	for (i = 0; i < 16; i++) {
		sprintf(&ret[i*2], "%02x", (unsigned)file_hash->hash[i]);
	}
	sprintf(&ret[i*2], "-%u", (unsigned)file_hash->size);

	return ret;
}

/*
 * Transform a name to a full path into the cache directory, creating needed
 * sublevels if needed. Caller frees.
 */
static char *get_path_in_cache(const char *name, const char *suffix,
			       int nlevels)
{
	int i;
	char *path;
	char *result;

	path = x_strdup(cache_dir);
	for (i = 0; i < nlevels; ++i) {
		char *p;
		x_asprintf(&p, "%s/%c", path, name[i]);
		free(path);
		path = p;
		if (create_dir(path) != 0) {
			cc_log("failed to create %s\n", path);
			failed();
		}
	}
	x_asprintf(&result, "%s/%s%s", path, name + nlevels, suffix);
	free(path);
	return result;
}

/* Takes over ownership of path. */
static void remember_include_file(char *path, size_t path_len)
{
	struct file_hash *h;
	struct mdfour fhash;
	struct stat st;
	int fd = -1;
	int ret;

	if (!included_files) {
		goto ignore;
	}

	if (path_len >= 2 && (path[0] == '<' && path[path_len - 1] == '>')) {
		/* Typically <built-in> or <command-line>. */
		goto ignore;
	}

	if (strcmp(path, input_file) == 0) {
		/* Don't remember the input file. */
		goto ignore;
	}

	if (hashtable_search(included_files, path)) {
		/* Already known include file. */
		goto ignore;
	}

	/* Let's hash the include file. */
	fd = open(path, O_RDONLY|O_BINARY);
	if (fd == -1) {
		cc_log("Failed to open include file \"%s\"\n", path);
		goto failure;
	}
	if (fstat(fd, &st) != 0) {
		cc_log("Failed to fstat include file \"%s\"\n", path);
		goto failure;
	}
	if (S_ISDIR(st.st_mode)) {
		/* Ignore directory, typically $PWD. */
		goto ignore;
	}
	if (st.st_mtime >= time_of_compilation
	    || st.st_ctime >= time_of_compilation) {
		cc_log("Include file \"%s\" too new\n", path);
		goto failure;
	}
	hash_start(&fhash);
	ret = hash_fd(&fhash, fd);
	if (!ret) {
		cc_log("Failed hashing include file \"%s\"\n", path);
		goto failure;
	}

	/* Hashing OK. */
	h = x_malloc(sizeof(*h));
	hash_result_as_bytes(&fhash, h->hash);
	h->size = fhash.totalN;
	hashtable_insert(included_files, path, h);
	close(fd);
	return;

failure:
	cc_log("Disabling direct mode\n");
	enable_direct = 0;
	hashtable_destroy(included_files, 1);
	included_files = NULL;
	/* Fall through. */
ignore:
	free(path);
	if (fd != -1) {
		close(fd);
	}
}

/*
 * Make a relative path from CCACHE_BASEDIR to path. Takes over ownership of
 * path. Caller frees.
 */
static char *make_relative_path(char *path)
{
	char *relpath;

	if (!base_dir || strncmp(path, base_dir, strlen(base_dir)) != 0) {
		return path;
	}

	relpath = get_relative_path(current_working_dir, path);
	free(path);
	return relpath;
}

/*
 * This function reads and hashes a file. While doing this, it also does these
 * things with preprocessor lines starting with a hash:
 *
 * - Makes include file paths whose prefix is CCACHE_BASEDIR relative.
 * - Stores the paths of included files in the global variable included_files.
 */
static void process_preprocessed_file(struct mdfour *hash, const char *path)
{
	int fd;
	char *data;
	char *p, *q, *end;
	off_t size;
	struct stat st;

	fd = open(path, O_RDONLY);
	if (fd == -1) {
		cc_log("failed to open %s\n", path);
		failed();
	}
	if (fstat(fd, &st) != 0) {
		cc_log("failed to fstat %s\n", path);
		failed();
	}
	size = st.st_size;
	data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (data == (void *)-1) {
		cc_log("failed to mmap %s\n", path);
		failed();
	}
	close(fd);

	if (enable_direct) {
		included_files = create_hashtable(1000, hash_from_string,
						  strings_equal);
	}

	/* Bytes between p and q are pending to be hashed. */
	end = data + size;
	p = data;
	q = data;
	while (q < end - 1) {
		if (q[0] == '#' && q[1] == ' ' /* Need to avoid "#pragma"... */
		    && (q == data || q[-1] == '\n')) {
			char *path;

			while (q < end && *q != '"') {
				q++;
			}
			q++;
			if (q >= end) {
				failed();
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
			if (enable_direct) {
				remember_include_file(path, q - p);
			} else {
				free(path);
			}
			p = q;
		} else {
			q++;
		}
	}

	hash_buffer(hash, p, (end - p));
	munmap(data, size);
}

/* run the real compiler and put the result in cache */
static void to_cache(ARGS *args)
{
	char *tmp_stdout, *tmp_stderr, *tmp_hashname;
	struct stat st;
	int status;
	int compress;

	x_asprintf(&tmp_stdout, "%s.tmp.stdout.%s", object_path, tmp_string());
	x_asprintf(&tmp_stderr, "%s.tmp.stderr.%s", object_path, tmp_string());
	x_asprintf(&tmp_hashname, "%s.tmp.%s", object_path, tmp_string());

	args_add(args, "-o");
	args_add(args, tmp_hashname);

	/* Turn off DEPENDENCIES_OUTPUT when running cc1, because
	 * otherwise it will emit a line like
	 *
	 *  tmp.stdout.vexed.732.o: /home/mbp/.ccache/tmp.stdout.vexed.732.i
	 *
	 * unsetenv() is on BSD and Linux but not portable. */
	putenv("DEPENDENCIES_OUTPUT");

	if (getenv("CCACHE_CPP2")) {
		args_add(args, input_file);
	} else {
		args_add(args, i_tmpfile);
	}

	cc_log("Running real compiler\n");
	status = execute(args->argv, tmp_stdout, tmp_stderr);
	args_pop(args, 3);

	if (stat(tmp_stdout, &st) != 0 || st.st_size != 0) {
		cc_log("Compiler produced stdout for %s\n", output_file);
		stats_update(STATS_STDOUT);
		unlink(tmp_stdout);
		unlink(tmp_stderr);
		unlink(tmp_hashname);
		failed();
	}
	unlink(tmp_stdout);

	/*
	 * Merge stderr from the preprocessor (if any) and stderr from the real
	 * compiler into tmp_stderr.
	 */
	if (cpp_stderr) {
		int fd_cpp_stderr;
		int fd_real_stderr;
		int fd_result;

		fd_cpp_stderr = open(cpp_stderr, O_RDONLY | O_BINARY);
		if (fd_cpp_stderr == -1) {
			failed();
		}
		fd_real_stderr = open(tmp_stderr, O_RDONLY | O_BINARY);
		if (fd_real_stderr == -1) {
			failed();
		}
		unlink(tmp_stderr);
		fd_result = open(tmp_stderr,
				 O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
				 0666);
		if (fd_result == -1) {
			failed();
		}
		copy_fd(fd_cpp_stderr, fd_result);
		copy_fd(fd_real_stderr, fd_result);
		close(fd_cpp_stderr);
		close(fd_real_stderr);
		close(fd_result);
		unlink(cpp_stderr);
		free(cpp_stderr);
	}

	if (status != 0) {
		int fd;
		cc_log("Compile of %s gave status = %d\n", output_file, status);
		stats_update(STATS_STATUS);

		fd = open(tmp_stderr, O_RDONLY | O_BINARY);
		if (fd != -1) {
			if (strcmp(output_file, "/dev/null") == 0
			    || move_file(tmp_hashname, output_file, 0) == 0
			    || errno == ENOENT) {
				/* we can use a quick method of
				   getting the failed output */
				copy_fd(fd, 2);
				close(fd);
				unlink(tmp_stderr);
				if (i_tmpfile && !direct_i_file) {
					unlink(i_tmpfile);
				}
				exit(status);
			}
		}

		unlink(tmp_stderr);
		unlink(tmp_hashname);
		failed();
	}

	compress = !getenv("CCACHE_NOCOMPRESS");

	if (stat(tmp_stderr, &st) != 0) {
		cc_log("Failed to stat %s\n", tmp_stderr);
		stats_update(STATS_ERROR);
		failed();
	}
	if (st.st_size > 0) {
		char *path_stderr;
		x_asprintf(&path_stderr, "%s.stderr", object_path);
		if (move_file(tmp_stderr, path_stderr, compress) != 0) {
			cc_log("Failed to move tmp stderr to the cache\n");
			stats_update(STATS_ERROR);
			failed();
		}
		cc_log("Stored stderr from the compiler in the cache\n");
		free(path_stderr);
	} else {
		unlink(tmp_stderr);
	}
	if (move_file(tmp_hashname, object_path, compress) != 0) {
		cc_log("Failed to move tmp object file into the cache\n");
		stats_update(STATS_ERROR);
		failed();
	}

	/*
	 * Do an extra stat on the potentially compressed object file for the
	 * size statistics.
	 */
	if (stat(object_path, &st) != 0) {
		cc_log("Failed to stat %s\n", strerror(errno));
		stats_update(STATS_ERROR);
		failed();
	}

	cc_log("Placed object file into the cache\n");
	stats_tocache(file_size(&st));

	free(tmp_hashname);
	free(tmp_stderr);
	free(tmp_stdout);
}

/*
 * Find the object file name by running the compiler in preprocessor mode.
 * Returns the hash as a heap-allocated hex string.
 */
static struct file_hash *
get_object_name_from_cpp(ARGS *args, struct mdfour *hash)
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
	input_base = str_basename(input_file);
	tmp = strchr(input_base, '.');
	if (tmp != NULL) {
		*tmp = 0;
	}
	if (strlen(input_base) > 10) {
		input_base[10] = 0;
	}

	/* now the run */
	x_asprintf(&path_stdout, "%s/%s.tmp.%s.%s", temp_dir,
		   input_base, tmp_string(), i_extension);
	x_asprintf(&path_stderr, "%s/tmp.cpp_stderr.%s", temp_dir,
		   tmp_string());

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
			stats_update(STATS_ERROR);
			cc_log("failed to create empty stderr file\n");
			failed();
		}
		status = 0;
	}

	if (status != 0) {
		if (!direct_i_file) {
			unlink(path_stdout);
		}
		unlink(path_stderr);
		cc_log("the preprocessor gave %d\n", status);
		stats_update(STATS_PREPROCESSOR);
		failed();
	}

	/* if the compilation is with -g then we have to include the whole of the
	   preprocessor output, which means we are sensitive to line number
	   information. Otherwise we can discard line number info, which makes
	   us less sensitive to reformatting changes

	   Note! I have now disabled the unification code by default
	   as it gives the wrong line numbers for warnings. Pity.
	*/
	if (!enable_unify) {
		process_preprocessed_file(hash, path_stdout);
	} else {
		if (unify_hash(hash, path_stdout) != 0) {
			stats_update(STATS_ERROR);
			failed();
		}
	}

	if (!hash_file(hash, path_stderr)) {
		fatal("Failed to open %s\n", path_stderr);
	}

	i_tmpfile = path_stdout;

	if (!getenv("CCACHE_CPP2")) {
		/* if we are using the CPP trick then we need to remember this
		   stderr stderr data and output it just before the main stderr
		   from the compiler pass */
		cpp_stderr = path_stderr;
	} else {
		unlink(path_stderr);
		free(path_stderr);
	}

	result = x_malloc(sizeof(*result));
	hash_result_as_bytes(hash, result->hash);
	result->size = hash->totalN;
	return result;
}

/* find the hash for a command. The hash includes all argument lists,
   plus the output from running the compiler with -E */
static int find_hash(ARGS *args, enum findhash_call_mode mode)
{
	int i;
	char *s;
	struct stat st;
	int nlevels = 2;
	struct mdfour hash;

	switch (mode) {
	case FINDHASH_DIRECT_MODE:
		cc_log("Trying direct lookup\n");
		break;

	case FINDHASH_CPP_MODE:
		cc_log("Running preprocessor\n");
		break;
	}

	if ((s = getenv("CCACHE_NLEVELS"))) {
		nlevels = atoi(s);
		if (nlevels < 1) nlevels = 1;
		if (nlevels > 8) nlevels = 8;
	}

	hash_start(&hash);

	/* when we are doing the unifying tricks we need to include
	   the input file name in the hash to get the warnings right */
	if (enable_unify) {
		hash_string(&hash, input_file);
	}

	/* we have to hash the extension, as a .i file isn't treated the same
	   by the compiler as a .ii file */
	hash_string(&hash, i_extension);

	/* first the arguments */
	for (i=1;i<args->argc;i++) {
		/* -L doesn't affect compilation. */
		if (i < args->argc-1 && strcmp(args->argv[i], "-L") == 0) {
			i++;
			continue;
		}
		if (strncmp(args->argv[i], "-L", 2) == 0) {
			continue;
		}

		/* When using the preprocessor, some arguments don't contribute
		   to the hash. The theory is that these arguments will change
		   the output of -E if they are going to have any effect at
		   all. */
		if (mode == FINDHASH_CPP_MODE) {
			if (i < args->argc-1) {
				if (strcmp(args->argv[i], "-I") == 0 ||
				    strcmp(args->argv[i], "-include") == 0 ||
				    strcmp(args->argv[i], "-D") == 0 ||
				    strcmp(args->argv[i], "-idirafter") == 0 ||
				    strcmp(args->argv[i], "-isystem") == 0) {
					/* Skip from hash. */
					i++;
					continue;
				}
			}
			if (strncmp(args->argv[i], "-I", 2) == 0 ||
			    strncmp(args->argv[i], "-D", 2) == 0) {
				/* Skip from hash. */
				continue;
			}
		}

		if (strncmp(args->argv[i], "--specs=", 8) == 0 &&
		    stat(args->argv[i]+8, &st) == 0) {
			/* If given a explicit specs file, then hash that file,
			   but don't include the path to it in the hash. */
			if (!hash_file(&hash, args->argv[i]+8)) {
				failed();
			}
			continue;
		}

		/* All other arguments are included in the hash. */
		hash_string(&hash, args->argv[i]);
	}

	/* The compiler driver size and date. This is a simple minded way
	   to try and detect compiler upgrades. It is not 100% reliable. */
	if (stat(args->argv[0], &st) != 0) {
		cc_log("Couldn't stat the compiler!? (argv[0]='%s')\n", args->argv[0]);
		stats_update(STATS_COMPILER);
		failed();
	}

	/* also include the hash of the compiler name - as some compilers
	   use hard links and behave differently depending on the real name */
	if (st.st_nlink > 1) {
		hash_string(&hash, str_basename(args->argv[0]));
	}

	if (getenv("CCACHE_HASH_COMPILER")) {
		hash_file(&hash, args->argv[0]);
	} else if (!getenv("CCACHE_NOHASH_SIZE_MTIME")) {
		hash_int(&hash, st.st_size);
		hash_int(&hash, st.st_mtime);
	}

	/* possibly hash the current working directory */
	if (getenv("CCACHE_HASHDIR")) {
		char *cwd = gnu_getcwd();
		if (cwd) {
			hash_string(&hash, cwd);
			free(cwd);
		}
	}

	switch (mode) {
	case FINDHASH_DIRECT_MODE:
		if (!hash_file(&hash, input_file)) {
			failed();
		}
		manifest_name = hash_result(&hash);
		manifest_path = get_path_in_cache(manifest_name, ".manifest",
						  nlevels);
		object_hash = manifest_get(manifest_path);
		if (object_hash) {
			cc_log("Got object file hash from manifest\n");
		} else {
			cc_log("Did not find object file hash in manifest\n");
			return 0;
		}
		break;

	case FINDHASH_CPP_MODE:
		object_hash = get_object_name_from_cpp(args, &hash);
		cc_log("Got object file hash from preprocessor\n");
		if (generating_dependencies) {
			cc_log("Preprocessor created %s\n", dependency_path);
		}
		break;
	}

	object_name = format_file_hash(object_hash);
	object_path = get_path_in_cache(object_name, "", nlevels);
	x_asprintf(&stats_file, "%s/%c/stats", cache_dir, object_name[0]);

	return 1;
}

/*
   try to return the compile result from cache. If we can return from
   cache then this function exits with the correct status code,
   otherwise it returns */
static void from_cache(enum fromcache_call_mode mode, int put_object_in_manifest)
{
	int fd_stderr;
	char *stderr_file;
	char *dep_file;
	int ret;
	struct stat st;
	int produce_dep_file;

	/* the user might be disabling cache hits */
	if (mode != FROMCACHE_COMPILED_MODE && getenv("CCACHE_RECACHE")) {
		return;
	}

	/* Check if the object file is there. */
	if (stat(object_path, &st) != 0) {
		cc_log("Did not find object file in cache\n");
		return;
	}

	/*
	 * (If mode != FROMCACHE_DIRECT_MODE, the dependency file is created by
	 * gcc.)
	 */
	produce_dep_file = \
		generating_dependencies && mode == FROMCACHE_DIRECT_MODE;

	/* If the dependency file should be in the cache, check that it is. */
	x_asprintf(&dep_file, "%s.d", object_path);
	if (produce_dep_file && stat(dep_file, &st) != 0) {
		cc_log("Dependency file missing in cache\n");
		free(dep_file);
		return;
	}

	x_asprintf(&stderr_file, "%s.stderr", object_path);

	if (strcmp(output_file, "/dev/null") == 0) {
		ret = 0;
	} else {
		unlink(output_file);
		/* only make a hardlink if the cache file is uncompressed */
		if (getenv("CCACHE_HARDLINK") &&
		    test_if_compressed(object_path) == 0) {
			ret = link(object_path, output_file);
		} else {
			ret = copy_file(object_path, output_file, 0);
		}
	}

	if (ret == -1) {
		if (errno == ENOENT) {
			/* Someone removed the file just before we began copying? */
			cc_log("Object file missing for %s\n", output_file);
			stats_update(STATS_MISSING);
		} else {
			cc_log("Failed to copy/link %s -> %s (%s)\n",
			       object_path, output_file, strerror(errno));
			stats_update(STATS_ERROR);
			failed();
		}
		unlink(output_file);
		unlink(stderr_file);
		unlink(object_path);
		unlink(dep_file);
		free(dep_file);
		free(stderr_file);
		return;
	} else {
		cc_log("Created %s\n", output_file);
	}

	if (produce_dep_file) {
		unlink(dependency_path);
		/* only make a hardlink if the cache file is uncompressed */
		if (getenv("CCACHE_HARDLINK") &&
		    test_if_compressed(dep_file) == 0) {
			ret = link(dep_file, dependency_path);
		} else {
			ret = copy_file(dep_file, dependency_path, 0);
		}
		if (ret == -1) {
			if (errno == ENOENT) {
				/*
				 * Someone removed the file just before we
				 * began copying?
				 */
				cc_log("dependency file missing for %s\n",
				       output_file);
				stats_update(STATS_MISSING);
			} else {
				cc_log("failed to copy/link %s -> %s (%s)\n",
				       dep_file, dependency_path,
				       strerror(errno));
				stats_update(STATS_ERROR);
				failed();
			}
			unlink(output_file);
			unlink(stderr_file);
			unlink(object_path);
			unlink(dep_file);
			free(dep_file);
			free(stderr_file);
			return;
		} else {
			cc_log("Created %s\n", dependency_path);
		}
	}

	/* update timestamps for LRU cleanup
	   also gives output_file a sensible mtime when hard-linking (for make) */
#ifdef HAVE_UTIMES
	utimes(object_path, NULL);
	utimes(stderr_file, NULL);
	if (produce_dep_file) {
		utimes(dep_file, NULL);
	}
#else
	utime(object_path, NULL);
	utime(stderr_file, NULL);
	if (produce_dep_file) {
		utime(dep_file, NULL);
	}
#endif

	if (generating_dependencies && mode != FROMCACHE_DIRECT_MODE) {
		/* Store the dependency file in the cache. */
		ret = copy_file(dependency_path, dep_file, 1);
		if (ret == -1) {
			cc_log("Failed to copy %s -> %s\n", dependency_path,
			       dep_file);
			/* Continue despite the error. */
		} else {
			cc_log("Placed dependency file into the cache\n");
		}
	}
	free(dep_file);

	/* get rid of the intermediate preprocessor file */
	if (i_tmpfile) {
		if (!direct_i_file) {
			unlink(i_tmpfile);
		}
		free(i_tmpfile);
		i_tmpfile = NULL;
	}

	/* Send the stderr, if any. */
	fd_stderr = open(stderr_file, O_RDONLY | O_BINARY);
	if (fd_stderr != -1) {
		copy_fd(fd_stderr, 2);
		close(fd_stderr);
	}
	free(stderr_file);

	/* Create or update the manifest file. */
	if (put_object_in_manifest && included_files) {
		if (manifest_put(manifest_path, object_hash, included_files)) {
			cc_log("Added object file hash to manifest\n");
			/* Update timestamp for LRU cleanup. */
#ifdef HAVE_UTIMES
			utimes(manifest_path, NULL);
#else
			utime(manifest_path, NULL);
#endif
		} else {
			cc_log("Failed to add object file hash to manifest\n");
		}
	}

	/* log the cache hit */
	switch (mode) {
	case FROMCACHE_DIRECT_MODE:
		cc_log("Succeded getting cached result\n");
		stats_update(STATS_CACHEHIT_DIR);
		break;

	case FROMCACHE_CPP_MODE:
		cc_log("Succeded getting cached result\n");
		stats_update(STATS_CACHEHIT_CPP);
		break;

	case FROMCACHE_COMPILED_MODE:
		break;
	}

	/* and exit with the right status code */
	exit(0);
}

/* find the real compiler. We just search the PATH to find a executable of the
   same name that isn't a link to ourselves */
static void find_compiler(int argc, char **argv)
{
	char *base;
	char *path;

	orig_args = args_init(argc, argv);

	base = str_basename(argv[0]);

	/* we might be being invoked like "ccache gcc -c foo.c" */
	if (strcmp(base, MYNAME) == 0) {
		args_remove_first(orig_args);
		free(base);
		if (strchr(argv[1],'/')) {
			/* a full path was given */
			return;
		}
		base = str_basename(argv[1]);
	}

	/* support user override of the compiler */
	if ((path=getenv("CCACHE_CC"))) {
		base = strdup(path);
	}

	orig_args->argv[0] = find_executable(base, MYNAME);

	/* can't find the compiler! */
	if (!orig_args->argv[0]) {
		stats_update(STATS_COMPILER);
		perror(base);
		exit(1);
	}
}


/* check a filename for C/C++ extension. Return the pre-processor
   extension */
static const char *check_extension(const char *fname, int *direct_i)
{
	int i;
	const char *p;

	if (direct_i) {
		*direct_i = 0;
	}

	p = strrchr(fname, '.');
	if (!p) return NULL;
	p++;
	for (i=0; extensions[i].extension; i++) {
		if (strcmp(p, extensions[i].extension) == 0) {
			if (direct_i && strcmp(p, extensions[i].i_extension) == 0) {
				*direct_i = 1;
			}
			p = getenv("CCACHE_EXTENSION");
			if (p) return p;
			return extensions[i].i_extension;
		}
	}
	return NULL;
}


/*
   process the compiler options to form the correct set of options
   for obtaining the preprocessor output
*/
static void process_args(int argc, char **argv)
{
	int i;
	int found_c_opt = 0;
	int found_S_opt = 0;
	struct stat st;
	/* is the dependency makefile name overridden with -MF? */
	int dependency_filename_specified = 0;
	/* is the dependency makefile target name specified with -MT or -MQ? */
	int dependency_target_specified = 0;

	stripped_args = args_init(0, NULL);

	args_add(stripped_args, argv[0]);

	for (i=1; i<argc; i++) {
		/* some options will never work ... */
		if (strcmp(argv[i], "-E") == 0) {
			failed();
		}

		/* these are too hard */
		if (strcmp(argv[i], "-fbranch-probabilities") == 0 ||
		    strcmp(argv[i], "--coverage") == 0 ||
		    strcmp(argv[i], "-fprofile-arcs") == 0 ||
		    strcmp(argv[i], "-ftest-coverage") == 0 ||
		    strcmp(argv[i], "-M") == 0 ||
		    strcmp(argv[i], "-MM") == 0 ||
		    strcmp(argv[i], "-x") == 0) {
			cc_log("argument %s is unsupported\n", argv[i]);
			stats_update(STATS_UNSUPPORTED);
			failed();
			continue;
		}

		/* we must have -c */
		if (strcmp(argv[i], "-c") == 0) {
			args_add(stripped_args, argv[i]);
			found_c_opt = 1;
			continue;
		}

		/* -S changes the default extension */
		if (strcmp(argv[i], "-S") == 0) {
			args_add(stripped_args, argv[i]);
			found_S_opt = 1;
			continue;
		}

		/* we need to work out where the output was meant to go */
		if (strcmp(argv[i], "-o") == 0) {
			if (i == argc-1) {
				cc_log("missing argument to %s\n", argv[i]);
				stats_update(STATS_ARGS);
				failed();
			}
			output_file = argv[i+1];
			i++;
			continue;
		}

		/* alternate form of -o, with no space */
		if (strncmp(argv[i], "-o", 2) == 0) {
			output_file = &argv[i][2];
			continue;
		}

		/* debugging is handled specially, so that we know if we
		   can strip line number info
		*/
		if (strncmp(argv[i], "-g", 2) == 0) {
			args_add(stripped_args, argv[i]);
			if (strcmp(argv[i], "-g0") != 0) {
				enable_unify = 0;
			}
			continue;
		}

		/* The user knows best: just swallow the next arg */
		if (strcmp(argv[i], "--ccache-skip") == 0) {
			i++;
			if (i == argc) {
				failed();
			}
			args_add(stripped_args, argv[i]);
			continue;
		}

		/* These options require special handling, because they
		   behave differently with gcc -E, when the output
		   file is not specified. */
		if (strcmp(argv[i], "-MD") == 0
		    || strcmp(argv[i], "-MMD") == 0) {
			generating_dependencies = 1;
		}
		if (i < argc - 1) {
			if (strcmp(argv[i], "-MF") == 0) {
				dependency_filename_specified = 1;
				dependency_path = make_relative_path(
					x_strdup(argv[i + 1]));
			} else if (strcmp(argv[i], "-MQ") == 0
				   || strcmp(argv[i], "-MT") == 0) {
				dependency_target_specified = 1;
			}
		}

		if (enable_direct && strncmp(argv[i], "-Wp,", 4) == 0) {
			if (strncmp(argv[i], "-Wp,-MD,", 8) == 0) {
				generating_dependencies = 1;
				dependency_filename_specified = 1;
				dependency_path = make_relative_path(
					x_strdup(argv[i] + 8));
			} else if (strncmp(argv[i], "-Wp,-MMD,", 9) == 0) {
				generating_dependencies = 1;
				dependency_filename_specified = 1;
				dependency_path = make_relative_path(
					x_strdup(argv[i] + 9));
			} else if (enable_direct) {
				cc_log("Unsupported compiler option for direct mode: %s\n",
				       argv[i]);
				enable_direct = 0;
			}
		}

		/*
		 * Options taking an argument that that we may want to rewrite
		 * to relative paths to get better hit rate. A secondary effect
		 * is that paths in the standard error output produced by the
		 * compiler will be normalized.
		 */
		{
			const char *opts[] = {
				"-I", "-idirafter", "-include", "-isystem", NULL
			};
			int j;
			char *relpath;
			for (j = 0; opts[j]; j++) {
				if (strcmp(argv[i], opts[j]) == 0) {
					if (i == argc-1) {
						cc_log("missing argument to %s\n",
						       argv[i]);
						stats_update(STATS_ARGS);
						failed();
					}

					args_add(stripped_args, argv[i]);
					relpath = make_relative_path(x_strdup(argv[i+1]));
					args_add(stripped_args, relpath);
					free(relpath);
					i++;
					break;
				}
			}
			if (opts[j]) {
				continue;
			}
		}

		/* Same as above but options with concatenated argument. */
		{
			const char *opts[] = {"-I", NULL};
			int j;
			char *relpath;
			char *option;
			for (j = 0; opts[j]; j++) {
				if (strncmp(argv[i], opts[j], strlen(opts[j])) == 0) {
					relpath = make_relative_path(
						x_strdup(argv[i] + strlen(opts[j])));
					x_asprintf(&option, "%s%s", opts[j], relpath);
					args_add(stripped_args, option);
					free(relpath);
					free(option);
					break;
				}
			}
			if (opts[j]) {
				continue;
			}
		}

		/* options that take an argument */
		{
			const char *opts[] = {"-imacros", "-iprefix",
					      "-iwithprefix", "-iwithprefixbefore",
					      "-L", "-D", "-U", "-x", "-MF",
					      "-MT", "-MQ", "-aux-info",
					      "--param", "-A", "-Xlinker", "-u",
					      NULL};
			int j;
			for (j=0;opts[j];j++) {
				if (strcmp(argv[i], opts[j]) == 0) {
					if (i == argc-1) {
						cc_log("missing argument to %s\n",
						       argv[i]);
						stats_update(STATS_ARGS);
						failed();
					}

					args_add(stripped_args, argv[i]);
					args_add(stripped_args, argv[i+1]);
					i++;
					break;
				}
			}
			if (opts[j]) continue;
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
			args_add(stripped_args, argv[i]);
			continue;
		}

		if (input_file) {
			if (check_extension(argv[i], NULL)) {
				cc_log("multiple input files (%s and %s)\n",
				       input_file, argv[i]);
				stats_update(STATS_MULTIPLE);
			} else if (!found_c_opt) {
				cc_log("called for link with %s\n", argv[i]);
				if (strstr(argv[i], "conftest.")) {
					stats_update(STATS_CONFTEST);
				} else {
					stats_update(STATS_LINK);
				}
			} else {
				cc_log("non C/C++ file %s\n", argv[i]);
				stats_update(STATS_NOTC);
			}
			failed();
		}

		/* Rewrite to relative to increase hit rate. */
		input_file = make_relative_path(x_strdup(argv[i]));
	}

	if (!input_file) {
		cc_log("No input file found\n");
		stats_update(STATS_NOINPUT);
		failed();
	}

	i_extension = check_extension(input_file, &direct_i_file);
	if (i_extension == NULL) {
		cc_log("Not a C/C++ file - %s\n", input_file);
		stats_update(STATS_NOTC);
		failed();
	}

	if (!found_c_opt) {
		cc_log("No -c option found for %s\n", input_file);
		/* I find that having a separate statistic for autoconf tests is useful,
		   as they are the dominant form of "called for link" in many cases */
		if (strstr(input_file, "conftest.")) {
			stats_update(STATS_CONFTEST);
		} else {
			stats_update(STATS_LINK);
		}
		failed();
	}


	/* don't try to second guess the compilers heuristics for stdout handling */
	if (output_file && strcmp(output_file, "-") == 0) {
		stats_update(STATS_OUTSTDOUT);
		failed();
	}

	if (!output_file) {
		char *p;
		output_file = x_strdup(input_file);
		if ((p = strrchr(output_file, '/'))) {
			output_file = p+1;
		}
		p = strrchr(output_file, '.');
		if (!p || !p[1]) {
			cc_log("badly formed output_file %s\n", output_file);
			stats_update(STATS_ARGS);
			failed();
		}
		p[1] = found_S_opt ? 's' : 'o';
		p[2] = 0;
	}

	/* If dependencies are generated, configure the preprocessor */

	if (generating_dependencies && output_file) {
		if (!dependency_filename_specified) {
			char *default_depfile_name = x_strdup(output_file);
			char *p = strrchr(default_depfile_name, '.');

			if (p) {
				if (strlen(p) < 2) {
					stats_update(STATS_ARGS);
					failed();
					return;
				}
				*p = 0;
			}
			else  {
				int len = p - default_depfile_name;

				p = x_malloc(len + 3);
				strncpy(default_depfile_name, p, len - 1);
				free(default_depfile_name);
				default_depfile_name = p;
			}

			strcat(default_depfile_name, ".d");
			args_add(stripped_args, "-MF");
			args_add(stripped_args, default_depfile_name);
			dependency_path = make_relative_path(
				x_strdup(default_depfile_name));
		}

		if (!dependency_target_specified) {
			args_add(stripped_args, "-MT");
			args_add(stripped_args, output_file);
		}
	}

	/* cope with -o /dev/null */
	if (strcmp(output_file,"/dev/null") != 0 && stat(output_file, &st) == 0 && !S_ISREG(st.st_mode)) {
		cc_log("Not a regular file %s\n", output_file);
		stats_update(STATS_DEVICE);
		failed();
	}
}

/* the main ccache driver function */
static void ccache(int argc, char *argv[])
{
	char *prefix;
	char now[64];
	time_t t;
	struct tm *tm;
	int put_object_in_manifest = 0;
	struct file_hash *object_hash_from_manifest = NULL;

	t = time(NULL);
	tm = localtime(&t);
	if (!tm) {
		cc_log("localtime failed\n");
		failed();
	}

	if (strftime(now, sizeof(now), "%Y-%m-%d %H:%M:%S", tm) == 0) {
		cc_log("strftime failed\n");
		failed();
	}

	cc_log("=== %s ===\n", now);

	cc_log("Base directory: %s\n", base_dir);

	/* find the real compiler */
	find_compiler(argc, argv);

	/* use the real compiler if HOME is not set */
	if (!cache_dir) {
		cc_log("Unable to determine home directory\n");
		cc_log("ccache is disabled\n");
		failed();
	}

	/* we might be disabled */
	if (getenv("CCACHE_DISABLE")) {
		cc_log("ccache is disabled\n");
		failed();
	}

	if (getenv("CCACHE_UNIFY")) {
		enable_unify = 1;
	}

	if (getenv("CCACHE_NODIRECT") || enable_unify) {
		cc_log("Direct mode disabled\n");
		enable_direct = 0;
	}

	/* process argument list, returning a new set of arguments for
	   pre-processing */
	process_args(orig_args->argc, orig_args->argv);

	cc_log("Source file: %s\n", input_file);
	if (generating_dependencies) {
		cc_log("Dependency file: %s\n", dependency_path);
	}
	cc_log("Object file: %s\n", output_file);

	/* try to find the hash using the manifest */
	if (enable_direct) {
		if (find_hash(stripped_args, FINDHASH_DIRECT_MODE)) {
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
			put_object_in_manifest = 0;

			object_hash_from_manifest = object_hash;
			object_hash = NULL;
		} else {
			/* Add object to manifest later. */
			put_object_in_manifest = 1;
		}
	}

	/*
	 * Find the hash using the preprocessed output. Also updates
	 * included_files.
	 */
	find_hash(stripped_args, FINDHASH_CPP_MODE);

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
		cc_log("Hash from manifest doesn't match preprocessor output\n");
		cc_log("Likely reason: different CCACHE_BASEDIRs used\n");
		cc_log("Removing manifest as a safety measure\n");
		unlink(manifest_path);

		put_object_in_manifest = 1;
	}

	/* if we can return from cache at this point then do */
	from_cache(FROMCACHE_CPP_MODE, put_object_in_manifest);

	if (getenv("CCACHE_READONLY")) {
		cc_log("read-only set - doing real compile\n");
		failed();
	}

	prefix = getenv("CCACHE_PREFIX");
	if (prefix) {
		char *p = find_executable(prefix, MYNAME);
		if (!p) {
			perror(prefix);
			exit(1);
		}
		args_add_prefix(stripped_args, p);
	}

	/* run real compiler, sending output to cache */
	to_cache(stripped_args);

	/* return from cache */
	from_cache(FROMCACHE_COMPILED_MODE, put_object_in_manifest);

	/* oh oh! */
	cc_log("secondary from_cache failed!\n");
	stats_update(STATS_ERROR);
	failed();
}


static void usage(void)
{
	printf("ccache, a compiler cache. Version %s\n", CCACHE_VERSION);
	printf("Copyright Andrew Tridgell, 2002\n\n");

	printf("Usage:\n");
	printf("\tccache [options]\n");
	printf("\tccache compiler [compile options]\n");
	printf("\tcompiler [compile options]    (via symbolic link)\n");
	printf("\nOptions:\n");

	printf("-s, --show-stats         show statistics summary\n");
	printf("-z, --zero-stats         zero statistics\n");
	printf("-c, --cleanup            run a cache cleanup\n");
	printf("-C, --clear              clear the cache completely\n");
	printf("-F <n>, --max-files=<n>  set maximum files in cache\n");
	printf("-M <n>, --max-size=<n>   set maximum size of cache (use G, M or K)\n");
	printf("-h, --help               this help page\n");
	printf("-V, --version            print version number\n");
}

static void check_cache_dir(void)
{
	if (!cache_dir) {
		fatal("Unable to determine home directory\n");
	}
}

/* the main program when not doing a compile */
static int ccache_main(int argc, char *argv[])
{
	int c;
	size_t v;

	static struct option long_options[] = {
		{"show-stats", no_argument,       0, 's'},
		{"zero-stats", no_argument,       0, 'z'},
		{"cleanup",    no_argument,       0, 'c'},
		{"clear",      no_argument,       0, 'C'},
		{"max-files",  required_argument, 0, 'F'},
		{"max-size",   required_argument, 0, 'M'},
		{"help",       no_argument,       0, 'h'},
		{"version",    no_argument,       0, 'V'},
		{0, 0, 0, 0}
	};
	int option_index = 0;

	while ((c = getopt_long(argc, argv, "hszcCF:M:V", long_options, &option_index)) != -1) {
		switch (c) {
		case 'V':
			printf("ccache version %s\n", CCACHE_VERSION);
			printf("Copyright Andrew Tridgell 2002\n");
			printf("Released under the GNU GPL v2 or later\n");
			exit(0);

		case 'h':
			usage();
			exit(0);

		case 's':
			check_cache_dir();
			stats_summary();
			break;

		case 'c':
			check_cache_dir();
			cleanup_all(cache_dir);
			printf("Cleaned cache\n");
			break;

		case 'C':
			check_cache_dir();
			wipe_all(cache_dir);
			printf("Cleared cache\n");
			break;

		case 'z':
			check_cache_dir();
			stats_zero();
			printf("Statistics cleared\n");
			break;

		case 'F':
			check_cache_dir();
			v = atoi(optarg);
			if (stats_set_limits(v, -1) == 0) {
				printf("Set cache file limit to %u\n", (unsigned)v);
			} else {
				printf("Could not set cache file limit.\n");
				exit(1);
			}
			break;

		case 'M':
			check_cache_dir();
			v = value_units(optarg);
			if (stats_set_limits(-1, v) == 0) {
				printf("Set cache size limit to %uk\n", (unsigned)v);
			} else {
				printf("Could not set cache size limit.\n");
				exit(1);
			}
			break;

		default:
			usage();
			exit(1);
		}
	}

	return 0;
}


/* Make a copy of stderr that will not be cached, so things like
   distcc can send networking errors to it. */
static void setup_uncached_err(void)
{
	char *buf;
	int uncached_fd;

	uncached_fd = dup(2);
	if (uncached_fd == -1) {
		cc_log("dup(2) failed\n");
		failed();
	}

	/* leak a pointer to the environment */
	x_asprintf(&buf, "UNCACHED_ERR_FD=%d", uncached_fd);

	if (putenv(buf) == -1) {
		cc_log("putenv failed\n");
		failed();
	}
}


int main(int argc, char *argv[])
{
	char *p;

	current_working_dir = get_cwd();

	cache_dir = getenv("CCACHE_DIR");
	if (!cache_dir) {
		const char *home_directory = get_home_directory();
		if (home_directory) {
			x_asprintf(&cache_dir, "%s/.ccache", home_directory);
		}
	}

	temp_dir = getenv("CCACHE_TEMPDIR");
	if (!temp_dir) {
		x_asprintf(&temp_dir, "%s/tmp", cache_dir);
	}

	cache_logfile = getenv("CCACHE_LOGFILE");

	base_dir = getenv("CCACHE_BASEDIR");
	if (base_dir) {
		if (strcmp(base_dir, "") == 0) {
			base_dir = NULL;
		}
	} else {
		base_dir = get_cwd();
	}

	setup_uncached_err();

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


	/* check if we are being invoked as "ccache" */
	if (strlen(argv[0]) >= strlen(MYNAME) &&
	    strcmp(argv[0] + strlen(argv[0]) - strlen(MYNAME), MYNAME) == 0) {
		if (argc < 2) {
			usage();
			exit(1);
		}
		/* if the first argument isn't an option, then assume we are
		   being passed a compiler name and options */
		if (argv[1][0] == '-') {
			return ccache_main(argc, argv);
		}
	}

	/* make sure the cache dir exists */
	if (create_dir(cache_dir) != 0) {
		fprintf(stderr,"ccache: failed to create %s (%s)\n",
			cache_dir, strerror(errno));
		exit(1);
	}

	/* make sure the temp dir exists */
	if (create_dir(temp_dir) != 0) {
		fprintf(stderr,"ccache: failed to create %s (%s)\n",
			temp_dir, strerror(errno));
		exit(1);
	}

	if (!getenv("CCACHE_READONLY")) {
		if (create_cachedirtag(cache_dir) != 0) {
			fprintf(stderr,"ccache: failed to create %s/CACHEDIR.TAG (%s)\n",
				cache_dir, strerror(errno));
			exit(1);
		}
	}

	ccache(argc, argv);
	return 1;
}
