#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef DISABLE_UTF8

#include "utf8.h"

#define MAX_UTF8_LEN 4
#define MAX_CHAR_LEN MAX_UTF8_LEN

static inline size_t
char_len(char const c)
{
	return utf8_char_len(c);
}
static inline size_t
char_decode(char const * const s, size_t const len, uint32_t * const dst)
{
	return utf8_char_decode(s, len, dst);
}

static inline size_t
char_encode(uint32_t const c, char * const s, size_t const len)
{
	return utf8_char_encode(c, s, len);
}

static inline size_t
char_next(char const * const s, size_t const len, size_t const point)
{
	return utf8_char_next(s, len, point);
}

static inline size_t
char_prev(char const * const s, size_t const len, size_t const point)
{
	return utf8_char_prev(s, len, point);
}

static inline size_t
char_width(char const * const s, size_t const len, size_t const point)
{
	return utf8_char_width(s, len, point);
}

static inline size_t
grapheme_next(char const * const s, size_t const len, size_t const point)
{
	return utf8_grapheme_next(s, len, point);
}

static inline size_t
grapheme_prev(char const * const s, size_t const len, size_t const point)
{
	return utf8_grapheme_prev(s, len, point);
}

static inline size_t
grapheme_width(
	char const * const s,
	size_t const len,
	size_t const point,
	size_t * const pnext)
{
	return utf8_grapheme_width(s, len, point, pnext);
}

#else

#include "utils.h"

#define MAX_CHAR_LEN 1

static inline size_t
char_len(char const c)
{
	UNUSED_ARG(c);

	return 1;
}

static inline size_t
char_decode(char const * const s, size_t const len, uint32_t * const dst)
{
	UNUSED_ARG(len);

	if (dst != NULL) {
		*dst = *s;
	}

	return 1;
}

static inline size_t
char_encode(uint32_t const c, char * const s, size_t const len)
{
	if (len < 1) {
		return 0;
	}
	*s = c;

	return 1;
}

static inline size_t
char_next(char const * const s, size_t const len, size_t const point)
{
	UNUSED_ARG(s);

	if (point >= len) {
		return len;
	}

	return point + 1;
}

static inline size_t
char_prev(char const * const s, size_t const len, size_t const point)
{
	UNUSED_ARG(s);
	UNUSED_ARG(len);

	if (point <= 0) {
		return 0;
	}

	return point - 1;
}

static inline size_t
char_width(char const * const s, size_t const len, size_t const point)
{
#define FIRST_PRINTABLE_ASCII 0x20
#define MAX_ASCII 0x7f

	if (s[point] >= FIRST_PRINTABLE_ASCII && s[point] <= MAX_ASCII) {
		return 1;
	}

	return 0;
}

static inline size_t
grapheme_next(char const * const s, size_t const len, size_t const point)
{
	return char_next(s, len, point);
}

static inline size_t
grapheme_prev(char const * const s, size_t const len, size_t const point)
{
	return char_prev(s, len, point);
}

static inline size_t
grapheme_width(
	char const * const s,
	size_t const len,
	size_t const point,
	size_t * const pnext)
{
	if (pnext != NULL) {
		*pnext = char_next(s, len, point);
	}

	return char_width(s, len, point);
}

#endif

