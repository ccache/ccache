#define _GNU_SOURCE


#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <utime.h>
#include <stdarg.h>

#define STATUS_NOTFOUND 3
#define STATUS_FATAL 4
#define STATUS_NOCACHE 5

typedef unsigned uint32;

#include "mdfour.h"

void hash_start(void);
void hash_string(const char *s);
void hash_int(int x);
void hash_file(const char *fname);
char *hash_result(void);
void hash_buffer(const char *s, int len);

void cc_log(const char *format, ...);
void fatal(const char *msg);

void copy_fd(int fd_in, int fd_out);
int copy_file(const char *src, const char *dest);

int create_dir(const char *dir);
void x_asprintf(char **ptr, const char *format, ...);
char *x_strdup(const char *s);

int execute(char **argv, 
	    const char *path_stdout,
	    const char *path_stderr);

typedef struct {
	char **argv;
	int argc;
} ARGS;


ARGS *args_init(void);
void args_add(ARGS *args, const char *s);
