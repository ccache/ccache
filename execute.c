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

#ifdef _WIN32
/*
 * Re-create a win32 command line string based on **argv.
 * http://msdn.microsoft.com/en-us/library/17w5ykft.aspx
 */
static char *
argvtos(char *prefix, char **argv)
{
	char *arg;
	char *ptr;
	char *str;
	int l = 0;
	int i, j;

	i = 0;
	arg = prefix ? prefix : argv[i++];
	do {
		int bs = 0;
		for (j = 0; arg[j]; j++) {
			switch (arg[j]) {
			case '\\':
				bs++;
				break;
			case '"':
				bs = (bs << 1) + 1;
			default:
				l += bs + 1;
				bs = 0;
			}
		}
		l += (bs << 1) + 3;
	} while ((arg = argv[i++]));

	str = ptr = malloc(l + 1);
	if (str == NULL)
		return NULL;

	i = 0;
	arg = prefix ? prefix : argv[i++];
	do {
		int bs = 0;
		*ptr++ = '"';
		for (j = 0; arg[j]; j++) {
			switch (arg[j]) {
			case '\\':
				bs++;
				break;
			case '"':
				bs = (bs << 1) + 1;
			default:
				while (bs && bs--)
					*ptr++ = '\\';
				*ptr++ = arg[j];
			}
		}
		bs <<= 1;
		while (bs && bs--)
			*ptr++ = '\\';
		*ptr++ = '"';
		*ptr++ = ' ';
	} while ((arg = argv[i++]));
	ptr[-1] = '\0';

	return str;
}

int
win32execute(char *path, char **argv, int doreturn,
             const char *path_stdout, const char *path_stderr)
{
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	BOOL ret;
	DWORD exitcode;
	char *path_env;
	char *sh = NULL;
	char *args;
	const char *ext;

	memset(&pi, 0x00, sizeof(pi));
	memset(&si, 0x00, sizeof(si));

	ext = get_extension(path);
	if (ext && strcasecmp(ext, ".sh") == 0 && (path_env = getenv("PATH")))
		sh = find_executable_in_path("sh.exe", NULL, path_env);
	if (!sh && getenv("CCACHE_DETECT_SHEBANG")) {
		/* Detect shebang. */
		FILE *fp;
		fp = fopen(path, "r");
		if (fp) {
			char buf[10];
			fgets(buf, sizeof(buf), fp);
			buf[9] = 0;
			if (str_eq(buf, "#!/bin/sh") && (path_env = getenv("PATH")))
				sh = find_executable_in_path("sh.exe", NULL, path_env);
			fclose(fp);
		}
	}
	if (sh)
		path = sh;

	si.cb = sizeof(STARTUPINFO);
	if (path_stdout) {
		SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
		si.hStdOutput = CreateFile(path_stdout, GENERIC_WRITE, 0, &sa,
		                           CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY |
		                           FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		si.hStdError  = CreateFile(path_stderr, GENERIC_WRITE, 0, &sa,
		                           CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY |
		                           FILE_FLAG_SEQUENTIAL_SCAN, NULL);
		si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
		si.dwFlags    = STARTF_USESTDHANDLES;
		if (si.hStdOutput == INVALID_HANDLE_VALUE ||
		    si.hStdError  == INVALID_HANDLE_VALUE)
			return -1;
	}
	args = argvtos(sh, argv);
	ret = CreateProcess(path, args, NULL, NULL, 1, 0, NULL, NULL, &si, &pi);
	free(args);
	if (path_stdout) {
		CloseHandle(si.hStdOutput);
		CloseHandle(si.hStdError);
	}
	if (ret == 0)
		return -1;
	WaitForSingleObject(pi.hProcess, INFINITE);
	GetExitCodeProcess(pi.hProcess, &exitcode);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	if (!doreturn)
		exit(exitcode);
	return exitcode;
}

#else

/*
  execute a compiler backend, capturing all output to the given paths
  the full path to the compiler to run is in argv[0]
*/
int
execute(char **argv, const char *path_stdout, const char *path_stderr)
{
	pid_t pid;
	int status;

	cc_log_argv("Executing ", argv);

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
#endif


/*
 * Find an executable by name in $PATH. Exclude any that are links to
 * exclude_name.
*/
char *
find_executable(const char *name, const char *exclude_name)
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
	for (tok = strtok(path, PATH_DELIM); tok; tok = strtok(NULL, PATH_DELIM)) {
#ifdef _WIN32
		char namebuf[MAX_PATH];
		int ret = SearchPath(tok, name, ".exe",
		                     sizeof(namebuf), namebuf, NULL);
		if (!ret)
			ret = SearchPath(tok, name, NULL,
			                 sizeof(namebuf), namebuf, NULL);
		(void) exclude_name;
		if (ret) {
			free(path);
			return x_strdup(namebuf);
		}
#else
		struct stat st1, st2;
		char *fname = format("%s/%s", tok, name);
		/* look for a normal executable file */
		if (access(fname, X_OK) == 0 &&
		    lstat(fname, &st1) == 0 &&
		    stat(fname, &st2) == 0 &&
		    S_ISREG(st2.st_mode)) {
			if (S_ISLNK(st1.st_mode)) {
				char *buf = x_realpath(fname);
				if (buf) {
					char *p = basename(buf);
					if (str_eq(p, exclude_name)) {
						/* It's a link to "ccache"! */
						free(p);
						free(buf);
						continue;
					}
					free(buf);
					free(p);
				}
			}

			/* Found it! */
			free(path);
			return fname;
		}
		free(fname);
#endif
	}

	free(path);
	return NULL;
}

void
print_command(FILE *fp, char **argv)
{
	int i;
	for (i = 0; argv[i]; i++) {
		fprintf(fp, "%s%s",  (i == 0) ? "" : " ", argv[i]);
	}
	fprintf(fp, "\n");
}

void
print_executed_command(FILE *fp, char **argv)
{
	fprintf(fp, "%s: executing ", MYNAME);
	print_command(fp, argv);
}
