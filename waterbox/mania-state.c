/* mania-state.c - where Sonic Mania keeps what the property table names.
 *
 * The engine knows an object's engine part only (game-state.cpp); the rest
 * of a player - its rings, lives, score, shield - is Sonic Mania's own
 * structure (Objects/Global/Player.h, EntityPlayer), laid over the same
 * object, and the game's global variables are its GlobalVariables
 * (GameVariables.h). This file is compiled with Mania's headers and flags,
 * so every offset and every value below is the game's own definition, not a
 * copy of it.
 */
#include <stddef.h>
#include <stdint.h>

#include "Game.h"

#include "mania-state.h"

const struct mania_layout *mania_layout(void)
{
	static const struct mania_layout layout = {
		.character_id = offsetof(EntityPlayer, characterID),
		.rings = offsetof(EntityPlayer, rings),
		.shield = offsetof(EntityPlayer, shield),
		.lives = offsetof(EntityPlayer, lives),
		.score = offsetof(EntityPlayer, score),
		.invincible_timer = offsetof(EntityPlayer, invincibleTimer),
		.speed_shoes_timer = offsetof(EntityPlayer, speedShoesTimer),
		.super_state = offsetof(EntityPlayer, superState),
		.global_game_mode = offsetof(GlobalVariables, gameMode),
		.global_continues = offsetof(GlobalVariables, continues),
		.sonic = ID_SONIC, .tails = ID_TAILS, .knuckles = ID_KNUCKLES,
#if MANIA_USE_PLUS
		.mighty = ID_MIGHTY, .ray = ID_RAY,
		.mode_encore = MODE_ENCORE,
#else
		.mighty = -1, .ray = -1,
		.mode_encore = -1,
#endif
		.mode_mania = MODE_MANIA, .mode_time_attack = MODE_TIMEATTACK, .mode_competition = MODE_COMPETITION,
		.shield_none = SHIELD_NONE, .shield_blue = SHIELD_BLUE, .shield_bubble = SHIELD_BUBBLE,
		.shield_fire = SHIELD_FIRE, .shield_lightning = SHIELD_LIGHTNING,
		.super_none = SUPERSTATE_NONE, .super_fade_in = SUPERSTATE_FADEIN, .super_super = SUPERSTATE_SUPER,
		.super_fade_out = SUPERSTATE_FADEOUT, .super_done = SUPERSTATE_DONE,
	};
	return &layout;
}

/* the class the scene gave Player (-1 before the game registered it), so
 * the core reads a player's fields only from an object that is one */
int32_t mania_player_class(void) { return Player ? (int32_t)Player->classID : -1; }

/* the game's global variables, or NULL before the game made them */
uint8_t *mania_globals(void) { return (uint8_t *)globals; }
