#pragma once

#define KEYMAP_SIZE 256

typedef struct minirl_keymap_st minirl_keymap_st;
typedef struct key_handler_st key_handler_st;

struct key_handler_st {
	minirl_key_binding_handler_cb handler;
	minirl_keymap_st *keymap;
	void *user_ctx;
};

struct minirl_keymap_st {
	key_handler_st keys[KEYMAP_SIZE];
};

minirl_keymap_st *
minirl_keymap_new(void);

void
minirl_keymap_free(minirl_keymap_st *keymap);

