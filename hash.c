/*
  simple front-end functions to mdfour code
  Copyright tridge@samba.org 2002
  
*/

#include "ccache.h"

static struct mdfour md;

void hash_start(void)
{
	mdfour_begin(&md);
}

void hash_string(const char *s)
{
	mdfour_update(&md, s, strlen(s));
}

void hash_int(int x)
{
	mdfour_update(&md, (unsigned char *)&x, sizeof(x));
}

/* add contents of a file to the hash */
void hash_file(const char *fname)
{
	char buf[1024];
	int fd, n;

	fd = open(fname, O_RDONLY);
	if (fd == -1) {
		cc_log("Failed to open %s\n", fname);
		fatal("hash_file");
	}

	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		mdfour_update(&md, buf, n);
	}
	close(fd);
}

/* return the hash result as a static string */
char *hash_result(void)
{
	unsigned char sum[16];
	static char ret[33];
	int i;

	mdfour_result(&md, sum);
	
	for (i=0;i<16;i++) {
		snprintf(&ret[i*2], 3, "%02x", (unsigned)sum[i]);
	}

	return ret;
}
