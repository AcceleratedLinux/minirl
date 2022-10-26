#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct minirl_st minirl_st;

typedef bool (*minirl_key_binding_handler_cb)(
	minirl_st *minirl, char const *key, void *user_ctx);


/*
 * Get the current pointer to the line buffer.
 * Note that any additions made to the line by key handler callbacks may result
 * in this pointer becoming invalid, so it should be re-obtained after any
 * additions.
 */
char *
minirl_line_get(minirl_st *minirl);

/* Get the current editing position in the line. */
size_t
minirl_point_get(minirl_st *minirl);

/* Get the position of the end of the line. */
size_t
minirl_end_get(minirl_st *minirl);

/* Set the current editing position in the line. Must be <= the line end. */
void
minirl_point_set(minirl_st *minirl, size_t new_point);

/* Delete text between start and end from the line. */
void
minirl_text_delete(minirl_st *minirl, size_t start, size_t end);

/*
 * Insert 'length' characters pointed to by 'text' at the current editing
 * position.
 */
bool
minirl_text_len_insert(minirl_st *minirl, char const *text, size_t length);

/* Insert the string pointed to by 'text' at the current editing position. */
bool
minirl_text_insert(minirl_st *minirl, char const *text);

/* Get the current terminal width. */
int
minirl_terminal_width(minirl_st *minirl);

/*
 * Given a list of possible completions, attempt to complete the current word
 * as much as possible.
 * If unable to make any progress with completing the word then display the
 * list of completions above the edit line.
 */
bool minirl_complete(
	minirl_st *minirl,
	unsigned start,
	char **matches,
	bool allow_prefix);

/* Display the set of matches on the terminal. */
void
minirl_display_matches(minirl_st *minirl, char **matches);

/* Bind a key handler to a key. */
bool
minirl_bind_key(
	minirl_st *minirl,
	uint8_t key,
	minirl_key_binding_handler_cb handler,
	void *user_ctx);

/*
 * Bind a key sequence to a key. Useful for binding CTRL and escape sequences
 * to a handler.
 */
bool
minirl_bind_key_sequence(
	minirl_st *minirl,
	const char *seq,
	minirl_key_binding_handler_cb handler,
	void *context);

/*
 * The main 'readline' function.
 * 'prompt' is displayed at the start of the line.
 */
char *
minirl_readline(minirl_st *minirl, char const *prompt);

/* Free a line returned by minirl_readline. */
void
minirl_line_free(void *ptr);

/* Add a line to the history. Access the history using the up/down arrows. */
int
minirl_history_add(minirl_st *minirl, char const *line);

/*
 * Set the maximum length of the history.
 * Setting to 0 indicates there should be no history.
 * Defaults to 100.
 */
int
minirl_history_set_max_len(minirl_st *minirl, size_t len);

/* Clear the screen. */
void
minirl_screen_clear(minirl_st *minirl);

/*
 * Force minirl to think the input is a TTY. Useful in cases where key handlers
 * have been assigned to particular ascii keys (e.g. enter, '"') that might
 * affect the input line.
 */
void
minirl_force_isatty(minirl_st *minirl);

/*
 * Create a new minirl instance.
 * This should be passed to minirl_readline(), and passed to other API
 * functions. It is passed to key handler callbacks.
 */
struct minirl_st *
minirl_new(FILE *in_stream, FILE *out_stream);

/* Free a minirl instance created using minirl_new(). */
void
minirl_delete(minirl_st *minirl);

/*
 * Print text to the terminal. Should normally require a call to
 * minirl_line_state_reset() so that the edit line is printed afresh.
 */
int
minirl_printf(minirl_st *minirl, char const * fmt, ...);

/*
 * Called by a key handler callback to indicate that line editing has completed.
 */
void
minirl_is_done(minirl_st *minirl);

/*
 * Called by a key handler callback to indicate that the edit line needs to be
 * refreshed.
 */
void
minirl_requires_refresh(minirl_st *minirl);

/*
 * Called by a key handler callback to indicate that the cursor position needs
 * to be updated.
 */
void
minirl_requires_cursor_refresh(minirl_st *minirl);

/*
 * Called by a key handler callback to indicate that an error has occurred.
 * The readline function will return NULL in this case.
 */
void
minirl_had_error(minirl_st *minirl);

/*
 * Reset the line editing state. Useful in cases where a callback has output
 * text that minirl isn't aware of that would require the edit line and its
 * perception of the cursor location to be recalculated.
 */
void
minirl_line_state_reset(minirl_st *minirl);

/*
 * Enable echoing of input characters to the terminal.
 * This is enabled by default.
 */
void
minirl_echo_enable(minirl_st *minirl);

/*
 * Disable the default character echo and write the specified 'echo_char'
 * instead. If 'echo_char' is '\0' then nothing is written.
 * Useful for inputting private information like passwords.
 */
void
minirl_echo_disable(minirl_st *minirl, char echo_char);

#ifdef __cplusplus
}
#endif

