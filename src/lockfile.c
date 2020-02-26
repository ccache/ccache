// Copyright (C) 2010-2020 Joel Rosdahl
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

#ifndef _WIN32

// This function acquires a lockfile for the given path. Returns true if the
// lock was acquired, otherwise false. If the lock has been considered stale
// for the number of microseconds specified by staleness_limit, the function
// will (if possible) break the lock and then try to acquire it again. The
// staleness limit should be reasonably larger than the longest time the lock
// can be expected to be held, and the updates of the locked path should
// probably be made with an atomic rename(2) to avoid corruption in the rare
// case that the lock is broken by another process.
bool
lockfile_acquire(const char *path, unsigned staleness_limit)
{
	char *lockfile = format("%s.lock", path);
	char *my_content = NULL;
	char *content = NULL;
	char *initial_content = NULL;
	const char *hostname = get_hostname();
	bool acquired = false;
	unsigned to_sleep = 1000; // Microseconds.
	unsigned max_to_sleep = 10000; // Microseconds.
	unsigned slept = 0; // Microseconds.

	while (true) {
		free(my_content);
		my_content = format("%s:%d:%d", hostname, (int)getpid(), (int)time(NULL));

		if (symlink(my_content, lockfile) == 0) {
			// We got the lock.
			acquired = true;
			goto out;
		}
		int saved_errno = errno;
		cc_log("lockfile_acquire: symlink %s: %s", lockfile, strerror(saved_errno));
		if (saved_errno == ENOENT) {
			// Directory doesn't exist?
			if (create_parent_dirs(lockfile) == 0) {
				// OK. Retry.
				continue;
			}
		}
		if (saved_errno == EPERM) {
			// The file system does not support symbolic links. We have no choice but
			// to grant the lock anyway.
			acquired = true;
			goto out;
		}
		if (saved_errno != EEXIST) {
			// Directory doesn't exist or isn't writable?
			goto out;
		}
		free(content);
		content = x_readlink(lockfile);
		if (!content) {
			if (errno == ENOENT) {
				// The symlink was removed after the symlink() call above, so retry
				// acquiring it.
				continue;
			} else {
				cc_log("lockfile_acquire: readlink %s: %s", lockfile, strerror(errno));
				goto out;
			}
		}

		if (str_eq(content, my_content)) {
			// Lost NFS reply?
			cc_log("lockfile_acquire: symlink %s failed but we got the lock anyway",
			       lockfile);
			acquired = true;
			goto out;
		}
		// A possible improvement here would be to check if the process holding the
		// lock is still alive and break the lock early if it isn't.
		cc_log("lockfile_acquire: lock info for %s: %s", lockfile, content);
		if (!initial_content) {
			initial_content = x_strdup(content);
		}
		if (slept > staleness_limit) {
			if (str_eq(content, initial_content)) {
				// The lock seems to be stale -- break it.
				cc_log("lockfile_acquire: breaking %s", lockfile);
				// Try to acquire path.lock.lock:
				if (lockfile_acquire(lockfile, staleness_limit)) {
					lockfile_release(path); // Remove path.lock
					lockfile_release(lockfile); // Remove path.lock.lock
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
		to_sleep = MIN(max_to_sleep, 2 * to_sleep);
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

// Release the lockfile for the given path. Assumes that we are the legitimate
// owner.
void
lockfile_release(const char *path)
{
	char *lockfile = format("%s.lock", path);
	cc_log("Releasing lock %s", lockfile);
	tmp_unlink(lockfile);
	free(lockfile);
}

#else

HANDLE lockfile_handle = NULL;

// This function acquires a lockfile for the given path. Returns true if the
// lock was acquired, otherwise false. If the lock has been acquired within the
// limit (in microseconds) the function will give up and return false. The time
// limit should be reasonably larger than the longest time the lock can be
// expected to be held.
bool
lockfile_acquire(const char *path, unsigned time_limit)
{
	char *lockfile = format("%s.lock", path);
	unsigned to_sleep = 1000; // Microseconds.
	unsigned max_to_sleep = 10000; // Microseconds.
	unsigned slept = 0; // Microseconds.
	bool acquired = false;

	while (true) {
		DWORD flags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE;
		lockfile_handle = CreateFile(
			lockfile,
			GENERIC_WRITE, // desired access
			0,             // shared mode (0 = not shared)
			NULL,          // security attributes
			CREATE_ALWAYS, // creation disposition,
			flags,         // flags and attributes
			NULL           // template file
		);
		if (lockfile_handle != INVALID_HANDLE_VALUE) {
			acquired = true;
			break;
		}

		DWORD error = GetLastError();
		cc_log("lockfile_acquire: CreateFile %s: error code %lu", lockfile, error);
		if (error == ERROR_PATH_NOT_FOUND) {
			// Directory doesn't exist?
			if (create_parent_dirs(lockfile) == 0) {
				// OK. Retry.
				continue;
			}
		}

		// ERROR_SHARING_VIOLATION: lock already held.
		// ERROR_ACCESS_DENIED: maybe pending delete.
		if (error != ERROR_SHARING_VIOLATION && error != ERROR_ACCESS_DENIED) {
			// Fatal error, give up.
			break;
		}

		if (slept > time_limit) {
			cc_log("lockfile_acquire: gave up acquiring %s", lockfile);
			break;
		}

		cc_log("lockfile_acquire: failed to acquire %s; sleeping %u microseconds",
		       lockfile, to_sleep);
		usleep(to_sleep);
		slept += to_sleep;
		to_sleep = MIN(max_to_sleep, 2 * to_sleep);
	}

	if (acquired) {
		cc_log("Acquired lock %s", lockfile);
	} else {
		cc_log("Failed to acquire lock %s", lockfile);
	}
	free(lockfile);
	return acquired;
}

// Release the lockfile for the given path. Assumes that we are the legitimate
// owner.
void
lockfile_release(const char *path)
{
	assert(lockfile_handle != INVALID_HANDLE_VALUE);
	cc_log("Releasing lock %s.lock", path);
	CloseHandle(lockfile_handle);
	lockfile_handle = NULL;
}

#endif

#ifdef TEST_LOCKFILE

int
main(int argc, char **argv)
{
	extern struct conf *conf;
	conf = conf_create();
	if (argc == 3) {
		unsigned staleness_limit = atoi(argv[1]);
		printf("Acquiring\n");
		bool acquired = lockfile_acquire(argv[2], staleness_limit);
		if (acquired) {
			printf("Sleeping 2 seconds\n");
			sleep(2);
			lockfile_release(argv[2]);
			printf("Released\n");
		} else {
			printf("Failed to acquire\n");
		}
	} else {
		fprintf(stderr,
		        "Usage: testlockfile <staleness_limit> <path>\n");
	}
	return 1;
}
#endif
