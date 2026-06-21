# Robbo for Game Boy Color — build via GBDK-2020
GBDK   := third_party/gbdk
LCC    := $(GBDK)/bin/lcc
PY     := python3

SRCDIR := src
GENDIR := src/gen
BUILD  := build
ROM    := $(BUILD)/robbo.gbc

# CGB ROM, MBC5 (0x1B), enough banks for level data
LCCFLAGS := -Wm-yc -Wl-yt0x1B -Wl-yo16 -Wl-ya4 -DCGB
# -autobank: let bankpack place each `#pragma bank 255` module (the level packs)
# into a free ROM bank automatically; HOME code stays in bank 0
LINKFLAGS := $(LCCFLAGS) -autobank

# Original game source (for asset conversion)
ORIG := /tmp/lkavalon-atari/robbo

# Generated sources
GEN := $(GENDIR)/gfx_tiles.c $(GENDIR)/levels_c1.c \
       $(GENDIR)/levels_c2.c $(GENDIR)/levels_c3.c
# Generated headers (included only, no matching .c)
GENH := $(GENDIR)/instr.h $(GENDIR)/sounds.h
HANDSRC := $(wildcard $(SRCDIR)/*.c)
SRCS := $(HANDSRC) $(GEN)
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRCS)))

VPATH := $(SRCDIR):$(GENDIR)

.PHONY: all clean assets run

all: $(ROM)

# --- asset generation ---
assets: $(GEN) $(GENH)

$(GENDIR)/gfx_tiles.c $(GENDIR)/gfx_tiles.h: tools/convert_font.py
	$(PY) tools/convert_font.py "$(ORIG)" $(GENDIR)

$(GENDIR)/instr.h: tools/extract_instr.py
	$(PY) tools/extract_instr.py "$(ORIG)" $(GENDIR)

$(GENDIR)/sounds.h: tools/convert_sound.py
	$(PY) tools/convert_sound.py "$(ORIG)" $(GENDIR)

$(GENDIR)/levels_c1.c $(GENDIR)/levels_c1.h: tools/convert_levels.py
	$(PY) tools/convert_levels.py "$(ORIG)/d2/C1.txt" $(GENDIR) c1

$(GENDIR)/levels_c2.c $(GENDIR)/levels_c2.h: tools/convert_levels.py
	$(PY) tools/convert_levels.py "$(ORIG)/d2/C2.txt" $(GENDIR) c2

$(GENDIR)/levels_c3.c $(GENDIR)/levels_c3.h: tools/convert_levels.py
	$(PY) tools/convert_levels.py "$(ORIG)/d2/C3.txt" $(GENDIR) c3

# --- compile ---
$(BUILD)/%.o: %.c | $(GEN) $(GENH)
	@mkdir -p $(BUILD)
	$(LCC) $(LCCFLAGS) -c -o $@ $<

# rebuild title.o when the generated instruction text changes
$(BUILD)/title.o: $(GENDIR)/instr.h
# rebuild sound.o when the generated sound tables change
$(BUILD)/sound.o: $(GENDIR)/sounds.h

$(ROM): $(OBJS)
	$(LCC) $(LINKFLAGS) -o $@ $(OBJS)
	@echo "Built $@"

run: $(ROM)
	bgb $(ROM)

clean:
	rm -rf $(BUILD) $(GENDIR)/*.c $(GENDIR)/*.h
