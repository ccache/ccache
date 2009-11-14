#include <string.h>

#include "hashutil.h"
#include "murmurhashneutral2.h"

unsigned int hash_from_string(void *str)
{
	return murmurhashneutral2(str, strlen((const char *)str), 0);
}

int strings_equal(void *str1, void *str2)
{
	return strcmp((const char *)str1, (const char *)str2) == 0;
}
