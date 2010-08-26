/*
 * Copyright (C) 2009-2010 Joel Rosdahl
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
#include "hashutil.h"
#include "murmurhashneutral2.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

unsigned
hash_from_string(void *str)
{
	return murmurhashneutral2(str, strlen((const char *)str), 0);
}

unsigned
hash_from_int(int i)
{
	return murmurhashneutral2(&i, sizeof(int), 0);
}

int
strings_equal(void *str1, void *str2)
{
	return str_eq((const char *)str1, (const char *)str2);
}

int
file_hashes_equal(struct file_hash *fh1, struct file_hash *fh2)
{
	return memcmp(fh1->hash, fh2->hash, 16) == 0
		&& fh1->size == fh2->size;
}

#define HASH(ch) \
	do {\
		hashbuf[hashbuflen] = ch; \
		hashbuflen++; \
		if (hashbuflen == sizeof(hashbuf)) {\
			hash_buffer(hash, hashbuf, sizeof(hashbuf)); \
			hashbuflen = 0; \
		} \
	} while (0)

/*
 * Hash a string ignoring comments. Returns a bitmask of HASH_SOURCE_CODE_*
 * results.
 */
int
hash_source_code_string(
	struct mdfour *hash, const char *str, size_t len, const char *path)
{
	const char *p;
	const char *end;
	char hashbuf[64];
	size_t hashbuflen = 0;
	int result = HASH_SOURCE_CODE_OK;
	extern unsigned sloppiness;

	p = str;
	end = str + len;
	while (1) {
		if (p >= end) {
			goto end;
		}
		switch (*p) {
		/* Potential start of comment. */
		case '/':
			if (p+1 == end) {
				break;
			}
			switch (*(p+1)) {
			case '*':
				HASH(' '); /* Don't paste tokens together when removing the comment. */
				p += 2;
				while (p+1 < end
				       && (*p != '*' || *(p+1) != '/')) {
					if (*p == '\n') {
						/* Keep line numbers. */
						HASH('\n');
					}
					p++;
				}
				if (p+1 == end) {
					goto end;
				}
				p += 2;
				continue;

			case '/':
				p += 2;
				while (p < end
				       && (*p != '\n' || *(p-1) == '\\')) {
					p++;
				}
				continue;

			default:
				break;
			}
			break;

		/* Start of string. */
		case '"':
			HASH(*p);
			p++;
			while (p < end && (*p != '"' || *(p-1) == '\\')) {
				HASH(*p);
				p++;
			}
			if (p == end) {
				goto end;
			}
			break;

		/* Potential start of volatile macro. */
		case '_':
			if (p + 7 < end
			    && p[1] == '_' && p[5] == 'E'
			    && p[6] == '_' && p[7] == '_') {
				if (p[2] == 'D' && p[3] == 'A'
				    && p[4] == 'T') {
					result |= HASH_SOURCE_CODE_FOUND_DATE;
				} else if (p[2] == 'T' && p[3] == 'I'
				           && p[4] == 'M') {
					result |= HASH_SOURCE_CODE_FOUND_TIME;
				}
				/*
				 * Of course, we can't be sure that we have found a __{DATE,TIME}__
				 * that's actually used, but better safe than sorry. And if you do
				 * something like
				 *
				 * #define TIME __TI ## ME__
				 *
				 * in your code, you deserve to get a false cache hit.
				 */
			}
			break;

		default:
			break;
		}

		HASH(*p);
		p++;
	}

end:
	hash_buffer(hash, hashbuf, hashbuflen);

	if (sloppiness & SLOPPY_TIME_MACROS) {
		return 0;
	}
	if (result & HASH_SOURCE_CODE_FOUND_DATE) {
		/*
		 * Make sure that the hash sum changes if the (potential) expansion of
		 * __DATE__ changes.
		 */
		time_t t = time(NULL);
		struct tm *now = localtime(&t);
		cc_log("Found __DATE__ in %s", path);
		hash_delimiter(hash, "date");
		hash_buffer(hash, &now->tm_year, sizeof(now->tm_year));
		hash_buffer(hash, &now->tm_mon, sizeof(now->tm_mon));
		hash_buffer(hash, &now->tm_mday, sizeof(now->tm_mday));
	}
	if (result & HASH_SOURCE_CODE_FOUND_TIME) {
		/*
		 * We don't know for sure that the program actually uses the __TIME__
		 * macro, but we have to assume it anyway and hash the time stamp. However,
		 * that's not very useful since the chance that we get a cache hit later
		 * the same second should be quite slim... So, just signal back to the
		 * caller that __TIME__ has been found so that the direct mode can be
		 * disabled.
		 */
		cc_log("Found __TIME__ in %s", path);
	}

	return result;
}

/*
 * Hash a file ignoring comments. Returns a bitmask of HASH_SOURCE_CODE_*
 * results.
 */
int
hash_source_code_file(struct mdfour *hash, const char *path)
{
	char *data;
	size_t size;
	int result;

	if (is_precompiled_header(path)) {
		if (hash_file(hash, path)) {
			return HASH_SOURCE_CODE_OK;
		} else {
			return HASH_SOURCE_CODE_ERROR;
		}
	} else {
		if (!read_file(path, 0, &data, &size)) {
			return HASH_SOURCE_CODE_ERROR;
		}
		result = hash_source_code_string(hash, data, size, path);
		free(data);
		return result;
	}
}

int
hash_command_output(struct mdfour *hash, const char *command,
                    const char *compiler)
{
	pid_t pid;
	int pipefd[2];

	struct args *args = args_init_from_string(command);
	int i;
	for (i = 0; i < args->argc; i++) {
		if (str_eq(args->argv[i], "%compiler%")) {
			args_set(args, i, compiler);
		}
	}
	cc_log_argv("Executing compiler check command ", args->argv);

	if (pipe(pipefd) == -1) {
		fatal("pipe failed");
	}
	pid = fork();
	if (pid == -1) {
		fatal("fork failed");
	}

	if (pid == 0) {
		/* Child. */
		close(pipefd[0]);
		close(0);
		dup2(pipefd[1], 1);
		dup2(pipefd[1], 2);
		_exit(execvp(args->argv[0], args->argv));
		return 0; /* Never reached. */
	} else {
		/* Parent. */
		int status, ok;
		args_free(args);
		close(pipefd[1]);
		ok = hash_fd(hash, pipefd[0]);
		if (!ok) {
			cc_log("Error hashing compiler check command output: %s", strerror(errno));
			stats_update(STATS_COMPCHECK);
		}
		close(pipefd[0]);
		if (waitpid(pid, &status, 0) != pid) {
			cc_log("waitpid failed");
			return 0;
		}
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			cc_log("Compiler check command returned %d", WEXITSTATUS(status));
			stats_update(STATS_COMPCHECK);
			return 0;
		}
		return ok;
	}
}

int
hash_multicommand_output(struct mdfour *hash, const char *commands,
                         const char *compiler)
{
	char *command_string, *command, *p, *saveptr = NULL;
	int ok = 1;

	command_string = x_strdup(commands);
	p = command_string;
	while ((command = strtok_r(p, ";", &saveptr))) {
		if (!hash_command_output(hash, command, compiler)) {
			ok = 0;
		}
		p = NULL;
	}
	free(command_string);
	return ok;
}
