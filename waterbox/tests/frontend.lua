-- Frontend witness for the Sonic Mania package: the project's movie plays
-- inside Chimera for a fixed number of frames, then the Game State and Players
-- domains are written out for the gate to compare with the native reference,
-- and the game.* functions are tried against the same bytes.
--
-- The job comes from the file named by MINIHAWK_JOB:
--   frames=<how many frames to advance>
--   out=<directory for the dumps>
--   meta=<path for the result>
--   save=<1: save the project at the end>

local function writeAll(path, data)
	local f = assert(io.open(path, "wb"))
	f:write(data)
	f:close()
end

local meta = {}
local function finish(status, detail)
	local lines = { "status=" .. status, "detail=" .. (detail or "") }
	for k, v in pairs(meta) do
		if k ~= "path" then lines[#lines + 1] = k .. "=" .. tostring(v) end
	end
	table.sort(lines)
	if meta.path then writeAll(meta.path, table.concat(lines, "\n") .. "\n") end
	client.exit()
end

local job = {}
for line in io.lines(os.getenv("MINIHAWK_JOB")) do
	local k, v = line:match("^([^=]+)=(.*)$")
	if k then job[k] = v end
end
meta.path = job.meta

if emu.getsystemid() ~= "SonicMania" then finish("ERROR", "wrong system id: " .. tostring(emu.getsystemid())) end

pcall(function() client.speedmode(6400) end)
pcall(function() client.invisibleemulation(true) end)

-- to the frame count asked for, whichever frame the project opened at (a
-- project run pauses at the end of its movie, so one frame more never comes)
local frames = tonumber(job.frames) or 100
while emu.framecount() < frames do
	emu.frameadvance()
end
meta.frames = emu.framecount()
meta.lag = emu.lagcount()

local function dump(domain, file)
	memory.usememorydomain(domain)
	local size = memory.getcurrentmemorydomainsize()
	local bytes = memory.read_bytes_as_array(0, size, domain)
	local chunks = {}
	for i = 1, #bytes do chunks[i] = string.char(bytes[i]) end
	writeAll(job.out .. "/" .. file, table.concat(chunks))
	return bytes
end
dump("Game State", "gamestate.bin")
local pl = dump("Players", "players.bin")

if job.save == "1" then
	local ok, err = pcall(function() movie.save() end)
	meta.saved = ok and "1" or tostring(err)
end

if game ~= nil and game.list ~= nil then
	local ok, err = pcall(function()
		meta.game_list = #game.list()
		meta.game_x = game.get("Player 1.X")
		meta.game_x_bytes = pl[1] + pl[2] * 256 + pl[3] * 65536 + pl[4] * 16777216  -- Player 1.X is the block's first s32
		meta.game_set = tostring(game.set("Player 1.X", 9))
		meta.game_x_after_set = game.get("Player 1.X")
		meta.game_unknown = tostring(game.get("No Such Property"))
		meta.game_time_ms = game.get("Scene.Time Ms")
		-- Sonic Mania's own: a value by its name, rings set, a shield refused (read only)
		meta.game_character = tostring(game.describe("Player 1.Character").label)
		meta.game_rings_set = tostring(game.set("Player 1.Rings", 77))
		meta.game_rings = game.get("Player 1.Rings")
		meta.game_shield_set = tostring(game.set("Player 1.Shield", 1))
	end)
	if not ok then meta.game_error = tostring(err) end
else
	meta.game_list = "absent"
end

finish("OK", "")
