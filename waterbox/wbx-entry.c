/* wbx-entry.c - the chimera guest ABI over rsdk-driver.
 *
 * Compiles identically for the guest (miniBox emulibc) and for the native
 * reference (native-shim/emulibc.h), which is what makes the equivalence gate
 * a real proof: the same driver, the same exports, one in the sandbox and one
 * out of it.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <emulibc.h>

#include "rsdk-driver.h"

static char g_load_error[1024];

/* buttons set one at a time (SetButton) and the packed mask of FrameAdvance:
 * a step sees their union */
static uint8_t g_set_buttons[RSDK_BTN_COUNT];

/* Turbo: the picture is not converted. Only the conversion - the engine draws
 * its screen whatever happens, because what it draws is part of the game. */
ECL_INVISIBLE int chimera_render_enabled = 1;

ECL_EXPORT const char *GetLoadError(void) { return g_load_error; }

ECL_EXPORT int Init(void)
{
	g_load_error[0] = '\0';
	return rsdkdrv_init(g_load_error, (int)sizeof g_load_error);
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < RSDK_BTN_COUNT) g_set_buttons[index] = state ? 1 : 0;
}

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	for (int i = 0; i < RSDK_BTN_COUNT; i++)
		rsdkdrv_set_button(i, g_set_buttons[i] | (int)((packed >> i) & 1));
	rsdkdrv_frame(chimera_render_enabled);
}

ECL_EXPORT void SetRenderingEnabled(int on) { chimera_render_enabled = on != 0; }

/* the live size: the screen at its width, or twice that on a frame drawn at
 * high resolution (waterbox.config gives the capacity) */
ECL_EXPORT uint32_t *GetVideoBgra(void)
{
	int w, h;
	return (uint32_t *)rsdkdrv_video(&w, &h);
}
ECL_EXPORT int GetVideoWidth(void)
{
	int w, h;
	rsdkdrv_video(&w, &h);
	return w;
}
ECL_EXPORT int GetVideoHeight(void)
{
	int w, h;
	rsdkdrv_video(&w, &h);
	return h;
}
/* square pixels: 424x240 is a 16:9 screen, 320x240 a 4:3 one */
ECL_EXPORT int GetDisplayAspectX(void)
{
	int w, h;
	rsdkdrv_video(&w, &h);
	return w;
}
ECL_EXPORT int GetDisplayAspectY(void)
{
	int w, h;
	rsdkdrv_video(&w, &h);
	return h;
}

ECL_EXPORT int16_t *GetAudio(void)
{
	int n;
	return (int16_t *)rsdkdrv_audio(&n);
}

ECL_EXPORT int GetAudioSampleCount(void)
{
	int n;
	rsdkdrv_audio(&n);
	return n;
}

ECL_EXPORT int GetVsyncNumerator(void) { return RSDK_FRAME_RATE; }
ECL_EXPORT int GetVsyncDenominator(void) { return 1; }

ECL_EXPORT int InputWasRead(void) { return rsdkdrv_input_was_read(); }

/* memory domains: Game State (the property block), the players, the objects,
 * the game's global variables */
ECL_EXPORT int GetMemoryDomainCount(void) { return rsdkdrv_domain_count(); }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return rsdkdrv_domain_name(i); }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return rsdkdrv_domain_ptr(i); }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return rsdkdrv_domain_size(i); }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return rsdkdrv_domain_writable(i); }

/* chimera docs/game-cores.md: the property table */
ECL_EXPORT const char *GetGameProperties(void) { return rsdkdrv_game_properties(); }

/* the machine's own clock, in frames, for harnesses that compare machines */
ECL_EXPORT uint64_t GetCycleCount(void) { return rsdkdrv_frames(); }
