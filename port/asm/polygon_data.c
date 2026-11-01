/*
 * POLYGON.ASM's data: draw_polygon's state words (polygon.c), all zero at the start as in
 * the original.
 */
#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GFX/RASTER/POLYGON.H"
#include "bak_game_end.h"

unsigned short g_wDrawPolyTopVertexOff;
unsigned short g_wDrawPolyBottomVertexOff;
unsigned short g_wDrawPolyChain1VertexCount;
unsigned short g_wDrawPolyChain2VertexCount;
unsigned short g_wDrawPolyChainRemaining;
unsigned short g_wDrawPolyEdgeWalkOff;
unsigned short g_wRasterEdgeColumnOff;
short g_nDrawPolyLastVertexX;
short g_nDrawPolyLastVertexY;
unsigned short g_wDrawPolySpanBufRetSeg;
unsigned short g_wDrawPolyOutlineVertexCount;
unsigned short g_wDrawPolyClipReplayByteCount;
unsigned char g_bRasterSpanDiStride;
unsigned char g_bDrawPolyReplayPending;
