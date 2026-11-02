/*
 * ROTBLIT.ASM's data in C, laid out as the original's (rotblit.c says why): the corners of the
 * last sprite drawn rotated and which is which (1.00's DGROUP 2F4Ch-2F63h), and the source
 * tables it shares with STRBLIT.C (the BSS's X table, then its Y table). The original's pointer
 * to its own body (a far jump's target) and its four tables of the octants' code addresses have
 * no native use: the translation switches on the octant.
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GFX/SPRITE/ROTBLIT.H"
#include "bak_game_end.h"

short g_anRotBlitState[12];
short g_aStretchSrcTables[640 + 400];
