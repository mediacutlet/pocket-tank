# pocket-tank PC simulator (SDL3)
#
# Builds for the host compiler's architecture (Apple Silicon on a current
# Mac). Against an Intel Homebrew SDL3 (/usr/local) on Apple Silicon:
# `make ARCH="-arch x86_64"`, run under Rosetta.

CC      := cc
ARCH    ?=
SDL_CFLAGS := $(shell pkg-config --cflags sdl3)
SDL_LIBS   := $(shell pkg-config --libs sdl3)

CFLAGS  := -O2 -std=c11 $(ARCH) -I. -I../common -I../common/llm -DSOUNDS_BIN='"../assets/sounds/sounds.bin"' -DPT_VERSION='"$(shell git describe --always --dirty --exclude='*' 2>/dev/null || echo sim)"' $(SDL_CFLAGS) \
           -Wall -Wextra -Wno-unused-parameter -MMD -MP
LDFLAGS := $(ARCH) $(SDL_LIBS) -lm

APP_SRCS  := main.c advisor_llm.c persist_port_sim.c ../common/tank.c ../common/advisor.c ../common/render.c ../common/icons.c ../common/progression.c ../common/setup.c ../common/audio.c ../common/notice.c ../common/sounds.c ../common/battery.c ../common/llm/q4_model.c ../common/llm/word_tok.c ../common/llm/advisor_core.c

OBJS      := $(patsubst %.c,build/%.o,$(filter-out ../common/%,$(APP_SRCS))) $(patsubst ../common/%.c,build/common/%.o,$(filter ../common/%,$(APP_SRCS)))

fishsim: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

SELFTESTS := --selftest --selftest-hunger --selftest-shop --selftest-battery \
             --selftest-pop --selftest-sleep --selftest-tend --selftest-llm \
             --selftest-saves

check: fishsim
	@set -e; for test in $(SELFTESTS); do echo "RUN $$test"; ./fishsim "$$test"; done

build/common/llm/%.o: ../common/llm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O3 -c $< -o $@

build/common/%.o: ../common/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build fishsim

-include $(OBJS:.o=.d)

.PHONY: check clean
