#pragma once

#include <string.h>
#include <stdlib.h>

#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define UNUSED_ARG(arg) (void)arg

static inline char *
chrdup(char const to_dup, size_t const count)
{
	char * const buffer = malloc(count + 1);

	if (buffer != NULL) {
		memset(buffer, to_dup, count);
		buffer[count] = '\0';
	}

	return buffer;
}

