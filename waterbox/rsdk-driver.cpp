/* rsdk-driver.cpp - the Retro Engine v5 decompilation, with Sonic Mania's
 * logic linked in, as a machine that is stepped.
 *
 * The engine's own main loop (RSDK::RunRetroEngine) reads events, runs the
 * game for a frame, draws it, and waits for the next sixtieth of a second.
 * The core runs the start-up, then the same pass without the wait, once a
 * step (InitRetroEngine and RunRetroEngineFrame, patches/0001):
 *
 *   - A step is one frame of the engine: one sixtieth of a second of the
 *     machine's time. The machine's clock is the number of frames run, in
 *     microseconds (seams.c answers SDL's counters from it).
 *   - The sound is the engine's own mixer, run once a frame for exactly 735
 *     stereo samples, the frame's share of 44100 a second - by the driver,
 *     in place of the sound device's thread - in the engine's floats, made
 *     16-bit here.
 *   - The controls are the engine's keyboard: each player's buttons are the
 *     bit mask the engine keeps for a keyboard (SKU::keyState), set before
 *     the frame, where key presses would have set it.
 *   - The picture is what the engine's SDL device draws into its window
 *     (240 lines, as wide as the screen setting), with SDL's software
 *     renderer, read back after the frame.
 *
 * The engine is upstream's, built from source with one patch, its software
 * renderer, mixer and video decoder, and no mod loader (docs/PLAN.md).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "RSDK/Core/RetroEngine.hpp"

/* Sonic Mania's entry point, as upstream's main.cpp declares it when the
 * game's logic is linked in (RETRO_STANDALONE 0) */
#define EngineInfo RSDK::EngineInfo
#include <GameMain.h>
#undef EngineInfo

#include "rsdk-driver.h"

using namespace RSDK;

namespace {

struct Driver
{
	struct rsdk_settings settings;
	int init_done;

	uint8_t held[RSDK_BTN_COUNT];

	uint64_t frames;
	int render;
	int read;

	SDL_AudioCallback audio_cb;
	void *audio_user;
	float mix[RSDK_AUDIO_SAMPLES * 2];
	int16_t audio[RSDK_AUDIO_SAMPLES * 2];
	int audio_frames;

	uint32_t video[RSDK_VIDEO_MAX_WIDTH * RSDK_VIDEO_MAX_HEIGHT];
	int video_w, video_h;
};

Driver g;

/* the engine's mask for each button (Input.hpp KeyMasks) */
const int32 k_masks[RSDK_BTN_PER_PLAYER] = {
	KEYMASK_UP, KEYMASK_DOWN, KEYMASK_LEFT, KEYMASK_RIGHT, KEYMASK_A, KEYMASK_B,
	KEYMASK_C, KEYMASK_X, KEYMASK_Y, KEYMASK_Z, KEYMASK_START, KEYMASK_SELECT,
};

/* the picture, as the engine's SDL device draws it into its window - the
 * screens (two, one above the other, in the competition mode), a video, the
 * game's idle dimming - read back from SDL's software renderer after the
 * frame (the size kept whether read or not) */
void read_screen(int render)
{
	int w = 0, h = 0;
	if (!RenderDevice::renderer || SDL_GetRendererOutputSize(RenderDevice::renderer, &w, &h) != 0) return;
	if (w <= 0 || h <= 0 || w > RSDK_VIDEO_MAX_WIDTH || h > RSDK_VIDEO_MAX_HEIGHT) return;
	g.video_w = w;
	g.video_h = h;
	if (!render) return;
	SDL_RenderReadPixels(RenderDevice::renderer, NULL, SDL_PIXELFORMAT_ARGB8888, g.video, w * 4);
}

} // namespace

/* ------------------------------------------------------------ seams.c */

extern "C" uint64_t rsdkdrv_clock_us(void) { return g.frames * 1000000ull / RSDK_FRAME_RATE; }
extern "C" long rsdkdrv_random_seed(void) { return g.settings.random_seed; }
extern "C" void rsdkdrv_audio_open(SDL_AudioCallback cb, void *user)
{
	g.audio_cb = cb;
	g.audio_user = user;
}

/* ------------------------------------------------------------ the exports */

extern "C" {

void gamestate_from_game(void);
void gamestate_to_game(void);

int rsdkdrv_init(char *err, int errsize)
{
	for (int i = 0; i < RSDK_VIDEO_MAX_WIDTH * RSDK_VIDEO_MAX_HEIGHT; i++) g.video[i] = 0xFF000000u;
	rsdk_settings_read(&g.settings);
	if (g.settings.screen_width < 320 || g.settings.screen_width > 424)
	{
		snprintf(err, (size_t)errsize, "the setting screenWidth is %d; it goes from 320 to 424", g.settings.screen_width);
		return 0;
	}
	g.video_w = g.settings.screen_width;
	g.video_h = RSDK_VIDEO_HEIGHT;
	if (!rsdk_files_load(&g.settings, err, errsize)) return 0;

	g.render = 1;
	/* upstream's RSDK_main: the game's logic handed to the engine, the
	 * engine's function table made, then the engine started */
	char arg0[] = "chimera";
	char *argv[] = { arg0, NULL };
	linkGameLogic = (LogicLinkHandle)LinkGameLogicDLL;
	InitCoreAPI();
	InitRetroEngine(1, argv);
	if (!RenderDevice::isRunning || !engine.initialized)
	{
		snprintf(err, (size_t)errsize, "the engine did not start: the data file is not Sonic Mania's, or not a Retro Engine v5 data file");
		return 0;
	}
	if (!g.audio_cb)
	{
		snprintf(err, (size_t)errsize, "the engine did not open its sound device");
		return 0;
	}
	g.init_done = 1;
	gamestate_from_game();
	return 1;
}

void rsdkdrv_set_button(int index, int down)
{
	if (index >= 0 && index < RSDK_BTN_COUNT) g.held[index] = down ? 1 : 0;
}

void rsdkdrv_frame(int render)
{
	g.render = render;
	g.audio_frames = 0;
	if (!RenderDevice::isRunning) return;
	for (int p = 0; p < RSDK_PLAYERS; p++)
	{
		int32 mask = 0;
		for (int b = 0; b < RSDK_BTN_PER_PLAYER; b++)
			if (g.held[p * RSDK_BTN_PER_PLAYER + b]) mask |= k_masks[b];
		SKU::keyState[p] = mask;
	}
	/* a frame reads the controls unless it is one in which the engine loads
	 * a scene (ENGINESTATE_LOAD: files, objects' startup, no update) */
	g.read = sceneInfo.state != ENGINESTATE_LOAD;
	gamestate_to_game();
	RunRetroEngineFrame();
	g.frames++;
	memset(g.mix, 0, sizeof g.mix);
	g.audio_cb(g.audio_user, (Uint8 *)g.mix, (int)sizeof g.mix);
	for (int i = 0; i < RSDK_AUDIO_SAMPLES * 2; i++)
	{
		float v = g.mix[i] * 32767.0f;
		if (v > 32767.0f) v = 32767.0f;
		if (v < -32768.0f) v = -32768.0f;
		g.audio[i] = (int16_t)v;
	}
	g.audio_frames = RSDK_AUDIO_SAMPLES;
	read_screen(render);
	gamestate_from_game();
}

const uint32_t *rsdkdrv_video(int *w, int *h)
{
	*w = g.video_w;
	*h = g.video_h;
	return g.video;
}

const int16_t *rsdkdrv_audio(int *samples)
{
	*samples = g.audio_frames;
	return g.audio;
}

/* whether the frame just run read the controls: the engine reads them at
 * the start of every frame and hands them to the game, except in a frame
 * it loads a scene; a stopped engine reads nothing */
int rsdkdrv_input_was_read(void) { return RenderDevice::isRunning ? g.read : 0; }

uint64_t rsdkdrv_frames(void) { return g.frames; }

} // extern "C"
