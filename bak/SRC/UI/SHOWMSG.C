#include "structs.h"
#include "SRC/UI/SHOWMSG.H"
#include "SRC/DIALOG/DIALOG.H"

void far show_message_plus_1600000(s16 msg_id) {
    dialog_play_record((s32)msg_id + 1600000, 1);
}
