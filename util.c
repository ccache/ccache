#include "ccache.h"

static FILE *logfile;

int cc_log(const char *format, ...)
{
	int ret;
	va_list ap;

	if (!logfile) logfile = fopen(CCACHE_LOGFILE, "a");
	if (!logfile) return -1;
	
	va_start(ap, format);
	ret = vfprintf(logfile, format, ap);
	va_end(ap);
	fflush(logfile);

	return ret;
}

void fatal(const char *msg)
{
	cc_log("FATAL: %s\n", msg);
	exit(1);
}

void copy_fd(int fd_in, int fd_out)
{
	char buf[1024];
	int n;

	while ((n = read(fd_in, buf, sizeof(buf))) > 0) {
		if (write(fd_out, buf, n) != n) {
			fatal("Failed to copy fd");
		}
	}
}

void oom(const char *msg)
{
	cc_log("Out of memory: %s\n", msg);
	exit(1);
}

int create_dir(const char *dir)
{
	struct stat st;
	if (stat(dir, &st) == 0) {
		if (S_ISDIR(st.st_mode)) {
			return 0;
		}
		errno = ENOTDIR;
		return 1;
	}
	if (mkdir(dir, 0777) != 0) {
		return 1;
	}
	return 0;
}
