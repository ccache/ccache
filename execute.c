#include "ccache.h"


/*
  execute a compiler backend, capturing all output to the given paths
  the full path to the compiler to run is in argv[0]
*/
void execute(char **argv, 
	     const char *path_stdout,
	     const char *path_stderr,
	     const char *path_status)
{
	pid_t pid;
	int fd;
	int s, status;

	pid = fork();
	if (pid == -1) fatal("Failed to fork");
	
	if (pid == 0) {
		unlink(path_stdout);
		fd = open(path_stdout, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, 0644);
		if (fd == -1) {
			exit(STATUS_NOCACHE);
		}
		dup2(fd, 1);
		close(fd);

		unlink(path_stderr);
		fd = open(path_stderr, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, 0644);
		if (fd == -1) {
			exit(STATUS_NOCACHE);
		}
		dup2(fd, 2);
		close(fd);

		exit(execv(argv[0], argv));
	}

	if (waitpid(pid, &status, 0) != pid) {
		fatal("waitpid failed");
	}
	
	fd = open(path_status, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, 0644);
	if (fd == -1) {
		fatal("Failed to create status file");
	}
	s = WEXITSTATUS(status);
	if (write(fd, &s, sizeof(s)) != sizeof(s)) {
		fatal("failed to write status file");
	}
	close(fd);
}

