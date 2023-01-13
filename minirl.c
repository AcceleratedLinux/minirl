#include "minirl.h"
#include "buffer.h"
#include "export.h"
#include "io.h"
#include "private.h"
#include "utils.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/ttydefaults.h>
#include <sys/types.h>
#include <unistd.h>


#define DEFAULT_TERMINAL_WIDTH 80
#define ESCAPESTR "\x1b"


enum KEY_ACTION
{
	KEY_NULL = 0,		/* NULL */
	TAB = 9,		/* Tab */
	ENTER = 13,		/* Enter */
	ESC = 27,		/* Escape */
	BACKSPACE =  127	/* Backspace */
};

int
minirl_printf(minirl_st * const minirl, char const * const fmt, ...)
{
	va_list args;
	int len;

	va_start(args, fmt);
	len = vfprintf(minirl->out.stream, fmt, args);
	va_end(args);

	return len;
}

static void
minirl_state_had_error(minirl_state_st * const l)
{
	l->flags.error = true;
}

static void
minirl_state_is_done(minirl_state_st * const l)
{
	l->flags.done = true;
}

static void
minirl_state_refresh_required(minirl_state_st * const l)
{
	l->flags.refresh_required = true;
}

static void
minirl_state_cursor_refresh_required(minirl_state_st * const l)
{
	l->flags.cursor_refresh_required = true;
}

static void
minirl_state_reset_line_state(minirl_state_st * const l)
{
	l->max_rows = 1;
	minirl_state_refresh_required(l);
}

static void
move_edit_position_right(minirl_state_st * const l)
{
	if (l->pos < l->len) {
		l->pos++;
		minirl_state_cursor_refresh_required(l);
	}
}

static void
move_edit_position_left(minirl_state_st * const l)
{
	if (l->pos > 0) {
		l->pos--;
		minirl_state_cursor_refresh_required(l);
	}
}

static void
move_edit_position_to_start(minirl_state_st * const l)
{
	if (l->pos > 0) {
		l->pos = 0;
		minirl_state_cursor_refresh_required(l);
	}
}

static void
move_edit_position_to_end(minirl_state_st * const l)
{
	if (l->pos < l->len) {
		l->pos = l->len;
		minirl_state_cursor_refresh_required(l);
	}
}

char *
minirl_line_get(minirl_st * const minirl)
{
	return minirl->state.line_buf->b;
}

size_t
minirl_point_get(minirl_st * const minirl)
{
	return minirl->state.pos;
}

size_t
minirl_end_get(minirl_st * const minirl)
{
	return minirl->state.len;
}

void
minirl_point_set(minirl_st * const minirl, size_t const new_point)
{
	minirl_state_st * const l = &minirl->state;

	if (l->pos != new_point && new_point <= l->len) {
		l->pos = new_point;
		minirl_state_cursor_refresh_required(l);
	}
}

void
minirl_force_isatty(minirl_st * const minirl)
{
	minirl->options.force_isatty = true;
}

/* Raw mode: 1960 magic shit. */
static int
enable_raw_mode(minirl_st * const minirl, int const fd)
{
	if (!isatty(fd)) {
		/*
		 * Don't consider this fatal - Just don't attempt to set
		 * TTY settings.
		 */
		return 0;
	}

	if (tcgetattr(fd, &minirl->orig_termios) == -1) {
		goto fatal;
	}

	struct termios raw;

	raw = minirl->orig_termios;  /* modify the original mode */
	raw.c_iflag = 0;
	raw.c_oflag = OPOST | ONLCR;
	raw.c_lflag = 0;
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;

	/* put terminal in raw mode after flushing */
	if (tcsetattr(fd, TCSADRAIN, &raw) < 0) {
		goto fatal;
	}

	minirl->in_raw_mode = true;
	return 0;

fatal:
	errno = ENOTTY;
	return -1;
}

static void
disable_raw_mode(minirl_st * const minirl, int const fd)
{
	if (minirl->in_raw_mode
	    && tcsetattr(fd, TCSADRAIN, &minirl->orig_termios) != -1) {
		minirl->in_raw_mode = false;
	}
}

/*
 * Try to get the number of columns in the current terminal, or assume 80
 * if it fails.*
 */
int
minirl_terminal_width(minirl_st * const minirl)
{
	int cols = DEFAULT_TERMINAL_WIDTH;
	struct winsize ws;

	if (ioctl(minirl->out.fd, TIOCGWINSZ, &ws) != -1 && ws.ws_col != 0) {
		cols = ws.ws_col;
	}

	return cols;
}

/* Clear the screen. Used to handle ctrl+l */
void
minirl_screen_clear(minirl_st * const minirl)
{
	if (io_write(minirl->out.fd, "\x1b[H\x1b[2J", 7) <= 0) {
		/* nothing to do, just to avoid warning. */
	}

	minirl_state_st * const l = &minirl->state;

	minirl_state_reset_line_state(l);
	minirl_state_refresh_required(l);
}

static void
cursor_add_ch(
	cursor_st * const cursor,
	char const ch,
	size_t const terminal_width,
	echo_st const * const echo)
{
	char const echo_ch = echo->disable ? echo->ch : ch;

	if (echo_ch == '\0') {
		return;
	}
	cursor->col++;
	/* TODO: Support '\t' <TAB> characters. 8 chars per TAB. */
	if (cursor->col == terminal_width || echo_ch == '\n') {
		cursor->row++;
		cursor->col = 0;
	}
}

static void
cursor_calculate_position(
	size_t const terminal_width,
	size_t const prompt_len,
	char const * const line,
	size_t const max_chars,
	echo_st const * const echo,
	cursor_st * const cursor_out)
{
	/* Assume no newlines in the prompt. */
	cursor_st cursor = {
		.row = prompt_len / terminal_width,
		.col = prompt_len % terminal_width
	};
	size_t char_count = 0;

	for (char const *pch = line;
	     *pch != '\0' && char_count < max_chars;
	     pch++, char_count++) {
		cursor_add_ch(&cursor, *pch, terminal_width, echo);
	}
	*cursor_out = cursor;
};

static void
emit_set_column(struct buffer * const ab, size_t const column_num)
{
	char buf[10];

	buffer_snprintf(ab, buf, sizeof buf, ESCAPESTR "[%zuG", column_num);
}

static void
emit_row_clear(struct buffer * const ab)
{
	/* Note: Also moves the cursor to the start of the row. */
	char const seq[] = "\r" ESCAPESTR  "[0K";

	buffer_append(ab, seq, strlen(seq));
}

static void
emit_cursor_up(struct buffer * const ab, size_t const count)
{
	char buf[10];

	buffer_snprintf(ab, buf, sizeof buf, ESCAPESTR "[%zuA", count);
}

static void
emit_cursor_down(struct buffer * const ab, size_t const count)
{
	char buf[10];

	buffer_snprintf(ab, buf, sizeof buf, ESCAPESTR "[%zuB", count);
}

static void
emit_cursor_right(struct buffer * const ab, size_t const count)
{
	char buf[10];

	buffer_snprintf(ab, buf, sizeof buf, ESCAPESTR "[%zuC", count);
}

static void
emit_cursor_left(struct buffer * const ab, size_t const count)
{
	char buf[10];

	buffer_snprintf(ab, buf, sizeof buf, ESCAPESTR "[%zuD", count);
}

static bool
minirl_refresh_cursor(minirl_st * const minirl)
{
	bool success = true;
	minirl_state_st * const l = &minirl->state;

	/* Calculate row and column of current cursor. */
	cursor_st current_cursor;
	cursor_calculate_position(l->terminal_width,
				  l->prompt_len,
				  l->line_buf->b,
				  l->pos,
				  &minirl->options.echo,
				  &current_cursor);

	/* Check that the cursor has actually moved. */
	if (current_cursor.row == l->previous_cursor.row
	    && current_cursor.col == l->previous_cursor.col) {
		return true;
	}

	struct buffer ab;

	buffer_init(&ab, 20);

	/* Update the cursor position. */
	if (current_cursor.row < l->previous_cursor.row) {
		int const up_count = l->previous_cursor.row - current_cursor.row;

		emit_cursor_up(&ab, up_count);
	} else if (current_cursor.row > l->previous_cursor.row) {
		int const down_count = current_cursor.row - l->previous_cursor.row;

		emit_cursor_down(&ab, down_count);
	}
	if (current_cursor.col > l->previous_cursor.col) {
		int const right_count = current_cursor.col - l->previous_cursor.col;

		emit_cursor_right(&ab, right_count);
	} else if (current_cursor.col < l->previous_cursor.col) {
		int const left_count = l->previous_cursor.col - current_cursor.col;

		emit_cursor_left(&ab, left_count);
	}

	l->previous_cursor = current_cursor;
	l->flags.cursor_refresh_required = false;

	if (io_write(minirl->out.fd, ab.b, ab.len) == -1) {
		success = false;
	}

	buffer_clear(&ab);

	return success;
}

/* Multi line low level line refresh.
 *
 * Rewrite the currently edited line according to the buffer content,
 * cursor position, and number of columns of the terminal.
 */
static bool
minirl_refresh_line(minirl_st * const minirl)
{
	bool success = true;
	minirl_state_st * const l = &minirl->state;
	size_t const prompt_len = strlen(l->prompt);

	l->terminal_width = minirl_terminal_width(minirl);

	/* Calculate row and column of end of line. */
	cursor_st line_end_cursor;
	cursor_calculate_position(l->terminal_width,
				  prompt_len,
				  l->line_buf->b,
				  l->len,
				  &minirl->options.echo,
				  &line_end_cursor);

	/* Calculate row and column of end of cursor. */
	cursor_st current_cursor;
	cursor_calculate_position(l->terminal_width,
				  prompt_len,
				  l->line_buf->b,
				  l->pos,
				  &minirl->options.echo,
				  &current_cursor);

	struct buffer ab;

	buffer_init(&ab, 20);

	/*
	 * First step: clear all the lines used before.
	 * To do so start by going to the last row.
	 */
	if (l->max_rows > 1) {
		unsigned const down_count = l->max_rows - l->previous_cursor.row - 1;

		if (down_count > 0) {
			/* Move down. to last row. */
			emit_cursor_down(&ab, down_count);
		}

		/* Now for every row clear it, then go up. */
		for (size_t j = 0; j < l->max_rows - 1; j++) {
			emit_row_clear(&ab);
			emit_cursor_up(&ab, 1);
		}
	}

	/*
	 * Go to beginning of the line and clear to the end.
	 * This means the prompt will also be cleared, so will need to be
	 * output afresh.
	 */
	emit_row_clear(&ab);

	/* Write the prompt and the current buffer content */
	buffer_append(&ab, l->prompt, strlen(l->prompt));
	if (minirl->options.echo.disable) {
		if (minirl->options.echo.ch != '\0') {
			for (size_t i = 0; i < l->len; i++) {
				buffer_append(&ab, &minirl->options.echo.ch, 1);
			}
		}
	} else {
		buffer_append(&ab, l->line_buf->b, l->len);
	}

	/*
	 * If we are at the very end of the screen with our cursor, we need to
	 * emit a newline and move the cursor to the first column of the next
	 * line.
	 * If the last character on the row is a '\n' there is no need to emit
	 * the newline because that character already moved the cursor.
	 */
	if (l->pos > 0
	    && l->pos == l->len
	    && current_cursor.row > 0
	    && current_cursor.col == 0
	    && l->line_buf->b[l->pos - 1] != '\n') {
		buffer_append(&ab, "\n\r", strlen("\n\r"));
	}

	/*
	 * Move cursor to right position. At present it will be at the end of the
	 * current line.
	 */

	/* Go up till we reach the expected positon. */
	if (line_end_cursor.row > current_cursor.row) {
		unsigned const up_count = line_end_cursor.row - current_cursor.row;

		emit_cursor_up(&ab, up_count);
	}

	/* Set column. */
	emit_set_column(&ab, current_cursor.col + 1);

	l->previous_cursor = current_cursor;
	l->previous_line_end = line_end_cursor;

	/* Update max_rows if needed. */
	size_t const num_rows = line_end_cursor.row + 1;

	if (num_rows > l->max_rows) {
		l->max_rows = num_rows;
	}
	l->flags.refresh_required = false;
	l->flags.cursor_refresh_required = false;

	if (io_write(minirl->out.fd, ab.b, ab.len) == -1) {
		success = false;
	}

	buffer_clear(&ab);

	return success;
}

/*
 * Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0.
 */
static int
minirl_edit_insert(minirl_st * const minirl, char const c)
{
	minirl_state_st * const l = &minirl->state;

	if (l->len >= l->line_buf->capacity) {
		if (!buffer_grow(l->line_buf, l->len - l->line_buf->capacity)) {
			minirl_state_had_error(l);
			return -1;
		}
	}

	/* Insert the new char into the line buffer. */
	if (l->len != l->pos) {
		memmove(l->line_buf->b + l->pos + 1,
			l->line_buf->b + l->pos,
			l->len - l->pos);
	}
	l->line_buf->b[l->pos] = c;
	l->len++;
	l->pos++;
	l->line_buf->b[l->len] = '\0';

	bool require_full_refresh = true;

	if (l->len == l->pos) { /* Cursor is at the end of the line. */
		cursor_st new_line_end = l->previous_cursor;

		cursor_add_ch(&new_line_end, c, l->terminal_width, &minirl->options.echo);
		/*
		 * As long as the cursor remains on the same row as before the
		 * current character was added, and hasn't filled the terminal
		 * width, there is no need for a full refresh.
		 * If the character that filled the row (so col == 0) was a '\n'
		 * then the line still doesn't need to be refreshed.
		 */
		if (new_line_end.col > 0 || c == '\n') {
			require_full_refresh = false;
			/*
			 * After the io_write() is done the saved cursor positions
			 * will become  out of date, so update the saved cursor
			 * positions to reflect where the current cursor position
			 * is after the io_write.
			 */
			l->previous_cursor = new_line_end;
			l->previous_line_end = new_line_end;
			if (l->max_rows < (l->previous_line_end.row + 1)) {
				l->max_rows = l->previous_line_end.row + 1;
			}
		}
	}

	if (require_full_refresh) {
		minirl_state_refresh_required(l);
	} else {
		char const d = minirl->options.echo.disable ? minirl->options.echo.ch : c;

		if (d != '\0') {
			if (io_write(minirl->out.fd, &d, sizeof d) == -1) {
				minirl_state_had_error(l);
				return -1;
			}
		}
	}

	return 0;
}

/*
 * Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'.
 */
enum minirl_history_direction
{
	minirl_HISTORY_NEXT = 0,
	minirl_HISTORY_PREV = 1
};

static bool
minirl_edit_history_next(minirl_st * const minirl, enum minirl_history_direction const dir)
{
	minirl_state_st * const l = &minirl->state;

	if (minirl->history.current_len > 1) {
		/*
		 * Update the current history entry before to
		 * overwrite it with the next one.
		 */
		free(minirl->history.history[minirl->history.current_len - 1 - l->history_index]);
		minirl->history.history[minirl->history.current_len - 1 - l->history_index] = strdup(l->line_buf->b);
		/* Show the new entry */
		l->history_index += (dir == minirl_HISTORY_PREV) ? 1 : -1;
		if (l->history_index < 0) {
			l->history_index = 0;
			return false;
		} else if (l->history_index >= minirl->history.current_len) {
			l->history_index = minirl->history.current_len - 1;
			return false;
		}
		buffer_clear(l->line_buf);
		buffer_init(l->line_buf,
			    strlen(minirl->history.history[minirl->history.current_len - 1 - l->history_index]));
		buffer_append(l->line_buf,
			      minirl->history.history[minirl->history.current_len - 1 - l->history_index],
			      strlen(minirl->history.history[minirl->history.current_len - 1 - l->history_index]));
		l->len = l->pos = l->line_buf->len;
		return true;
	}
	return false;
}

/*
 * Delete the character at the right of the cursor without altering the cursor
 * position.
 * Basically this is what happens with the "Delete" keyboard key.
 */
static bool
delete_char_right(minirl_state_st * const l)
{
	if (l->len > 0 && l->pos < l->len) {
		memmove(l->line_buf->b + l->pos,
			l->line_buf->b + l->pos + 1,
			l->len - l->pos - 1);
		l->len--;
		l->line_buf->b[l->len] = '\0';

		return true;
	}

	return false;
}

static bool
delete_char_left(minirl_state_st * const l)
{
	if (l->pos > 0 && l->len > 0) {
		memmove(l->line_buf->b + l->pos - 1,
			l->line_buf->b + l->pos,
			l->len - l->pos);
		l->pos--;
		l->len--;
		l->line_buf->b[l->len] = '\0';

		return true;
	}

	return false;
}

static bool
delete_all_chars_left(minirl_state_st * const l)
{
	/* Delete all chars to the left of the cursor. */
	if (l->pos > 0 && l->len > 0) {
		memmove(l->line_buf->b,
			l->line_buf->b + l->pos,
			l->len - l->pos);
		l->len -= l->pos;
		l->pos = 0;
		l->line_buf->b[l->len] = '\0';

		return true;
	}

	return false;
}

/*
 * Delete the previous word, maintaining the cursor at the start of the
 * current word.
 */
static void
minirl_edit_delete_prev_word(minirl_state_st * const l)
{
	size_t old_pos = l->pos;

	while (l->pos > 0 && l->line_buf->b[l->pos - 1] == ' ') {
		l->pos--;
	}
	while (l->pos > 0 && l->line_buf->b[l->pos - 1] != ' ') {
		l->pos--;
	}

	size_t const diff = old_pos - l->pos;

	if (diff != 0) {
		memmove(l->line_buf->b + l->pos,
			l->line_buf->b + old_pos,
			l->len - old_pos + 1);
		l->len -= diff;
		minirl_state_refresh_required(l);
	}
}

static bool
delete_whole_line(minirl_state_st * const l)
{
	if (l->len > 0) {
		l->line_buf->b[0] = '\0';
		l->pos = 0;
		l->len = 0;

		return true;
	}

	return false;
}

static bool
swap_chars_at_cursor(minirl_state_st * const l)
{
	if (l->pos > 0 && l->pos < l->len) {
		char const aux = l->line_buf->b[l->pos - 1];

		l->line_buf->b[l->pos - 1] = l->line_buf->b[l->pos];
		l->line_buf->b[l->pos] = aux;
		if (l->pos != l->len - 1) {
			l->pos++;
		}

		return true;
	}

	return false;
}

static bool
delete_from_cursor_to_eol(minirl_state_st * const l)
{
	if (l->pos != l->len) {
		l->line_buf->b[l->pos] = '\0';
		l->len = l->pos;

		return true;
	}

	return false;
}

static void
remove_current_line_from_history(minirl_st * const minirl)
{
	if (minirl->history.current_len == 0) {
		/* Shouldn't happen. assert instead? */
		return;
	}
	minirl->history.current_len--;
	free(minirl->history.history[minirl->history.current_len]);
	minirl->history.history[minirl->history.current_len] = NULL;
}

static void
minirl_edit_done(minirl_st * const minirl)
{
	remove_current_line_from_history(minirl);
	move_edit_position_to_end(&minirl->state);
}

static bool
null_handler(minirl_st * const minirl, char const * const key, void * const user_ctx)
{
	/*
	 * Ignore this key.
	 * Handy for ignoring unhandled escape sequence characeters.
	 */
	return true;
}

static bool
delete_handler(minirl_st * const minirl, char const * const key, void * const user_ctx)
{
	/* Delete the character to the right of the cursor. */
	minirl_state_st * const l = &minirl->state;

	if (delete_char_right(l)) {
		minirl_state_refresh_required(l);
	}

	return true;
}

static bool
up_handler(minirl_st * const minirl, char const * const key, void * const user_ctx)
{
	/* Show the previous history entry. */
	if (minirl_edit_history_next(minirl, minirl_HISTORY_PREV)) {
		minirl_state_refresh_required(&minirl->state);
	}

	return true;
}

static bool
down_handler(minirl_st * const minirl, char const * const key, void * const user_ctx)
{
	/* Show the next history entry. */
	if (minirl_edit_history_next(minirl, minirl_HISTORY_NEXT)) {
		minirl_state_refresh_required(&minirl->state);
	}

	return true;
}

static bool
right_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	move_edit_position_right(&minirl->state);

	return true;
}

static bool
left_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	move_edit_position_left(&minirl->state);

	return true;
}

static bool
home_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	move_edit_position_to_start(&minirl->state);

	return true;
}

static bool
end_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	move_edit_position_to_end(&minirl->state);

	return true;
}

static bool
default_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	/* Insert the key at the current cursor position. */
	minirl_edit_insert(minirl, *key);

	return true;
}


static bool
enter_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	minirl_is_done(minirl);

	return true;
}

static bool
ctrl_c_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	/* Clear the whole line and indicate that processing is done. */
	delete_whole_line(&minirl->state);
	minirl_is_done(minirl);

	return true;
}

static bool
backspace_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	/* Delete the character to the left of the cursor. */
	if (delete_char_left(&minirl->state)) {
		minirl_state_refresh_required(&minirl->state);
	}

	return true;
}

static bool
ctrl_d_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	/*
	 * Delete the character to the right of the cursor if there is one,
	 * else indicate EOF (i.e. results in an error and program typically exits).
	 */
	minirl_state_st * const l = &minirl->state;
	bool result;

	if (l->len > 0) {
		result = delete_handler(minirl, key, user_ctx);
	} else {
		/* Line is empty, so indicate an error. */
		remove_current_line_from_history(minirl);
		minirl_state_had_error(l);
		result = true;
	}

	return result;
}

static bool
ctrl_t_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	/*
	 * Swap the current character with the one to its left, and move the
	 * cursor right one position.
	 */
	minirl_state_st * const l = &minirl->state;

	if (swap_chars_at_cursor(l)) {
		minirl_state_refresh_required(l);
	}

	return true;
}

static bool
ctrl_u_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	/* Delete everythng before the cursor. */
	minirl_state_st * const l = &minirl->state;

	if (delete_all_chars_left(l)) {
		minirl_state_refresh_required(l);
	}

	return true;
}

static bool
ctrl_k_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	/* Delete from cursor to EOL. */
	minirl_state_st * const l = &minirl->state;

	if (delete_from_cursor_to_eol(l)) {
		minirl_state_refresh_required(l);
	}

	return true;
}

static bool
ctrl_l_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	/* Clear the screen and move the cursor to EOL. */
	minirl_screen_clear(minirl);

	return true;
}

static bool
ctrl_w_handler(minirl_st * const minirl, char const *key, void * const user_ctx)
{
	/* Delete the previous word. */
	minirl_edit_delete_prev_word(&minirl->state);

	return true;
}

static void
key_handler_lookup(
	minirl_st * const minirl,
	uint8_t * const c,
	minirl_key_binding_handler_cb *handler,
	void ** const user_ctx)
{
	/*
	 * Look through the key map sequence until a match is found, or
	 * there is no keymap assigned to the current key.
	 */
	minirl_keymap_st *keymap = minirl->keymap;

	for (;;) {
		uint8_t const index = *c;

		if (keymap->keys[index].handler != NULL) {
			/* Indicates the end a sequence. */
			*handler = keymap->keys[index].handler;
			*user_ctx = keymap->keys[index].user_ctx;
			break;
		}
		keymap = keymap->keys[index].keymap;
		if (keymap == NULL) {
			break;
		}

		/* Get here with multi-byte sequences. */
		uint8_t new_c;
		int const nread = io_read(minirl->in.fd, &new_c, 1);

		if (nread <= 0) {
			break;
		}
		*c = new_c;
	}
}

/*
 * This function is the core of the line editing capability of minirl.
 * It expects 'fd' to be already in "raw mode" so that every key pressed
 * will be returned ASAP to read().
 *
 * The function returns the length of the current buffer, or -1 if and error
 * occurred.
 */
static int minirl_edit(
	minirl_st * const minirl,
	struct buffer * const line_buf,
	char const * const prompt)
{
	memset(&minirl->state, 0, sizeof minirl->state);

	minirl_state_st * const l = &minirl->state;

	/* Populate the minirl state implementing editing functionalities. */
	l->line_buf = line_buf;
	l->prompt = prompt;
	l->prompt_len = strlen(prompt);
	l->pos = 0;
	l->len = 0;
	l->terminal_width = minirl_terminal_width(minirl);
	l->max_rows = 1;
	l->history_index = 0;

	/* Buffer starts empty. */
	l->line_buf->b[0] = '\0';
	cursor_calculate_position(l->terminal_width,
				  l->prompt_len,
				  l->line_buf->b,
				  0,
				  &minirl->options.echo,
				  &l->previous_cursor);
	l->previous_line_end = l->previous_cursor;

	/*
	 * The latest history entry is always our current buffer, that
	 * initially is just an empty string.
	 */
	minirl_history_add(minirl, "");

	/* Get the prompt printed by refreshing the empty line. */
	minirl_refresh_line(minirl);

	for (;;) {
		uint8_t c;
		int const nread = io_read(minirl->in.fd, &c, 1);
		if (nread <= 0) {
			return -1;
		}

		minirl_key_binding_handler_cb handler = NULL;
		void *user_ctx = NULL;

		key_handler_lookup(minirl, &c, &handler, &user_ctx);

		if (handler != NULL) {
			/* TODO: Should pass the complete key sequence. */
			char key_str[2] = { c, '\0' };

			l->flags = (minirl_key_handler_flags_st){0};

			bool const res = handler(minirl, key_str, user_ctx);
			(void)res; //* TODO: Treat false as an error?

			if (l->flags.error) {
				return -1;
			}

			if (l->flags.refresh_required) {
				minirl_refresh_line(minirl);
			} else if (!l->flags.done && l->flags.cursor_refresh_required) {
				minirl_refresh_cursor(minirl);
			}

			if (l->flags.done) {
				minirl_edit_done(minirl);
				break;
			}
		}
	}

	return l->len;
}

/*
 * This function calls the line editing function minirlEdit() using
 * the in_fd file descriptor set in raw mode.
 */
static int
minirl_raw(
	minirl_st * const minirl,
	struct buffer * const line_buf,
	char const * const prompt)
{
	if (enable_raw_mode(minirl, minirl->in.fd) == -1) {
		return -1;
	}

	int const count = minirl_edit(minirl, line_buf, prompt);

	disable_raw_mode(minirl, minirl->in.fd);

	return count;
}

/* This function is called when minirl() is called with the standard
 * input file descriptor not attached to a TTY. So for example when the
 * program using minirl is called in pipe or with a file redirected
 * to its standard input. In this case, we want to be able to return the
 * line regardless of its length (by default we are limited to 4k). */
static char *
minirl_no_tty(minirl_st * const minirl)
{
	char *line = NULL;
	size_t len = 0;
	size_t maxlen = 0;

	while (1) {
		/*
		 * Grow the buffer.
		 * XXX - Use append buffer?
		 */
		if (len == maxlen) {
			if (maxlen == 0) {
				maxlen = 16;
			}
			maxlen *= 2;
			char * const oldval = line;
			line = realloc(line, maxlen + 1);
			if (line == NULL) {
				if (oldval != NULL) {
					free(oldval);
				}
				return line;
			}
			line[len] = '\0';
		}

		int c = fgetc(minirl->in.stream);
		if (c == EOF || c == '\n') {
			if (c == EOF && len == 0) {
				free(line);
				line = NULL;
			}
			return line;
		} else {
			line[len] = c;
			len++;
			line[len] = '\0';
		}
	}
	/* Unreachable */
	return NULL;
}

/* The high level function that is the main API of the minirl library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
char *
minirl_readline(minirl_st * const minirl, char const * const prompt)
{
	char *line;

	if (!minirl->options.force_isatty && !minirl->is_a_tty) {
		/* Not a tty: read from file / pipe. In this mode we don't want any
		 * limit to the line size, so we call a function to handle that. */
		line = minirl_no_tty(minirl);
	} else {
		struct buffer line_buf;

		buffer_init(&line_buf, 0);

		int const line_length = minirl_raw(minirl, &line_buf, prompt);

		if (line_length == -1) {
			line = NULL;
		} else {
			line = strdup(line_buf.b);
		}

		buffer_clear(&line_buf);
	}

	if (line == NULL || line[0] == '\0') {
		/*
		 * Without this, when empty lines (e.g. after CTRL-C) are returned,
		 * the next prompt gets written out on the same line as the previous.
		 */
		char const nl = '\n';
		int const res = io_write(minirl->out.fd, &nl, sizeof nl);
		(void)res;
	}

	return line;
}

/*
 * This is just a wrapper the user may want to call in order to make sure
 * the minirl returned buffer is freed with the same allocator it was
 * created with. Useful when the main program is using an alternative
 * allocator.
 */
void
minirl_line_free(void * const ptr)
{
	free(ptr);
}


/*
 * Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co.
 */
static void
free_history(minirl_st * const minirl)
{
	if (minirl->history.history != NULL) {
		for (size_t j = 0; j < minirl->history.current_len; j++) {
			free(minirl->history.history[j]);
		}
		free(minirl->history.history);
	}
}

/*
 * This is the API call to add a new entry in the minirl history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one, so it is not exactly suitable for huge
 * histories, but will work well for a few hundred of entries.
 *
 * Using a circular buffer is smarter, but a bit more complex to handle.
 */
int
minirl_history_add(minirl_st * const minirl, char const * const line)
{
	if (minirl->history.max_len == 0) {
		return 0;
	}

	/* Initialization on first call. */
	if (minirl->history.history == NULL) {
		minirl->history.history =
			calloc(sizeof(*minirl->history.history), minirl->history.max_len);
		if (minirl->history.history == NULL) {
			return 0;
		}
	}

	/* Don't add duplicated lines. */
	if (minirl->history.current_len > 0
	    && strcmp(minirl->history.history[minirl->history.current_len - 1], line) == 0) {
		return 0;
	}

	/*
	 * Add a heap allocated copy of the line in the history.
	 * If we reached the max length, remove the older line.
	 */
	char * const linecopy = strdup(line);

	if (linecopy == NULL) {
		return 0;
	}
	if (minirl->history.current_len == minirl->history.max_len) {
		free(minirl->history.history[0]);
		memmove(minirl->history.history,
			minirl->history.history + 1,
			sizeof(char *) * (minirl->history.max_len - 1));
		minirl->history.current_len--;
	}
	minirl->history.history[minirl->history.current_len] = linecopy;
	minirl->history.current_len++;

	return 1;
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int
minirl_history_set_max_len(minirl_st * const minirl, size_t const len)
{
	if (len < 1) {
		return 0;
	}
	if (minirl->history.history) {
		size_t tocopy = minirl->history.current_len;
		char ** const new_history = calloc(sizeof(*new_history), len);

		if (new_history == NULL) {
			return 0;
		}

		/* If we can't copy everything, free the elements we'll not use. */
		if (len < tocopy) {
			for (size_t j = 0; j < tocopy - len; j++) {
				free(minirl->history.history[j]);
			}
			tocopy = len;
		}
		memcpy(new_history,
		       minirl->history.history + (minirl->history.current_len - tocopy),
		       sizeof(char *) * tocopy);
		free(minirl->history.history);
		minirl->history.history = new_history;
	}
	minirl->history.max_len = len;
	if (minirl->history.current_len > minirl->history.max_len) {
		minirl->history.current_len = minirl->history.max_len;
	}

	return 1;
}

void
minirl_text_delete(minirl_st * const minirl, size_t const start, size_t const end)
{
	minirl_state_st * const l = &minirl->state;
	unsigned const delta = end - start;

	if (delta == 0) {
		return;
	}

	/* Move any text which is left, including the terminator */
	char * const line = minirl_line_get(minirl);
	memmove(&line[start], &line[start + delta], l->len + 1 - end);
	l->len -= delta;

	/* now adjust the indexes */
	if (l->pos > end) {
		/* move the insertion point back appropriately */
		l->pos -= delta;
	} else if (l->pos > start) {
		/* move the insertion point to the start */
		l->pos = start;
	}

	minirl_state_refresh_required(l);
}

/*
 * Insert text into the line at the current cursor position.
 */
bool
minirl_text_len_insert(
	minirl_st * const minirl,
	char const * const text,
	size_t const length)
{
	for (size_t i = 0; i < length; i++) {
		minirl_edit_insert(minirl, text[i]);
	}

	return true;
}

bool
minirl_text_insert(
	minirl_st * const minirl,
	char const * const text)
{
	return minirl_text_len_insert(minirl, text, strlen(text));
}

void
minirl_display_matches(minirl_st * const minirl, char ** const matches)
{
	size_t max;

	/* Find maximum completion length. */
	max = 0;
	for (char **m = matches; *m != NULL; m++) {
		size_t const size = strlen(*m);

		if (max < size) {
			max = size;
		}
	}

	/* Allow for a space between words. */
	size_t const num_cols = minirl_terminal_width(minirl) / (max + 1);

	/* Print out a table of completions. */
	fprintf(minirl->out.stream, "\r\n");
	for (char **m = matches; *m != NULL;) {
		for (size_t c = 0; c < num_cols && *m; c++, m++) {
			fprintf(minirl->out.stream, "%-*s ", (int)max, *m);
		}
		fprintf(minirl->out.stream, "\r\n");
	}
}

bool
minirl_complete(
	minirl_st * const minirl,
	unsigned const start,
	char ** const matches,
	bool const allow_prefix)
{
	bool did_some_completion;
	bool prefix;
	bool res = false;

	if (matches == NULL || matches[0] == NULL) {
		return false;
	}

	/* Identify a common prefix. */
	unsigned len = strlen(matches[0]);
	prefix = true;
	for (size_t i = 1; matches[i] != NULL; i++) {
		unsigned common;

		for (common = 0; common < len; common++) {
			if (matches[0][common] != matches[i][common]) {
				break;
			}
		}
		if (len != common) {
			len = common;
			prefix = !matches[i][len];
		}
	}

	unsigned start_from = 0;
	unsigned const end = minirl_point_get(minirl);

	/*
	 * The portion of the match from the start to the cursor position
	 * matches so it's only necessary to insert from that position now.
	 * Exclude the characters that already match.
	 */
	start_from = end - start;
	len -= end - start;

	/* Insert the rest of the common prefix */

	if (len > 0) {
		if (!minirl_text_len_insert(minirl, &matches[0][start_from], len)) {
			return false;
		}
		did_some_completion = true;
	} else {
		did_some_completion = false;
	}

	/* Is there only one completion? */
	if (matches[1] == NULL) {
		res = true;
		goto done;
	}

	/* Is the prefix valid? */
	if (prefix && allow_prefix) {
		res = true;
		goto done;
	}

	/* Display matches if no progress was made */
	if (!did_some_completion) {
		/*
		 * line state needs to be reset so that the cursor isn't moved
		 * around during the terminal refresh.
		 */
		minirl_display_matches(minirl, matches);
		minirl_line_state_reset(minirl);
	}

done:
	return res;
}

struct minirl_st *
minirl_new(FILE * const in_stream, FILE * const out_stream)
{
	minirl_st *minirl = calloc(1, sizeof *minirl);

	if (minirl == NULL) {
		goto done;
	}

	minirl->keymap = minirl_keymap_new();
	if (minirl->keymap == NULL) {
		free(minirl);
		minirl = NULL;

		goto done;
	}

	for (size_t i = 32; i < 256; i++) {
		minirl_bind_key(minirl, i, default_handler, NULL);
	}

	minirl_bind_key(minirl, CTRL('a'), home_handler, NULL);
	minirl_bind_key(minirl, CTRL('b'), left_handler, NULL);
	minirl_bind_key(minirl, CTRL('c'), ctrl_c_handler, NULL);
	minirl_bind_key(minirl, CTRL('d'), ctrl_d_handler, NULL);
	minirl_bind_key(minirl, CTRL('e'), end_handler, NULL);
	minirl_bind_key(minirl, CTRL('f'), right_handler, NULL);
	minirl_bind_key(minirl, CTRL('h'), backspace_handler, NULL);
	minirl_bind_key(minirl, CTRL('k'), ctrl_k_handler, NULL);
	minirl_bind_key(minirl, CTRL('l'), ctrl_l_handler, NULL);
	minirl_bind_key(minirl, CTRL('n'), down_handler, NULL);
	minirl_bind_key(minirl, CTRL('p'), up_handler, NULL);
	minirl_bind_key(minirl, CTRL('t'), ctrl_t_handler, NULL);
	minirl_bind_key(minirl, CTRL('u'), ctrl_u_handler, NULL);
	minirl_bind_key(minirl, CTRL('w'), ctrl_w_handler, NULL);

	minirl_bind_key(minirl, ENTER, enter_handler, NULL);
	minirl_bind_key(minirl, BACKSPACE, backspace_handler, NULL);

	minirl_bind_key_sequence(minirl, ESCAPESTR "[2~", null_handler, NULL); /* Insert. */
	minirl_bind_key_sequence(minirl, ESCAPESTR "[3~", delete_handler, NULL);
	minirl_bind_key_sequence(minirl, ESCAPESTR "[A", up_handler, NULL);
	minirl_bind_key_sequence(minirl, ESCAPESTR "[B", down_handler, NULL);
	minirl_bind_key_sequence(minirl, ESCAPESTR "[C", right_handler, NULL);
	minirl_bind_key_sequence(minirl, ESCAPESTR "[D", left_handler, NULL);
	minirl_bind_key_sequence(minirl, ESCAPESTR "[H", home_handler, NULL);
	minirl_bind_key_sequence(minirl, ESCAPESTR "[F", end_handler, NULL);
	minirl_bind_key_sequence(minirl, ESCAPESTR "OH", home_handler, NULL);
	minirl_bind_key_sequence(minirl, ESCAPESTR "OF", end_handler, NULL);


	minirl->in.stream = in_stream;
	minirl->in.fd = fileno(in_stream);
	minirl->is_a_tty = isatty(minirl->in.fd);

	minirl->out.stream = out_stream;
	minirl->out.fd = fileno(out_stream);

	minirl->history.max_len = MINIRL_DEFAULT_HISTORY_MAX_LEN;

done:
	return minirl;
}

void
minirl_delete(minirl_st * const minirl)
{
	if (minirl == NULL) {
		goto done;
	}

	if (minirl->in_raw_mode) {
		disable_raw_mode(minirl, minirl->in.fd);
	}
	minirl_keymap_free(minirl->keymap);
	minirl->keymap = NULL;

	free_history(minirl);

	free(minirl);

done:
	return;
}

void
minirl_is_done(minirl_st * const minirl)
{
	minirl_state_is_done(&minirl->state);
}

void
minirl_requires_refresh(minirl_st * const minirl)
{
	minirl_state_refresh_required(&minirl->state);
}

void
minirl_requires_cursor_refresh(minirl_st * const minirl)
{
	minirl_state_cursor_refresh_required(&minirl->state);
}

void
minirl_had_error(minirl_st * const minirl)
{
	minirl_state_had_error(&minirl->state);
}

void
minirl_line_state_reset(minirl_st * const minirl)
{
	minirl_state_reset_line_state(&minirl->state);
}

void
minirl_echo_enable(minirl_st * const minirl)
{
	minirl->options.echo.disable = false;
}

void
minirl_echo_disable(minirl_st * const minirl, char const echo_char)
{
	minirl->options.echo.disable = true;
	minirl->options.echo.ch = echo_char;
}
