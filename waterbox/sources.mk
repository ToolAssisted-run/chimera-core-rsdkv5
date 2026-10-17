# sources.mk - what native.mk and guest.mk both build: the same files with the
# same defines, so the native reference and the sandboxed core are the same
# program. Included, not run.

ROOT   := ..
RSDK   := $(ROOT)/extern/RSDKv5
MANIA  := $(ROOT)/extern/SonicMania
SDL    := $(ROOT)/extern/SDL
OGG    := $(ROOT)/extern/ogg
THEORA := $(ROOT)/extern/theora
MB     ?= $(or $(MINIBOX_DIR),$(HOME)/chimera/extern/chimera-common-minibox)
INC_DIR := $(ROOT)/build/include

# ---- SDL 2 (the submodule extern/SDL, tag release-2.32.10), built the way
# its own Makefile.minimal builds it: the platform-neutral configuration
# (include/SDL_config_minimal.h - dummy video and audio drivers, no threads,
# no timers, no joysticks, no dynamic loading) and the portable C of every
# subsystem the engine touches. On top of that, as the SDLPoP core builds it:
#   SDL_CPUINFO_DISABLED  SDL asks the CPU nothing, so every blitter and
#                         audio converter is the C one on every host
#   DYNAPI_NEEDS_DLOPEN   (without HAVE_DLOPEN) turns off SDL's dynamic API,
#                         which would otherwise let an environment variable
#                         load another SDL into the process
#   HAVE_MALLOC, HAVE_STDIO_H
#                         SDL allocates and opens files with the C library,
#                         like the engine (so its opens go through rsdk-files.c)
SDL_DIRS := src src/atomic src/audio src/audio/dummy src/cpuinfo src/events src/file \
	src/haptic src/haptic/dummy src/hidapi src/joystick src/joystick/dummy src/loadso/dummy \
	src/power src/filesystem/dummy src/locale src/locale/dummy src/misc src/misc/dummy \
	src/render src/render/software src/sensor src/sensor/dummy src/stdlib src/libm \
	src/thread src/thread/generic src/timer src/timer/dummy src/video src/video/yuv2rgb \
	src/video/dummy
SDL_WARN := -w -Wno-error=incompatible-pointer-types
SDL_SRCS := $(sort $(foreach d,$(SDL_DIRS),$(wildcard $(SDL)/$(d)/*.c)))
SDL_DEFS := -DSDL_CPUINFO_DISABLED=1 -DDYNAPI_NEEDS_DLOPEN=1 -DHAVE_MALLOC=1 -DHAVE_STDLIB_H=1 -DHAVE_STDIO_H=1 -DHAVE_STRING_H=1

# the transcendental functions, which detmath.c answers in both builds (the
# engine's, the game's and the codecs' - the link wraps every object): not
# builtins, so the compiler neither folds them nor fuses a sin and a cos
MATH_FUNCS := sin cos tan acos atan atan2 exp log pow exp2 log2 log10 sinf cosf tanf acosf asinf atanf atan2f \
	expf logf powf exp2f log2f log10f
NO_BUILTIN_MATH := $(foreach f,$(MATH_FUNCS) sincos sincosf,-fno-builtin-$(f))

# ---- libogg (extern/ogg, v1.3.5) and libtheora (extern/theora, v1.1.1): the
# game's videos are Ogg Theora. libtheora's decoder files only, in plain C
# (no x86 assembly: the same code on every machine). The engine's music and
# sound effects are Ogg Vorbis, decoded by the stb_vorbis it carries.
OGG_SRCS := $(OGG)/src/bitwise.c $(OGG)/src/framing.c
THEORA_NAMES := apiwrapper bitpack decapiwrapper decinfo decode dequant fragment huffdec idct info internal quant state
THEORA_SRCS := $(addprefix $(THEORA)/lib/,$(addsuffix .c,$(THEORA_NAMES)))
# miniz (MIT, in the engine's dependencies/all) inflates the scenes and
# sprites the data file keeps compressed, and the saves the game writes
MINIZ_SRCS := $(RSDK)/dependencies/all/miniz/miniz.c
CODEC_INCS := -Icompat -I$(OGG)/include -I$(THEORA)/include

# ---- the engine: upstream's CMakeLists.txt RETRO_FILES but main.cpp (the core
# is the frontend) and tinyxml2 (the mod loader is off); miniz is with the
# codecs above. The backends (SDL 2's video, sound and controllers, the
# keyboard) are compiled inside Graphics/Drawing.cpp, Audio/Audio.cpp and
# Input/Input.cpp, as upstream has them.
RSDK_NAMES := Core/RetroEngine Core/Math Core/Reader Core/Link Core/ModAPI Dev/Debug Storage/Storage Storage/Text \
	Graphics/Drawing Graphics/Scene3D Graphics/Animation Graphics/Sprite Graphics/Palette Graphics/Video \
	Audio/Audio Input/Input Scene/Scene Scene/Collision Scene/Object Scene/Objects/DefaultObject \
	Scene/Objects/DevOutput User/Core/UserAchievements User/Core/UserCore User/Core/UserLeaderboards \
	User/Core/UserPresence User/Core/UserStats User/Core/UserStorage
RSDK_SRCS := $(addprefix $(RSDK)/RSDKv5/RSDK/,$(addsuffix .cpp,$(RSDK_NAMES))) \
	$(RSDK)/dependencies/all/iniparser/iniparser.cpp $(RSDK)/dependencies/all/iniparser/dictionary.cpp
# As upstream's CMake configures it, with:
#   RSDK_USE_SDL2=1          SDL 2 for the window, the sound and controllers
#                            (on Linux upstream's default is GLFW and OpenGL)
#   RETRO_REVISION=3         RSDKv5U, upstream's default
#   RETRO_USE_MOD_LOADER=0   no mods: the game is the data file's
#   RETRO_STANDALONE=0       the game's logic linked in (Sonic Mania, below),
#                            not loaded from a library
#   RSDK_AUTOBUILD=1         every DLC flag off, as the licence requires of a
#                            build given to anyone (RETRO_DISABLE_PLUS)
RSDK_DEFS := -DRSDK_USE_SDL2=1 -DRETRO_REVISION=3 -DRETRO_USE_MOD_LOADER=0 -DRETRO_MOD_LOADER_VER=2 \
	-DRETRO_STANDALONE=0 -DRSDK_AUTOBUILD=1 -DRETRO_DISABLE_LOG=0 -DRETRO_USE_ORIGINAL_CODE=0 \
	'-DRETRO_DEV_EXTRA="Chimera"'
RSDK_CXXFLAGS_COMMON := -std=gnu++17 -O2 $(NO_BUILTIN_MATH) $(RSDK_DEFS) -I$(INC_DIR) -I$(SDL)/include $(CODEC_INCS) \
	-I$(RSDK)/RSDKv5 -I$(RSDK)/dependencies/all -I$(RSDK)/dependencies/all/iniparser -I$(MANIA)/SonicMania

# ---- Sonic Mania's logic (the Sonic Mania decompilation, extern/SonicMania):
# Game.c and Objects/All.c, as upstream's CMakeLists.txt builds it without
# GAME_INCREMENTAL_BUILD, as the last Steam release (GAME_VERSION 6). Upstream
# turns off MSVC's merging of identical functions, because the game compares
# function addresses ("not drowning when suspended to a pull chain in HCZ");
# GCC's -O2 does the same merging (-fipa-icf), so it is off here too.
MANIA_SRCS := $(MANIA)/SonicMania/Game.c $(MANIA)/SonicMania/Objects/All.c
MANIA_DEFS := -DRETRO_REVISION=3 -DRETRO_USE_MOD_LOADER=0 -DRETRO_MOD_LOADER_VER=2 -DGAME_INCLUDE_EDITOR=1 \
	-DMANIA_PREPLUS=0 -DMANIA_FIRST_RELEASE=0 -DGAME_VERSION=6
MANIA_CFLAGS_COMMON := -std=gnu11 -O2 -fno-ipa-icf $(NO_BUILTIN_MATH) $(MANIA_DEFS) -I$(MANIA)/SonicMania \
	-I$(MANIA)/SonicMania/Objects

# ---- the core
CORE_C_NAMES := rsdk-files seams detmath wbx-entry
CORE_CXX_NAMES := rsdk-driver game-state
CORE_HDRS := rsdk-driver.h $(wildcard compat/*/*.h)
CORE_CFLAGS_COMMON := -std=gnu11 -O2 -I$(INC_DIR) -I$(SDL)/include
CORE_CXXFLAGS_COMMON := $(RSDK_CXXFLAGS_COMMON)

# ---- the calls the core answers itself
WRAPS := fopen fclose SDL_RWFromFile time localtime rand srand SDL_GetPerformanceCounter SDL_GetPerformanceFrequency \
	SDL_GetTicks SDL_GetTicks64 SDL_Delay SDL_Init SDL_InitSubSystem SDL_OpenAudioDevice SDL_OpenAudio \
	SDL_PauseAudioDevice SDL_PauseAudio SDL_CloseAudioDevice SDL_CloseAudio SDL_LockAudio SDL_UnlockAudio \
	SDL_LockAudioDevice SDL_UnlockAudioDevice SDL_CreateThread SDL_CreateRenderer SDL_RenderPresent \
	$(MATH_FUNCS)
WRAP_FLAGS := $(foreach w,$(WRAPS),-Wl,--wrap=$(w))

# the patch series goes onto the submodule before anything of the engine builds
PATCH_STAMP := $(ROOT)/build/patches.stamp
$(PATCH_STAMP): $(wildcard $(ROOT)/patches/*.patch) apply-patches.sh
	sh apply-patches.sh
	@mkdir -p $(dir $@)
	@touch $@

# The engine includes <SDL2/SDL.h>: a directory (INC_DIR) that has SDL's
# headers under SDL2/
SDL_HDRS := $(wildcard $(SDL)/include/*.h)
$(INC_DIR)/SDL2/.stamp: $(SDL_HDRS)
	@mkdir -p $(INC_DIR)/SDL2
	cp $(SDL_HDRS) $(INC_DIR)/SDL2/
	@touch $@

SDL_CFLAGS_COMMON := -O2 -fno-strict-aliasing $(SDL_DEFS) -I$(SDL)/include
CODEC_CFLAGS_COMMON := -std=gnu11 -O2 -w $(CODEC_INCS) $(NO_BUILTIN_MATH)

# Every object depends on the flags it was built with: a change to them
# rebuilds it.
define flags_stamp
$(shell mkdir -p $(1); printf '%s\n' '$(2)' | cmp -s - $(1)/flags || printf '%s\n' '$(2)' > $(1)/flags)
endef
