/*
  a re-implementation of the compilercache scripts in C
  Copyright tridge@samba.org 2002

  The idea is based on the shell-script compilercache by Erik Thiele <erikyyy@erikyyy.de>
*/

#include "ccache.h"

static char *cache_dir = CACHE_BASEDIR;


static ARGS *stripped_args;
static ARGS *orig_args;
static char *output_file;
static char *hashname;
static int found_debug;

static void failed(void)
{
	execv(orig_args->argv[0], orig_args->argv);
	cc_log("execv returned (%s)!\n", strerror(errno));
	exit(1);
}

/* run the real compiler and put the result in cache */
static void to_cache(ARGS *args)
{
	char *path_stdout, *path_status, *path_stderr;
	struct stat st;

	x_asprintf(&path_status, "%s.status", hashname);
	x_asprintf(&path_stderr, "%s.stderr", hashname);
	x_asprintf(&path_stdout, "%s.stdout", hashname);

	args_add(args, "-o");
	args_add(args, hashname);

	execute(args->argv, path_stdout, path_stderr, path_status);

	args->argc -= 2;

	if (stat(path_stdout, &st) != 0 || st.st_size != 0) {
		cc_log("compiler produced stdout!\n");
		unlink(path_stdout);
		unlink(path_stderr);
		unlink(path_status);
		unlink(hashname);
		failed();
	}

	unlink(path_stdout);
	cc_log("Placed %s into cache\n", output_file);
}

static void stabs_hash(const char *fname)
{
	FILE *f;
	char *s;
	static char line[102400];

	line[sizeof(line)-2] = 0;

	f = fopen(fname, "r");
	if (!f) {
		cc_log("Failed to open preprocessor output\n");
		failed();
	}
	
	while ((s = fgets(line, sizeof(line), f))) {
		if (line[sizeof(line)-2]) {
			cc_log("line too long in preprocessor output!\n");
			failed();
		}

		/* ignore debugging output */
		if (line[0] == '#' && line[1] == ' ' && isdigit(line[2])) {
			continue;
		}

		hash_string(s);
	}

	fclose(f);
}


/* find the hash for a command. The hash includes all argument lists,
   plus the output from running the compiler with -E */
static void find_hash(ARGS *args)
{
	int i;
	char *path_stdout, *path_stderr, *path_status;
	char *hash_dir;
	char *s;
	struct stat st;

	hash_start();

	/* first the arguments */
	for (i=0;i<args->argc;i++) {
		hash_string(args->argv[i]);
	}

	/* the compiler driver size and date. This is a simple minded way
	   to try and detect compiler upgrades. It is not 100% reliable */
	if (stat(args->argv[0], &st) != 0) {
		cc_log("Couldn't stat the compiler!?\n");
		failed();
	}
	hash_int(st.st_size);
	hash_int(st.st_mtime);

	/* now the run */
	x_asprintf(&path_stdout, "%s/tmp.stdout.%d", cache_dir, getpid());
	x_asprintf(&path_stderr, "%s/tmp.stderr.%d", cache_dir, getpid());
	x_asprintf(&path_status, "%s/tmp.status.%d", cache_dir, getpid());

	args_add(args, "-E");
	execute(args->argv, path_stdout, path_stderr, path_status);
	args->argc--;

	/* if the compilation is with -g then we have to inlcude the whole of the
	   preprocessor output, which means we are sensitive to line number
	   information. Otherwise we can discard line number info, which makes
	   us less sensitive to reformatting changes 
	*/
	if (found_debug) {
		hash_file(path_stdout);
	} else {
		stabs_hash(path_stdout);
	}
	hash_file(path_stderr);
	hash_file(path_status);

	unlink(path_stdout);
	unlink(path_stderr);
	unlink(path_status);
	free(path_stdout);
	free(path_stderr);
	free(path_status);

	s = hash_result();
	x_asprintf(&hash_dir, "%s/%c", cache_dir, *s);
	if (create_dir(hash_dir) != 0) {
		cc_log("failed to create %s\n", cache_dir);
		failed();
	}
	x_asprintf(&hashname, "%s/%s", hash_dir, s+1);
	free(hash_dir);
}


/* 
   try to return the compile result from cache. If we can return from
   cache then this function exits with the correct status code,
   otherwise it returns */
static void from_cache(int first)
{
	int fd_status, fd_stderr;
	char *s;
	int ret, status;

	x_asprintf(&s, "%s.status", hashname);
	fd_status = open(s, O_RDONLY);
	free(s);
	if (fd_status == -1) {
		/* its not cached */
		return;
	}
	if (read(fd_status, &status, sizeof(status)) != sizeof(status)) {
		cc_log("status file is too short\n");
		close(fd_status);
		return;
	}
	close(fd_status);

	x_asprintf(&s, "%s.stderr", hashname);
	fd_stderr = open(s, O_RDONLY);
	free(s);
	if (fd_stderr == -1) {
		cc_log("stderr file not found\n");
		return;
	}

	unlink(output_file);
	ret = link(hashname, output_file);
	if (ret == -1 && errno != ENOENT) {
		ret = symlink(hashname, output_file);
	}
	if (ret == 0) {
		utime(output_file, NULL);
	}

	/* send the stderr */
	copy_fd(fd_stderr, 2);
	close(fd_stderr);

	/* and exit with the right status code */
	if (first) {
		cc_log("got cached result for %s with status = %d\n", 
		       output_file, status);
	}

	if (status != 0) {
		/* we delete cached entries with non-zero status as we use them,
		   which basically means we do them non-cached. This is needed to cope
		   with someone interrupting a compile
		   Is there a better way?
		*/
		x_asprintf(&s, "%s.status", hashname);
		unlink(s);
	}

	exit(status);
}


static char *find_compiler(const char *argv0)
{
	char *p;
	char *base;
	char *path, *tok;
	struct stat st1, st2;

	/* we compare size, device and inode */
	if (stat("/proc/self/exe", &st1) != 0) {
		cc_log("Can't stat ccache executable!?\n");
		exit(1);
	}

	p = strrchr(argv0, '/');
	if (p) {
		base = p+1;
	} else {
		base = argv0;
	}

	path = getenv("CCACHE_PATH");
	if (!path) {
		path = getenv("PATH");
	}
	if (!path) {
		cc_log("no PATH variable!?\n");
		failed();
	}

	path = x_strdup(path);
	
	/* search the path looking for the first compiler of the same name
	   that isn't us */
	for (tok=strtok(path,":"); tok; tok = strtok(NULL, ":")) {
		char *fname;
		x_asprintf(&fname, "%s/%s", tok, base);
		if (stat(fname, &st2) == 0) {
			if (st1.st_size != st2.st_size ||
			    st1.st_dev != st2.st_dev ||
			    st1.st_ino != st2.st_ino) {
				/* found it! */
				free(path);
				return fname;
			}
		}
	}
	
	return NULL;
}


static void process_args(int argc, char **argv)
{
	int i;
	int found_c_opt = 0;
	char *input_file = NULL;

	stripped_args = args_init();

	args_add(stripped_args, argv[0]);

	for (i=1; i<argc; i++) {
		if (strcmp(argv[i], "-E") == 0) {
			cc_log("Tried to do -E\n");
			failed();
		}

		if (strcmp(argv[i], "-c") == 0) {
			args_add(stripped_args, argv[i]);
			found_c_opt = 1;
			continue;
		}

		if (strcmp(argv[i], "-o") == 0) {
			if (i == argc-1) {
				cc_log("missing argument to %s\n", argv[i]);
				failed();
			}
			output_file = argv[i+1];
			i++;
			continue;
		}

		if (strncmp(argv[i], "-g", 2) == 0) {
			args_add(stripped_args, argv[i]);
			if (strcmp(argv[i], "-g0") != 0) {
				found_debug = 1;
			}
			continue;
		}

		/* options that take an argument */
		if (strcmp(argv[i], "-I") == 0 ||
		    strcmp(argv[i], "-include") == 0 ||
		    strcmp(argv[i], "-L") == 0 ||
		    strcmp(argv[i], "-D") == 0 ||
		    strcmp(argv[i], "-isystem") == 0) {
			if (i == argc-1) {
				cc_log("missing argument to %s\n", argv[i]);
				failed();
			}
						
			args_add(stripped_args, argv[i]);
			args_add(stripped_args, argv[i+1]);
			i++;
			continue;
		}
		    
		if (argv[i][0] == '-') {
			args_add(stripped_args, argv[i]);
			continue;
		}

		if (input_file) {
			cc_log("multiple input files (%s and %s)\n",
			       input_file, argv[i]);
			failed();
		}

		input_file = argv[i];
		args_add(stripped_args, argv[i]);
	}

	if (!input_file) {
		cc_log("No input file found\n");
		failed();
	}

	if (!found_c_opt) {
		cc_log("No -c option found for %s\n", input_file);
		failed();
	}

	if (!output_file) {
		char *p;
		output_file = strdup(input_file);
		if ((p = strrchr(output_file, '/'))) {
			output_file = p+1;
		}
		p = strrchr(output_file, '.');
		if (!p || !p[1]) {
			cc_log("badly formed output_file %s\n", output_file);
			failed();
		}
		p[1] = 'o';
		p[2] = 0;
#if 0
		cc_log("Formed output file %s from input_file %s\n", 
		       output_file, input_file);
#endif
	}
}

static void ccache(int argc, char *argv[])
{
	/* find the real compiler */
	argv[0] = find_compiler(argv[0]);
	if (!argv[0]) {
		exit(STATUS_NOTFOUND);
	}

	orig_args = args_init();

	orig_args->argv = argv;
	orig_args->argc = argc;

	/* process argument list, returning a new set of arguments for pre-processing */
	process_args(argc, argv);

	/* run with -E to find the hash */
	find_hash(stripped_args);

	/* if we can return from cache at this point then do */
	from_cache(1);
	
	/* run real compiler, semding output to cache */
	to_cache(stripped_args);

	/* return from cache */
	from_cache(0);

	/* oh oh! */
	cc_log("secondary from_cache failed!\n");
	failed();
}


int main(int argc, char *argv[])
{
	if (create_dir(cache_dir) != 0) {
		fprintf(stderr,"ccache: failed to create %s (%s)\n", 
			cache_dir, strerror(errno));
		exit(1);
	}
	ccache(argc, argv);
	return 1;
}
