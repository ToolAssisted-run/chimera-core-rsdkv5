/* mania-state.h - Sonic Mania's own layout, as mania-state.c reads it from
 * the game's headers, for game-state.cpp */
#ifndef MANIA_STATE_H
#define MANIA_STATE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mania_layout
{
	/* offsets in an object that is a player (EntityPlayer) */
	size_t character_id, rings, shield, lives, score, invincible_timer, speed_shoes_timer, super_state;
	/* offsets in the game's global variables */
	size_t global_game_mode, global_continues;
	/* values: characters (-1: not in this build), modes, shields, super states */
	int32_t sonic, tails, knuckles, mighty, ray;
	int32_t mode_mania, mode_encore, mode_time_attack, mode_competition;
	int32_t shield_none, shield_blue, shield_bubble, shield_fire, shield_lightning;
	int32_t super_none, super_fade_in, super_super, super_fade_out, super_done;
};

const struct mania_layout *mania_layout(void);
int32_t mania_player_class(void);
uint8_t *mania_globals(void);

#ifdef __cplusplus
}
#endif

#endif
