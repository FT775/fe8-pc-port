-- Press scan (diagnostic): play a per-FRAME key file as movie_check.lua does
-- and test, for each press that starts in a frame window, whether the game
-- reacted to it (port/test/README.md, "TAS route").
--
--   MOVIE_KEYS=FILE MOVIE_FRAME0=N SCAN_FROM=F SCAN_TO=G [SCAN_K=10] \
--     build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/movie_presses.lua 3000000
--
-- A press is a key that is down in interval n and was up in interval n-1.
-- At callback n the core's state is saved (emu:saveStateBuffer), SCAN_K
-- frames are run as the movie has them, the game state is hashed, the state
-- is loaded again, the same SCAN_K frames are run with that press's keys left
-- out for as long as the movie holds them, hashed again, and the state is
-- loaded once more before the run goes on as the movie has it. Equal hashes:
-- the game did not react within SCAN_K frames ("IGNORED"). A TAS makes no
-- press the game ignores on the emulator it was made on (a few deliberate
-- no-op presses aside), so the first IGNORED press of a run is where it left
-- that emulator's timing. The hash covers the proc array, gBmSt (without
-- prevVCount), gPlaySt, gRNSeeds, the unit arrays' first 0x48*8 bytes, the
-- action data and gMainCallback; not sKeyStatusBuffer, which every press
-- changes.

-- (no test cheat here, port/test/README.md "Test cheats": ROUTE_CHEAT, which route_lib.lua would act on, stops it)
assert((os.getenv("ROUTE_CHEAT") or "") == "", "movie_presses.lua: ROUTE_CHEAT is not supported here: it has no "
  .. "test cheat (port/test/README.md, \"Test cheats\")")
local L = dofile(os.getenv("ROUTE_LIB") or "port/test/oracle/route_lib.lua")
local masks = L.readKeys(assert(os.getenv("MOVIE_KEYS"), "MOVIE_KEYS is required"))
local frame0 = tonumber(os.getenv("MOVIE_FRAME0") or "0")
local from = tonumber(os.getenv("SCAN_FROM") or "0")
local to = tonumber(os.getenv("SCAN_TO") or "3000")
local K = tonumber(os.getenv("SCAN_K") or "10")
local stopAfterIgnored = tonumber(os.getenv("SCAN_STOP_AFTER") or "0")  -- 0: never

local function maskFor(n) return masks[n + frame0 + 1] or 0 end

local function fnv1a(s, h)
  h = h or 0x811C9DC5
  for i = 1, #s do h = ((h ~ s:byte(i)) * 0x01000193) & 0xFFFFFFFF end
  return h
end
local GACTION = 0x0203A958  -- gActionData (struct ActionData, 0x38 bytes), fireemblem8.map
local function stateHash()
  local h = fnv1a(emu:readRange(L.ADDR.sProcArray, 64 * 0x6C))
  local bm = emu:readRange(L.ADDR.gBmSt, 0x40)
  bm = bm:sub(1, 0x2C) .. bm:sub(0x2F)   -- leave out prevVCount (u16 at +0x2C), README "gBmSt.prevVCount"
  h = fnv1a(bm, h)
  h = fnv1a(emu:readRange(L.ADDR.gPlaySt, 0x4C), h)
  h = fnv1a(emu:readRange(L.ADDR.gRNSeeds, 6), h)
  h = fnv1a(emu:readRange(L.ADDR.gUnitArrayBlue, 0x48 * 8), h)
  h = fnv1a(emu:readRange(GACTION, 0x38), h)
  h = fnv1a(emu:readRange(L.ADDR.gMainCallback, 4), h)
  return h
end

local frame, tick = 0, 0
local ignored, tested = 0, 0
local names = { [0x1] = "A", [0x2] = "B", [0x4] = "Select", [0x8] = "Start", [0x10] = "Right", [0x20] = "Left",
                [0x40] = "Up", [0x80] = "Down", [0x100] = "R", [0x200] = "L" }
local function keyNames(m)
  local t = {}
  for b = 0, 9 do if (m >> b) & 1 == 1 then t[#t + 1] = names[1 << b] end end
  return table.concat(t, "+")
end

-- Run k frames from the current state with keys(n) for interval n.
local function runBranch(n0, keysFor)
  for j = 1, K do
    emu:runFrame()
    emu:setKeys(keysFor(n0 + j))
  end
  return stateHash()
end

emu:setKeys(maskFor(0))
callbacks:add("frame", function()
  frame = frame + 1
  local n = frame
  local m, prev = maskFor(n), maskFor(n - 1)
  local new = m & ~prev
  if n >= from and n <= to and new ~= 0 then
    local hold = 0
    while hold < K and (maskFor(n + hold) & new) == new do hold = hold + 1 end
    local save = emu:saveStateBuffer()
    emu:setKeys(m)
    local h1 = runBranch(n, maskFor)
    emu:loadStateBuffer(save)
    emu:setKeys(m & ~new)
    local h2 = runBranch(n, function(i)
      if i < n + hold then return maskFor(i) & ~new end
      return maskFor(i)
    end)
    emu:loadStateBuffer(save)
    tested = tested + 1
    local ps = L.playSt()
    local verdict = (h1 == h2) and "IGNORED" or "ok"
    if h1 == h2 then ignored = ignored + 1 end
    print(string.format("press frame %d (movie entry %d) %s held %d: %s  [chapterIndex %d turn %d phase %02x]", n,
      n + frame0, keyNames(new), hold, verdict, ps.chapter, ps.turn, ps.faction))
    io.stdout:flush()
    if h1 == h2 and stopAfterIgnored > 0 and ignored >= stopAfterIgnored then
      print(string.format("movie_presses: stopped after %d ignored presses (%d tested)", ignored, tested))
      os.exit(0)
    end
  end
  emu:setKeys(m)
  if n > to then
    print(string.format("movie_presses: frames %d-%d: %d presses tested, %d ignored", from, to, tested, ignored))
    os.exit(0)
  end
end)
