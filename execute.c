/*
 * Copyright (C) Andrew Tridgell 2002
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

#include <sys/types.h>
#include <sys/stat.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static char *
find_executable_in_path(const char *name, const char *exclude_name, char *path);

/*
  execute a compiler backend, capturing all output to the given paths
  the full path to the compiler to run is in argv[0]
*/
int execute(char **argv,
	    const char *path_stdout,
	    const char *path_stderr)
{
	pid_t pid;
	int status;

	cc_log_executed_command(argv);

	pid = fork();
	if (pid == -1) fatal("Failed to fork");

	if (pid == 0) {
		int fd;

		unlink(path_stdout);
		fd = open(path_stdout, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL|O_BINARY, 0666);
		if (fd == -1) {
			exit(1);
		}
		dup2(fd, 1);
		close(fd);

		unlink(path_stderr);
		fd = open(path_stderr, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL|O_BINARY, 0666);
		if (fd == -1) {
			exit(1);
		}
		dup2(fd, 2);
		close(fd);

		exit(execv(argv[0], argv));
	}

	if (waitpid(pid, &status, 0) != pid) {
		fatal("waitpid failed");
	}

	if (WEXITSTATUS(status) == 0 && WIFSIGNALED(status)) {
		return -1;
	}

	return WEXITSTATUS(status);
}


/*
  find an executable by name in $PATH. Exclude any that are links to exclude_name
*/
char *find_executable(const char *name, const char *exclude_name)
{
	char *path;

	if (is_absolute_path(name)) {
		return x_strdup(name);
	}

	path = getenv("CCACHE_PATH");
	if (!path) {
		path = getenv("PATH");
	}
	if (!path) {
		cc_log("No PATH variable");
		return NULL;
	}

	return find_executable_in_path(name, exclude_name, path);
}

static char *
find_executable_in_path(const char *name, const char *exclude_name, char *path)
{
	char *tok;

	path = x_strdup(path);

	/* search the path looking for the first compiler of the right name
	   that isn't us */
	for (tok=strtok(path, PATH_DELIM); tok; tok = strtok(NULL, PATH_DELIM)) {
		struct stat st1, st2;
		char *fname;
		x_asprintf(&fname, "%s/%s", tok, name);
		/* look for a normal executable file */
		if (access(fname, X_OK) == 0 &&
		    lstat(fname, &st1) == 0 &&
		    stat(fname, &st2) == 0 &&
		    S_ISREG(st2.st_mode)) {
			/* if its a symlink then ensure it doesn't
                           point at something called exclude_name */
			if (S_ISLNK(st1.st_mode)) {
				char *buf = x_realpath(fname);
				if (buf) {
					char *p = basename(buf);
					if (strcmp(p, exclude_name) == 0) {
						/* its a link to "ccache" ! */
						free(p);
						free(buf);
						continue;
					}
					free(buf);
					free(p);
				}
			}

			/* found it! */
			free(path);
			return fname;
		}
		free(fname);
	}

	free(path);
	return NULL;
}

void print_command(FILE *fp, char **argv)
{
	int i;
	for (i = 0; argv[i]; i++) {
		fprintf(fp, "%s%s",  (i == 0) ? "" : " ", argv[i]);
	}
	fprintf(fp, "\n");
}

void print_executed_command(FILE *fp, char **argv)
{
	fprintf(fp, "%s: executing ", MYNAME);
	print_command(fp, argv);
}
