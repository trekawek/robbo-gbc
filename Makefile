# Robbo for Game Boy Color - a port of GNU Robbo's game logic, built with GBDK-2020.
GBDK   := third_party/gbdk
LCC    := $(GBDK)/bin/lcc
PY     := python3
SRCDIR := src
GENDIR := src/gen
BUILD  := build
ROM    := $(BUILD)/robbo.gbc
ITCH_ZIP := $(BUILD)/robbo-itch.zip
TITLE  := ROBBO

# Assets converted at build time: the Atari original supplies the font, sound
# tables, instruction text AND the authentic level designs (d2/C*.txt, converted
# to the engine's level format by tools/convert_atari_levels.py).
ORIG ?= third_party/lkavalon-atari/robbo
ATARI_FONTS := $(addprefix $(ORIG)/d2/,F.FNT I.FNT M.FNT S.FNT)
ATARI_LEVELS := $(addprefix $(ORIG)/d2/,C1.txt C2.txt C3.txt)

# `make OUTRO_MENU=1` adds an optional pause-menu preview of the ending.
OUTRO_MENU ?= 0
ifeq ($(filter $(OUTRO_MENU),0 1),)
$(error OUTRO_MENU must be 0 or 1)
endif

# `make ENDING_TEST=1` adds a trivial bonus planet (last level) whose completion
# triggers the final animation -- warp to the highest level and step right twice.
ifdef ENDING_TEST
LCCFLAGS_EXTRA += -DENDING_TEST=1
endif

LCCFLAGS  := -Wm-yc -Wm-yn"$(TITLE)" -Wl-yt0x1B -Wl-yo16 -Wl-ya4 -DCGB -I$(SRCDIR) -Wf--opt-code-size $(LCCFLAGS_EXTRA) -DOUTRO_MENU=$(OUTRO_MENU)
LINKFLAGS := $(LCCFLAGS) -autobank
# board.c is one huge translation unit (GNU Robbo's 940-line update_game, split
# into upd_g1..g5): cap SDCC register allocation so it compiles in ~1min.
BOARDFLAGS := $(LCCFLAGS) -Wf--max-allocs-per-node3000

# generated assets
GFX    := $(GENDIR)/gfx_tiles.c
GFXH   := $(GENDIR)/gfx_tiles.h
SOUNDH := $(GENDIR)/sounds.h
INSTRH := $(GENDIR)/instr.h
PALTXT := tools/atari_pal_palette.txt
# generated level data (authentic Atari levels): HOME index + banked grids
LEVELS    := $(SRCDIR)/levels_idx.c $(SRCDIR)/levels_d0.c $(SRCDIR)/levels_d1.c $(SRCDIR)/levels_d2.c
ATARIDAT  := $(GENDIR)/atari_levels.dat

HAND := $(SRCDIR)/globals.c $(SRCDIR)/glue.c $(SRCDIR)/render.c $(SRCDIR)/loader.c \
        $(SRCDIR)/hud.c $(SRCDIR)/atari_pal.c $(SRCDIR)/menu.c \
        $(SRCDIR)/ending.c $(SRCDIR)/object_tables.c $(SRCDIR)/sound.c $(SRCDIR)/overview.c
SRCS := $(HAND) $(LEVELS) $(GFX)
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRCS))) \
        $(BUILD)/board.o $(BUILD)/board_upd.o $(BUILD)/board_robbo.o
# every object includes one or more generated headers (below: order-only dep)
GENHDRS := $(GFXH) $(SOUNDH) $(INSTRH) $(SRCDIR)/levels_data.h

.PHONY: all clean itch
all: $(ROM)

itch: $(ITCH_ZIP)

$(ITCH_ZIP): $(ROM) tools/package_itch.py
	$(PY) tools/package_itch.py --rom $(ROM) --output $@

# make the generated headers exist before any compile (order-only: regenerating
# a header doesn't force a full rebuild).  Placed after `all` so it stays default.
$(OBJS): | $(GENHDRS)

$(BUILD):
	@mkdir -p $(BUILD)

# Rebuild menu and gameplay dispatch when switching the option without `make clean`.
$(BUILD)/.outro-menu-$(OUTRO_MENU): | $(BUILD)
	@rm -f $(BUILD)/.outro-menu-0 $(BUILD)/.outro-menu-1
	@touch $@

$(BUILD)/menu.o $(BUILD)/glue.o: $(BUILD)/.outro-menu-$(OUTRO_MENU)

# --- asset generation ---
$(PALTXT): tools/convert_altirra_palette.py tools/altirra_default_pal.pal
	$(PY) tools/convert_altirra_palette.py tools/altirra_default_pal.pal > $@.tmp
	mv $@.tmp $@

$(GFX) $(GFXH): tools/convert_font.py tools/sim_logo.py tools/gen_atari_pal.py tools/atari_pal_palette.txt $(ATARI_FONTS)
	$(PY) tools/convert_font.py "$(ORIG)" $(GENDIR)

$(SOUNDH): tools/convert_sound.py $(ORIG)/d1/R1.ASM
	$(PY) tools/convert_sound.py "$(ORIG)" $(GENDIR)

$(INSTRH): tools/extract_instr.py
	$(PY) tools/extract_instr.py "$(ORIG)" $(GENDIR)

$(SRCDIR)/atari_pal.c: tools/gen_atari_pal.py tools/atari_pal_palette.txt $(ATARI_LEVELS)
	$(PY) tools/gen_atari_pal.py "$(ORIG)/d2" > $@.tmp
	mv $@.tmp $@

# authentic Atari level designs -> engine .dat
$(ATARIDAT): tools/convert_atari_levels.py $(ATARI_LEVELS)
	$(PY) tools/convert_atari_levels.py "$(ORIG)/d2" $(ATARIDAT)

$(LEVELS) $(SRCDIR)/levels_data.h: tools/convert_gnu_levels.py $(ATARIDAT)
	$(PY) tools/convert_gnu_levels.py "$(ATARIDAT)" $(SRCDIR)

# --- compile ---
# board.c / board_upd.c are large: capped register allocation
$(BUILD)/board.o: $(SRCDIR)/board.c | $(BUILD)
	$(LCC) $(BOARDFLAGS) -c -o $@ $<
$(BUILD)/board_upd.o: $(SRCDIR)/board_upd.c | $(BUILD)
	$(LCC) $(BOARDFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(SRCDIR)/%.c | $(BUILD)
	$(LCC) $(LCCFLAGS) -c -o $@ $<

$(BUILD)/gfx_tiles.o: $(GFX) | $(BUILD)
	$(LCC) $(LCCFLAGS) -c -o $@ $<

# generated-header dependencies (so these .o rebuild when assets regenerate)
$(BUILD)/render.o $(BUILD)/ending.o: $(GFXH)
$(BUILD)/render.o $(BUILD)/glue.o $(BUILD)/overview.o: $(SRCDIR)/render.h
$(BUILD)/glue.o $(BUILD)/menu.o $(BUILD)/overview.o: $(SRCDIR)/game.h
$(BUILD)/menu.o:   $(INSTRH)
$(BUILD)/sound.o:  $(SOUNDH)
$(BUILD)/sound.o $(BUILD)/glue.o $(BUILD)/menu.o $(BUILD)/ending.o: $(SRCDIR)/sound.h

$(ROM): $(OBJS)
	$(LCC) $(LINKFLAGS) -o $@ $(OBJS)
	@echo "Built $@"

clean:
	rm -rf $(BUILD)
