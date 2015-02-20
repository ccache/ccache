/*
 * Copyright (C) 2009-2015 Joel Rosdahl
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
#include "macroskip.h"
#include "murmurhashneutral2.h"

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
 * Search for the strings "__DATE__" and "__TIME__" in str.
 *
 * Returns a bitmask with HASH_SOURCE_CODE_FOUND_DATE and
 * HASH_SOURCE_CODE_FOUND_TIME set appropriately.
 */
int
check_for_temporal_macros(const char *str, size_t len)
{
	int result = 0;

	/*
	 * We're using the Boyer-Moore-Horspool algorithm, which searches starting
	 * from the *end* of the needle. Our needles are 8 characters long, so i
	 * starts at 7.
	 */
	size_t i = 7;

	while (i < len) {
		/*
		 * Check whether the substring ending at str[i] has the form "__...E__". On
		 * the assumption that 'E' is less common in source than '_', we check
		 * str[i-2] first.
		 */
		if (str[i - 2] == 'E' &&
		    str[i - 0] == '_' &&
		    str[i - 7] == '_' &&
		    str[i - 1] == '_' &&
		    str[i - 6] == '_') {
			/*
			 * Check the remaining characters to see if the substring is "__DATE__"
			 * or "__TIME__".
			 */
			if (str[i - 5] == 'D' && str[i - 4] == 'A' &&
			    str[i - 3] == 'T') {
				result |= HASH_SOURCE_CODE_FOUND_DATE;
			}
			else if (str[i - 5] == 'T' && str[i - 4] == 'I' &&
			         str[i - 3] == 'M') {
				result |= HASH_SOURCE_CODE_FOUND_TIME;
			}
		}

		/*
		 * macro_skip tells us how far we can skip forward upon seeing str[i] at
		 * the end of a substring.
		 */
		i += macro_skip[(uint8_t)str[i]];
	}

	return result;
}

/*
 * Hash a string ignoring comments. Returns a bitmask of HASH_SOURCE_CODE_*
 * results.
 */
int
hash_source_code_string(
	struct mdfour *hash, const char *str, size_t len, const char *path)
{
	int result = HASH_SOURCE_CODE_OK;
	extern unsigned sloppiness;

	/*
	 * Check for __DATE__ and __TIME__ if the sloppiness configuration tells us
	 * we should.
	 */
	if (!(sloppiness & SLOPPY_TIME_MACROS)) {
		result |= check_for_temporal_macros(str, len);
	}

	/*
	 * Hash the source string.
	 */
	hash_buffer(hash, str, len);

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

bool
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
		return false; /* Never reached. */
	} else {
		/* Parent. */
		int status;
		bool ok;
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
			return false;
		}
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			cc_log("Compiler check command returned %d", WEXITSTATUS(status));
			stats_update(STATS_COMPCHECK);
			return false;
		}
		return ok;
	}
}

bool
hash_multicommand_output(struct mdfour *hash, const char *commands,
                         const char *compiler)
{
	char *command_string, *command, *p, *saveptr = NULL;
	bool ok = true;

	command_string = x_strdup(commands);
	p = command_string;
	while ((command = strtok_r(p, ";", &saveptr))) {
		if (!hash_command_output(hash, command, compiler)) {
			ok = false;
		}
		p = NULL;
	}
	free(command_string);
	return ok;
}
