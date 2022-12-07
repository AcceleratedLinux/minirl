# Minirl

A heavily modified version of linenoise that can be found here...

    https://github.com/antirez/linenoise

with portions of code included from here...

    https://github.com/philipc/tinyrl

The changes to the linenoise code are extensive enough that this project
can be considered a distinct piece of work. 
It is not compatible with linenoise.

* History handling.
* Completion.
* Only uses a subset of VT100 escapes (ANSI.SYS compatible).

# The API

Minirl is very easy to use, and reading the example shipped with the
library should get you up to speed ASAP. Here is a list of API calls
and how to use them.

## Create a context

    struct minirl_st * minirl_new(FILE * in_stream, FILE * out_stream);

Create a context that contains all of the state information relating to minirl.
e.g.
    minirl_st * minirl = minirl_new(stdin, output_fp);

## Free a context

    minirl_delete(minirl_st * minirl);

Delete the minirl context. Call this once you are done using the context.

## Read line input

    char *minirl(minirl_st * minirl, const char *prompt);

This is the main Minirl call: It shows the user a prompt with line editing
and history capabilities. The prompt you specify is used as a prompt, that is,
it will be printed to the left of the cursor. The library returns a buffer
with the line composed by the user, or NULL on end of file or when there
is an out of memory condition.

When instead the standard input is not a tty, which happens every time you 
redirect a file
to a program, or use it in an Unix pipeline, there are no limits to the
length of the line that can be returned.

The returned line should be freed with the `free()` standard system call.
However sometimes it could happen that your program uses a different dynamic
allocation library, so you may also used `minirl_free` to make sure the
line is freed with the same allocator it was created.

The canonical loop used by a program using Minirl will be something like
this:

    while((line = minirl("prompt> ")) != NULL) {
        printf("You wrote: %s\n", line);
        minirl_free(line); /* Or just free(line) if you use libc malloc. */
    }

## History

Minirl supports history, so that the user does not have to re-type
again and again the same things, but can use the down and up arrows in order
to search and re-edit already inserted lines of text.

The followings are the history API calls:

    int minirl_history_add(const char *line);
    int minirl_history_set_max_len(int len);

Use `minirl_history_add` every time you want to add a new element
to the top of the history (it will be the first the user will see when
using the up arrow).

Note that for history to work, you have to set a length for the history
(which is zero by default, so history will be disabled if you don't set
a proper one). This is accomplished using the `minirl_history_set_max_len`
function.

## Completion

TODO: Document completion.

## Screen handling

Sometimes you may want to clear the screen as a result of something the
user typed. You can do this by calling the following function:

    void minirl_clear_screen(minirl_st * minirl);

## Related projects

https://github.com/antirez/linenoise
https://github.com/philipc/tinyrl

