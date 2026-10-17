/* game-state.cpp - what the tools see of the game: chimera docs/game-cores.md.
 *
 * Two memory domains, both the core's own blocks (Sonic Mania's own fields
 * are found by mania-state.c, from the game's headers):
 *
 *   - "Game State": copied from the engine after every step - the engine's
 *     state, the scene and its timer. Read-only, as the engine derives them.
 *   - "Players": the engine's part of the first two objects (in Sonic Mania
 *     the players: object 0 is player 1, object 1 player 2), their numbers
 *     only - the engine's object list itself holds the game's own data,
 *     pointers among it, which are host addresses and no part of the game.
 *     Copied out after every step and back in before the next, so a poke
 *     lands when the next step begins.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "RSDK/Core/RetroEngine.hpp"

#include "mania-state.h"
#include "rsdk-driver.h"

using namespace RSDK;

namespace {

struct __attribute__((packed)) GameState
{
	uint64_t frames;          /* 0 */
	uint8_t state;            /* 8: sceneInfo.state */
	uint8_t category;         /* 9: sceneInfo.activeCategory */
	uint16_t list_pos;        /* 10: sceneInfo.listPos */
	uint32_t time_ms;         /* 12: the scene timer, in milliseconds */
	int32_t time_counter;     /* 16 */
	uint8_t time_enabled;     /* 20 */
	uint8_t screens;          /* 21 */
	uint8_t has_globals;      /* 22: the game has made its global variables */
	uint8_t pad;
	int32_t game_mode;        /* 24: globals->gameMode */
	int32_t continues;        /* 28: globals->continues */
	char scene_name[32];      /* 32 */
	char scene_folder[16];    /* 64 */
	char scene_id[8];         /* 80 */
};

struct __attribute__((packed)) PlayerState
{
	int32_t x, y;                     /* 0, 4: 16.16 fixed point */
	int32_t x_velocity, y_velocity;   /* 8, 12 */
	int32_t ground_velocity;          /* 16 */
	int32_t angle, rotation;          /* 20, 24 */
	uint16_t class_id;                /* 28 */
	uint8_t on_ground, direction;     /* 30, 31 */
	uint8_t collision_mode, collision_plane, visible, active;   /* 32 */
	/* Sonic Mania's own part of a player (EntityPlayer), when the object is
	 * one; zeros otherwise */
	int32_t character;                /* 36 */
	int32_t rings, lives, score;      /* 40, 44, 48 */
	int32_t shield;                   /* 52 */
	int32_t invincible_timer;         /* 56 */
	int32_t speed_shoes_timer;        /* 60 */
	int32_t super_state;              /* 64 */
	uint8_t is_player;                /* 68 */
	uint8_t pad[3];
};
static_assert(sizeof(PlayerState) == 72, "PlayerState layout");

/* the object in slot p, when it is one of Sonic Mania's players */
uint8_t *mania_player(int p)
{
	const int32_t cls = mania_player_class();
	if (cls < 0 || objectEntityList[p].classID != (uint16)cls) return NULL;
	return (uint8_t *)&objectEntityList[p];
}

inline int32_t rd(const uint8_t *base, size_t off)
{
	int32_t v;
	memcpy(&v, base + off, 4);
	return v;
}
inline void wr(uint8_t *base, size_t off, int32_t v) { memcpy(base + off, &v, 4); }

GameState gs;
PlayerState ps[2];

} // namespace

extern "C" {

void gamestate_from_game(void)
{
	gs.frames = rsdkdrv_frames();
	gs.state = sceneInfo.state;
	gs.category = sceneInfo.activeCategory;
	gs.list_pos = sceneInfo.listPos;
	gs.time_ms = (uint32_t)(sceneInfo.minutes * 60000 + sceneInfo.seconds * 1000 + sceneInfo.milliseconds * 10);
	gs.time_counter = sceneInfo.timeCounter;
	gs.time_enabled = sceneInfo.timeEnabled ? 1 : 0;
	gs.screens = (uint8_t)videoSettings.screenCount;
	const struct mania_layout *ml = mania_layout();
	const uint8_t *glob = mania_globals();
	gs.has_globals = glob ? 1 : 0;
	gs.game_mode = glob ? rd(glob, ml->global_game_mode) : 0;
	gs.continues = glob ? rd(glob, ml->global_continues) : 0;
	memset(gs.scene_name, 0, sizeof gs.scene_name);
	memset(gs.scene_folder, 0, sizeof gs.scene_folder);
	memset(gs.scene_id, 0, sizeof gs.scene_id);
	if (sceneInfo.listData && sceneInfo.listPos < sceneInfo.listCategory[sceneInfo.activeCategory].sceneOffsetEnd)
	{
		const SceneListEntry &e = sceneInfo.listData[sceneInfo.listPos];
		memcpy(gs.scene_name, e.name, sizeof gs.scene_name - 1);
		memcpy(gs.scene_folder, e.folder, sizeof gs.scene_folder - 1);
		memcpy(gs.scene_id, e.id, sizeof gs.scene_id - 1);
	}
	for (int p = 0; p < 2; p++)
	{
		const Entity &e = objectEntityList[p];
		PlayerState &o = ps[p];
		o.x = e.position.x;
		o.y = e.position.y;
		o.x_velocity = e.velocity.x;
		o.y_velocity = e.velocity.y;
		o.ground_velocity = e.groundVel;
		o.angle = e.angle;
		o.rotation = e.rotation;
		o.class_id = e.classID;
		o.on_ground = e.onGround ? 1 : 0;
		o.direction = e.direction;
		o.collision_mode = e.collisionMode;
		o.collision_plane = e.collisionPlane;
		o.visible = e.visible;
		o.active = e.active;
		const uint8_t *pl = mania_player(p);
		o.is_player = pl ? 1 : 0;
		o.character = pl ? rd(pl, ml->character_id) : 0;
		o.rings = pl ? rd(pl, ml->rings) : 0;
		o.lives = pl ? rd(pl, ml->lives) : 0;
		o.score = pl ? rd(pl, ml->score) : 0;
		o.shield = pl ? rd(pl, ml->shield) : 0;
		o.invincible_timer = pl ? rd(pl, ml->invincible_timer) : 0;
		o.speed_shoes_timer = pl ? rd(pl, ml->speed_shoes_timer) : 0;
		o.super_state = pl ? rd(pl, ml->super_state) : 0;
	}
}

/* the players' block back into the engine before a step: what a poke or a
 * freeze wrote there (the numbers the engine moves the object by) */
void gamestate_to_game(void)
{
	for (int p = 0; p < 2; p++)
	{
		Entity &e = objectEntityList[p];
		const PlayerState &o = ps[p];
		e.position.x = o.x;
		e.position.y = o.y;
		e.velocity.x = o.x_velocity;
		e.velocity.y = o.y_velocity;
		e.groundVel = o.ground_velocity;
		e.angle = o.angle;
		e.rotation = o.rotation;
		e.direction = o.direction;
		/* Sonic Mania's part only into an object that is still a player, and
		 * only the numbers that are the whole of the thing: a shield is also
		 * an object of its own, and invincibility and speed shoes also start
		 * stars, music and speeds, so those are read only */
		uint8_t *pl = mania_player(p);
		if (pl && o.is_player)
		{
			const struct mania_layout *ml = mania_layout();
			wr(pl, ml->rings, o.rings);
			wr(pl, ml->lives, o.lives);
			wr(pl, ml->score, o.score);
		}
	}
}

int rsdkdrv_domain_count(void) { return 2; }

const char *rsdkdrv_domain_name(int i)
{
	static const char *const names[] = { "Game State", "Players" };
	return i >= 0 && i < 2 ? names[i] : "";
}

uint8_t *rsdkdrv_domain_ptr(int i)
{
	switch (i)
	{
	case 0: return (uint8_t *)&gs;
	case 1: return (uint8_t *)ps;
	}
	return NULL;
}

int64_t rsdkdrv_domain_size(int i)
{
	switch (i)
	{
	case 0: return (int64_t)sizeof gs;
	case 1: return (int64_t)sizeof ps;
	}
	return 0;
}

int rsdkdrv_domain_writable(int i) { return i == 1; }

const char *rsdkdrv_game_properties(void)
{
	static char json[32768];
	if (json[0]) return json;
	int n = 0;
#define P(...) n += snprintf(json + n, sizeof json - (size_t)n, __VA_ARGS__)
#define GS(name, off, type, extra) \
	P("    { \"name\": \"%s\", \"domain\": \"Game State\", \"offset\": %d, \"type\": \"%s\", \"writable\": false%s },\n", name, off, type, extra)
	P("{\n  \"gameTimer\": \"Scene.Time Ms\",\n  \"properties\": [\n");
	GS("Engine.Frames", 0, "u64", ", \"group\": \"Engine\"");
	GS("Engine.State", 8, "u8", ", \"group\": \"Engine\", \"values\": { \"0\": \"Load\", \"1\": \"Regular\", \"2\": \"Paused\", \"3\": \"Frozen\", "
	   "\"8\": \"Developer Menu\", \"9\": \"Video\", \"10\": \"Image\" }");
	GS("Scene.Category", 9, "u8", ", \"group\": \"Scene\"");
	GS("Scene.List Position", 10, "u16", ", \"group\": \"Scene\"");
	GS("Scene.Time Ms", 12, "u32", ", \"group\": \"Scene\", \"description\": \"The scene timer (minutes, seconds and hundredths), in milliseconds\"");
	GS("Scene.Time Counter", 16, "s32", ", \"group\": \"Scene\"");
	GS("Scene.Time Enabled", 20, "bool", ", \"group\": \"Scene\"");
	GS("Engine.Screens", 21, "u8", ", \"group\": \"Engine\"");
	const struct mania_layout *ml = mania_layout();
	{
		char vals[256];
		int k = snprintf(vals, sizeof vals, ", \"group\": \"Game\", \"description\": \"The game's mode (0 before the game has started)\", \"values\": { \"%d\": \"Mania\"", ml->mode_mania);
		if (ml->mode_encore >= 0) k += snprintf(vals + k, sizeof vals - (size_t)k, ", \"%d\": \"Encore\"", ml->mode_encore);
		snprintf(vals + k, sizeof vals - (size_t)k, ", \"%d\": \"Time Attack\", \"%d\": \"Competition\" }", ml->mode_time_attack, ml->mode_competition);
		GS("Game.Mode", 24, "s32", vals);
	}
	GS("Game.Continues", 28, "s32", ", \"group\": \"Game\"");
	P("    { \"name\": \"Scene.Name\", \"domain\": \"Game State\", \"offset\": 32, \"type\": \"string\", \"length\": 32, \"writable\": false, \"group\": \"Scene\" },\n");
	P("    { \"name\": \"Scene.Folder\", \"domain\": \"Game State\", \"offset\": 64, \"type\": \"string\", \"length\": 16, \"writable\": false, \"group\": \"Scene\" },\n");
	P("    { \"name\": \"Scene.ID\", \"domain\": \"Game State\", \"offset\": 80, \"type\": \"string\", \"length\": 8, \"writable\": false, \"group\": \"Scene\" },\n");
	static const struct { const char *name; int off; const char *type; int writable; const char *desc; } k_player[] = {
		{ "X", 0, "s32", 1, "Position, 16.16 fixed point" },
		{ "Y", 4, "s32", 1, "Position, 16.16 fixed point" },
		{ "X Velocity", 8, "s32", 1, "" },
		{ "Y Velocity", 12, "s32", 1, "" },
		{ "Ground Velocity", 16, "s32", 1, "" },
		{ "Angle", 20, "s32", 1, "" },
		{ "Rotation", 24, "s32", 1, "" },
		{ "Class", 28, "u16", 0, "The object's class (a number the scene gives it)" },
		{ "On Ground", 30, "bool", 0, "" },
		{ "Direction", 31, "u8", 1, "" },
		{ "Collision Mode", 32, "u8", 0, "" },
		{ "Collision Plane", 33, "u8", 0, "" },
		{ "Visible", 34, "u8", 0, "" },
		{ "Active", 35, "u8", 0, "" },
		{ "Rings", 40, "s32", 1, "Sonic Mania's ring count" },
		{ "Lives", 44, "s32", 1, "" },
		{ "Score", 48, "s32", 1, "" },
		{ "Invincible Timer", 56, "s32", 0, "Frames of invincibility left" },
		{ "Speed Shoes Timer", 60, "s32", 0, "Frames of speed shoes left" },
		{ "Is Player", 68, "bool", 0, "The object is one of Sonic Mania's players (the fields above Character are zero when not)" },
	};
	for (int p = 0; p < 2; p++)
		for (size_t i = 0; i < sizeof k_player / sizeof k_player[0]; i++)
		{
			P("    { \"name\": \"Player %d.%s\", \"domain\": \"Players\", \"offset\": %d, \"type\": \"%s\", \"group\": \"Player %d\"%s",
			  p + 1, k_player[i].name, p * (int)sizeof(PlayerState) + k_player[i].off, k_player[i].type, p + 1,
			  k_player[i].writable ? "" : ", \"writable\": false");
			if (k_player[i].desc[0]) P(", \"description\": \"%s\"", k_player[i].desc);
			P(" },\n");
		}
	for (int p = 0; p < 2; p++)
	{
		const int base = p * (int)sizeof(PlayerState);
		P("    { \"name\": \"Player %d.Character\", \"domain\": \"Players\", \"offset\": %d, \"type\": \"s32\", \"group\": \"Player %d\", \"writable\": false, "
		  "\"values\": { \"%d\": \"Sonic\", \"%d\": \"Tails\", \"%d\": \"Knuckles\"", p + 1, base + 36, p + 1, ml->sonic, ml->tails, ml->knuckles);
		if (ml->mighty >= 0) P(", \"%d\": \"Mighty\", \"%d\": \"Ray\"", ml->mighty, ml->ray);
		P(" } },\n");
		P("    { \"name\": \"Player %d.Shield\", \"domain\": \"Players\", \"offset\": %d, \"type\": \"s32\", \"group\": \"Player %d\", \"writable\": false, "
		  "\"values\": { \"%d\": \"None\", \"%d\": \"Blue\", \"%d\": \"Bubble\", \"%d\": \"Fire\", \"%d\": \"Lightning\" } },\n",
		  p + 1, base + 52, p + 1, ml->shield_none, ml->shield_blue, ml->shield_bubble, ml->shield_fire, ml->shield_lightning);
		P("    { \"name\": \"Player %d.Super State\", \"domain\": \"Players\", \"offset\": %d, \"type\": \"s32\", \"group\": \"Player %d\", \"writable\": false, "
		  "\"values\": { \"%d\": \"None\", \"%d\": \"Fading In\", \"%d\": \"Super\", \"%d\": \"Fading Out\", \"%d\": \"Done\" } }%s\n",
		  p + 1, base + 64, p + 1, ml->super_none, ml->super_fade_in, ml->super_super, ml->super_fade_out, ml->super_done, p == 1 ? "" : ",");
	}
	P("  ]\n}\n");
#undef GS
#undef P
	return json;
}

} // extern "C"
