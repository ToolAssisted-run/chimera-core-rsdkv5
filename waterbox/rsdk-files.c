/* rsdk-files.c - every file the engine opens, answered by the core.
 *
 * The engine opens its files through SDL's file reader, and SDL and the
 * C library open them with fopen; the link wraps both SDL_RWFromFile and
 * fopen (sources.mk), so each open lands here:
 *
 *   - Data.rsdk, the game's data pack: the file the project brings as its
 *     firmware (waterbox.config), copied once at Init into sealed memory -
 *     read-only after Init, and so in no savestate - and opened from there
 *     with fmemopen. The stream itself (its position, its buffer) is in the
 *     guest's heap, which a savestate carries; a host file handle would not
 *     survive a load.
 *   - Settings.ini: written by the core from the project's settings, so the
 *     engine starts as the project says and never as a settings file left on
 *     some disk says.
 *   - every file the engine or the game writes (the saves, the time attack
 *     records, the options, Settings.ini when the game writes it back): a
 *     file in guest memory, empty when the machine starts, kept by name and
 *     read back by name, and carried by savestates like the rest of it.
 *   - the project's mods (the Mods slot, chimera#590): each zip unpacked at
 *     Init, before the seal, into sealed memory, as a "mods" folder that
 *     exists only in the machine, with the modconfig.ini the engine's mod
 *     loader reads to know which mods are on and in what order. What the
 *     mod loader writes there (a mod's modConfig.cfg or modSettings.ini, the
 *     modconfig.ini of the developer menu's Mods page) is a file in guest
 *     memory like the saves.
 *   - anything else: not there.
 *
 * Names are matched by their last part and without regard to case; under
 * "mods/", a mod's own files and the mod loader's are matched by their
 * whole path.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <SDL2/SDL.h>

#include <emulibc.h>
#include <miniz/miniz.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "rsdk-driver.h"

/* A text a person reads is a key of languages/en.json, each of its
 * arguments after a TEXT_ARG (chimera docs/translations.md). TEXT_ARG is a
 * string of its own so the byte after it is never read as more of its hex. */
#define TEXT_ARG "\x1f"

FILE *__real_fopen(const char *path, const char *mode);
int __real_fclose(FILE *f);

/* the data pack, sealed after Init */
static uint8_t *g_pack;
static size_t g_pack_size;

/* Settings.ini, as the core wrote it */
static char g_ini[4096];
static size_t g_ini_len;

/* the files written: a stream being written is an open_memstream, whose
 * buffer the C library hands back at fclose; the last one closed is the
 * file. Room for the game's files and for what the mod loader writes, a
 * file or two for each mod (its modConfig.cfg, its modSettings.ini). */
#define MEMFILES 256
/* what the Save data slot takes: 32 files, names of at most 63 characters */
#define SAVE_FILES 32
#define SAVE_NAME 63
struct mem_file
{
	char name[256];     /* the last part of the path; under mods/, the mod
	                       loader's files by the whole path */
	char *data;         /* the file's bytes (malloc'd: guest heap) */
	size_t size;
	char *writing;      /* open_memstream's, while a write is open */
	size_t writing_size;
	FILE *stream;
};
static struct mem_file g_files[MEMFILES];

/* ------------------------------------------------------------ settings */

void rsdk_settings_read(struct rsdk_settings *s)
{
	char str[16] = "en";
	wbx_setting_str("language", str, (int)sizeof str);
	static const char *const langs[] = { "en", "fr", "it", "de", "es", "ja", "ko", "zhs", "zh" };
	s->language = 0;
	for (int i = 0; i < (int)(sizeof langs / sizeof langs[0]); i++)
		if (!strcmp(str, langs[i])) s->language = i;
	s->screen_width = (int)wbx_setting_long("screenWidth", 424);
	snprintf(str, sizeof str, "data");
	wbx_setting_str("region", str, (int)sizeof str);
	s->region = !strcmp(str, "us") ? 0 : !strcmp(str, "jp") ? 1 : !strcmp(str, "eu") ? 2 : -1;
	s->plus = wbx_setting_bool("plus", 0);
	s->random_seed = wbx_setting_long("randomSeed", 0);
	s->dev_menu = wbx_setting_bool("devMenu", 0);
	snprintf(str, sizeof str, "2");
	wbx_setting_str("players", str, (int)sizeof str);
	s->players = !strcmp(str, "4") ? 4 : 2;
	/* the name goes into the engine's settings file as one line: what could
	 * end the line or start a section is left out (chimera#302) */
	char name[64] = "";
	wbx_setting_str("username", name, (int)sizeof name);
	int n = 0;
	for (int i = 0; name[i] && n < (int)sizeof s->username - 1; i++)
		if ((unsigned char)name[i] >= 0x20 && name[i] != 0x7f && name[i] != '[' && name[i] != ']' && name[i] != ';')
			s->username[n++] = name[i];
	s->username[n] = '\0';
}

/* the engine's settings file (UserCore.cpp LoadSettingsINI reads it): the
 * keys it knows, the values the project's settings give. What is not a
 * setting is upstream's default, except where the default would make the
 * picture or the run depend on the host: windowed, no VSync, no shader, no
 * focus pause, no developer menu. The keyboard maps are upstream's own
 * defaults (Windows key codes); the core sets the keyboard's state itself
 * (rsdk-driver.cpp). */
static void write_ini(const struct rsdk_settings *s)
{
	int n = snprintf(g_ini, sizeof g_ini,
		"[Game]\n"
		"dataFile=Data.rsdk\n"
		"devMenu=%s\n"
		"language=%d\n"
		"region=%d\n"
		"faceButtonFlip=n\n"
		"enableControllerDebugging=n\n"
		"disableFocusPause=y\n"
		"fastForwardSpeed=8\n"
		"txtScripts=n\n"
		"gameType=1\n"
		"gameLogic=Game\n"
		"username=%s\n"
		"[Video]\n"
		"windowed=y\n"
		"border=y\n"
		"exclusiveFS=n\n"
		"vsync=n\n"
		"tripleBuffering=n\n"
		"pixWidth=%d\n"
		"winWidth=%d\n"
		"winHeight=240\n"
		"fsWidth=0\n"
		"fsHeight=0\n"
		"refreshRate=60\n"
		"shaderSupport=n\n"
		"screenShader=0\n"
		"maxPixWidth=%d\n"
		"[Audio]\n"
		"streamsEnabled=y\n"
		"streamVolume=1.0\n"
		"sfxVolume=1.0\n",
		s->dev_menu ? "y" : "n", s->language, s->region, s->username, s->screen_width, s->screen_width, s->screen_width);
	g_ini_len = (size_t)n;
}

/* ------------------------------------------------------------ the pack */

/* the game's data file: the project's firmware Data.rsdk, mounted under
 * that name (waterbox.config) */
static int read_pack(char *err, int errsize)
{
	const char *name = "Data.rsdk";
	FILE *f = __real_fopen(name, "rb");
	if (!f)
	{
		snprintf(err, (size_t)errsize, "error.game_s_data_file_data_rsdk");
		return 0;
	}
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 6)
	{
		fclose(f);
		snprintf(err, (size_t)errsize, "error.is_not_a_retro_engine_data" TEXT_ARG "%s" TEXT_ARG "%ld", name, size);
		return 0;
	}
	g_pack = (uint8_t *)alloc_sealed((size_t)size);
	if (!g_pack)
	{
		fclose(f);
		snprintf(err, (size_t)errsize, "error.is_too_large_bytes_for_the" TEXT_ARG "%s" TEXT_ARG "%ld", name, size);
		return 0;
	}
	const size_t got = fread(g_pack, 1, (size_t)size, f);
	fclose(f);
	if (got != (size_t)size)
	{
		snprintf(err, (size_t)errsize, "error.could_not_be_read_whole" TEXT_ARG "%s", name);
		return 0;
	}
	/* the engine takes a file that is not a pack for no data at all and
	 * starts its developer menu: the core refuses it instead */
	if (memcmp(g_pack, "RSDKv5", 6) != 0)
	{
		snprintf(err, (size_t)errsize, "error.is_not_a_retro_engine_v5" TEXT_ARG "%s", name);
		return 0;
	}
	g_pack_size = (size_t)size;
	return 1;
}

/* The project's save data (the savedata slot, chimera#302): the files the
 * game writes (SaveData.bin and the rest) as Export Save Data wrote them, put
 * in place here at Init, before the seal, so they are part of the machine as
 * it starts. Settings.ini is the project's settings, not save data. */
static int seed_saves(char *err, int errsize)
{
	char name[256];
	for (int i = 0; wbx_slot_name("savedata", i, name, (int)sizeof name); i++)
	{
		if (!strcasecmp(name, "Settings.ini") || strlen(name) > SAVE_NAME)
		{
			snprintf(err, (size_t)errsize, "error.save_data_is_not_one_this" TEXT_ARG "%s" TEXT_ARG "%d",
			         name, SAVE_NAME);
			return 0;
		}
		struct mem_file *mf = NULL;
		for (int k = 0; k < SAVE_FILES && !mf; k++)
			if (!g_files[k].name[0]) mf = &g_files[k];
		if (!mf)
		{
			snprintf(err, (size_t)errsize, "error.save_data_has_more_than" TEXT_ARG "%d", SAVE_FILES);
			return 0;
		}
		FILE *f = __real_fopen(name, "rb");
		if (!f)
		{
			snprintf(err, (size_t)errsize, "error.save_data_could_not_be" TEXT_ARG "%s", name);
			return 0;
		}
		size_t cap = 0, n = 0;
		char *data = NULL;
		for (;;)
		{
			if (n == cap)
			{
				cap = cap ? cap * 2 : 65536;
				data = (char *)realloc(data, cap);
			}
			const size_t got = fread(data + n, 1, cap - n, f);
			n += got;
			if (got == 0) break;
		}
		__real_fclose(f);
		snprintf(mf->name, sizeof mf->name, "%s", name);
		mf->data = data;
		mf->size = n;
	}
	return 1;
}

/* ------------------------------------------------------------ mods */

/* The project's mods (the Mods slot, chimera#590). Each file of the slot is
 * a zip of one or more of the decompilation's mod folders, each folder with
 * its mod.ini (and its Data folder, and whatever else it carries), at the
 * zip's root or deeper; a zip whose root holds mod.ini is one mod, named as
 * the zip is. At Init, before the seal, every mod's files are unpacked into
 * sealed memory, read-only from then on and in no savestate, as the folder
 * mods/<the mod's folder>/ of the machine's own "mods" folder, which the
 * engine's mod loader reads through mod-fs.hpp; the computer's disk is never
 * read or written.
 *
 * The order is the slot's: the mods of its first zip first. Within a zip,
 * the order of the modconfig.ini beside its mod folders when it has one (the
 * file in which upstream keeps the order of its mods folder, so a zip of a
 * whole mods folder loads as it did there; a mod it turns off is left out),
 * else the folders' names in alphabetical order. The core writes the
 * engine's modconfig.ini in that order with every mod on, and the engine
 * does as it does with its own: a file two mods both have is taken from the
 * one that comes first (the top of the developer menu's Mods page).
 *
 * Refused, each with a text that names it: a file that is not a zip, a zip
 * with no mod.ini, two mods with one folder name, a mod for another version
 * of the engine (its TargetVersion or ForceVersion), and a mod with compiled
 * code (a LogicFile: a Windows or Linux library the engine would load,
 * which cannot run inside the sandbox). */

struct mod_file
{
	char *path;            /* mods/<folder>/<path in the mod>, as the zip spells it */
	const uint8_t *data;   /* sealed (the mods' files) or heap (modconfig.ini) */
	size_t size;
};
static struct mod_file *g_mods;
static int g_mods_count, g_mods_cap;

/* a path as the core compares it: '/' between parts, no "." part, no empty
 * part, no leading or trailing '/' */
static void norm_path(const char *in, char *out, size_t n)
{
	size_t o = 0;
	const char *p = in;
	while (*p)
	{
		if (*p == '/' || *p == '\\') { p++; continue; }
		if (p[0] == '.' && (p[1] == '/' || p[1] == '\\' || p[1] == 0)) { p++; continue; }
		if (o && o + 1 < n) out[o++] = '/';
		while (*p && *p != '/' && *p != '\\')
		{
			if (o + 1 < n) out[o++] = *p;
			p++;
		}
	}
	out[o] = '\0';
}

static int under_mods(const char *norm) { return !strncasecmp(norm, "mods/", 5); }

/* a file the mod loader itself writes: the mods folder's modconfig.ini, a
 * mod's modConfig.cfg and modSettings.ini - kept by their whole path, as
 * every mod has its own */
static int loader_file(const char *norm)
{
	const char *b = strrchr(norm, '/');
	b = b ? b + 1 : norm;
	return under_mods(norm) && (!strcasecmp(b, "modconfig.ini") || !strcasecmp(b, "modconfig.cfg") || !strcasecmp(b, "modsettings.ini"));
}

static const struct mod_file *find_mod_file(const char *norm)
{
	for (int i = 0; i < g_mods_count; i++)
		if (!strcasecmp(g_mods[i].path, norm)) return &g_mods[i];
	return NULL;
}

static int add_mod_file(const char *path, const uint8_t *data, size_t size)
{
	if (g_mods_count == g_mods_cap)
	{
		g_mods_cap = g_mods_cap ? g_mods_cap * 2 : 256;
		g_mods = (struct mod_file *)realloc(g_mods, sizeof *g_mods * (size_t)g_mods_cap);
		if (!g_mods) return 0;
	}
	g_mods[g_mods_count].path = strdup(path);
	g_mods[g_mods_count].data = data;
	g_mods[g_mods_count].size = size;
	g_mods_count++;
	return 1;
}

int rsdk_mods_kind(const char *path)
{
	char norm[1024];
	norm_path(path, norm, sizeof norm);
	const size_t len = strlen(norm);
	if (!len) return RSDK_MODS_NONE;
	int kind = RSDK_MODS_NONE;
	for (int i = 0; i < g_mods_count && kind != RSDK_MODS_FILE; i++)
	{
		if (!strcasecmp(g_mods[i].path, norm)) kind = RSDK_MODS_FILE;
		else if (!strncasecmp(g_mods[i].path, norm, len) && g_mods[i].path[len] == '/') kind = RSDK_MODS_FOLDER;
	}
	return kind;
}

void rsdk_mods_list(const char *dir, int recursive, void (*each)(void *user, const char *name, int folder), void *user)
{
	char norm[1024];
	norm_path(dir, norm, sizeof norm);
	const size_t len = strlen(norm);
	if (!len) return;
	/* the children named so far (a folder is named once) */
	const char **seen = NULL;
	size_t *seen_len = NULL;
	int seen_count = 0;
	for (int i = 0; i < g_mods_count; i++)
	{
		const char *p = g_mods[i].path;
		if (strncasecmp(p, norm, len) || p[len] != '/') continue;
		const char *rest = p + len + 1;
		size_t part = strlen(rest);
		int folder = 0;
		if (!recursive)
		{
			const char *slash = strchr(rest, '/');
			if (slash)
			{
				part = (size_t)(slash - rest);
				folder = 1;
			}
			int dup = 0;
			for (int k = 0; k < seen_count && !dup; k++)
				dup = seen_len[k] == part && !strncasecmp(seen[k], rest, part);
			if (dup) continue;
			seen = (const char **)realloc(seen, sizeof *seen * (size_t)(seen_count + 1));
			seen_len = (size_t *)realloc(seen_len, sizeof *seen_len * (size_t)(seen_count + 1));
			seen[seen_count] = rest;
			seen_len[seen_count] = part;
			seen_count++;
		}
		const size_t dl = strlen(dir);
		char *name = (char *)malloc(dl + 1 + part + 1);
		memcpy(name, dir, dl);
		name[dl] = '/';
		memcpy(name + dl + 1, rest, part);
		name[dl + 1 + part] = '\0';
		each(user, name, folder);
		free(name);
	}
	free(seen);
	free(seen_len);
}

/* ---- reading the zips */

/* one file of a zip to unpack: which zip, which entry, where to */
struct mod_plan
{
	int zip;
	mz_uint entry;
	char *path;
	size_t size;
};

/* a zip entry, its name as the core compares it */
struct zip_name
{
	char *name;
	mz_uint entry;
	size_t size;
};

/* a mod folder in a zip: where (empty at the zip's root), the folder name
 * it gets under mods/, its place in the zip's order */
struct zip_mod
{
	char root[512];
	char folder[256];
	int rank, keep;
};

static const char *last_part(const char *p)
{
	const char *b = strrchr(p, '/');
	return b ? b + 1 : p;
}

static void parent_of(const char *p, char *out, size_t n)
{
	const char *b = strrchr(p, '/');
	size_t l = b ? (size_t)(b - p) : 0;
	if (l >= n) l = n - 1;
	memcpy(out, p, l);
	out[l] = '\0';
}

/* the start of a line's value after "key=", trimmed and unquoted as upstream's
 * iniparser reads it, into out */
static void ini_value(const char *v, const char *end, char *out, size_t n)
{
	while (v < end && (*v == ' ' || *v == '\t')) v++;
	while (end > v && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
	if (end - v >= 2 && ((*v == '"' && end[-1] == '"') || (*v == '\'' && end[-1] == '\'')))
	{
		v++;
		end--;
	}
	size_t l = (size_t)(end - v);
	if (l >= n) l = n - 1;
	memcpy(out, v, l);
	out[l] = '\0';
}

/* each "key=value" line of an ini text in the section named (empty: the
 * lines before any section), in order */
static void ini_each(const char *text, size_t size, const char *section, void (*fn)(void *u, const char *key, const char *value), void *u)
{
	const char *p = text, *end = text + size;
	char cur[256] = "";
	while (p < end)
	{
		const char *eol = memchr(p, '\n', (size_t)(end - p));
		if (!eol) eol = end;
		const char *q = p;
		while (q < eol && (*q == ' ' || *q == '\t')) q++;
		if (q < eol && *q == '[')
		{
			const char *close = memchr(q, ']', (size_t)(eol - q));
			if (close) ini_value(q + 1, close, cur, sizeof cur);
		}
		else if (q < eol && *q != ';' && *q != '#' && !strcasecmp(cur, section))
		{
			const char *eq = memchr(q, '=', (size_t)(eol - q));
			if (eq)
			{
				char key[256], value[512];
				ini_value(q, eq, key, sizeof key);
				ini_value(eq + 1, eol, value, sizeof value);
				fn(u, key, value);
			}
		}
		p = eol + 1;
	}
}

struct mod_ini
{
	char logic[512];
	long target, force;
};

static void mod_ini_key(void *u, const char *key, const char *value)
{
	struct mod_ini *mi = (struct mod_ini *)u;
	if (!strcasecmp(key, "LogicFile")) snprintf(mi->logic, sizeof mi->logic, "%s", value);
	else if (!strcasecmp(key, "TargetVersion")) mi->target = strtol(value, NULL, 0);
	else if (!strcasecmp(key, "ForceVersion")) mi->force = strtol(value, NULL, 0);
}

/* a zip's modconfig.ini: its [Mods] lines, in order, rank the folders beside
 * it; one turned off (as iniparser_getboolean reads it: not y, t or 1) is
 * left out */
struct mod_order
{
	struct zip_mod *mods;
	int count;
	const char *dir;
	int next;
};

static void mod_order_key(void *u, const char *key, const char *value)
{
	struct mod_order *o = (struct mod_order *)u;
	for (int i = 0; i < o->count; i++)
	{
		char parent[512];
		parent_of(o->mods[i].root, parent, sizeof parent);
		if (o->mods[i].rank || strcasecmp(parent, o->dir) || strcasecmp(last_part(o->mods[i].root), key)) continue;
		o->mods[i].rank = ++o->next;
		o->mods[i].keep = strchr("yYtT1", value[0]) && value[0];
		return;
	}
}

static int mod_rank_cmp(const void *a, const void *b)
{
	const struct zip_mod *x = (const struct zip_mod *)a, *y = (const struct zip_mod *)b;
	/* those the zip's modconfig.ini ranks first, in its order; the rest by name */
	if (x->rank && y->rank) return x->rank - y->rank;
	if (x->rank || y->rank) return x->rank ? -1 : 1;
	return strcasecmp(x->root, y->root);
}

/* the whole entry, unpacked onto the heap (a mod.ini, a modconfig.ini) */
static char *zip_text(mz_zip_archive *za, mz_uint entry, size_t *size)
{
	return (char *)mz_zip_reader_extract_to_heap(za, entry, size, 0);
}

/* The mods of one zip: what to unpack (plan) and the folders, in order
 * (folders, "name\n" each) */
static int plan_zip(int zi, const char *zipname, struct mod_plan **plan, int *plan_count, int *plan_cap, char **folders,
                    size_t *folders_len, char *err, int errsize)
{
	FILE *f = __real_fopen(zipname, "rb");
	mz_zip_archive za;
	memset(&za, 0, sizeof za);
	if (!f || !mz_zip_reader_init_cfile(&za, f, 0, 0))
	{
		if (f) __real_fclose(f);
		snprintf(err, (size_t)errsize, "error.mods_not_zip" TEXT_ARG "%s", zipname);
		return 0;
	}
	int ok = 0;
	const mz_uint n = mz_zip_reader_get_num_files(&za);
	struct zip_name *names = (struct zip_name *)calloc(n ? n : 1, sizeof *names);
	struct zip_mod *mods = NULL;
	int names_count = 0, mods_count = 0;
	for (mz_uint i = 0; i < n; i++)
	{
		mz_zip_archive_file_stat st;
		if (mz_zip_reader_is_file_a_directory(&za, i) || !mz_zip_reader_file_stat(&za, i, &st)) continue;
		char norm[1024];
		norm_path(st.m_filename, norm, sizeof norm);
		/* a name that leaves its folder means nothing here */
		if (!norm[0] || !strcmp(norm, "..") || !strncmp(norm, "../", 3) || strstr(norm, "/../") ||
		    (strlen(norm) >= 3 && !strcmp(norm + strlen(norm) - 3, "/.."))) continue;
		names[names_count].name = strdup(norm);
		names[names_count].entry = i;
		names[names_count].size = (size_t)st.m_uncomp_size;
		names_count++;
		if (!strcasecmp(last_part(norm), "mod.ini"))
		{
			mods = (struct zip_mod *)realloc(mods, sizeof *mods * (size_t)(mods_count + 1));
			memset(&mods[mods_count], 0, sizeof *mods);
			parent_of(norm, mods[mods_count].root, sizeof mods[mods_count].root);
			mods[mods_count].keep = 1;
			mods_count++;
		}
	}
	/* a mod.ini inside another mod's folder is that mod's file */
	for (int i = 0; i < mods_count; i++)
		for (int k = 0; k < mods_count; k++)
		{
			const size_t l = strlen(mods[k].root);
			if (i != k && mods[k].keep && (!l || (!strncasecmp(mods[i].root, mods[k].root, l) && mods[i].root[l] == '/')))
				mods[i].keep = -1;
		}
	{
		int w = 0;
		for (int i = 0; i < mods_count; i++)
			if (mods[i].keep == 1) mods[w++] = mods[i];
		mods_count = w;
	}
	if (!mods_count)
	{
		snprintf(err, (size_t)errsize, "error.mods_no_mod_ini" TEXT_ARG "%s", zipname);
		goto done;
	}
	/* the zip's own modconfig.ini, if one sits beside its mod folders */
	for (int i = 0; i < names_count; i++)
	{
		if (strcasecmp(last_part(names[i].name), "modconfig.ini")) continue;
		struct mod_order o = { mods, mods_count, NULL, 0 };
		char dir[512];
		parent_of(names[i].name, dir, sizeof dir);
		o.dir = dir;
		size_t size = 0;
		char *text = zip_text(&za, names[i].entry, &size);
		if (!text)
		{
			snprintf(err, (size_t)errsize, "error.mods_could_not_be_read" TEXT_ARG "%s", zipname);
			goto done;
		}
		ini_each(text, size, "Mods", mod_order_key, &o);
		mz_free(text);
	}
	qsort(mods, (size_t)mods_count, sizeof *mods, mod_rank_cmp);
	for (int m = 0; m < mods_count; m++)
	{
		struct zip_mod *mod = &mods[m];
		if (!mod->keep) continue;
		/* its folder's name; a mod at the zip's root is named as the zip */
		if (mod->root[0])
			snprintf(mod->folder, sizeof mod->folder, "%s", last_part(mod->root));
		else
		{
			snprintf(mod->folder, sizeof mod->folder, "%s", last_part(zipname));
			char *dot = strrchr(mod->folder, '.');
			if (dot && dot != mod->folder) *dot = '\0';
		}
		/* one folder name, one mod */
		for (const char *p = *folders; p && *p;)
		{
			const char *nl = strchr(p, '\n');
			if ((size_t)(nl - p) == strlen(mod->folder) && !strncasecmp(p, mod->folder, (size_t)(nl - p)))
			{
				snprintf(err, (size_t)errsize, "error.mods_same_folder" TEXT_ARG "%s" TEXT_ARG "%s", mod->folder, zipname);
				goto done;
			}
			p = nl + 1;
		}
		/* its mod.ini: compiled code, and the engine's version */
		const size_t rl = strlen(mod->root);
		char ini_name[600];
		snprintf(ini_name, sizeof ini_name, "%s%smod.ini", mod->root, rl ? "/" : "");
		struct mod_ini mi = { "", 5, 0 };
		for (int i = 0; i < names_count; i++)
		{
			if (strcasecmp(names[i].name, ini_name)) continue;
			size_t size = 0;
			char *text = zip_text(&za, names[i].entry, &size);
			if (!text)
			{
				snprintf(err, (size_t)errsize, "error.mods_could_not_be_read" TEXT_ARG "%s", zipname);
				goto done;
			}
			ini_each(text, size, "", mod_ini_key, &mi);
			mz_free(text);
		}
		if (mi.logic[0])
		{
			snprintf(err, (size_t)errsize, "error.mod_needs_native_code" TEXT_ARG "%s" TEXT_ARG "%s" TEXT_ARG "%s", mod->folder,
			         zipname, mi.logic);
			goto done;
		}
		const long version = mi.force ? mi.force : mi.target;
		if (version != 5 && version != -1)
		{
			snprintf(err, (size_t)errsize, "error.mod_wrong_engine" TEXT_ARG "%s" TEXT_ARG "%s" TEXT_ARG "%ld", mod->folder, zipname,
			         version);
			goto done;
		}
		/* its files, under mods/<folder>/ */
		for (int i = 0; i < names_count; i++)
		{
			const char *rest;
			if (!rl) rest = names[i].name;
			else if (!strncasecmp(names[i].name, mod->root, rl) && names[i].name[rl] == '/') rest = names[i].name + rl + 1;
			else continue;
			if (*plan_count == *plan_cap)
			{
				*plan_cap = *plan_cap ? *plan_cap * 2 : 256;
				*plan = (struct mod_plan *)realloc(*plan, sizeof **plan * (size_t)*plan_cap);
			}
			struct mod_plan *pl = &(*plan)[(*plan_count)++];
			pl->zip = zi;
			pl->entry = names[i].entry;
			pl->size = names[i].size;
			pl->path = (char *)malloc(5 + strlen(mod->folder) + 1 + strlen(rest) + 1);
			sprintf(pl->path, "mods/%s/%s", mod->folder, rest);
		}
		const size_t fl = strlen(mod->folder);
		*folders = (char *)realloc(*folders, *folders_len + fl + 2);
		memcpy(*folders + *folders_len, mod->folder, fl);
		(*folders)[*folders_len + fl] = '\n';
		(*folders)[*folders_len + fl + 1] = '\0';
		*folders_len += fl + 1;
	}
	ok = 1;
done:
	for (int i = 0; i < names_count; i++) free(names[i].name);
	free(names);
	free(mods);
	mz_zip_reader_end(&za);
	__real_fclose(f);
	return ok;
}

static int load_mods(char *err, int errsize)
{
	char zipname[256];
	struct mod_plan *plan = NULL;
	int plan_count = 0, plan_cap = 0, zips = 0, ok = 0;
	char *folders = NULL;
	size_t folders_len = 0;
	for (; wbx_slot_name("mods", zips, zipname, (int)sizeof zipname); zips++)
		if (!plan_zip(zips, zipname, &plan, &plan_count, &plan_cap, &folders, &folders_len, err, errsize)) goto done;
	if (!zips)
		return 1;
	/* every mod's files in one sealed block */
	size_t total = 0;
	for (int i = 0; i < plan_count; i++) total += plan[i].size;
	uint8_t *block = total ? (uint8_t *)alloc_sealed(total) : NULL;
	if (total && !block)
	{
		snprintf(err, (size_t)errsize, "error.mods_too_large" TEXT_ARG "%lu", (unsigned long)((total + (1 << 20) - 1) >> 20));
		goto done;
	}
	size_t at = 0;
	for (int z = 0; z < zips; z++)
	{
		wbx_slot_name("mods", z, zipname, (int)sizeof zipname);
		FILE *f = __real_fopen(zipname, "rb");
		mz_zip_archive za;
		memset(&za, 0, sizeof za);
		if (!f || !mz_zip_reader_init_cfile(&za, f, 0, 0))
		{
			if (f) __real_fclose(f);
			snprintf(err, (size_t)errsize, "error.mods_could_not_be_read" TEXT_ARG "%s", zipname);
			goto done;
		}
		int good = 1;
		for (int i = 0; i < plan_count && good; i++)
		{
			if (plan[i].zip != z) continue;
			if (plan[i].size && !mz_zip_reader_extract_to_mem(&za, plan[i].entry, block + at, plan[i].size, 0)) good = 0;
			else if (!add_mod_file(plan[i].path, block + at, plan[i].size)) good = 0;
			at += plan[i].size;
		}
		mz_zip_reader_end(&za);
		__real_fclose(f);
		if (!good)
		{
			snprintf(err, (size_t)errsize, "error.mods_could_not_be_read" TEXT_ARG "%s", zipname);
			goto done;
		}
	}
	/* the engine's modconfig.ini: every mod on, in the order above */
	{
		size_t len = 7 + folders_len * 2 + 64;
		char *ini = (char *)malloc(len), *w = ini;
		w += sprintf(w, "[Mods]\n");
		for (const char *p = folders; p && *p;)
		{
			const char *nl = strchr(p, '\n');
			w += sprintf(w, "%.*s=y\n", (int)(nl - p), p);
			p = nl + 1;
		}
		add_mod_file("mods/modconfig.ini", (const uint8_t *)ini, (size_t)(w - ini));
	}
	ok = 1;
done:
	for (int i = 0; i < plan_count; i++) free(plan[i].path);
	free(plan);
	free(folders);
	return ok;
}

int rsdk_files_load(const struct rsdk_settings *s, char *err, int errsize)
{
	write_ini(s);
	return read_pack(err, errsize) && seed_saves(err, errsize) && load_mods(err, errsize);
}

/* ------------------------------------------------------------ export */

static int g_export[MEMFILES];
static int g_export_count;

int rsdk_saves_count(void)
{
	g_export_count = 0;
	for (int i = 0; i < MEMFILES; i++)
		if (g_files[i].name[0] && g_files[i].data && g_files[i].size && strcasecmp(g_files[i].name, "Settings.ini") &&
		    strcasecmp(g_files[i].name, "log.txt") && !under_mods(g_files[i].name))
			g_export[g_export_count++] = i;
	return g_export_count;
}
const char *rsdk_saves_name(int index) { return index >= 0 && index < g_export_count ? g_files[g_export[index]].name : ""; }
int64_t rsdk_saves_size(int index) { return index >= 0 && index < g_export_count ? (int64_t)g_files[g_export[index]].size : 0; }
const uint8_t *rsdk_saves_data(int index) { return index >= 0 && index < g_export_count ? (const uint8_t *)g_files[g_export[index]].data : NULL; }

/* ------------------------------------------------------------ fopen */

static const char *base_name(const char *path)
{
	const char *b = strrchr(path, '/');
	return b ? b + 1 : path;
}

static int writes(const char *mode) { return strchr(mode, 'w') || strchr(mode, 'a') || strchr(mode, '+'); }

static struct mem_file *find_file(const char *name)
{
	for (int i = 0; i < MEMFILES; i++)
		if (g_files[i].name[0] && !strcasecmp(g_files[i].name, name)) return &g_files[i];
	return NULL;
}

static FILE *open_written(const char *name)
{
	struct mem_file *mf = find_file(name);
	if (!mf)
	{
		for (int i = 0; i < MEMFILES && !mf; i++)
			if (!g_files[i].name[0]) mf = &g_files[i];
		if (!mf || strlen(name) >= sizeof mf->name) { errno = ENOSPC; return NULL; }
		snprintf(mf->name, sizeof mf->name, "%s", name);
	}
	if (mf->stream) { errno = EBUSY; return NULL; } /* one writer at a time */
	free(mf->writing);
	mf->writing = NULL;
	mf->writing_size = 0;
	mf->stream = open_memstream(&mf->writing, &mf->writing_size);
	return mf->stream;
}

/* a mod's file of no bytes: a stream that is at its end (fmemopen takes no
 * empty buffer) */
static ssize_t empty_read(void *c, char *buf, size_t n) { (void)c; (void)buf; (void)n; return 0; }
static int empty_seek(void *c, off_t *at, int whence) { (void)c; (void)whence; *at = 0; return 0; }
static int empty_close(void *c) { (void)c; return 0; }

FILE *__wrap_fopen(const char *path, const char *mode)
{
	if (!path || !mode) { errno = EINVAL; return NULL; }
	/* the core's own channels, read by the miniBox kit at Init */
	if (!strcmp(path, "settings") || !strcmp(path, "slots")) return __real_fopen(path, mode);
	const char *name = base_name(path);
	/* the mods folder (chimera#590): a mod's files, and what the mod loader
	 * writes, by the whole path; anything else there (a save a mod's
	 * RedirectSaveRAM puts in its folder) is a file like the others */
	char norm[1024];
	norm_path(path, norm, sizeof norm);
	if (under_mods(norm))
	{
		if (writes(mode)) return open_written(loader_file(norm) ? norm : name);
		/* the mod loader's files by their whole path; directly in a mod's
		 * folder, where a mod's RedirectSaveRAM puts the saves, a save the
		 * game wrote or the Save data slot gave by its name */
		const char *slash = strchr(norm + 5, '/');
		struct mem_file *mf = loader_file(norm) ? find_file(norm) : slash && !strchr(slash + 1, '/') ? find_file(name) : NULL;
		if (mf && mf->data && mf->size) return fmemopen(mf->data, mf->size, "rb");
		if (mf && loader_file(norm)) { errno = ENOENT; return NULL; }
		const struct mod_file *mod = find_mod_file(norm);
		if (mod)
		{
			if (!mod->size)
			{
				cookie_io_functions_t io = { empty_read, NULL, empty_seek, empty_close };
				return fopencookie(NULL, "rb", io);
			}
			return fmemopen((void *)mod->data, mod->size, "rb");
		}
		if (loader_file(norm)) { errno = ENOENT; return NULL; }
	}
	if (writes(mode)) return open_written(name);
	struct mem_file *mf = find_file(name);
	if (mf)
	{
		if (!mf->data || !mf->size) { errno = ENOENT; return NULL; }
		return fmemopen(mf->data, mf->size, "rb");
	}
	if (!strcasecmp(name, "Settings.ini") && g_ini_len) return fmemopen(g_ini, g_ini_len, "rb");
	if (!strcasecmp(name, "Data.rsdk") && g_pack) return fmemopen(g_pack, g_pack_size, "rb");
	errno = ENOENT;
	return NULL;
}

/* The engine opens its files through SDL (Reader.hpp: FileIO is an
 * SDL_RWops), and SDL's SDL_RWFromFile refuses a stream that is not a
 * regular file or a pipe - a memory stream is neither, having no file
 * descriptor. So SDL is answered here too, with the same stream wrapped as
 * SDL would wrap a file's: closing it closes the stream (and a written
 * file's close, below, keeps its bytes). */
SDL_RWops *__wrap_SDL_RWFromFile(const char *file, const char *mode)
{
	FILE *f = __wrap_fopen(file, mode);
	if (!f)
	{
		SDL_SetError("Couldn't open %s", file ? file : "(null)");
		return NULL;
	}
	return SDL_RWFromFP(f, SDL_TRUE);
}

/* a file written and closed: its bytes become the file */
int __wrap_fclose(FILE *f)
{
	for (int i = 0; i < MEMFILES; i++)
	{
		struct mem_file *mf = &g_files[i];
		if (f && f == mf->stream)
		{
			const int r = __real_fclose(f);
			mf->stream = NULL;
			free(mf->data);
			mf->data = mf->writing;
			mf->size = mf->writing_size;
			mf->writing = NULL;
			mf->writing_size = 0;
			return r;
		}
	}
	return __real_fclose(f);
}
