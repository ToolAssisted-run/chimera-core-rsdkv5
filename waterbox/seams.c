/* seams.c - what the engine reaches through SDL and the C library, answered
 * by the core.
 *
 * Linked with -Wl,--wrap=<name> for each __wrap_ below, in both builds
 * (sources.mk). SDL is the real SDL 2, built from source in its minimal
 * configuration (the dummy video driver, no threads, no timers, no CPU
 * feature dispatch); the engine uses it as it is, but for:
 *
 *   - time: the engine paces itself with SDL's performance counter, the
 *     machine's clock, which only a step moves (rsdk-driver.cpp). time() is
 *     asked for the random seed and for the dates the game writes into its
 *     records (the time attack table, the replays): it answers the project's
 *     randomSeed setting, and localtime() reads it as UTC, in both builds,
 *     whatever the host's time zone.
 *   - threads: the engine loads a music track on a thread of its own
 *     (SDL_CreateThread); there are no threads, so the load runs at once,
 *     where the engine asked for it.
 *   - rand and srand: musl's generator, in both builds (glibc's rand is
 *     another sequence), its state in guest memory.
 *   - SDL_Init: everything a build without threads cannot give is left out;
 *     what is left is the video driver (dummy) and events.
 *   - the sound device: never opened. The engine's mixer is called by the
 *     driver for exactly one frame's samples at the end of each frame.
 *   - the renderer: SDL's software one, whatever the engine asks for; and
 *     presenting: the driver reads the engine's own screens, so SDL drawing
 *     them into a window nobody sees is skipped.
 */
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "rsdk-driver.h"

/* rsdk-driver.cpp */
uint64_t rsdkdrv_clock_us(void);
long rsdkdrv_random_seed(void);
void rsdkdrv_audio_open(SDL_AudioCallback cb, void *user);

/* ----------------------------------------------------------------- time */

Uint64 __wrap_SDL_GetPerformanceCounter(void) { return rsdkdrv_clock_us(); }
Uint64 __wrap_SDL_GetPerformanceFrequency(void) { return 1000000; }
Uint32 __wrap_SDL_GetTicks(void) { return (Uint32)(rsdkdrv_clock_us() / 1000); }
Uint64 __wrap_SDL_GetTicks64(void) { return rsdkdrv_clock_us() / 1000; }
void __wrap_SDL_Delay(Uint32 ms) { (void)ms; }

time_t __wrap_time(time_t *t)
{
	const time_t v = (time_t)rsdkdrv_random_seed();
	if (t) *t = v;
	return v;
}

struct tm *__wrap_localtime(const time_t *t) { return gmtime(t); }

static uint64_t g_rand_seed;
void __wrap_srand(unsigned s) { g_rand_seed = s - 1; }
int __wrap_rand(void)
{
	g_rand_seed = 6364136223846793005ULL * g_rand_seed + 1;
	return (int)(g_rand_seed >> 33);
}

/* ------------------------------------------------------------------- init */

int __real_SDL_Init(Uint32 flags);
int __real_SDL_InitSubSystem(Uint32 flags);
static const Uint32 k_unavailable = SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK |
	SDL_INIT_HAPTIC | SDL_INIT_GAMECONTROLLER | SDL_INIT_SENSOR;

int __wrap_SDL_Init(Uint32 flags)
{
	/* the dummy video driver, over whatever the environment says, so a host's
	 * SDL_VIDEODRIVER never reaches the native reference either */
	SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, "dummy", SDL_HINT_OVERRIDE);
	return __real_SDL_Init(flags & ~k_unavailable);
}

int __wrap_SDL_InitSubSystem(Uint32 flags)
{
	if (flags & k_unavailable) return SDL_SetError("not in this machine");
	return __real_SDL_InitSubSystem(flags);
}

/* ------------------------------------------------------------------ audio */

/* the engine asks for 44100 Hz, 32-bit float, stereo (SDL2AudioDevice.cpp),
 * which the driver converts to the 16 bits it hands out */
SDL_AudioDeviceID __wrap_SDL_OpenAudioDevice(const char *device, int iscapture, const SDL_AudioSpec *desired,
                                             SDL_AudioSpec *obtained, int allowed_changes)
{
	(void)device; (void)iscapture; (void)allowed_changes;
	if (!desired || desired->freq != RSDK_AUDIO_RATE || desired->format != AUDIO_F32SYS || desired->channels != 2 || !desired->callback)
	{
		SDL_SetError("the core's sound device is 44100 Hz, float, stereo");
		return 0;
	}
	if (obtained)
	{
		*obtained = *desired;
		obtained->silence = 0;
	}
	rsdkdrv_audio_open(desired->callback, desired->userdata);
	return 1;
}

int __wrap_SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
	return __wrap_SDL_OpenAudioDevice(NULL, 0, desired, obtained, 0) ? 0 : -1;
}

void __wrap_SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on) { (void)dev; (void)pause_on; }
void __wrap_SDL_PauseAudio(int pause_on) { (void)pause_on; }
void __wrap_SDL_CloseAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
void __wrap_SDL_CloseAudio(void) {}
void __wrap_SDL_LockAudio(void) {}
void __wrap_SDL_UnlockAudio(void) {}
void __wrap_SDL_LockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
void __wrap_SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }

/* --------------------------------------------------------------- threads */

SDL_Thread *__wrap_SDL_CreateThread(SDL_ThreadFunction fn, const char *name, void *data)
{
	(void)name;
	if (fn) fn(data);
	return NULL;
}

/* ------------------------------------------------------------ the screen */

/* the engine asks for an accelerated renderer, and the dummy video driver's
 * only one is SDL's software renderer: it is given that */
SDL_Renderer *__real_SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags);
SDL_Renderer *__wrap_SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
	(void)flags;
	return __real_SDL_CreateRenderer(window, index, SDL_RENDERER_SOFTWARE);
}

void __wrap_SDL_RenderPresent(SDL_Renderer *renderer) { (void)renderer; }
