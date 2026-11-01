#include <dos.h>
#include <mem.h>
#include <stdlib.h>
#include "SRC/GEN/GFXCTX.H"
#include "SRC/SYS/SYSLOWIO.H"
#include "SRC/SCRIPT/TTM.H"
#include "SRC/SCRIPT/TTMDLG.H"
#include "structs.h"

#include "SRC/SCRIPT/ANIMSCR.H"
#include "SRC/SYS/RAND.H"
#include "SRC/IO/RESOURCE.H"
#include "SRC/SYS/FARTHUNK.H"
#include "SRC/SYS/DOSMEM.H"
#include "SRC/STREAM/RESLOAD/IFFREAD.H"
#include "SRC/STREAM/CODEC/STREAM.H"
#include "SRC/SCRIPT/ADSCRIPT.H"


short g_nScriptPendingJumpTarget;
unsigned short g_wScriptFrameHoldFlag;
short g_nCombatResolveStaticThreshold;
short g_nChapterSnapshotDeadStore;
Slot far *g_anim_slots[2];
unsigned short g_wScriptYieldPending;
unsigned short g_wAnimResSfxId;
ScriptAnimNode *g_pScriptAnimWalker;
ScriptAnimNode *g_pScriptAnimTarget;
short g_nAnimIterCursor;

short g_nCurrentChannelSlot = 0;
short g_current_animation_slot = -1;
short g_aScriptResourceOpcodes[2] = {8192, 8197};

s16 anim_script_subsystem_init(void) {
    anim_script_close_all();
    adscript_set_dbb_if_202d_eq_0c();
    return 1;
}

void anim_script_close_all(void) {
    s16 i;

    i = 0;
    do {
        anim_script_close(i + 1);
        i++;
    } while (i < 1);
    adscript_drain_object_list();
    return;
}

static s16 anim_script_register_resource(char *name, unsigned short resource_id) {
    if (adscript_resource_load(name) < 0) {
        return -1;
    }
    g_pCurScriptObject->wResourceId = resource_id;
    adscript_channel_dispatch_loop(resource_id);
    return resource_id;
}

s16 anim_script_open(char *filename, s16 mode) {
    s16 err;
    ResFile *fp;
    char *filenameArg;
    Slot far *slot;
    s16 streamId;
    u16 bytesRead;
    s16 slotIdx;
    char *namePtr;
    unsigned char far *scriptBuf;
    u16 scriptSize;
    s16 resourceCount;
    s16 newSlotIdx;
    char nameBuf[20];

    filenameArg = filename;
    err = 0;
    scriptBuf = (unsigned char far *)0;
    g_wScriptEnhancedAuxFlag = 0;
    slotIdx = 0;
    do {
        if (g_anim_slots[slotIdx] == (Slot far *)0)
            break;
        slotIdx++;
    } while (slotIdx < 1);
    if (slotIdx >= 1) {
        return 0;
    }
    newSlotIdx = slotIdx;
    if ((slot = (Slot far *)alloc_far(RECORD_SIZE(Slot, 0x472), ALLOC_FAR_ZERO_FILL)) == (Slot far *)0) {
        return 0;
    }
    g_anim_slots[newSlotIdx] = slot;
    streamId = -1;
    if ((fp = cached_file_open(filenameArg)) == (ResFile *)0) {
        _freemem(g_anim_slots[newSlotIdx]);
        g_anim_slots[newSlotIdx] = (Slot far *)0;
        return 0;
    }
    if (chunk_seek(fp, "ADS:RES:", 0) == -1) {
        err++;
    } else {
        res_fread(&resourceCount, 2, 1, fp);
        g_pScriptObjectChainHead = (ScriptAnimNode *)0;
        g_pScriptObjectListHead = (ScriptObject far *)0;
        while (resourceCount-- != 0 && err == 0) {
            res_fread(&g_wAnimResSfxId, 2, 1, fp);
            namePtr = nameBuf;
            do {
                res_fread(namePtr, 1, 1, fp);
            } while (*namePtr++ != '\0');
            if (anim_script_register_resource(nameBuf, g_wAnimResSfxId) == -1) {
                err++;
                break;
            }
        }
        slot->wObjects_head_alt = (NearPtr)g_pScriptObjectChainHead;
        slot->pObjects_head = g_pScriptObjectListHead;
    }
    if (err == 0) {
        if (chunk_seek(fp, "ADS:SCR:", 0) == -1)
            goto scr_fail;
        if ((streamId = stream_open(0, fp, "r", cached_file_chunk_size(fp))) >= 0)
            goto scr_ok;
    scr_fail:
        err++;
        goto cleanup;
    scr_ok:
        scriptSize = (u16)stream_size(streamId);
        scriptBuf = (unsigned char far *)alloc_far((s32)(s16)scriptSize, 0);
        if (scriptBuf == (unsigned char far *)0)
            goto scr_fail;
        bytesRead = stream_read(streamId, scriptBuf, scriptSize);
        if (bytesRead != scriptSize) {
            _freemem(scriptBuf);
            goto scr_fail;
        }
        slot->pScript_buf = (unsigned short far *)scriptBuf;
        slot->pScript_end = (unsigned short far *)(scriptBuf + scriptSize - 1);
        slot->nInitialised = 0;
        goto cleanup;
    }
cleanup:
    if (streamId >= 0) {
        stream_close(streamId);
    }
    if (fp != (ResFile *)0) {
        cached_file_close(fp);
    }
    anim_script_activate(0);
    if (err != 0) {
        anim_script_close_all();
        return 0;
    }
    return slotIdx + 1;
}

static unsigned short far *anim_script_step_opcode(unsigned short far *pc) {
    short op;

    if (g_anim_slots[g_current_animation_slot]->pScript_end < pc || pc == (unsigned short far *)0) {
        return (unsigned short far *)0;
    }
    op = *pc;
    switch (op) {
    case 0x2000:
    case 0x2005:
        pc += 5;
        break;
    case 0x2010:
    case 0x2015:
    case 0x2020:
    case 0x4000:
    case 0x4010:
        pc += 4;
        break;
    case 0x1010:
    case 0x1020:
    case 0x1030:
    case 0x1040:
    case 0x1050:
    case 0x1060:
    case 0x1070:
    case 0x1310:
    case 0x1320:
    case 0x1330:
    case 0x1340:
    case 0x1350:
    case 0x1360:
    case 0x1370:
        pc += 3;
        break;
    case 0xf010:
    case 0xf200:
    case 0xf210:
    case 0x1080:
    case 0x1380:
    case 0x1390:
    case 0x13a0:
    case 0x13a1:
    case 0x13b0:
    case 0x13b1:
    case 0x13c0:
    case 0x13c1:
    case 0x3020:
        pc += 2;
        break;
    case 0xf000:
        pc += 1;
        break;
    default:
        pc += 1;
        break;
    }
    return pc;
}

static ScriptAnimNode **anim_script_gather_refd_objects(unsigned short far *pc) {
    ScriptAnimNode **result;
    s16 opcode;
    unsigned short wTag;
    unsigned short wIdB;
    ScriptAnimNode *nodes[100];
    ScriptAnimNode **pSlot;
    ScriptAnimNode *pNode;
    u16 opIdx;
    s16 count;

    result = (ScriptAnimNode **)0x0;
    memset(nodes, 0, 100 * NEAR_PTR_SIZE);
    while ((opcode = *pc) != -1) {
        wTag = pc[1];
        wIdB = pc[2];
        pc = anim_script_step_opcode(pc);
        opIdx = 0;
        do {
            if (g_aScriptResourceOpcodes[opIdx] == opcode) {
                pNode = anim_script_find_object(wTag, wIdB);
                for (pSlot = nodes; (*pSlot != 0 && (pNode != *pSlot)); pSlot = pSlot + 1) {
                }
                *pSlot = pNode;
            }
            opIdx = opIdx + 1;
        } while (opIdx < 2);
    }
    pSlot = nodes;
    count = 0;
    while (*pSlot++ != 0) {
        count = count + 1;
    }
    if (count != 0) {
        if ((result = my_calloc(count + 1, NEAR_PTR_SIZE)) != (ScriptAnimNode **)0x0) {
            memcpy(result, nodes, count << NEAR_PTR_SHIFT);
        }
    }
    return result;
}

s16 anim_script_activate(s16 slot_plus_one) {
    Slot far *slot;
    unsigned short far *pc;
    NearPtr ops;
    s16 ch;

    ch = 0;
    if (slot_plus_one == 0) {
        if (g_current_animation_slot >= 0) {
            g_pScriptObjectListHead = g_anim_slots[g_current_animation_slot]->pObjects_head;
        }
        return g_current_animation_slot + 1;
    }

    slot_plus_one = slot_plus_one - 1;
    if (slot_plus_one < 0 || slot_plus_one > 1 || g_anim_slots[slot_plus_one] == (Slot far *)0) {
        return 0;
    }

    slot = g_anim_slots[slot_plus_one];
    g_current_animation_slot = slot_plus_one;
    g_pScriptObjectListHead = slot->pObjects_head;

    if (slot->nInitialised != 0) {
        return slot_plus_one + 1;
    }

    pc = slot->pScript_buf + 1;
    slot->pChannels[0] = pc;
    slot->pOp_lists[0] = ops = (NearPtr)anim_script_gather_refd_objects(pc);

    while (slot->pScript_end > pc) {
        if (*(s16 far *)pc == -1) {
            ch = ch + 1;
            pc += 2;
            if (slot->pScript_end > pc) {
                slot->pChannels[ch] = pc;
                slot->pOp_lists[ch] = ops = (NearPtr)anim_script_gather_refd_objects(pc);
            }
        } else {
            pc = anim_script_step_opcode(pc);
        }
    }
    slot->nChannel_count = ch;

    for (; ch < 0x50; ch++) {
        slot->pChannels[ch] = (unsigned short far *)0;
    }
    for (ch = 0; ch < 0x50; ch++) {
        slot->pChannel_state[ch] = 8;
    }
    anim_script_reset_all_objects();
    slot->nInitialised = 1;
    return ++slot_plus_one;
}

void anim_script_close(s16 slot_plus_one) {
    Slot far *slot;
    s16 i;

    if (slot_plus_one == 0) {
        slot_plus_one = g_current_animation_slot;
    } else {
        slot_plus_one = slot_plus_one - 1;
    }
    if (slot_plus_one < 0 || 1 <= slot_plus_one) {
        return;
    }
    if (g_anim_slots[slot_plus_one] == (Slot far *)0) {
        return;
    }
    slot = g_anim_slots[slot_plus_one];
    g_pScriptObjectChainHead = (ScriptAnimNode *)slot->wObjects_head_alt;
    g_pScriptObjectListHead = slot->pObjects_head;
    adscript_drain_object_list();
    for (i = 0; i < 0x50; i++) {
        if (slot->pOp_lists[i] != 0) {
            my_free((void *)slot->pOp_lists[i]);
        }
    }
    if (slot->pScript_buf != (unsigned short far *)0) {
        _freemem(slot->pScript_buf);
    }
    _freemem(slot);
    g_anim_slots[slot_plus_one] = (Slot far *)0;
    if (slot_plus_one == g_current_animation_slot) {
        g_current_animation_slot = -1;
    }
}

s16 anim_script_channel_is_busy(s16 channel_id) {
    Slot far *slot;
    u16 channel_state;
    ScriptAnimNode **pNodeIter;
    ScriptAnimNode *pNode;

    channel_id = anim_script_find_channel(channel_id);
    if (channel_id == -1) {
        return 0;
    }
    slot = g_anim_slots[g_current_animation_slot];
    channel_state = slot->pChannel_state[channel_id] & 0xfff7;
    if (channel_state == 4 || channel_state == 1) {
        return 1;
    }
    if ((pNodeIter = (ScriptAnimNode **)slot->pOp_lists[channel_id]) != (ScriptAnimNode **)0) {
        while ((pNode = *pNodeIter++) != (ScriptAnimNode *)0) {
            if (pNode->wMode == 4) {
                continue;
            }
            if (pNode->wMode == 0) {
                continue;
            }
            if (pNode->wFld_0c == 0) {
                return 1;
            }
        }
    }
    return 0;
}

static void anim_script_channel_set_state_or(s16 slot, s16 channel_id, u16 new_state) {
    u16 old;
    Slot far *base;

    channel_id = anim_script_find_channel(channel_id);
    if (channel_id != -1) {
        base = g_anim_slots[slot];
        ((s16 far *)&base->pChannel_alt[channel_id])[1] = 0;
        ((s16 far *)&base->pChannel_alt[channel_id])[0] = 0;
        old = base->pChannel_state[channel_id];
        base->pChannel_state[channel_id] = old & 8 | new_state;
    }
    return;
}

void anim_script_channel_start(s16 channel_id) {
    anim_script_channel_set_state_or(g_current_animation_slot, channel_id, 3);
    return;
}

void anim_script_chan_request_pause(s16 channel_id) {
    anim_script_channel_set_state_or(g_current_animation_slot, channel_id, 4);
    return;
}

static void anim_script_channel_set_state(s16 slot, s16 channel_id, u16 new_state) {
    Slot far *base;

    channel_id = anim_script_find_channel(channel_id);
    if (channel_id != -1) {
        base = g_anim_slots[slot];
        ((s16 far *)&base->pChannel_alt[channel_id])[1] = 0;
        ((s16 far *)&base->pChannel_alt[channel_id])[0] = 0;
        if (base->pChannel_state[channel_id] != 8) {
            base->pChannel_state[channel_id] = new_state;
        }
    }
    return;
}

void anim_script_channel_stop(s16 channel_id) {
    anim_script_channel_set_state(g_current_animation_slot, channel_id, 6);
}

void anim_script_channel_pause_after(s16 channel_id) {
    anim_script_channel_set_state(g_current_animation_slot, channel_id, 5);
    return;
}

static s16 anim_script_resolve_channel(s16 channel_ref) {
    s16 slotIdx;

    if (channel_ref == 0) {
        slotIdx = g_current_animation_slot;
    } else {
        slotIdx = channel_ref - 1;
    }
    if (g_anim_slots[slotIdx] == (Slot far *)0) {
        return -1;
    }
    return slotIdx;
}

void anim_script_start_all_channels(s16 channel_ref) {
    s16 channelCount;
    s16 channel_id;

    channel_ref = anim_script_resolve_channel(channel_ref);
    if (channel_ref >= 0) {
        channelCount = g_anim_slots[channel_ref]->nChannel_count;
        for (channel_id = 1; channel_id <= channelCount; channel_id++) {
            anim_script_channel_set_state_or(channel_ref, channel_id, 3);
        }
    }
    return;
}

void anim_script_pause_all_channels(s16 script_object_id) {
    s16 channelCount;
    s16 channel_id;

    script_object_id = anim_script_resolve_channel(script_object_id);
    if (script_object_id >= 0) {
        channelCount = g_anim_slots[script_object_id]->nChannel_count;
        for (channel_id = 1; channel_id <= channelCount; channel_id++) {
            anim_script_channel_set_state_or(script_object_id, channel_id, 4);
        }
    }
    return;
}

void anim_script_stop_all_channels(s16 script_object_id) {
    s16 channelCount;
    s16 channel_id;

    script_object_id = anim_script_resolve_channel(script_object_id);
    if (script_object_id >= 0) {
        channelCount = g_anim_slots[script_object_id]->nChannel_count;
        for (channel_id = 1; channel_id <= channelCount; channel_id++) {
            anim_script_channel_set_state(g_current_animation_slot, channel_id, 6);
        }
    }
    return;
}

void anim_script_pause_all_chans(s16 script_object_id) {
    s16 channelCount;
    s16 channel_id;

    script_object_id = anim_script_resolve_channel(script_object_id);
    if (script_object_id >= 0) {
        channelCount = g_anim_slots[script_object_id]->nChannel_count;
        for (channel_id = 1; channel_id <= channelCount; channel_id++) {
            anim_script_channel_set_state(g_current_animation_slot, channel_id, 5);
        }
    }
    return;
}

void anim_script_delete_all_channels(s16 script_object_id) {
    s16 channelCount;
    short savedSlot;
    s16 channel_id;

    script_object_id = anim_script_resolve_channel(script_object_id);
    if (script_object_id >= 0) {
        channelCount = g_anim_slots[script_object_id]->nChannel_count;
        for (channel_id = 1; channel_id <= channelCount; channel_id++) {
            anim_script_channel_set_state(script_object_id, channel_id, 8);
        }
        savedSlot = g_current_animation_slot;
        g_current_animation_slot = script_object_id;
        anim_script_reset_all_objects();
        g_current_animation_slot = savedSlot;
    }
    return;
}

static void anim_script_object_reset(ScriptAnimNode *pNode) {
    s16 i;

    if (pNode != (ScriptAnimNode *)0x0) {
        for (i = 3; i != 0; i = i + -1) {
            (&pNode->wFld_10)[i] = 0;
        }
        pNode->wPc = pNode->wPcSaved;
        pNode->wPcJumpTarget = 0xffff;
        pNode->bSavedFgColor = pNode->bSavedFillColor = 0xf;
        pNode->wFld_1c = 0;
        pNode->wInterval = 0;
        pNode->dwTimerExpiry = 0;
        pNode->wCounter = pNode->wCountdown = 0;
        pNode->wOpCursor = 0;
        pNode->wMode = 0;
        pNode->wOpState = 0;
        pNode->wFld_0c = 0;
        pNode->bSavedClipEnabled = 1;
        pNode->nSavedClipYmin = pNode->nSavedClipXmin = 0;
        pNode->nSavedClipYmax = g_wScreen_height - 1;
        pNode->nSavedClipXmax = g_wScreen_width - 1;
    }
    return;
}

void anim_script_reset_all_objects(void) {
    ScriptAnimNode *pNode;

    for (pNode = (ScriptAnimNode *)g_anim_slots[g_current_animation_slot]->wObjects_head_alt;
         pNode != (ScriptAnimNode *)0; pNode = pNode->pNext) {
        anim_script_object_reset(pNode);
    }
    return;
}

ScriptAnimNode *anim_script_find_object(unsigned short wTag, unsigned short wIdB) {
    ScriptAnimNode *pNode;

    pNode = (ScriptAnimNode *)g_anim_slots[g_current_animation_slot]->wObjects_head_alt;
    while (pNode != (ScriptAnimNode *)0) {
        if (pNode->wTag == wTag && pNode->wIdB == wIdB)
            break;
        pNode = pNode->pNext;
    }
    return pNode;
}

static s16 far *anim_script_advance_to_wait(s16 far *pc) {
    s16 far *startPc;

    startPc = pc;
    while (*pc != -1) {
        if (*pc == 5) {
            return pc + 1;
        }
        if ((pc = (s16 far *)anim_script_step_opcode((unsigned short far *)pc)) == (s16 far *)0) {
            return startPc;
        }
    }
    return startPc;
}

static s16 anim_script_branch_weight(s16 far *pc) {
    s16 opcode;
    s16 weightOffset;

    opcode = *pc;
    switch (opcode) {
    case 0x3020:
        weightOffset = 2;
        break;
    case 0x2000:
    case 0x2005:
        weightOffset = 8;
        break;
    default:
        weightOffset = 6;
    }
    if (weightOffset != 0) {
        return *(s16 far *)((char far *)pc + weightOffset);
    }
    return 0;
}

static s16 far *anim_script_pick_wtd_rand_br(s16 far *pc) {
    s16 far *result;
    s16 far *p0;
    s16 total;

    total = 0;
    p0 = pc;
    while (pc != (s16 far *)0 && *pc != 0x30ff) {
        total += anim_script_branch_weight(pc);
        if ((pc = (s16 far *)anim_script_step_opcode((unsigned short far *)pc)) == (s16 far *)0)
            return (s16 far *)0;
    }

    result = pc + 1;
    if (total != 0) {
        pc = p0;
        total = abs((s16)((u16)rand_fx() % (u16)total)) + 1;
        do {
            total -= anim_script_branch_weight(pc);
            if (total <= 0)
                break;
        } while ((pc = (s16 far *)anim_script_step_opcode((unsigned short far *)pc)) != (s16 far *)0);
        if (*pc != 0x3020)
            anim_script_dispatch_opcode((unsigned short far *)pc);
    }
    return result;
}

static void anim_script_object_arm_state(unsigned short obj_key1, unsigned short obj_key2, s16 mode) {
    g_pCurScriptAnimNode = anim_script_find_object(obj_key1, obj_key2);
    if (g_pCurScriptAnimNode != (ScriptAnimNode *)0) {
        if (mode == 0) {
            g_pCurScriptAnimNode->wMode = 1;
        } else if (mode > 0) {
            g_pCurScriptAnimNode->wMode = 2;
            g_pCurScriptAnimNode->wCountdown = mode - 1;
        } else {
            g_pCurScriptAnimNode->wMode = 3;
            g_pCurScriptAnimNode->dwDeadline = g_dwSysTickCount - mode;
        }
        g_pCurScriptAnimNode->wCounter++;
    }
    return;
}

static void anim_script_object_restart(unsigned short obj_key1, unsigned short obj_key2, s16 mode) {
    g_pCurScriptAnimNode = anim_script_find_object(obj_key1, obj_key2);
    if (g_pCurScriptAnimNode != (ScriptAnimNode *)0) {
        g_pCurScriptAnimNode->wPc = g_pCurScriptAnimNode->wPcSaved;
        anim_script_object_arm_state(obj_key1, obj_key2, mode);
    }
    return;
}

static void anim_script_object_set_state5(unsigned short id, unsigned short p2) {
    g_pCurScriptAnimNode = anim_script_find_object(id, p2);
    if (g_pCurScriptAnimNode != (ScriptAnimNode *)0) {
        g_pCurScriptAnimNode->wMode = 5;
    }
    return;
}

static void anim_script_object_set_state0(unsigned short id, unsigned short p2) {
    g_pCurScriptAnimNode = anim_script_find_object(id, p2);
    if (g_pCurScriptAnimNode != (ScriptAnimNode *)0) {
        g_pCurScriptAnimNode->wMode = 0;
    }
    return;
}

static void anim_script_object_reset_by_id(unsigned short id, unsigned short p2) {
    g_pCurScriptAnimNode = anim_script_find_object(id, p2);
    if (g_pCurScriptAnimNode != (ScriptAnimNode *)0) {
        anim_script_object_reset(g_pCurScriptAnimNode);
    }
    return;
}

static void anim_script_object_move_to_front(unsigned short category, unsigned short object_id) {
    g_pScriptAnimWalker =
        (ScriptAnimNode *)g_anim_slots[g_current_animation_slot]->wObjects_head_alt;
    g_pScriptAnimTarget = anim_script_find_object(category, object_id);
    if ((g_pScriptAnimWalker != (ScriptAnimNode *)0) &&
        (g_pScriptAnimTarget != (ScriptAnimNode *)0)) {
        for (; g_pScriptAnimWalker->pNext != (ScriptAnimNode *)0;
             g_pScriptAnimWalker = g_pScriptAnimWalker->pNext) {
            if (g_pScriptAnimWalker->pNext == g_pScriptAnimTarget) {
                g_pScriptAnimWalker->pNext = g_pScriptAnimWalker->pNext->pNext;
                g_pScriptAnimTarget->pNext =
                    (ScriptAnimNode *)g_anim_slots[g_current_animation_slot]->wObjects_head_alt;
                g_anim_slots[g_current_animation_slot]->wObjects_head_alt =
                    (NearPtr)g_pScriptAnimTarget;
                return;
            }
        }
    }
    return;
}

static void anim_script_object_move_to_end(unsigned short category, unsigned short object_id) {
    g_pScriptAnimWalker =
        (ScriptAnimNode *)g_anim_slots[g_current_animation_slot]->wObjects_head_alt;
    g_pScriptAnimTarget = anim_script_find_object(category, object_id);
    if (g_pScriptAnimWalker != 0 && g_pScriptAnimTarget != 0) {
        if (g_pScriptAnimWalker == g_pScriptAnimTarget) {
            g_pScriptAnimWalker =
                (ScriptAnimNode *)(g_anim_slots[g_current_animation_slot]->wObjects_head_alt =
                                       (NearPtr)g_pScriptAnimTarget->pNext);
        } else {
            for (; g_pScriptAnimWalker->pNext != 0;
                 g_pScriptAnimWalker = g_pScriptAnimWalker->pNext) {
                if (g_pScriptAnimWalker->pNext == g_pScriptAnimTarget) {
                    g_pScriptAnimWalker->pNext = g_pScriptAnimWalker->pNext->pNext;
                }
            }
        }
        for (g_pScriptAnimWalker =
                 (ScriptAnimNode *)g_anim_slots[g_current_animation_slot]->wObjects_head_alt;
             g_pScriptAnimWalker->pNext != 0; g_pScriptAnimWalker = g_pScriptAnimWalker->pNext) {
        }
        g_pScriptAnimWalker->pNext = g_pScriptAnimTarget;
        g_pScriptAnimTarget->pNext = 0;
    }
}

static s16 far *anim_script_run_opcodes_dispatch(s16 far *script_pc, s16 flag) {
    while (*script_pc != 0) {
        if (script_pc == (s16 far *)0) {
            return (s16 far *)0;
        }
        switch (*script_pc) {
        case 0x1510:
            return script_pc;
        case 0x1500:
            return script_pc;
        case -1:
            return (s16 far *)0;
        case 0x1310:
        case 0x1320:
        case 0x1330:
        case 0x1340:
        case 0x1350:
        case 0x1360:
        case 0x1370:
        case 0x1380:
        case 0x1390:
        case 0x13a0:
        case 0x13a1:
        case 0x13b0:
        case 0x13b1:
        case 0x13c0:
        case 0x13c1:
            flag++;
            script_pc = anim_script_combat_resolve_loop(script_pc);
            script_pc = anim_script_run_opcodes_dispatch(script_pc, flag);
            if (*(unsigned short far *)script_pc == 0x1500) {
                script_pc = anim_script_run_opcodes_dispatch(
                    (s16 far *)((unsigned short far *)script_pc + 1), flag);
            }
            script_pc = (s16 far *)anim_script_step_opcode((unsigned short far *)script_pc);
            continue;
        }
        if ((script_pc = (s16 far *)anim_script_step_opcode((unsigned short far *)script_pc)) ==
            (s16 far *)0) {
            return (s16 far *)0;
        }
    }
    return (s16 far *)0;
}

static s16 far *anim_script_run_to_opcode_1520(s16 far *pc) {
    while (*pc != 0) {
        switch (*pc) {
        case 0x1520:
            return pc;
        case -1:
            return (s16 far *)0;
        }
        if ((pc = (s16 far *)anim_script_step_opcode((unsigned short far *)pc)) == (s16 far *)0)
            return (s16 far *)0;
    }
    return (s16 far *)0;
}

static s16 far *anim_script_opcode_1500_follow(s16 far *script_pc) {
    script_pc = anim_script_run_opcodes_dispatch(script_pc, 0);
    if (*script_pc == 0x1500) {
        script_pc = (s16 far *)anim_script_channel_step((unsigned short far *)(script_pc + 1));
    }
    return script_pc;
}

s16 far *anim_script_combat_resolve_loop(s16 far *script_pc) {
    ScriptAnimNode *node;
    u16 op;
    u16 wTag;
    u16 wIdB;
    s16 latchResult;
    u16 matched;

    latchResult = 0;
    matched = 0;

restart:
    op = *(u16 far *)script_pc;
    wTag = ((u16 far *)script_pc)[1];
    if (op != 0x1080 && op != 0x1380 && op != 0x1390 && op != 0x13a0 && op != 0x13b0 &&
        op != 0x13c0 && op != 0x13a1 && op != 0x13b1 && op != 0x13c1) {
        wIdB = ((u16 far *)script_pc)[2];
        node = anim_script_find_object(wTag, wIdB);
    }

    switch (op) {
    case 0x1380:
        if (g_nCombatResolveStaticThreshold > (s16)wTag)
            goto step;
    accept:
        matched = 1;
        goto step;
    case 0x1390:
        if (g_nCombatResolveStaticThreshold >= (s16)wTag)
            goto accept;
        goto step;
    case 0x13a0:
    case 0x13a1: {
        u16 idx = op & 0xf;
        if (*(u16 *)((char *)g_anim_slots + idx * 2 + 6) <= wTag)
            goto accept;
        goto step;
    }
    case 0x13b0:
    case 0x13b1: {
        u16 idx = op & 0xf;
        if (*(u16 *)((char *)g_anim_slots + idx * 2 + 6) >= wTag)
            goto accept;
        goto step;
    }
    case 0x13c0:
    case 0x13c1: {
        u16 idx = op & 0xf;
        if (*(u16 *)((char *)g_anim_slots + idx * 2 + 6) == wTag)
            goto accept;
        goto step;
    }
    case 0x1080:
        if (*(s16 far *)&g_anim_slots[g_current_animation_slot]
                 ->pChannel_b2[g_nCurrentChannelSlot] <= (s16)wTag)
            goto accept;
        goto step;
    case 0x1030:
    case 0x1330:
        if (node != (ScriptAnimNode *)0) {
            if (node->wCounter != 0)
                goto step;
        }
        matched = 1;
        goto step;
    case 0x1040:
    case 0x1340:
        if (node == (ScriptAnimNode *)0 || node->wCounter == 0)
            goto step;
        matched = 1;
        goto step;
    case 0x1060:
    case 0x1360:
        if (node != (ScriptAnimNode *)0) {
            if (node->wMode != 4) {
                if (node->wMode != 0)
                    goto step;
            }
        }
        matched = 1;
        goto step;
    case 0x1070:
    case 0x1370:
        if (node == (ScriptAnimNode *)0 ||
            (node->wMode != 1 && node->wMode != 2 && node->wMode != 3))
            goto step;
        matched = 1;
        goto step;
    case 0x1020:
    case 0x1320:
        if (node != (ScriptAnimNode *)0) {
            if (node->wMode == 5)
                goto step;
        }
        matched = 1;
        goto step;
    case 0x1010:
    case 0x1310:
        if (node == (ScriptAnimNode *)0 || node->wMode != 5)
            goto step;
        matched = 1;
        goto step;
    case 0x1050:
    case 0x1350:
        if (node == (ScriptAnimNode *)0 || node->wMode != 4)
            goto step;
        matched = 1;
        goto step;
    default:
        goto step;
    }

step:
    if ((script_pc = (s16 far *)anim_script_step_opcode((u16 far *)script_pc)) == (s16 far *)0)
        return (s16 far *)0;

    op = *(u16 far *)script_pc;
    if (op != 0x1430 && op != 0x1420) {
        *(u16 *)&g_anim_slots[1] = matched;
    } else if (matched != 0) {
        if (op == 0x1430) {
            latchResult = 1;
            if ((script_pc = (s16 far *)anim_script_step_opcode((u16 far *)script_pc)) !=
                (s16 far *)0)
                goto step;
            return (s16 far *)0;
        } else if (op == 0x1420) {
            matched = 0;
            if ((script_pc = (s16 far *)anim_script_step_opcode((u16 far *)script_pc)) !=
                (s16 far *)0)
                goto restart;
            return (s16 far *)0;
        }
    } else {
        if (op == 0x1430) {
            if ((script_pc = (s16 far *)anim_script_step_opcode((u16 far *)script_pc)) !=
                (s16 far *)0)
                goto restart;
            return (s16 far *)0;
        } else if (op == 0x1420) {
            if ((script_pc = (s16 far *)anim_script_step_opcode((u16 far *)script_pc)) !=
                (s16 far *)0)
                goto step;
            return (s16 far *)0;
        }
    }

    if (latchResult)
        *(u16 *)&g_anim_slots[1] = 1;
    return script_pc;
}

unsigned short far *anim_script_dispatch_opcode(unsigned short far *script_pc) {
    s16 op;
    s16 ch;
    Slot far *slot;
    unsigned short far *saved_pc;
    unsigned short far *cell;

    if (script_pc == (unsigned short far *)0) {
        goto noop;
    }
    op = *script_pc;
    switch (op) {
    case 0x0001:
    case 0x0005:
        return script_pc + 1;

    case 0xffff:
        return (unsigned short far *)0;

    case 0x1500:
        script_pc = (unsigned short far *)anim_script_run_opcodes_dispatch((s16 far *)(script_pc + 1), 0);
        g_wScriptYieldPending++;
        break;

    case 0x1510:
        g_wScriptYieldPending++;
        break;

    case 0x1520:
        g_wScriptYieldPending++;
        goto noop;

    case 0x2000:
        anim_script_object_restart(script_pc[1], script_pc[2], script_pc[3]);
        goto adv5;

    case 0x2005:
        anim_script_object_arm_state(script_pc[1], script_pc[2], script_pc[3]);
    adv5:
        return script_pc + 5;

    case 0x2010:
        anim_script_object_set_state0(script_pc[1], script_pc[2]);
        goto adv4;

    case 0x2015:
        anim_script_object_set_state5(script_pc[1], script_pc[2]);
        goto adv4;

    case 0x2020:
        anim_script_object_reset_by_id(script_pc[1], script_pc[2]);
        goto adv4;

    case 0x4000:
        anim_script_object_move_to_end(script_pc[1], script_pc[2]);
        goto adv4;

    case 0x4010:
        anim_script_object_move_to_front(script_pc[1], script_pc[2]);
    adv4:
        return script_pc + 4;

    case 0x3010:
        script_pc = (unsigned short far *)anim_script_pick_wtd_rand_br((s16 far *)(script_pc + 1));
        break;

    case 0x1420:
    case 0x1430:
    case 0x3020:
        return (unsigned short far *)0;

    case 0xf000:
        ch = g_nCurrentChannelSlot;
        if (ch != -1) {
            g_anim_slots[g_current_animation_slot]->pChannel_state[ch] = 2;
            return (unsigned short far *)0;
        }
        return (unsigned short far *)0;

    case 0xf010:
        ch = script_pc[1];
        if (ch == 0xffff) {
            ch = g_nCurrentChannelSlot;
        } else {
            ch = anim_script_find_channel(ch);
        }
        if (ch != -1) {
            g_anim_slots[g_current_animation_slot]->pChannel_state[ch] = 2;
        }
        if (script_pc != (unsigned short far *)0) {
            if (ch == g_nCurrentChannelSlot) {
            null_pc:
                script_pc = (unsigned short far *)0;
                break;
            }
            script_pc += 2;
            break;
        }
        return script_pc;

    case 0xf200:
        ch = script_pc[1];
        ch = anim_script_find_channel(ch);
        if (ch != -1) {
            cell = &g_anim_slots[g_current_animation_slot]->pChannel_state[ch];
            *cell = (*cell & 8) | 4;
        }
        return script_pc + 2;

    case 0xf210:
        ch = script_pc[1];
        ch = anim_script_find_channel(ch);
        if (ch != -1) {
            cell = &g_anim_slots[g_current_animation_slot]->pChannel_state[ch];
            *cell = (*cell & 8) | 3;
        }
        return script_pc + 2;

    case 0x1310:
    case 0x1320:
    case 0x1330:
    case 0x1340:
    case 0x1350:
    case 0x1360:
    case 0x1370:
    case 0x1380:
    case 0x1390:
    case 0x13a0:
    case 0x13a1:
    case 0x13b0:
    case 0x13b1:
    case 0x13c0:
    case 0x13c1:
        script_pc = (unsigned short far *)anim_script_combat_resolve_loop((s16 far *)script_pc);
        if (FP_OFF(g_anim_slots[1]) != 0) {
            script_pc = anim_script_channel_step(script_pc);
        } else {
            script_pc = (unsigned short far *)anim_script_opcode_1500_follow((s16 far *)script_pc);
        }
        if (script_pc != (unsigned short far *)0) {
            script_pc += 1;
            break;
        }
        return script_pc;

    case 0x1010:
    case 0x1020:
    case 0x1030:
    case 0x1040:
    case 0x1050:
    case 0x1060:
    case 0x1070:
    case 0x1080:
        saved_pc = script_pc;
        script_pc = (unsigned short far *)anim_script_combat_resolve_loop((s16 far *)script_pc);
        slot = g_anim_slots[g_current_animation_slot];
        if (FP_OFF(g_anim_slots[1]) != 0) {
            slot->pChannel_b2[g_nCurrentChannelSlot]++;
            *(unsigned short far *far *)&slot->pChannel_alt[g_nCurrentChannelSlot] = saved_pc;
            script_pc = anim_script_channel_step(script_pc);
            goto null_pc;
        } else {
            slot->pChannel_b2[g_nCurrentChannelSlot] = 0;
            *(unsigned short far *far *)&slot->pChannel_alt[g_nCurrentChannelSlot] = (unsigned short far *)0;
            script_pc = (unsigned short far *)anim_script_run_to_opcode_1520((s16 far *)script_pc) + 1;
        }
        break;

    default:
        goto noop;
    }
    return script_pc;

noop:
    return (unsigned short far *)0;
}

unsigned short far *anim_script_channel_step(unsigned short far *script_pc) {
    while (*(short far *)script_pc != -1) {
        script_pc = anim_script_dispatch_opcode(script_pc);
        if (g_wScriptYieldPending != 0) {
            g_wScriptYieldPending = 0;
            return script_pc;
        }
        if (script_pc == 0) {
            break;
        }
    }
    return (unsigned short far *)0;
}

static s16 anim_script_channel_advance(ScriptAnimNode *pNode) {
    s16 ready;

    ready = 0;
    if (pNode->wInterval != 0) {
        if (g_dwSysTickCount < pNode->dwTimerExpiry)
            goto LAB_done;
        pNode->dwTimerExpiry = g_dwSysTickCount + (s32)(s16)pNode->wInterval;
    }
    ++ready;
LAB_done:
    if (ready != 0) {
        pNode->wOpCursor = 0;
        if ((s16)pNode->wPcJumpTarget != -1) {
            pNode->wPc = pNode->wPcJumpTarget;
            pNode->wPcJumpTarget = (unsigned short)-1;
        } else {
            ++pNode->wPc;
        }
    }
    return ready;
}

s16 anim_script_find_channel(s16 channel_id) {
    Slot far *slot;
    s16 i;
    s16 id;

    if (g_current_animation_slot >= 0) {
        slot = g_anim_slots[g_current_animation_slot];
        for (i = 0; i < slot->nChannel_count; i++) {
            id = *((s16 far *)slot->pChannels[i] - 1);
            if (id == channel_id) {
                return i;
            }
        }
    }
    return -1;
}

s16 anim_script_has_channel(s16 channel_id) {
    return anim_script_find_channel(channel_id) != -1;
}

s16 anim_script_iter_next_object(u16 *out_id) {
    Slot far *slot;

    if (g_current_animation_slot >= 0) {
        slot = g_anim_slots[g_current_animation_slot];
        if (g_nAnimIterCursor < slot->nChannel_count) {
            *out_id = *((u16 far *)slot->pChannels[g_nAnimIterCursor] - 1);
            g_nAnimIterCursor++;
            return 0;
        }
    }
    return 1;
}

void anim_script_iter_first_object(u16 *out_id) {
    g_nAnimIterCursor = 0;
    anim_script_iter_next_object(out_id);
    return;
}

s16 anim_script_tick(void) {
    ScriptAnimNode **op_list;
    Slot far *slot;
    u32 far *channel;
    s16 nChannels;
    s16 anyRan;
    s16 i;
    u16 kind;
    unsigned short opState;
    unsigned short mode;

    anyRan = 0;
    if (g_current_animation_slot < 0) {
        return 0;
    } else {
        slot = g_anim_slots[g_current_animation_slot];
        nChannels = slot->nChannel_count;
        for (i = 0; i < nChannels; i++) {
            kind = slot->pChannel_state[i] & 0xfff7;
            if (op_list = (ScriptAnimNode **)slot->pOp_lists[i]) {
                for (; *op_list != 0; op_list++) {
                    if (kind == 3) {
                        anim_script_object_reset(*op_list);
                    } else {
                        (*op_list)->wOpState = kind;
                    }
                }
            }
        }
        for (i = 0; i < nChannels; i++) {
            kind = slot->pChannel_state[i];
            channel = (u32 far *)slot->pChannels[i];
            if (kind & 8) {
                kind = kind & 0xfff7;
                slot->pChannel_state[i] = kind;
            } else {
                channel = (u32 far *)anim_script_advance_to_wait((s16 far *)channel);
            }
            if ((void far *)slot->pChannel_alt[i] != (void far *)0) {
                channel = (u32 far *)slot->pChannel_alt[i];
            }
            if ((kind == 3) || (kind == 4)) {
                kind = slot->pChannel_state[i] = 1;
            }
            g_nCurrentChannelSlot = i;
            if ((channel != (u32 far *)0) && (kind == 1)) {
                anim_script_channel_step((unsigned short far *)channel);
            }
        }
        g_pCurScriptAnimNode = (ScriptAnimNode *)slot->wObjects_head_alt;
        while (g_pCurScriptAnimNode != (ScriptAnimNode *)0) {
            g_pCurScriptAnimNode->wDispatchPc = 0xffff;
            opState = g_pCurScriptAnimNode->wOpState;
            mode = g_pCurScriptAnimNode->wMode;
            if ((opState != 6) && ((mode == 1) || (mode == 3) || (mode == 2) || (mode == 5))) {
                adscript_select_object(g_pCurScriptAnimNode->wTag);
                g_graphics_context.bGfx_outline_color = g_pCurScriptAnimNode->bSavedFgColor;
                g_graphics_context.bText_fg_color = g_pCurScriptAnimNode->bSavedFgColor;
                g_graphics_context.bGfx_fill_color = g_pCurScriptAnimNode->bSavedFillColor;
                g_graphics_context.clip.xmin = g_pCurScriptAnimNode->nSavedClipXmin;
                g_graphics_context.clip.ymin = g_pCurScriptAnimNode->nSavedClipYmin;
                g_graphics_context.clip.xmax = g_pCurScriptAnimNode->nSavedClipXmax;
                g_graphics_context.clip.ymax = g_pCurScriptAnimNode->nSavedClipYmax;
                g_graphics_context.bClip_enabled = g_pCurScriptAnimNode->bSavedClipEnabled;
                g_wScriptFrameHoldFlag = 0;
                g_nScriptTickOverride = -1;
                if ((adscript_run_by_index(g_pCurScriptAnimNode->wPc) != 0) && (opState != 5)) {
                    g_pCurScriptAnimNode->wOpCursor = 1;
                    g_pCurScriptAnimNode->wDispatchPc = g_pCurScriptAnimNode->wPc;
                    anyRan = 1;
                    if ((g_nScriptTickOverride != -1) &&
                        (g_pCurScriptAnimNode->wInterval != g_nScriptTickOverride)) {
                        g_pCurScriptAnimNode->dwTimerExpiry =
                            g_dwSysTickCount + (s32)g_nScriptTickOverride;
                        g_pCurScriptAnimNode->wInterval = g_nScriptTickOverride;
                    }
                    if (g_wScriptFrameHoldFlag != 0) {
                        g_pCurScriptAnimNode->wPcJumpTarget = g_pCurScriptAnimNode->wPcSaved;
                        if ((g_pCurScriptAnimNode->wMode == 2) &&
                            (g_pCurScriptAnimNode->wCountdown != 0)) {
                            if (anim_script_channel_advance(g_pCurScriptAnimNode) != 0) {
                                g_pCurScriptAnimNode->wCountdown--;
                            }
                        } else {
                            if ((g_pCurScriptAnimNode->wMode == 3) &&
                                (g_pCurScriptAnimNode->dwDeadline != 0)) {
                                anim_script_channel_advance(g_pCurScriptAnimNode);
                            } else if (anim_script_channel_advance(g_pCurScriptAnimNode) != 0) {
                                g_pCurScriptAnimNode->wMode = 4;
                                g_pCurScriptAnimNode->wInterval = 0;
                            }
                        }
                    } else {
                        if (g_nScriptPendingJumpTarget != -1) {
                            g_pCurScriptAnimNode->wPcJumpTarget = g_nScriptPendingJumpTarget;
                            if (g_pCurScriptAnimNode->wPc == g_nScriptPendingJumpTarget) {
                                g_pCurScriptAnimNode->wFld_0c = 1;
                            }
                        }
                        if (g_pCurScriptAnimNode->wMode != 5) {
                            anim_script_channel_advance(g_pCurScriptAnimNode);
                        }
                    }
                } else {
                    if (opState != 5) {
                        g_pCurScriptAnimNode->wPcJumpTarget = g_pCurScriptAnimNode->wPcSaved;
                        g_pCurScriptAnimNode->wMode = 4;
                    }
                }
            } else {
                if ((opState != 6) && (opState != 5) && (mode == 4)) {
                    g_pCurScriptAnimNode->wMode = 0;
                }
            }
            if ((mode == 3) && (g_pCurScriptAnimNode->dwDeadline <= g_dwSysTickCount)) {
                g_pCurScriptAnimNode->wMode = 4;
            }
            g_pCurScriptAnimNode = g_pCurScriptAnimNode->pNext;
        }
        g_graphics_context.bClip_enabled = 1;
        g_graphics_context.clip.xmin = 0;
        g_graphics_context.clip.xmax = g_wScreen_width - 1;
        g_graphics_context.clip.ymin = 0;
        g_graphics_context.clip.ymax = g_wScreen_height - 1;
    }
    return anyRan;
}

void anim_script_rndr_all_objs(void) {
    unsigned short wSavedOpCursor;

    if (g_current_animation_slot >= 0) {
        g_pCurScriptAnimNode =
            (ScriptAnimNode *)g_anim_slots[g_current_animation_slot]->wObjects_head_alt;
        while (g_pCurScriptAnimNode != (ScriptAnimNode *)0) {
            if (g_pCurScriptAnimNode->wDispatchPc != 0xffff) {
                wSavedOpCursor = g_pCurScriptAnimNode->wOpCursor;
                g_pCurScriptAnimNode->wOpCursor = 1;
                adscript_select_object(g_pCurScriptAnimNode->wTag);
                g_graphics_context.bText_fg_color = g_graphics_context.bGfx_outline_color =
                    g_pCurScriptAnimNode->bSavedFgColor;
                g_graphics_context.bGfx_fill_color = g_pCurScriptAnimNode->bSavedFillColor;
                g_graphics_context.clip.xmin = g_pCurScriptAnimNode->nSavedClipXmin;
                g_graphics_context.clip.ymin = g_pCurScriptAnimNode->nSavedClipYmin;
                g_graphics_context.clip.xmax = g_pCurScriptAnimNode->nSavedClipXmax;
                g_graphics_context.clip.ymax = g_pCurScriptAnimNode->nSavedClipYmax;
                g_graphics_context.bClip_enabled = g_pCurScriptAnimNode->bSavedClipEnabled;
                adscript_run_by_index(g_pCurScriptAnimNode->wDispatchPc);
                g_pCurScriptAnimNode->wOpCursor = wSavedOpCursor;
            }
            g_pCurScriptAnimNode = g_pCurScriptAnimNode->pNext;
        }
        g_graphics_context.bClip_enabled = 1;
        g_graphics_context.clip.xmin = 0;
        g_graphics_context.clip.xmax = g_wScreen_width - 1;
        g_graphics_context.clip.ymin = 0;
        g_graphics_context.clip.ymax = g_wScreen_height - 1;
    }
    return;
}
