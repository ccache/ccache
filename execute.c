/*
 * Copyright (C) Andrew Tridgell 2002
 * Copyright (C) Joel Rosdahl 2011
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

extern struct conf *conf;

static char *
find_executable_in_path(const char *name, const char *exclude_name, char *path);

#ifdef _WIN32
/*
 * Re-create a win32 command line string based on **argv.
 * http://msdn.microsoft.com/en-us/library/17w5ykft.aspx
 */
char *
win32argvtos(char *prefix, char **argv)
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
	if (!str)
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

char *
win32getshell(char *path)
{
	char *path_env;
	char *sh = NULL;
	const char *ext;

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

	return sh;
}

void add_exe_ext_if_no_to_fullpath(char *full_path_win_ext, size_t max_size,
                                   const char *ext, const char *path) {
	if (!ext || (!str_eq(".exe", ext)
	             && !str_eq(".bat", ext)
	             && !str_eq(".EXE", ext)
	             && !str_eq(".BAT", ext))) {
		snprintf(full_path_win_ext, max_size, "%s.exe", path);
	} else {
		snprintf(full_path_win_ext, max_size, "%s", path);
	}
}

int
win32execute(char *path, char **argv, int doreturn,
             int fd_stdout, int fd_stderr)
{
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	BOOL ret;
	DWORD exitcode;
	char *sh = NULL;
	char *args;

	memset(&pi, 0x00, sizeof(pi));
	memset(&si, 0x00, sizeof(si));

	sh = win32getshell(path);
	if (sh)
		path = sh;

	si.cb = sizeof(STARTUPINFO);
	if (fd_stdout != -1) {
		si.hStdOutput = (HANDLE)_get_osfhandle(fd_stdout);
		si.hStdError = (HANDLE)_get_osfhandle(fd_stderr);
		si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		si.dwFlags = STARTF_USESTDHANDLES;
		if (si.hStdOutput == INVALID_HANDLE_VALUE
		    || si.hStdError == INVALID_HANDLE_VALUE) {
			return -1;
		}
	} else {
		/* redirect subprocess stdout, stderr into current process */
		si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
		si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
		si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		si.dwFlags = STARTF_USESTDHANDLES;
		if (si.hStdOutput == INVALID_HANDLE_VALUE
		    || si.hStdError == INVALID_HANDLE_VALUE) {
			return -1;
		}
	}
	args = win32argvtos(sh, argv);

	const char *ext = strrchr(path, '.');
	char full_path_win_ext[MAX_PATH] = {0};
	add_exe_ext_if_no_to_fullpath(full_path_win_ext, MAX_PATH, ext, path);
	ret = CreateProcess(full_path_win_ext, args, NULL, NULL, 1, 0, NULL, NULL,
	                    &si, &pi);
	if (fd_stdout != -1) {
		close(fd_stdout);
		close(fd_stderr);
	}
	free(args);
	if (ret == 0) {
		LPVOID lpMsgBuf;
		LPVOID lpDisplayBuf;
		DWORD dw = GetLastError();

		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, dw, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lpMsgBuf,
			0, NULL);

		lpDisplayBuf =
			(LPVOID) LocalAlloc(LMEM_ZEROINIT,
			                    (lstrlen((LPCTSTR) lpMsgBuf)
			                     + lstrlen((LPCTSTR) __FILE__) + 200)
			                    * sizeof(TCHAR));
		_snprintf((LPTSTR) lpDisplayBuf,
		          LocalSize(lpDisplayBuf) / sizeof(TCHAR),
		          TEXT("%s failed with error %d: %s"), __FILE__, dw, lpMsgBuf);

		cc_log("can't execute %s; OS returned error: %s",
		       full_path_win_ext, (char*)lpDisplayBuf);

		LocalFree(lpMsgBuf);
		LocalFree(lpDisplayBuf);

		return -1;
	}
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
execute(char **argv, int fd_out, int fd_err)
{
	pid_t pid;
	int status;

	cc_log_argv("Executing ", argv);
	pid = fork();
	if (pid == -1) {
		fatal("Failed to fork: %s", strerror(errno));
	}

	if (pid == 0) {
		/* Child. */
		dup2(fd_out, 1);
		close(fd_out);
		dup2(fd_err, 2);
		close(fd_err);
		exit(execv(argv[0], argv));
	}

	close(fd_out);
	close(fd_err);

	if (waitpid(pid, &status, 0) != pid) {
		fatal("waitpid failed: %s", strerror(errno));
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

	path = conf->path;
	if (str_eq(path, "")) {
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
	char *tok, *saveptr = NULL;

	path = x_strdup(path);

	/* search the path looking for the first compiler of the right name
	   that isn't us */
	for (tok = strtok_r(path, PATH_DELIM, &saveptr);
	     tok;
	     tok = strtok_r(NULL, PATH_DELIM, &saveptr)) {
#ifdef _WIN32
		char namebuf[MAX_PATH];
		int ret = SearchPath(tok, name, NULL,
		                     sizeof(namebuf), namebuf, NULL);
		if (!ret) {
			char *exename = format("%s.exe", name);
			ret = SearchPath(tok, exename, NULL,
			                 sizeof(namebuf), namebuf, NULL);
			free(exename);
		}
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
