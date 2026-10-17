/* rsdk-driver.h - Retro Engine v5 (the RSDKv5 decompilation) with Sonic
 * Mania's logic (the Sonic Mania decompilation) as a machine that is
 * stepped.
 *
 * The same driver is compiled for the miniBox guest and for the native
 * reference (run-native); wbx-entry.c puts the guest ABI on top of it.
 *
 * Wire format (waterbox.config "input.buttons", same order): two players'
 * pads, each the engine's twelve inputs - the directions, A, B, C, X, Y, Z,
 * Start and Select. Sonic Mania jumps with A, B or C, opens its menus with
 * Start, and backs out with B; the second player plays Tails or the
 * competition mode. The engine keeps a keyboard's state as a bit mask a
 * player (SKU::keyState); the buttons are that mask.
 */
#ifndef RSDK_DRIVER_H
#define RSDK_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum RsdkButton
{
	RSDK_BTN_UP,
	RSDK_BTN_DOWN,
	RSDK_BTN_LEFT,
	RSDK_BTN_RIGHT,
	RSDK_BTN_A,
	RSDK_BTN_B,
	RSDK_BTN_C,
	RSDK_BTN_X,
	RSDK_BTN_Y,
	RSDK_BTN_Z,
	RSDK_BTN_START,
	RSDK_BTN_SELECT,
	RSDK_BTN_PER_PLAYER
};
/* players 1 and 2 always; 3 and 4 are live only with the Players setting
 * at 4 (chimera#302), so a two-player project carries the first 24 */
#define RSDK_PLAYERS 4
#define RSDK_BTN_COUNT (RSDK_BTN_PER_PLAYER * RSDK_PLAYERS)
/* the button after them: the PC keyboard's Escape, which opens the engine's
 * developer menu when the Developer Menu setting is on (it is live only then) */
#define RSDK_BTN_DEV_MENU RSDK_BTN_COUNT

/* the picture: the engine's window, 240 lines and as wide as the
 * screenWidth setting (424 by default) */
#define RSDK_VIDEO_HEIGHT 240
#define RSDK_VIDEO_MAX_WIDTH 424
#define RSDK_VIDEO_MAX_HEIGHT 240

/* stereo, 44100 frames a second (the engine's mixing rate); one step is one
 * sixtieth of a second, 735 frames */
#define RSDK_AUDIO_RATE 44100
#define RSDK_FRAME_RATE 60
#define RSDK_AUDIO_SAMPLES (RSDK_AUDIO_RATE / RSDK_FRAME_RATE)

/* 0 on failure, with the reason in err */
int rsdkdrv_init(char *err, int errsize);
void rsdkdrv_set_button(int index, int down);
int rsdkdrv_dev_menu(void);   /* the Developer Menu setting */
int rsdkdrv_players(void);    /* the Players setting: 2 or 4 */
/* runs the engine through its next frame */
void rsdkdrv_frame(int render);
const uint32_t *rsdkdrv_video(int *w, int *h);
const int16_t *rsdkdrv_audio(int *samples);   /* stereo frames, left then right */
int rsdkdrv_input_was_read(void);
uint64_t rsdkdrv_frames(void);

/* game-state: the property block, the raw domains, the table */
int rsdkdrv_domain_count(void);
const char *rsdkdrv_domain_name(int i);
uint8_t *rsdkdrv_domain_ptr(int i);
int64_t rsdkdrv_domain_size(int i);
int rsdkdrv_domain_writable(int i);
const char *rsdkdrv_game_properties(void);

/* rsdk-files.c: the game's data, the settings file the engine reads, its
 * saves */
struct rsdk_settings
{
	int language;        /* LANGUAGE_EN .. */
	int screen_width;    /* 320 .. 424 */
	int region;          /* -1: the data's own */
	int plus;            /* the Plus content: off by default (the licence) */
	long random_seed;
	int dev_menu;        /* devMenu */
	int players;         /* 2 or 4: how many players' controls are live */
	char username[32];   /* username, the name the game shows for the player */
};
void rsdk_settings_read(struct rsdk_settings *s);
int rsdk_files_load(const struct rsdk_settings *s, char *err, int errsize);

/* The files the game wrote (SaveData.bin and the rest; not Settings.ini, not
 * the engine's log.txt) for
 * Emulator > Export Save Data: a snapshot of those that hold anything, then
 * each one's name and bytes. */
int rsdk_saves_count(void);
const char *rsdk_saves_name(int index);
int64_t rsdk_saves_size(int index);
const uint8_t *rsdk_saves_data(int index);

#ifdef __cplusplus
}
#endif

#endif
