/* COMBAT/SPELL/CSPELL.C's SPELLS.DAT reader: a record's fields from their places on disk. */
#include <stdint.h>

#include "difftest.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/COMBAT/SPELL/CSPELL.H"
#include "bak_game_end.h"

/* cspell.spelldef <SPELL_RECORD_SIZE bytes>... -> for each record, the name's offset from the
   string blob (u32), then nCost, nCost_max, nSchool, nSpell_kind, nEffect_sprite_id,
   nEffect_kind, nInv_component_id, nEffect_subkind, nEffect_magnitude, nEffect_param (u16) */
static void cmd_spelldef(DtIn *in, DtOut *out) {
    static char names[0x10000];
    while (dt_remaining(in) != 0) {
        SpellDef def;
        cspell_spelldef_unpack(&def, dt_bytes(in, SPELL_RECORD_SIZE), names);
        const s16 fields[] = {def.nCost,
                              def.nCost_max,
                              def.nSchool,
                              def.nSpell_kind,
                              def.nEffect_sprite_id,
                              def.nEffect_kind,
                              def.nInv_component_id,
                              def.nEffect_subkind,
                              def.nEffect_magnitude,
                              def.nEffect_param};
        dt_put_u32(out, (uint32_t)(def.pName - names));
        for (size_t i = 0; i < sizeof fields / sizeof fields[0]; i++)
            dt_put_u16(out, (uint16_t)fields[i]);
    }
}

const DtEntry dt_cspell_entries[] = {
    DT_COMMAND("cspell.spelldef", cmd_spelldef),
    DT_END,
};
