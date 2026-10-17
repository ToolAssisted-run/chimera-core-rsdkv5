/* game-state.cpp - what the tools see of the game: chimera docs/game-cores.md.
 *
 * Two memory domains, both the core's own blocks:
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
	uint8_t pad[10];
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
};
static_assert(sizeof(PlayerState) == 36, "PlayerState layout");

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
	static char json[16384];
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
	};
	for (int p = 0; p < 2; p++)
		for (size_t i = 0; i < sizeof k_player / sizeof k_player[0]; i++)
		{
			P("    { \"name\": \"Player %d.%s\", \"domain\": \"Players\", \"offset\": %d, \"type\": \"%s\", \"group\": \"Player %d\"%s",
			  p + 1, k_player[i].name, p * (int)sizeof(PlayerState) + k_player[i].off, k_player[i].type, p + 1,
			  k_player[i].writable ? "" : ", \"writable\": false");
			if (k_player[i].desc[0]) P(", \"description\": \"%s\"", k_player[i].desc);
			P(" }%s\n", p == 1 && i == sizeof k_player / sizeof k_player[0] - 1 ? "" : ",");
		}
	P("  ]\n}\n");
#undef GS
#undef P
	return json;
}

} // extern "C"
