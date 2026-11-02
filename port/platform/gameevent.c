/*
 * The scripted runs' events (BAK_INPUT's event step): one of the game's events written as a
 * dialog's or a scene's action writes it (gstate_event_write). Below 2134h and from DAC0h up
 * the id is a flag of the event bitmaps; between them some ids are fields of the game state:
 * the chapter at 7537h, and at 7541h the world loop's exit request, which the loop acts on
 * at the end of its pass (1 ends the chapter, any other value plays the chapter's third
 * cutscene and goes on in the world). Scenarios use it to reach what only the story's far end
 * sets, such as chapter 9's last cutscene.
 */
#include "platform.h"

#include "bak_game_begin.h"
#include "structs.h"
#include "SRC/GAME/STATE/GSTATE.H"
#include "bak_game_end.h"

void game_write_event(uint16_t id, uint16_t value) {
    gstate_event_write(id, value);
}
