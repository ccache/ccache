/*
 * Copyright (C) 2010 Joel Rosdahl
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

/*
 * This function acquires a lockfile for the given path. Returns true if the
 * lock was acquired, otherwise false. If the lock has been considered stale
 * for the number of microseconds specified by staleness_limit, the function
 * will (if possible) break the lock and then try to acquire it again. The
 * staleness limit should be reasonably larger than the longest time the lock
 * can be expected to be held, and the updates of the locked path should
 * probably be made with an atomic rename(2) to avoid corruption in the rare
 * case that the lock is broken by another process.
 */
bool
lockfile_acquire(const char *path, unsigned staleness_limit)
{
	char *lockfile = format("%s.lock", path);
	char *my_content = NULL, *content = NULL, *initial_content = NULL;
	const char *hostname = get_hostname();
	bool acquired = false;
#ifdef _WIN32
	const size_t bufsize = 1024;
	int fd, len;
#else
	int ret;
#endif
	unsigned to_sleep = 1000, slept = 0; /* Microseconds. */

	while (1) {
		free(my_content);
		my_content = format("%s:%d:%d", hostname, (int)getpid(), (int)time(NULL));

#ifdef _WIN32
		fd = open(lockfile, O_WRONLY|O_CREAT|O_EXCL|O_BINARY, 0666);
		if (fd == -1) {
			cc_log("lockfile_acquire: open WRONLY %s: %s", lockfile, strerror(errno));
			if (errno == ENOENT) {
				/* Directory doesn't exist? */
				if (create_parent_dirs(lockfile) == 0) {
					/* OK. Retry. */
					continue;
				}
			}
			if (errno != EEXIST) {
				/* Directory doesn't exist or isn't writable? */
				goto out;
			}
			/* Someone else has the lock. */
			fd = open(lockfile, O_RDONLY|O_BINARY);
			if (fd == -1) {
				if (errno == ENOENT) {
					/*
					 * The file was removed after the open() call above, so retry
					 * acquiring it.
					 */
					continue;
				} else {
					cc_log("lockfile_acquire: open RDONLY %s: %s",
					       lockfile, strerror(errno));
					goto out;
				}
			}
			free(content);
			content = x_malloc(bufsize);
			if ((len = read(fd, content, bufsize - 1)) == -1) {
				cc_log("lockfile_acquire: read %s: %s", lockfile, strerror(errno));
				close(fd);
				goto out;
			}
			close(fd);
			content[len] = '\0';
		} else {
			/* We got the lock. */
			if (write(fd, my_content, strlen(my_content)) == -1) {
				cc_log("lockfile_acquire: write %s: %s", lockfile, strerror(errno));
				close(fd);
				x_unlink(lockfile);
				goto out;
			}
			close(fd);
			acquired = true;
			goto out;
		}
#else
		ret = symlink(my_content, lockfile);
		if (ret == 0) {
			/* We got the lock. */
			acquired = true;
			goto out;
		}
		cc_log("lockfile_acquire: symlink %s: %s", lockfile, strerror(errno));
		if (errno == ENOENT) {
			/* Directory doesn't exist? */
			if (create_parent_dirs(lockfile) == 0) {
				/* OK. Retry. */
				continue;
			}
		}
		if (errno == EPERM) {
			/*
			 * The file system does not support symbolic links. We have no choice but
			 * to grant the lock anyway.
			 */
			acquired = true;
			goto out;
		}
		if (errno != EEXIST) {
			/* Directory doesn't exist or isn't writable? */
			goto out;
		}
		free(content);
		content = x_readlink(lockfile);
		if (!content) {
			if (errno == ENOENT) {
				/*
				 * The symlink was removed after the symlink() call above, so retry
				 * acquiring it.
				 */
				continue;
			} else {
				cc_log("lockfile_acquire: readlink %s: %s", lockfile, strerror(errno));
				goto out;
			}
		}
#endif

		if (str_eq(content, my_content)) {
			/* Lost NFS reply? */
			cc_log("lockfile_acquire: symlink %s failed but we got the lock anyway",
			       lockfile);
			acquired = true;
			goto out;
		}
		/*
		 * A possible improvement here would be to check if the process holding the
		 * lock is still alive and break the lock early if it isn't.
		 */
		cc_log("lockfile_acquire: lock info for %s: %s", lockfile, content);
		if (!initial_content) {
			initial_content = x_strdup(content);
		}
		if (slept > staleness_limit) {
			if (str_eq(content, initial_content)) {
				/* The lock seems to be stale -- break it. */
				cc_log("lockfile_acquire: breaking %s", lockfile);
				if (lockfile_acquire(lockfile, staleness_limit)) {
					lockfile_release(path);
					lockfile_release(lockfile);
					to_sleep = 1000;
					slept = 0;
					continue;
				}
			}
			cc_log("lockfile_acquire: gave up acquiring %s", lockfile);
			goto out;
		}
		cc_log("lockfile_acquire: failed to acquire %s; sleeping %u microseconds",
		       lockfile, to_sleep);
		usleep(to_sleep);
		slept += to_sleep;
		to_sleep *= 2;
	}

out:
	if (acquired) {
		cc_log("Acquired lock %s", lockfile);
	} else {
		cc_log("Failed to acquire lock %s", lockfile);
	}
	free(lockfile);
	free(my_content);
	free(initial_content);
	free(content);
	return acquired;
}

/*
 * Release the lockfile for the given path. Assumes that we are the legitimate
 * owner.
 */
void
lockfile_release(const char *path)
{
	char *lockfile = format("%s.lock", path);
	cc_log("Releasing lock %s", lockfile);
	tmp_unlink(lockfile);
	free(lockfile);
}

#ifdef TEST_LOCKFILE
int
main(int argc, char **argv)
{
	extern char *cache_logfile;
	cache_logfile = "/dev/stdout";
	if (argc == 4) {
		unsigned staleness_limit = atoi(argv[1]);
		if (str_eq(argv[2], "acquire")) {
			return lockfile_acquire(argv[3], staleness_limit) == 0;
		} else if (str_eq(argv[2], "release")) {
			lockfile_release(argv[3]);
			return 0;
		}
	}
	fprintf(stderr,
	        "Usage: testlockfile <staleness_limit> <acquire|release> <path>\n");
	return 1;
}
#endif
