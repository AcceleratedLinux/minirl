#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Define a simple "append buffer" structure, that is a heap allocated string
 * where we can append to. This is useful in order to write all the escape
 * sequences in a buffer and flush them to the output stream in a single call,
 * to avoid flickering effects.
 */
struct buffer {
	char *b;
	size_t len;
	size_t capacity;
};


bool
buffer_init(struct buffer *ab, size_t initial_capacity);

/*
 * Append characters to the buffer.
 * Return true if successful, else false.
 */
bool
buffer_append(struct buffer *ab, char const *s, size_t len);

int
buffer_snprintf(struct buffer *ab, char *buf, size_t buf_size, char const *fmt, ...);

bool
buffer_grow(struct buffer *ab, size_t amount);

void
buffer_clear(struct buffer *ab);

