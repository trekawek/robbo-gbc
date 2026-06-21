#ifndef OBJECT_TABLES_H
#define OBJECT_TABLES_H

/* LOOK[b] = Atari screen code for object byte b (0..127).
   Low 7 bits = glyph index in the 128-char playfield font (S.FNT order);
   bit 7 set = "inverse" colour (uses the PF3 palette).  Ported from R2.ASM LOOK. */
extern const unsigned char LOOK[128];

/* NEXT[b-'a'] = the byte an animation cell ('a'..'z') becomes next tick.
   Ported from R2.ASM NEXT. */
extern const unsigned char NEXT[26];

/* WHAT[b]: per-tile property (R2.ASM).  bit0 = destructible by a bullet,
   bit1 = destructible by a bomb blast. */
extern const unsigned char WHAT[128];

/* Object byte codes (ATASCII) used throughout the engine. */
#define OB_WALL    0xA0   /* solid wall (rendered as glyph 0, the per-level shape) */
#define OB_SPACE   0x20   /* empty */
#define OB_ROBBO   0x2A   /* '*' player start marker */
#define OB_ROBBO_S 27     /* 0x1B robbo standing (post-entry), LOOK -> waitnfac */
#define OB_AMMO    0x21   /* '!' ammo pack */
#define OB_BOX     0x23   /* '#' pushable box */
#define OB_SCREW   0x24   /* '$' screw (collect all to open exit) */
#define OB_GROUND  0x25   /* '%' diggable ground */
#define OB_LIFE    0x2B   /* '+' extra life */
#define OB_KEY     0x3D   /* '=' key */
#define OB_DOOR_H  0x12   /* '─' horizontal door */
#define OB_DOOR_V  0x7C   /* '|' vertical door */
#define OB_BOMB    0x40   /* '@' bomb */
#define OB_QUEST   0x3F   /* '?' surprise capsule */
#define OB_EXIT    0x14   /* exit capsule active glyph (LOOK 20/21 = wyjscie) */

#endif
