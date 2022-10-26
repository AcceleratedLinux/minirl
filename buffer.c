#include "buffer.h"
#include "export.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN_CAPACITY_INCREASE 256

NO_EXPORT
bool
buffer_grow(struct buffer * const ab, size_t const amount)
{
	size_t const extra_bytes =
		(amount < MIN_CAPACITY_INCREASE) ? MIN_CAPACITY_INCREASE : amount;
	size_t const new_capacity = ab->capacity + extra_bytes;
	/* Allow one extra byte for a NUL terminator. */
	char * const new_buf = realloc(ab->b, new_capacity + 1);

	if (new_buf == NULL) {
		return false;
	}
	ab->b = new_buf;
	ab->capacity = new_capacity;

	return true;
}

NO_EXPORT
bool
buffer_init(struct buffer * const ab, size_t const initial_capacity)
{
	ab->len = 0;
	ab->capacity = 0;
	ab->b = NULL;

	return buffer_grow(ab, initial_capacity);
}

NO_EXPORT
bool
buffer_append(struct buffer * const ab, char const * const s, size_t const len)
{
	size_t const new_len = ab->len + len;

	/*
	 * Grow the buffer if required.
	 * the buffer pointer may be NULL if the buffer wasn't initialised
	 * beforehand.
	 */
	if (ab->b == NULL || new_len > ab->capacity) {
		size_t const grow_amount = new_len - ab->capacity;

		if (!buffer_grow(ab, grow_amount)) {
			return false;
		}
	}

	memcpy(ab->b + ab->len, s, len);
	ab->len = new_len;
	ab->b[ab->len] = '\0';

	return true;
}

NO_EXPORT
void buffer_clear(struct buffer * const ab)
{
	free(ab->b);
	ab->b = NULL;
	ab->len = 0;
	ab->capacity = 0;
}

NO_EXPORT
int buffer_snprintf(
	struct buffer * const ab,
	char * const buf,
	size_t const buf_size,
	char const * const fmt, ...)
{
	va_list arg_ptr;

	va_start(arg_ptr, fmt);
	int const res = vsnprintf(buf, buf_size, fmt, arg_ptr);
	va_end(arg_ptr);

	buffer_append(ab, buf, strlen(buf));

	return res;
}

