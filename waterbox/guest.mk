# guest.mk - core.wbx: the same SDL, codec, engine, game and core sources as
# native.mk, built with miniBox's musl/libstdc++ guest toolchain (the engine
# is C++) and linked at the guest base. Objects land in build/guest; core.wbx
# is checked by miniBox's check-wbx.sh (no thread-local storage, no %fs, no
# red zone) before it counts as built.
#
# Needs miniBox built WITH the C++ guest toolchain:
#   meson setup <miniBox>/build/meson-cpp <miniBox> -Dguest_cpp=true && ninja -C <miniBox>/build/meson-cpp
#
# Usage: make -f guest.mk -j$(nproc) [MB=<miniBox checkout>]

.DEFAULT_GOAL := all
include sources.mk

B      := $(ROOT)/build/guest
MBUILD := $(MB)/build/meson-cpp
SR     := $(MBUILD)/guest-sysroot
GCCVER := $(shell gcc -dumpfullversion)
# the system's compilers over the guest sysroot, through its specs (musl's
# headers and start files, -mno-red-zone), as the DOSBox-X core's cross file
CC     := gcc -specs $(SR)/lib/musl-gcc.specs
CXX    := g++ -specs $(SR)/lib/musl-gcc.specs

# BizHawk waterbox's frozen guest flags, as miniBox's source/guest/meson.build
# gives them to a guest
WBFLAGS := -fvisibility=hidden -mcmodel=large -mno-red-zone -mstack-protector-guard=global \
	-fno-stack-protector -fno-pic -fno-pie -fcf-protection=none -DNDEBUG -DCHIMERA_GUEST
MBINCS := -I$(MB)/extern/emulibc -I$(MB)/source/guest/include -I$(MB)/extern/jsmn
CXXINCS := -I$(SR)/include/c++/$(GCCVER) -I$(SR)/include/c++/$(GCCVER)/x86_64-linux-musl

SDL_CFLAGS := $(WBFLAGS) $(SDL_CFLAGS_COMMON) $(SDL_WARN)
CODEC_CFLAGS := $(WBFLAGS) $(CODEC_CFLAGS_COMMON)
RSDK_CXXFLAGS := $(WBFLAGS) $(RSDK_CXXFLAGS_COMMON) $(CXXINCS) -fexceptions -w
MANIA_CFLAGS := $(WBFLAGS) $(MANIA_CFLAGS_COMMON) -w
CORE_CFLAGS := $(WBFLAGS) $(CORE_CFLAGS_COMMON) $(MBINCS) -I. -Wall
CORE_CXXFLAGS := $(WBFLAGS) $(CORE_CXXFLAGS_COMMON) $(CXXINCS) $(MBINCS) -I. -fexceptions -Wall -Wno-unused-function

$(call flags_stamp,$(B),$(CC) | $(CXX) | $(SDL_CFLAGS) | $(CODEC_CFLAGS) | $(RSDK_CXXFLAGS) | $(MANIA_CFLAGS) | $(CORE_CFLAGS) | $(CORE_CXXFLAGS))

SDL_OBJS := $(patsubst $(SDL)/%.c,$(B)/sdl/%.o,$(SDL_SRCS))
CODEC_OBJS := $(patsubst $(ROOT)/extern/%.c,$(B)/codec/%.o,$(OGG_SRCS) $(THEORA_SRCS) $(MINIZ_SRCS))
RSDK_OBJS := $(patsubst $(RSDK)/%.cpp,$(B)/rsdk/%.o,$(RSDK_SRCS))
MANIA_OBJS := $(patsubst $(MANIA)/%.c,$(B)/mania/%.o,$(MANIA_SRCS))
CORE_OBJS := $(addprefix $(B)/core/,$(addsuffix .o,$(CORE_C_NAMES) $(CORE_CXX_NAMES)))

all: $(B)/core.wbx

$(SR)/lib/libstdc++.a:
	@echo "miniBox's C++ guest toolchain is missing: $(SR)" >&2
	@echo "build it: meson setup $(MBUILD) $(MB) -Dguest_cpp=true && ninja -C $(MBUILD)" >&2
	@false

$(B)/sdl/%.o: $(SDL)/%.c $(INC_DIR)/SDL2/.stamp $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(SDL_CFLAGS) -c -o $@ $<

$(B)/codec/%.o: $(ROOT)/extern/%.c $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(CODEC_CFLAGS) -c -o $@ $<

$(B)/rsdk/%.o: $(RSDK)/%.cpp $(PATCH_STAMP) $(INC_DIR)/SDL2/.stamp $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CXX) $(RSDK_CXXFLAGS) -c -o $@ $<

$(B)/mania/%.o: $(MANIA)/%.c $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(MANIA_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.c $(CORE_HDRS) $(PATCH_STAMP) $(INC_DIR)/SDL2/.stamp $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CC) $(CORE_CFLAGS) -c -o $@ $<

$(B)/core/%.o: %.cpp $(CORE_HDRS) $(PATCH_STAMP) $(INC_DIR)/SDL2/.stamp $(B)/flags | $(SR)/lib/libstdc++.a
	@mkdir -p $(dir $@)
	$(CXX) $(CORE_CXXFLAGS) -c -o $@ $<

# the guest kit's link recipe (the DOSBox-X core's): the large code model's
# --no-relax, the weak pthread pulls libgcc_eh needs, cxxglue for the unwinder
$(B)/core.wbx: $(CORE_OBJS) $(RSDK_OBJS) $(MANIA_OBJS) $(CODEC_OBJS) $(SDL_OBJS)
	$(CXX) -static -no-pie -Wl,--eh-frame-hdr -Wl,-O2 -Wl,--no-relax -Wl,--strip-debug -Wl,-z,stack-size=8388608 \
		-T $(MB)/source/guest/linkscript.T \
		-Wl,-u,pthread_once -Wl,-u,pthread_cond_wait -Wl,-u,pthread_cond_broadcast -Wl,-u,pthread_key_create \
		-o $@.tmp $^ $(MBUILD)/source/guest/cxxglue.c.o $(MBUILD)/source/guest/emulibc.c.o $(WRAP_FLAGS) \
		-L$(SR)/lib -lstdc++ -lm -lgcc -lgcc_eh -lc
	sh $(MB)/source/guest/check-wbx.sh $@.tmp
	mv $@.tmp $@

clean:
	rm -rf $(B)

.PHONY: all clean
