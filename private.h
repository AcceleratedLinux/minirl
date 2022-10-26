#pragma once

#include "minirl.h"
#include "buffer.h"
#include "key_binding.h"

#include <termios.h>

#define MINIRL_DEFAULT_HISTORY_MAX_LEN 100
#define MINIRL_MAX_LINE 4096

/* The minirlState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
typedef struct cursor_st {
	int row;
	int col;
} cursor_st;

typedef struct minirl_key_handler_flags_st {
	bool done;
	bool refresh_required;
	bool cursor_refresh_required;
	bool error;
} minirl_key_handler_flags_st;

typedef struct minirl_state_st {
	struct buffer *line_buf;

	char const *prompt;     /* Prompt to display. */
	size_t prompt_len;      /* Prompt length. */
	size_t pos;             /* Current cursor position. */
	size_t len;             /* Current edited line length. */

	size_t terminal_width;  /* Number of columns in terminal. */
	size_t max_rows;        /* Maximum num of rows used so far */
	int history_index;      /* The history index we are currently editing. */

	cursor_st previous_cursor;
	cursor_st previous_line_end;

	minirl_key_handler_flags_st flags;
} minirl_state_st;

typedef struct echo_st {
	bool disable;
	char ch;
} echo_st;

struct minirl_st {
	struct {
		FILE *stream;
		int fd;
	} in;
	struct {
		FILE *stream;
		int fd;
	} out;

	bool is_a_tty;
	bool in_raw_mode;
	struct termios orig_termios;
	minirl_keymap_st *keymap;
	minirl_state_st state;

	struct {
		bool mask_mode;
		bool force_isatty;
		echo_st echo;
	} options;

	struct {
		size_t max_len;
		size_t current_len;
		char **history;
	} history;
};

