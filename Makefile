# Robbo for Game Boy Color - a port of GNU Robbo's game logic, built with GBDK-2020.
GBDK   := third_party/gbdk
LCC    := $(GBDK)/bin/lcc
PY     := python3
SRCDIR := src
GENDIR := src/gen
BUILD  := build
ROM    := $(BUILD)/robbo.gbc

# Assets converted at build time: the Atari original supplies the font, sound
# tables and instruction text; the levels come from GNU Robbo's original.dat.
ORIG     ?= $(HOME)/dev/lkavalon-atari/robbo
GNUROBBO ?= $(HOME)/dev/gnurobbo-0.66

LCCFLAGS  := -Wm-yc -Wl-yt0x1B -Wl-yo16 -Wl-ya4 -DCGB -I$(SRCDIR) -Wf--opt-code-size
LINKFLAGS := $(LCCFLAGS) -autobank
# board.c is one huge translation unit (GNU Robbo's 940-line update_game, split
# into upd_g1..g5): cap SDCC register allocation so it compiles in ~1min.
BOARDFLAGS := $(LCCFLAGS) -Wf--max-allocs-per-node3000

# generated assets
GFX    := $(GENDIR)/gfx_tiles.c
GFXH   := $(GENDIR)/gfx_tiles.h
SOUNDH := $(GENDIR)/sounds.h
INSTRH := $(GENDIR)/instr.h
# generated level data (from GNU Robbo original.dat): HOME index + banked grids
LEVELS := $(SRCDIR)/levels_idx.c $(SRCDIR)/levels_d0.c $(SRCDIR)/levels_d1.c $(SRCDIR)/levels_d2.c

HAND := $(SRCDIR)/globals.c $(SRCDIR)/glue.c $(SRCDIR)/render.c $(SRCDIR)/loader.c \
        $(SRCDIR)/hud.c $(SRCDIR)/atari_pal.c $(SRCDIR)/menu.c \
        $(SRCDIR)/object_tables.c $(SRCDIR)/sound.c
SRCS := $(HAND) $(LEVELS) $(GFX)
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRCS))) \
        $(BUILD)/board.o $(BUILD)/board_upd.o $(BUILD)/board_robbo.o
# every object includes one or more generated headers (below: order-only dep)
GENHDRS := $(GFXH) $(SOUNDH) $(INSTRH) $(SRCDIR)/levels_data.h

.PHONY: all clean
all: $(ROM)

# make the generated headers exist before any compile (order-only: regenerating
# a header doesn't force a full rebuild).  Placed after `all` so it stays default.
$(OBJS): | $(GENHDRS)

$(BUILD):
	@mkdir -p $(BUILD)

# --- asset generation ---
$(GFX) $(GFXH): tools/convert_font.py
	$(PY) tools/convert_font.py "$(ORIG)" $(GENDIR)

$(SOUNDH): tools/convert_sound.py
	$(PY) tools/convert_sound.py "$(ORIG)" $(GENDIR)

$(INSTRH): tools/extract_instr.py
	$(PY) tools/extract_instr.py "$(ORIG)" $(GENDIR)

$(LEVELS) $(SRCDIR)/levels_data.h: tools/convert_gnu_levels.py
	$(PY) tools/convert_gnu_levels.py "$(GNUROBBO)/data/levels/original.dat" $(SRCDIR)

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
$(BUILD)/render.o: $(GFXH)
$(BUILD)/menu.o:   $(INSTRH)
$(BUILD)/sound.o:  $(SOUNDH)

$(ROM): $(OBJS)
	$(LCC) $(LINKFLAGS) -o $@ $(OBJS)
	@echo "Built $@"

clean:
	rm -rf $(BUILD)
