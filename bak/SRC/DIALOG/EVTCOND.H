#ifndef BAK_EVTCOND_H
#define BAK_EVTCOND_H

#include "structs.h"

extern s16 evtcond_range_d_read_handler(u16 cond);
extern void evtcond_range_d_write_stub(u16 event_id, u16 value);
extern void evtcond_dialog_action_dispatch(DdxOp far *action_record);
extern void evtcond_dispatch_key_to_handler(u16 event_id);
extern void evtcond_pty_inv_repair_cnt(s16 *count_out, s16 *repair_out, s16 do_repair);
extern void evtcond_pty_dirty_flags_process(void);

#endif
