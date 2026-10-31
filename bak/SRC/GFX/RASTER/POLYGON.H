#ifndef BAK_POLYGON_H
#define BAK_POLYGON_H

#include "structs.h"

extern u16 g_wDrawPolyTopVertexOff;
extern u16 g_wDrawPolyBottomVertexOff;
extern u16 g_wDrawPolyChain1VertexCount;
extern u16 g_wDrawPolyChain2VertexCount;
extern u16 g_wDrawPolyChainRemaining;
extern u16 g_wDrawPolyEdgeWalkOff;
extern u16 g_wRasterEdgeColumnOff;
extern s16 g_nDrawPolyLastVertexX;
extern s16 g_nDrawPolyLastVertexY;
extern u16 g_wDrawPolySpanBufRetSeg;
extern u16 g_wDrawPolyOutlineVertexCount;
extern u16 g_wDrawPolyClipReplayByteCount;
extern unsigned char g_bRasterSpanDiStride;
extern unsigned char g_bDrawPolyReplayPending;

extern void fill_spans_dispatch(u16 off, u16 seg);
extern void blit_rect_buf_dispatch(unsigned char far *far_ptr);
extern s16 draw_polygon(s16 count, s16 *xs, s16 *ys);
extern void near draw_polygon_edges(void);
extern void near state_machine_enter_state2_a(void);
extern void near rasterize_polygon_edge(void);
extern void near state_machine_enter_state2_b(void);
extern void near rasterize_polygon_edge_steep(void);
extern void near rasterize_polygon_edge_steep_b(void);
extern void near state_machine_disp_tbl_5d80(void);

#endif
