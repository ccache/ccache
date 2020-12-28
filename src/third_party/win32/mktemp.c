/*	$OpenBSD: mktemp.c,v 1.39 2017/11/28 06:55:49 tb Exp $ */
/*
 * Copyright (c) 1996-1998, 2008 Theo de Raadt
 * Copyright (c) 1997, 2008-2009 Todd C. Miller
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600 // _WIN32_WINNT_VISTA
#endif

#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#endif

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX 1
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>

// Work-around wrong calling convention for RtlGenRandom in old mingw-w64
#define SystemFunction036 __stdcall SystemFunction036
#include <ntsecapi.h>
#undef SystemFunction036
#endif

#ifdef _MSC_VER
#define S_IRUSR	(_S_IREAD)
#define S_IWUSR	(_S_IWRITE)
#endif

#define MKTEMP_NAME	0
#define MKTEMP_FILE	1
#define MKTEMP_DIR	2

#define TEMPCHARS	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
#define NUM_CHARS	(sizeof(TEMPCHARS) - 1)
#define MIN_X		6

#ifdef _WIN32
#define MKOTEMP_FLAGS		(_O_APPEND|_O_NOINHERIT|_O_BINARY|_O_TEXT| \
				 _O_U16TEXT|_O_U8TEXT|_O_WTEXT)
#define MKTEMP_FLAGS_DEFAULT	(_O_BINARY)
#else
#define MKOTEMP_FLAGS		(O_APPEND|O_CLOEXEC|O_DSYNC|O_RSYNC|O_SYNC)
#define MKTEMP_FLAGS_DEFAULT	(0)
#endif

#ifndef nitems
#define nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

#ifdef _WIN32
static BOOL CALLBACK
lookup_ntdll_function_once(
    PINIT_ONCE init_once, PVOID parameter, PVOID *context)
{
	(void)init_once;
	*context = (PVOID)GetProcAddress(
	    GetModuleHandleA("ntdll.dll"), parameter);
	return(TRUE);
}

static NTSTATUS
GetLastNtStatus()
{
	static INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;
	typedef NTSTATUS(NTAPI * RtlGetLastNtStatus_t)(void);
	RtlGetLastNtStatus_t get_last_nt_status = NULL;
	InitOnceExecuteOnce(&init_once, lookup_ntdll_function_once,
	    "RtlGetLastNtStatus", (LPVOID *)&get_last_nt_status);
	return(get_last_nt_status());
}

static int
normalize_msvcrt_errno(int ret)
{
	if (ret == -1 && errno == EACCES && _doserrno == ERROR_ACCESS_DENIED) {
		/*
		 * Win32 APIs return ERROR_ACCESS_DENIED for many distinct
		 * NTSTATUS codes, even when it's arguably inappropriate to do
		 * so, e.g. if you attempt to open a directory, or open a file
		 * that's in the "pending delete" state. These are mapped to
		 * EACCESS in the C runtime. We instead map these to EEXIST.
		 */
		NTSTATUS nt_err = GetLastNtStatus();
		if (nt_err == STATUS_FILE_IS_A_DIRECTORY ||
		    nt_err == STATUS_DELETE_PENDING) {
			errno = EEXIST;
		}
	}
	return(ret);
}

#define open(...)		(normalize_msvcrt_errno(open(__VA_ARGS__)))
#define mkdir(path, mode)	(normalize_msvcrt_errno(mkdir(path)))
#define lstat(path, sb)		(normalize_msvcrt_errno(stat(path, sb)))

static void (*_bsd_mkstemp_random_source)(void *buf, size_t n);

void
bsd_mkstemp_set_random_source(void (*f)(void *buf, size_t n))
{
	_bsd_mkstemp_random_source = f;
}

static void
arc4random_buf(void *buf, size_t nbytes)
{
	if (_bsd_mkstemp_random_source != NULL) {
		_bsd_mkstemp_random_source(buf, nbytes);
	} else {
		RtlGenRandom(buf, (ULONG)nbytes);
	}
}
#endif

static int
mktemp_internal(char *path, int slen, int mode, int flags)
{
	char *start, *cp, *ep;
	const char tempchars[] = TEMPCHARS;
	unsigned int tries;
	struct stat sb;
	size_t len;
	int fd;

	len = strlen(path);
	if (len < MIN_X || slen < 0 || (size_t)slen > len - MIN_X) {
		errno = EINVAL;
		return(-1);
	}
	ep = path + len - slen;

	for (start = ep; start > path && start[-1] == 'X'; start--)
		;
	if (ep - start < MIN_X) {
		errno = EINVAL;
		return(-1);
	}

	if (flags & ~MKOTEMP_FLAGS) {
		errno = EINVAL;
		return(-1);
	}
	flags |= O_CREAT|O_EXCL|O_RDWR;

	tries = INT_MAX;
	do {
		cp = start;
		do {
			unsigned short rbuf[16];
			unsigned int i;

			/*
			 * Avoid lots of arc4random() calls by using
			 * a buffer sized for up to 16 Xs at a time.
			 */
			arc4random_buf(rbuf, sizeof(rbuf));
			for (i = 0; i < nitems(rbuf) && cp != ep; i++)
				*cp++ = tempchars[rbuf[i] % NUM_CHARS];
		} while (cp != ep);

		switch (mode) {
		case MKTEMP_NAME:
			if (lstat(path, &sb) != 0)
				return(errno == ENOENT ? 0 : -1);
			break;
		case MKTEMP_FILE:
			fd = open(path, flags, S_IRUSR|S_IWUSR);
			if (fd != -1 || errno != EEXIST)
				return(fd);
			break;
		case MKTEMP_DIR:
			if (mkdir(path, S_IRUSR|S_IWUSR|S_IXUSR) == 0)
				return(0);
			if (errno != EEXIST)
				return(-1);
			break;
		}
	} while (--tries);

	errno = EEXIST;
	return(-1);
}

char *
bsd_mktemp(char *path)
{
	if (mktemp_internal(path, 0, MKTEMP_NAME, MKTEMP_FLAGS_DEFAULT) == -1)
		return(NULL);
	return(path);
}

int
bsd_mkostemps(char *path, int slen, int flags)
{
	return(mktemp_internal(path, slen, MKTEMP_FILE, flags));
}

int
bsd_mkstemp(char *path)
{
	return(mktemp_internal(path, 0, MKTEMP_FILE, MKTEMP_FLAGS_DEFAULT));
}

int
bsd_mkostemp(char *path, int flags)
{
	return(mktemp_internal(path, 0, MKTEMP_FILE, flags));
}

int
bsd_mkstemps(char *path, int slen)
{
	return(mktemp_internal(path, slen, MKTEMP_FILE, MKTEMP_FLAGS_DEFAULT));
}

char *
bsd_mkdtemp(char *path)
{
	int error;

	error = mktemp_internal(path, 0, MKTEMP_DIR, 0);
	return(error ? NULL : path);
}
