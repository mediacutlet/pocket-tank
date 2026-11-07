# pocket-tank PC simulator (LVGL v9 + SDL2)
#
# macOS: built as x86_64 by default because the Homebrew SDL2 here is the
# Intel build (/usr/local); the sim runs fine under Rosetta. `make ARCH=` for
# a native build against an arm64 SDL2. Elsewhere (Linux, CI) ARCH is empty
# and the host compiler's own architecture is used.

CC      := cc
ifeq ($(shell uname -s),Darwin)
ARCH    ?= -arch x86_64
endif
ARCH    ?=
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

# `make ROUND=1`: the round 466 px bowl (the Waveshare 1.75C's world) as
# ./fishsim-round, built beside the rectangle's in build-round/.
# `make WATCH=1`: the watch's 410 x 502 portrait tank (the Waveshare 2.06) as ./fishsim-watch
# `make SQUARE=1`: the square's 480 x 480 tank (the Waveshare 2.16) as ./fishsim-square
ifdef ROUND
BUILD   := build-round
OUT     := fishsim-round
ROUNDF  := -DTANK_ROUND
else ifdef WATCH
BUILD   := build-watch
OUT     := fishsim-watch
ROUNDF  := -DTANK_WATCH
else ifdef SQUARE
BUILD   := build-square
OUT     := fishsim-square
ROUNDF  := -DTANK_SQUARE
else
BUILD   := build
OUT     := fishsim
ROUNDF  :=
endif

CFLAGS  := $(ROUNDF) $(EXTRA_CFLAGS) -O2 -std=c11 $(ARCH) -I. -I../common -I../common/llm -DLV_CONF_INCLUDE_SIMPLE -DLANG_AUDIT -DSOUNDS_BIN='"../assets/sounds/sounds.bin"' -DPT_VERSION='"$(shell git describe --always --dirty --exclude='*' 2>/dev/null || echo sim)"' $(SDL_CFLAGS) \
           -Wall -Wextra -Wno-unused-parameter -MMD -MP
LDFLAGS := $(ARCH) $(SDL_LIBS) -lm

APP_SRCS  := main.c selftest_link.c advisor_llm.c persist_port_sim.c net_port_sim.c ../common/tank.c ../common/update.c ../common/advanced.c ../common/shelf.c ../common/boot_show.c ../common/advisor.c ../common/render.c ../common/backgrounds.c ../common/icons.c ../common/progression.c ../common/setup.c ../common/audio.c ../common/notice.c ../common/sounds.c ../common/battery.c ../common/passport.c ../common/link.c ../common/tank_link.c ../common/collection.c ../common/gift.c ../common/lang.c ../common/lang_de.c ../common/llm/q4_model.c ../common/llm/word_tok.c ../common/llm/advisor_core.c

APP_SRCS  += selftest_coll.c   # the visitor collection's pages (0.4)
APP_SRCS  += selftest_clock_gate.c   # the boot's TIME CHECK gate (0.4.1, common/clock_gate.h)
APP_SRCS  += selftest_seam.c   # the 0.4.1 audit's link x progression soak (two tanks, two progressions)
APP_SRCS  += selftest_touch.c  # the FIRMWARE's touch port built on the host (0.4.1): touchhost/ stands in for ESP-IDF
LVGL_SRCS := $(shell find lvgl/src -name '*.c')
OBJS      := $(patsubst %.c,$(BUILD)/%.o,$(filter-out ../common/%,$(APP_SRCS)) $(LVGL_SRCS)) $(patsubst ../common/%.c,$(BUILD)/common/%.o,$(filter ../common/%,$(APP_SRCS)))

$(OUT): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

SELFTESTS := --selftest --selftest-hunger --selftest-shop --selftest-battery \
             --selftest-pop --selftest-card --selftest-keeper --selftest-fry --selftest-sleep --selftest-tend --selftest-llm \
             --selftest-saves --selftest-update --selftest-genes --selftest-passport --selftest-link --selftest-doors --selftest-fry-cap --selftest-visitors --selftest-r2soak --selftest-seam --selftest-give --selftest-advanced --selftest-model --selftest-restore --selftest-grasscache --selftest-floor --selftest-stack --selftest-shelf --selftest-shelf-safe --selftest-power-swap-noclock --selftest-shelf-away --selftest-temper --selftest-board --selftest-critter-nav --selftest-pumpkin-light --selftest-breather --selftest-hold --selftest-grassloop --selftest-grasspatch --selftest-touch-lock --selftest-shelf-rename --selftest-depth --selftest-footprint --selftest-bake-order --selftest-place --selftest-order-squares --square-boot --selftest-cave --selftest-roost --selftest-boot-show --selftest-front-keep --selftest-seahorse-stack --selftest-depth-pop --selftest-castle-keep --selftest-hold-emerge --selftest-seahorse-post --selftest-clock --selftest-under-card --selftest-fish-pair --selftest-lang
SELFTESTS += --selftest-coll-pages
SELFTESTS += --selftest-backgrounds   # THE BACKGROUNDS (0.4.1): each bakes the same twice, draws, has its layers, survives a save
SELFTESTS += --selftest-double-tap   # a double tap never buys: the shop, MY TANKS, a card's SELL (0.4 rc)
SELFTESTS += --selftest-gift-cut   # a given fish that comes back, the power cut at every write (0.4.1)
SELFTESTS += --selftest-buy-cut   # NEW TANK: a tank and its price, or neither, the power cut at every write (0.4.1)
SELFTESTS += --selftest-remove-cut   # REMOVE: the tank, or its refund, the power cut at every write (0.4.1)
SELFTESTS += --selftest-reset-farm   # the reset farm: a reset never pays the starting set again (0.4.1)
SELFTESTS += --selftest-reset-cut   # the reset: the wallet whole, the power cut at every write (0.4.1)
SELFTESTS += --selftest-fry-from   # where a fry came from: THE SHOP, or BORN TO ... NEXT DOOR. (0.4.1)
SELFTESTS += --selftest-audit-a   # the 0.4.1 audit's P1-P10 (docs/audit-0.4.1/README.md): each case fails before its fix
SELFTESTS += --selftest-audit-r1   # the 0.4.1 audit's round-1 fixes: each case fails before its fix
SELFTESTS += --selftest-audit-r2   # the 0.4.1 audit's round-2 saves fixes: each red before (docs/audit-0.4.1/round2/FIX-saves.md)
SELFTESTS += --selftest-farm --selftest-farm2   # the 0.4.1 audit's money loops (reviewer progression): every loop exact
SELFTESTS += --selftest-saves-audit   # round 1's saves harness, booting in main.c's order (reviewer saves)
SELFTESTS += --selftest-r2-saves   # refused shelf writes, the round-2 harness: ABDEF (C's and A's stuck runs are deferred R2S-3/R2S-6: reported)
SELFTESTS += --selftest-ss2   # every save path x round 2's fixes, X1-X8 (reviewer seam-saves; X9 deferred: SS2_X9=1)
SELFTESTS += --selftest-r2-marker   # the shelf's records read paid/applied by the marker, never the wallet (FIX-saves2.md): red before
SELFTESTS += --selftest-dr3 --selftest-dr3g --selftest-dr3h --selftest-dr3j   # the third review's probes (DLOQM; S, C and R by name): R1, R2 and the release candidate's journals, the reset's markers, the SWAP IN boot's refused save, swaps
SELFTESTS += --selftest-rv4 --selftest-rv4e   # the fourth review's: a carry left on the flash survives a later refused SWAP IN (R3's stamp reverted, F1; rv4d is R3 itself: by name)
SELFTESTS += --selftest-seam-saves   # every 0.4.1 save field across a save, a 0.4.0 upgrade, a swap (cut at every write), a reset (0.4.1 audit round 1)
SELFTESTS += --selftest-roost-pass   # the crab in its castle IN FRONT stays whole while an animal passes behind a piece it stands before (0.4, #help "Shrimp shadows?")
SELFTESTS += --selftest-roost-exit   # out of a shelter IN FRONT the crab takes the front lane: never the volcano over it in the arch (0.4, Strato on the square)
SELFTESTS += --selftest-plant-under  # the sword plant BEHIND / IN FRONT OF THE SEAGRASS: the stack, the loop patched from its page, the page, the save (0.4.1, Strato 11-05)
SELFTESTS += --selftest-plant-order  # the sword plant re-ranked on its page: the grass loop takes the new order at once (0.4, Strato on the square)
SELFTESTS += --selftest-pop-lanes    # an animal changes depth only where nothing shows the jump: the urchin on the crab, the crab in the grass, the walk home, the seahorse bed to bed (0.4, the layer hunt)
SELFTESTS += --selftest-clock-gate   # the boot's TIME CHECK: never more owed than 0.4.0, nothing owed where the network pays, cheaper (0.4.1)
SELFTESTS += --selftest-touch-port   # the firmware's touch port: a firm press acts on every button, a slow CLOSE eats nothing, a page's double tap reaches nothing, MY TANKS over link mode keeps its holds (0.4.1)
# the dev repo does not track model/out (the public one does): NOMODEL=1 runs
# the same checks without the LLM selftest, for a checkout with no model
ifdef NOMODEL
SELFTESTS := $(filter-out --selftest-llm --selftest-model,$(SELFTESTS))
endif

# `make check` tests the rectangle; `make ROUND=1 check` / `make WATCH=1 check` /
# `make SQUARE=1 check` the same selftests in that board's world; `make check-all`
# all four - the gate for anything in common/ (docs/BOARDS.md)
check: $(OUT)
	@set -e; for test in $(SELFTESTS); do echo "RUN $(OUT) $$test"; ./$(OUT) "$$test"; done

check-all:
	$(MAKE) check
	$(MAKE) ROUND=1 check
	$(MAKE) WATCH=1 check
	$(MAKE) SQUARE=1 check

check-nomodel:
	$(MAKE) NOMODEL=1 check

SEAM_SWAP := -include seam_sect.h -DPT_SEAM
$(BUILD)/common/progression.o $(BUILD)/common/notice.o $(BUILD)/common/setup.o $(BUILD)/common/collection.o $(BUILD)/common/shelf.o: CFLAGS += $(SEAM_SWAP)
$(BUILD)/selftest_touch.o: CFLAGS += -Itouchhost -I../firmware/main   # (after the sim's own: only the touch port's headers come from there)

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
	rm -rf build build-round build-watch build-square fishsim fishsim-round fishsim-watch fishsim-square

-include $(OBJS:.o=.d)

.PHONY: check check-all check-nomodel clean
