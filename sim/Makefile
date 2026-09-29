# pocket-tank PC simulator (LVGL v9 + SDL2)
#
# NOTE: built as x86_64 because the installed Homebrew SDL2 is the Intel build
# (/usr/local); the sim runs fine under Rosetta. Drop ARCH for a native build
# against an arm64 SDL2.

# `make CYD=1` builds the tank at the 2.8" CYD's 320 x 240 (docs/CYD.md) as
# fishsim-cyd, in its own build directory - the two sizes never share an
# object, so both binaries can sit side by side.
ifeq ($(CYD),1)
SIZE    := -DTANK_W=320 -DTANK_H=240
BUILD   := build-cyd
TARGET  := fishsim-cyd
else
SIZE    :=
BUILD   := build
TARGET  := fishsim
endif

CC      := cc
ARCH    := -arch x86_64
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

CFLAGS  := -O2 -std=c11 $(ARCH) $(SIZE) -I. -I../common -I../common/llm -DLV_CONF_INCLUDE_SIMPLE -DSOUNDS_BIN='"../assets/sounds/sounds.bin"' -DPT_VERSION='"$(shell git describe --tags --always --dirty 2>/dev/null || echo sim)"' $(SDL_CFLAGS) \
           -Wall -Wextra -Wno-unused-parameter -MMD -MP
LDFLAGS := $(ARCH) $(SDL_LIBS) -lm

APP_SRCS  := main.c advisor_llm.c persist_port_sim.c ../common/tank.c ../common/advisor.c ../common/render.c ../common/icons.c ../common/progression.c ../common/setup.c ../common/audio.c ../common/notice.c ../common/sounds.c ../common/battery.c ../common/llm/q4_model.c ../common/llm/word_tok.c ../common/llm/advisor_core.c

LVGL_SRCS := $(shell find lvgl/src -name '*.c')
OBJS      := $(patsubst %.c,$(BUILD)/%.o,$(filter-out ../common/%,$(APP_SRCS)) $(LVGL_SRCS)) $(patsubst ../common/%.c,$(BUILD)/common/%.o,$(filter ../common/%,$(APP_SRCS)))

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(BUILD)/common/llm/%.o: ../common/llm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O3 -c $< -o $@

$(BUILD)/common/%.o: ../common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build fishsim build-cyd fishsim-cyd

-include $(OBJS:.o=.d)

.PHONY: clean
