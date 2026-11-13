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

CFLAGS  := $(ROUNDF) $(EXTRA_CFLAGS) -O2 -std=c11 $(ARCH) -I. -I../common -I../common/llm -DLV_CONF_INCLUDE_SIMPLE -DSOUNDS_BIN='"../assets/sounds/sounds.bin"' -DPT_VERSION='"$(shell git describe --always --dirty --exclude='*' 2>/dev/null || echo sim)"' $(SDL_CFLAGS) \
           -Wall -Wextra -Wno-unused-parameter -MMD -MP
LDFLAGS := $(ARCH) $(SDL_LIBS) -lm

APP_SRCS  := main.c selftest_link.c advisor_llm.c persist_port_sim.c net_port_sim.c ../common/tank.c ../common/update.c ../common/wifi_nets.c ../common/advanced.c ../common/shelf.c ../common/boot_show.c ../common/advisor.c ../common/render.c ../common/backgrounds.c ../common/icons.c ../common/icons4.c ../common/progression.c ../common/setup.c ../common/audio.c ../common/notice.c ../common/sounds.c ../common/battery.c ../common/passport.c ../common/link.c ../common/tank_link.c ../common/collection.c ../common/gift.c ../common/sdgift.c ../common/llm/q4_model.c ../common/llm/word_tok.c ../common/llm/advisor_core.c

APP_SRCS  += selftest_coll.c   # the visitor collection's pages (0.4)
APP_SRCS  += selftest_sdgift.c   # sand dollar gifts (0.4.2)
APP_SRCS  += selftest_clock_gate.c   # the boot's TIME CHECK gate (0.4.1, common/clock_gate.h)
APP_SRCS  += selftest_seam.c   # the 0.4.1 audit's link x progression soak (two tanks, two progressions)
APP_SRCS  += selftest_board_probe.c  # the firmware's board probe decision on the host (0.4.2, board_probe.h)
APP_SRCS  += selftest_old_keys.c  # 0.4.1's Wi-Fi keys while an update is not confirmed (0.4.2 audit r2 PW2-1, net_old_keys.h)
APP_SRCS  += selftest_charge_reason.c  # why the AXP2101's charger is stopped (0.4.2, axp2101_reason.h)
APP_SRCS  += ../common/lib_thumbs.c   # ... and their two pictures in THE LIBRARY, baked (tools/gen_lib_thumbs.py)
APP_SRCS  += ../common/school.c ../common/piranha_spike.c   # THE MINNOWS and THE PIRANHA TANK (0.4.2: experimental tanks, shelf types 3 and 2)
APP_SRCS  += jelly_sim.c ../common/jelly.c   # THE JELLY TANK (habitat #2, the crew's experimental tank)
APP_SRCS  += selftest_xtclock.c   # the experimental tanks' clocks over days awake (0.4.2 audit r2, FW2-1)
APP_SRCS  += selftest_libload.c ../common/lib_pics.c   # THE LIBRARY opens at once, its pictures made behind it (0.4.2, common/lib_pics.h)
APP_SRCS  += selftest_touch.c  # the FIRMWARE's touch port built on the host (0.4.1): touchhost/ stands in for ESP-IDF
APP_SRCS  += selftest_nightcards.c   # OWED CARDS: what the dark held, saved and said once the light is on (0.4.2, audit PW2-2 + PW2-3)
APP_SRCS  += selftest_night_badge.c   # FIRST FULL NIGHT'S SLEEP said at the boot that ends the night, in main.c's boot order (0.4.2, PR-2)
APP_SRCS  += selftest_dark_notices.c   # the dark holds notices; a fry's welcome waits for the light; a fish home from a visit saved at once (0.4.2)
APP_SRCS  += selftest_critshelf.c   # THE CRITTER SHELF and the cap of five (0.4.2, docs/critter-shelf.md)
APP_SRCS  += selftest_full_reset.c   # the reset prompt's two choices: RESET THIS TANK, ERASE EVERYTHING (0.4.2)
APP_SRCS  += selftest_breedwait.c   # SELECTIVE BREEDING: a pair fish next door waits on every path; NEW FRY not staged while the pair cannot court (0.4.2 audit r2, SLP-1 / SPS-1)
APP_SRCS  += selftest_move.c   # THE TRAVEL BOWL: a fish moved between the tanks of one board (0.4.2, docs/move-fish-0.4.2.md)
APP_SRCS  += selftest_clock_rebase.c   # THE FIRST TIME SYNC: the shelf's stamps and a gift's day move with the clock (0.4.2 audit r2, PW2-4 / PW2-5)
APP_SRCS  += selftest_home.c   # A HARD BOOT LOADS THE MAIN TANK: which tank comes back, and the swap (0.4.2)
APP_SRCS  += selftest_wifi3.c   # THREE SAVED NETWORKS (0.4.2, common/wifi_nets.h)
APP_SRCS  += updui_shot.c   # --selftest-updui and --updui-shot: the UPDATES page's two faces, and every state's picture (0.4.2)
LVGL_SRCS := $(shell find lvgl/src -name '*.c')
OBJS      := $(patsubst %.c,$(BUILD)/%.o,$(filter-out ../common/%,$(APP_SRCS)) $(LVGL_SRCS)) $(patsubst ../common/%.c,$(BUILD)/common/%.o,$(filter ../common/%,$(APP_SRCS)))

$(OUT): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

SELFTESTS := --selftest-jelly --selftest-jelly-shelf --selftest --selftest-hunger --selftest-shop --selftest-battery \
             --selftest-pop --selftest-card --selftest-fishmore --selftest-keeper --selftest-fry --selftest-sleep --selftest-tend --selftest-llm \
             --selftest-saves --selftest-update --selftest-genes --selftest-passport --selftest-link --selftest-doors --selftest-fry-cap --selftest-visitors --selftest-r2soak --selftest-seam --selftest-give --selftest-link-lk --selftest-advanced --selftest-model --selftest-restore --selftest-grasscache --selftest-floor --selftest-stack --selftest-shelf --selftest-shelf-safe --selftest-power-swap-noclock --selftest-shelf-away --selftest-temper --selftest-board --selftest-critter-nav --selftest-pumpkin-light --selftest-breather --selftest-hold --selftest-grassloop --selftest-grasspatch --selftest-touch-lock --selftest-shelf-rename --selftest-depth --selftest-footprint --selftest-bake-order --selftest-place --selftest-order-squares --square-boot --selftest-cave --selftest-roost --selftest-boot-show --selftest-front-keep --selftest-seahorse-stack --selftest-depth-pop --selftest-castle-keep --selftest-hold-emerge --selftest-seahorse-post --selftest-clock --selftest-under-card --selftest-fish-pair --selftest-crit-shelf
SELFTESTS += --selftest-coll-pages
SELFTESTS += --selftest-nature-marks   # ITS NATURE's parent marks by shape and place, not colour alone (0.4.2 audit TP2-9)
SELFTESTS += --selftest-board-probe   # which board the chips say, the ES7210 at 0x40..0x43 (0.4.2: the 1.75C rev 1.1)
SELFTESTS += --selftest-old-keys   # 0.4.1's Wi-Fi keys stay while an update is not confirmed; a FORGET still erases them (0.4.2 audit r2 PW2-1)
SELFTESTS += --selftest-charge-reason   # the charger's stop reason from its registers (0.4.2: Hugo's pendant)
SELFTESTS += --selftest-backgrounds   # THE BACKGROUNDS (0.4.1): each bakes the same twice, draws, has its layers, survives a save
SELFTESTS += --selftest-bg-golden     # the eight 0.4.1 backgrounds as main e8d49ea drew them, day and night, byte for byte (0.4.2 review)
SELFTESTS += --selftest-double-tap   # a double tap never buys: the shop, MY TANKS, a card's SELL (0.4 rc)
SELFTESTS += --selftest-sdgift   # sand dollar gifts: the book, the dial, the HUD's coin, two engines on a bad radio (0.4.2)
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
SELFTESTS += --selftest-crab-climb   # the crab climbing the volcano is in the volcano's stack: never over a piece ranked above it, from a foot clear of the pieces (0.4.2, Strato's pendant)
SELFTESTS += --selftest-crab-depth   # the crab climbing a volcano BEHIND is at the volcano's depth: under every frond before it (the sword plant, the seagrass) and every animal before it (0.4.2, Strato's square)
SELFTESTS += --selftest-plant-under  # the sword plant BEHIND / IN FRONT OF THE SEAGRASS: the stack, the loop patched from its page, the page, the save (0.4.1, Strato 11-05)
SELFTESTS += --selftest-plant-order  # the sword plant re-ranked on its page: the grass loop takes the new order at once (0.4, Strato on the square)
SELFTESTS += --selftest-pop-lanes    # an animal changes depth only where nothing shows the jump: the urchin on the crab, the crab in the grass, the walk home, the seahorse bed to bed (0.4, the layer hunt)
SELFTESTS += --selftest-clock-gate   # the boot's TIME CHECK: never more owed than 0.4.0, nothing owed where the network pays, cheaper (0.4.1)
SELFTESTS += --selftest-night-cards   # OWED CARDS: a dark morning slept again, news earned in the dark - said once the light is on; cuts, a full queue, MY TANKS, an older save, a rollback, a reset (0.4.2, audit PW2-2 + PW2-3)
SELFTESTS += --selftest-night-badge   # FIRST FULL NIGHT'S SLEEP said once at the boot that ends the night; old badges, a stage in the night, an OTA, a SWAP IN, a restore: as before (0.4.2, audit PR-2)
SELFTESTS += --selftest-clock-rebase   # the first time sync's jump: a DORMANT shelved tank lives only its real time, a gift in flight keeps its day; ordinary spans, PAUSED, a night's gift: as before (0.4.2 audit r2, PW2-4 / PW2-5)
SELFTESTS += --selftest-dark-notices  # light off: no card, no chime, no fry's welcome till the light is on (LOW BATTERY and three more apart); a fish home from a visit saved at once (0.4.2, Bananna's 5 am fry + audit T1-F1)
SELFTESTS += --selftest-touch-port   # the firmware's touch port: a firm press acts on every button, a slow CLOSE eats nothing, a page's double tap reaches nothing, MY TANKS over link mode keeps its holds (0.4.1)
SELFTESTS += --selftest-grassloop-through   # a fish THROUGH / BEHIND the grass, every moment of the loop as the boards draw it, the old way pixel for pixel (0.4.2, Strato on the square)
SELFTESTS += --selftest-grassloop-nomask    # a fish THROUGH the grass with no odd-frond masks (a plan held by PSRAM): loop and live frames alike (0.4.2, Strato on the square)
SELFTESTS += --selftest-grassloop-shrink    # the grass trimmed: the loop's band comes down and holds as many moments; grown back it grows; nibbles never replan it (0.4.2, Strato on the square)
SELFTESTS += --selftest-crit-names          # the critters' names: the card's NAME button on every board, the wheel, the save (0.4.2)
SELFTESTS += --selftest-icons              # the icons packed by palette: every pixel of every icon decodes exactly as its PNG (0.4.2, the app's room)
SELFTESTS += --selftest-vignette           # the vignette kept as one mirrored quarter: every pixel's alpha exact (0.4.2, the PSRAM census)
SELFTESTS += --selftest-settings-nav        # the settings foot + tiles hit where drawn on every board; GRAPHICS a door, its page (QUALITY, VIGNETTE, BACK, ONE MOMENT), device settings (0.4.2)
SELFTESTS += --selftest-vignette-off        # the VIGNETTE OFF and ON again: ON's frames exactly, OFF no darker, no stale loop, at LOW / MEDIUM / HIGH, bakes at frames' end and between (0.4.2)
SELFTESTS += --selftest-cuts                # THE RENDER CUTS move no pixel: 25,944 frames the old way and the new - three levels, day and night, the vignette, the volcano absent / BEHIND / IN FRONT, every depth, the seahorse in every pose and on the shelf (0.4.2; its control: make check-cuts-control)
SELFTESTS += --selftest-crit-cuts           # THE CRITTERS' CUTS move no pixel: 36,000 crab and urchin grids and 2,100 frames (the climb, the dark, a dig, a walk, a find, a scare, the cave under a fish; the volcano BEHIND / IN FRONT) the old way and the new (0.4.2; its control: make check-crit-cuts-control)
SELFTESTS += --selftest-gfx                 # GRAPHICS: LOW one still moment, MEDIUM half the moments by day and the night picture by night, the old way pixel for pixel, a plan only a crossing of MEDIUM, toggled the tank's state the same (0.4.2)
SELFTESTS += --selftest-gfx-high            # GRAPHICS HIGH and LOW: every scripted scenario's frames as 04a7a72's (the Mac's hashes; elsewhere said, not compared), and as HIGH's after a MEDIUM day and night (0.4.2 review)
SELFTESTS += --selftest-gfx-night           # GRAPHICS MEDIUM's night picture: the day's loop untouched all night, no bake at the light-on, core 1, mid-bake, refused, the pool, crossings (0.4.2)
SELFTESTS += --selftest-wifi3               # THREE SAVED NETWORKS: the list, the pick, the move from 0.4.1, the pages (0.4.2)
SELFTESTS += --selftest-updui               # THE UPDATES PAGE'S TWO FACES: UPDATES and WI-FI, one size of button, the door and BACK, doubles spent, ADD A NETWORK (0.4.2)
SELFTESTS += --selftest-back-overlap        # back from UPDATES a press where its BACK was (over LINK) is spent for 0.5 s, everything else at once; the GRAPHICS page as on main (0.4.2)
SELFTESTS += --selftest-nest                # SELECTIVE BREEDING: the unlock, pairs paid / refunded, the pair's fry, broken pairs, the save, the touch (0.4.2)
SELFTESTS += --selftest-breed-wait          # SELECTIVE BREEDING: a pair fish next door waits at a boot, a nap's wake, a forced birth (no refund, no stand-in); NEW FRY not staged while the pair cannot court (0.4.2 audit r2, SLP-1 / SPS-1)
SELFTESTS += --selftest-full-reset          # the reset prompt: RESET THIS TANK as it was, ERASE EVERYTHING only by its hold, the power cut at every write (0.4.2)
SELFTESTS += --selftest-move                # THE TRAVEL BOWL: a move there and back, the power cut and a refused write at every step, every refusal, the cards (0.4.2)
SELFTESTS += --selftest-xtclock            # the experimental tanks' clocks at 1 h .. 60 days awake: even steps, a moving phase, a seamless lap (0.4.2 audit r2, FW2-1): red before
SELFTESTS += --selftest-libload            # THE LIBRARY opens at once: the page before any picture, the pictures 0.4.1's pixel for pixel, the bubbles, taps while loading, closed mid-load (0.4.2)
SELFTESTS += --selftest-home               # A HARD BOOT LOADS THE MAIN TANK: the tank last on the glass, the swap whole, none left -> a new one, free (0.4.2)
# the dev repo does not track model/out (the public one does): NOMODEL=1 runs
# the same checks without the LLM selftest, for a checkout with no model
ifdef NOMODEL
SELFTESTS := $(filter-out --selftest-llm --selftest-model,$(SELFTESTS))
endif

# `make check` tests the rectangle; `make ROUND=1 check` / `make WATCH=1 check` /
# `make SQUARE=1 check` the same selftests in that board's world; `make check-all`
# all four - the gate for anything in common/ (docs/BOARDS.md)
# GRAPHICS (0.4.2): the grass loop's own tests again at LOW (one moment of HIGH's plan, still day and night) and MEDIUM (HIGH by day, a night loop by night)
SELFTESTS_LOW := --selftest-grassloop --selftest-grassloop-through --selftest-grassloop-nomask --selftest-grassloop-shrink --selftest-grasspatch
check: $(OUT)
	@set -e; for test in $(SELFTESTS); do echo "RUN $(OUT) $$test"; ./$(OUT) "$$test"; done
	@set -e; for test in $(SELFTESTS_LOW); do echo "RUN PT_GFX=low $(OUT) $$test"; PT_GFX=low ./$(OUT) "$$test"; done
	@set -e; for test in $(SELFTESTS_LOW); do echo "RUN PT_GFX=medium $(OUT) $$test"; PT_GFX=medium ./$(OUT) "$$test"; done
	@set -e; for g in low medium; do echo "RUN PT_GFX=$$g $(OUT) --selftest-crab-depth"; PT_CD_SHORT=1 PT_GFX=$$g ./$(OUT) --selftest-crab-depth; done
	@echo "RUN PT_VIG=off $(OUT) --selftest-crab-depth"; PT_CD_SHORT=1 PT_VIG=off ./$(OUT) --selftest-crab-depth
	@echo "RUN PT_CLIMB_GRASS_OLD=1 $(OUT) --selftest-crab-depth (the control: it must FAIL)"; if PT_CD_SHORT=1 PT_CLIMB_GRASS_OLD=1 ./$(OUT) --selftest-crab-depth > /dev/null; then echo "FAIL: crab-depth: the control passed - the test proves nothing"; exit 1; else echo "crab-depth: the control fails, as it must"; fi

# --selftest-cuts' control: THE RENDER CUTS broken on purpose in render.c alone (PT_CUT_BREAK 1: the lay's mask not begun from its
# band's; 2: the seahorse's spiral kept whatever its sweep; 3: the moment's mask held from one frame to the next; 4: the tail stamp's
# early-out a tenth too eager) - each build must FAIL
# (it leaves $(OUT)-cutbreak and its logs in $(BUILD))
check-cuts-control: $(OUT)
	@set -e; for b in 1 2 3 4; do \
	  $(CC) $(CFLAGS) -DPT_CUT_BREAK=$$b -c ../common/render.c -o $(BUILD)/render-cutbreak.o; \
	  $(CC) -o $(OUT)-cutbreak $(filter-out $(BUILD)/common/render.o,$(OBJS)) $(BUILD)/render-cutbreak.o $(LDFLAGS); \
	  echo "RUN $(OUT)-cutbreak --selftest-cuts (PT_CUT_BREAK=$$b, the control: it must FAIL)"; \
	  if ./$(OUT)-cutbreak --selftest-cuts > $(BUILD)/cutbreak-$$b.log; then echo "FAIL: cuts: control $$b passed - the test proves nothing"; exit 1; \
	  else echo "cuts: control $$b fails, as it must ($$(grep -c '^FAIL' $(BUILD)/cutbreak-$$b.log) FAIL lines)"; fi; done

# --selftest-crit-cuts' control: THE CRITTERS' CUTS broken on purpose in render.c alone (PT_CUT_BREAK 5: the urchin's spines' boxes too
# small, 6: the crab's tilted sampler's, 7: its claws' and eyes', 8: the front slot's hold a word short) - each build must FAIL
check-crit-cuts-control: $(OUT)
	@set -e; for b in 5 6 7 8; do \
	  $(CC) $(CFLAGS) -DPT_CUT_BREAK=$$b -c ../common/render.c -o $(BUILD)/render-cutbreak.o; \
	  $(CC) -o $(OUT)-cutbreak $(filter-out $(BUILD)/common/render.o,$(OBJS)) $(BUILD)/render-cutbreak.o $(LDFLAGS); \
	  echo "RUN $(OUT)-cutbreak --selftest-crit-cuts (PT_CUT_BREAK=$$b, the control: it must FAIL)"; \
	  if ./$(OUT)-cutbreak --selftest-crit-cuts > $(BUILD)/critbreak-$$b.log; then echo "FAIL: crit-cuts: control $$b passed - the test proves nothing"; exit 1; \
	  else echo "crit-cuts: control $$b fails, as it must ($$(grep -c '^FAIL' $(BUILD)/critbreak-$$b.log) FAIL lines)"; fi; done

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

.PHONY: check check-all check-nomodel check-cuts-control check-crit-cuts-control clean
