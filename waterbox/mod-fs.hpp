/* mod-fs.hpp - what the engine's mod loader asks of std::filesystem,
 * answered from the mods the project gives (rsdk-files.c).
 *
 * Upstream's mod loader (RSDK/Core/ModAPI.cpp) looks for mods in a "mods"
 * folder on the computer's disk with std::filesystem: whether a path exists,
 * whether it is a folder, what a folder holds. The sandbox has no folders
 * (a flat list of files, miniBox's MACHINE-SPEC section 8), and the core
 * never reads the computer's disk anyway, so patches/0003 has ModAPI.cpp use
 * this instead, as upstream's Android build uses one of its own: the same
 * names (fs::path, fs::exists, fs::directory_iterator and so on), answered
 * from the mods' files, which the core unpacked into the machine's memory at
 * power-on. Both builds use it, so the native reference reads no disk either.
 */
#ifndef RSDK_MOD_FS_HPP
#define RSDK_MOD_FS_HPP

#include <string>
#include <vector>

#include "rsdk-driver.h"

namespace fs
{

struct filesystem_error
{
	const char *what() const { return "mods"; }
};

struct path
{
	path() = default;
	path(const std::string &s) : str(s) {}
	path(const char *s) : str(s) {}
	const std::string &string() const { return str; }
	path filename() const { return str.substr(str.find_last_of('/') + 1); }

private:
	std::string str;
};

inline bool exists(const path &p) { return rsdk_mods_kind(p.string().c_str()) != RSDK_MODS_NONE; }
inline bool is_directory(const path &p) { return rsdk_mods_kind(p.string().c_str()) == RSDK_MODS_FOLDER; }

struct directory_entry
{
	directory_entry() = default;
	directory_entry(const std::string &p, bool folder) : where(p), folder(folder) {}
	bool exists() const { return true; }
	bool is_directory() const { return folder; }
	bool is_regular_file() const { return !folder; }
	const fs::path &path() const { return where; }

private:
	fs::path where;
	bool folder = false;
};

enum class directory_options
{
	follow_directory_symlink
};

/* a folder's entries, in the order the project's zips hold them; upstream
 * iterates these with a range-for */
inline std::vector<directory_entry> list(const path &p, int recursive)
{
	std::vector<directory_entry> out;
	rsdk_mods_list(
		p.string().c_str(), recursive,
		[](void *user, const char *name, int folder) {
			static_cast<std::vector<directory_entry> *>(user)->emplace_back(std::string(name), folder != 0);
		},
		&out);
	return out;
}

inline std::vector<directory_entry> directory_iterator(const path &p) { return list(p, 0); }
inline std::vector<directory_entry> recursive_directory_iterator(const path &p, directory_options) { return list(p, 1); }

} // namespace fs

#endif
