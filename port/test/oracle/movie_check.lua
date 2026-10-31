-- Movie check: play a per-FRAME key file in mGBA, as a movie player does, and
-- print the run's milestones (port/test/README.md, "TAS route").
--
--   MOVIE_KEYS=FILE MOVIE_FRAME0=N [MOVIE_OUT=milestones.jsonl] [MOVIE_FRAMES=F] \
--     build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/movie_check.lua 3000000
--
-- FILE is in the key-file format (README.md, "Input file": MASK or MASK*N,
-- '#' comments), but entry k is one emulated FRAME, not one logic tick.
-- Frame intervals: interval 0 runs from power-on to the first "frame"
-- callback (the first VBlank start, line 160), interval n from callback n to
-- callback n+1. Interval n plays entry n + MOVIE_FRAME0 (an entry before the
-- file's start is 000). The keys of interval n are set at callback n (interval
-- 0's before the first instruction), which is when a VBA-rr movie latches its
-- next frame's input: at VBlank start. No breakpoints: the core runs whole
-- frames (fast). Ticks are counted as route_check.lua counts them (a frame
-- that ends with gBmSt.main_loop_ended set), for the milestones only.
--
-- MOVIE_TICKS=1: FILE is an ordinary tick key file instead, played as
-- route_check.lua plays it (the mask for tick t+1 is set when tick t is
-- seen). The same milestones then come out for a tick route made from a
-- movie (tools/tas_route.py), and the two lists can be compared.
--
-- Movie resets: tools/tas_inputs.py refuses a movie with reset bits (none in
-- the movies used so far), so there is no reset entry here. The game's own
-- soft reset (A+B+Start+Select) is ordinary keys.
--
-- Milestones: route_lib.lua's (new game, map loaded, turn 1, first player
-- phase, first enemy phase, first battle, game saves, chapterIndex changes);
-- every turn change; blue units that die; and scenes: each time one of the
-- top-level screens below starts (health and safety, opening animation,
-- title, save menu, difficulty, opening subtitles, world map, battle map,
-- chapter intro, battle preparations, game over, ending credits). MOVIE_OUT
-- writes them as JSON lines too. The run ends at MOVIE_FRAMES (default: the
-- file's last entry + 600 frames), MOVIE_GAMEOVER_STOP frames after the game
-- over screen (default 600), or at the runner's frame limit.

-- (no test cheat here, port/test/README.md "Test cheats": ROUTE_CHEAT, which route_lib.lua would act on, stops it)
assert((os.getenv("ROUTE_CHEAT") or "") == "", "movie_check.lua: ROUTE_CHEAT is not supported here: it has no test "
  .. "cheat (port/test/README.md, \"Test cheats\")")
local L = dofile(os.getenv("ROUTE_LIB") or "port/test/oracle/route_lib.lua")
local keysPath = assert(os.getenv("MOVIE_KEYS"), "MOVIE_KEYS is required")
local tickMode = os.getenv("MOVIE_TICKS") == "1"
local frame0 = tonumber(os.getenv("MOVIE_FRAME0") or "0")
assert(not (tickMode and frame0 ~= 0), "MOVIE_FRAME0 is for frame key files only")
local masks = L.readKeys(keysPath)
local stopFrame = tonumber(os.getenv("MOVIE_FRAMES") or "") or (tickMode and math.huge or #masks - frame0 + 600)
local gameOverStop = tonumber(os.getenv("MOVIE_GAMEOVER_STOP") or "600")
local outPath = os.getenv("MOVIE_OUT")
local statusEvery = tonumber(os.getenv("MOVIE_STATUS_EVERY") or "20000")
local out = outPath and assert(io.open(outPath, "w"), "cannot open " .. outPath)

-- Proc scripts of the top-level screens (fireemblem8.elf).
local SCENES = {
  { 0x08AA7680, "health and safety screen (ProcScr_GameEarlyStartUI)" },
  { 0x08AA71C8, "opening animation (ProcScr_OpAnim)" },
  { 0x08AA6A50, "title screen (gProcScr_TitleScreen)" },
  { 0x08A200B8, "save menu (ProcScr_SaveMenu)" },
  { 0x08A20A10, "difficulty select (ProcScr_NewGameDifficultySelect)" },
  { 0x08AA21BC, "opening subtitles (gProcScr_OpSubtitle)" },
  { 0x08A3D748, "world map (ProcScr_WorldMapMain)" },
  { 0x0859A1F0, "battle map (gProc_BMapMain)" },
  { 0x0859B1B0, "chapter intro (gProcScr_ChapterIntro)" },
  { 0x08A1829C, "battle preparations (ProcScr_AtMenu)" },
  { 0x0859B358, "GAME OVER screen (ProcScr_GameOverScreen)" },
  { 0x08AA2084, "ending credits (ProcScr_EndingCredits)" },
}
local PROC_GAMEOVER = 0x0859B358

local function maskForInterval(n)
  return masks[n + frame0 + 1] or 0
end

local milestones = L.newMilestones()
local tick, frame = 0, 0
local lastTurnKey = nil
local deadSeen = {}
local scenePresent = {}
local gameOverFrame = nil
local battles, battleCb = 0, false

local function emit(kind, text)
  local ps = L.playSt()
  local seeds, lcg = L.rng()
  print(string.format("frame %d tick %d: %s (chapterIndex %d, turn %d, phase %02x, gRNSeeds %s, gLCGRNValue %s)",
    frame, tick, text, ps.chapter, ps.turn, ps.faction, seeds, lcg))
  if out then
    out:write(string.format('{"frame":%d,"tick":%d,"kind":"%s","text":"%s","chapter":%d,"turn":%d,"phase":%d,'
      .. '"state_bits":%d,"rn":"%s","lcg":"%s"}\n', frame, tick, kind, (text:gsub('"', "'")), ps.chapter, ps.turn,
      ps.faction, ps.stateBits, seeds, lcg))
    out:flush()
  end
end

local function summary(why)
  local ps = L.playSt()
  local seeds, lcg = L.rng()
  print(string.format("movie_check: %s: stopped (%s) at frame %d, tick %d; %d battle-screen entries", keysPath, why,
    frame, tick, battles))
  print(string.format("movie_check: final chapterIndex %d, turn %d, phase %02x, chapterStateBits %02x, gRNSeeds %s, "
    .. "gLCGRNValue %s", ps.chapter, ps.turn, ps.faction, ps.stateBits, seeds, lcg))
  for _, u in ipairs(L.blue()) do
    print(string.format("movie_check:   blue unit %02x char %02x class %02x lv %d at (%d,%d) hp %d/%d",
      u.id, u.char, u.class, u.level, u.x, u.y, u.hp, u.maxHp))
  end
  if out then
    out:write(string.format('{"frame":%d,"tick":%d,"kind":"end","text":"%s","chapter":%d,"turn":%d}\n',
      frame, tick, why, ps.chapter, ps.turn))
    out:close()
  end
  io.stdout:flush()
end

-- Once per logic tick, after it is seen (the frame ended with main_loop_ended set).
local function tickMilestones()
  for _, line in ipairs(milestones.step(tick)) do
    emit("route", line:match("^tick %d+: (.-) %(") or "milestone")
  end
  local ps = L.playSt()
  local cb = emu:read32(L.ADDR.gMainCallback)
  local inBattle = (cb == L.CB.InBattleMainRoutine or cb == L.CB.MainUpdate_8055C68)
  if inBattle and not battleCb then battles = battles + 1 end
  battleCb = inBattle
  if ps.mode ~= 0 and ps.chapter >= 0 and ps.turn >= 1 then
    local key = ps.chapter .. ":" .. ps.turn
    if key ~= lastTurnKey then
      lastTurnKey = key
      emit("turn", string.format("chapter %d turn %d", ps.chapter, ps.turn))
    end
  end
  if ps.mode ~= 0 then
    for i = 0, 61 do
      local u = L.ADDR.gUnitArrayBlue + i * 0x48
      local cp = emu:read32(u)
      if cp ~= 0 and (emu:read32(u + 0x0C) & L.US.DEAD) ~= 0 then
        local char = emu:read8(cp + 4)
        if not deadSeen[char] then
          deadSeen[char] = true
          emit("death", string.format("blue char %02x dead", char))
        end
      end
    end
  end
end

emu:setKeys(tickMode and (masks[1] or 0) or maskForInterval(0))
print(string.format("movie_check: %s, %d %s entries%s, stop at frame %s", keysPath, #masks,
  tickMode and "tick" or "frame", tickMode and "" or string.format(", interval 0 plays entry %d", frame0),
  stopFrame == math.huge and "(end of file)" or tostring(stopFrame)))

callbacks:add("frame", function()
  frame = frame + 1
  if not tickMode then emu:setKeys(maskForInterval(frame)) end  -- the interval that starts now (VBlank start)
  if emu:read8(L.ADDR.gBmSt) ~= 0 then
    tickMilestones()
    tick = tick + 1
    if tickMode then emu:setKeys(masks[tick + 1] or 0) end
  end
  local procs = L.procs()
  for _, s in ipairs(SCENES) do
    local here = L.findScript(procs, s[1]) ~= nil
    if here and not scenePresent[s[1]] then
      emit(s[1] == PROC_GAMEOVER and "gameover" or "scene", s[2])
      if s[1] == PROC_GAMEOVER and not gameOverFrame then gameOverFrame = frame end
    end
    scenePresent[s[1]] = here
  end
  if statusEvery > 0 and frame % statusEvery == 0 then
    local ps = L.playSt()
    local seeds = L.rng()
    print(string.format("movie_check: status frame %d tick %d chapterIndex %d turn %d phase %02x gRNSeeds %s",
      frame, tick, ps.chapter, ps.turn, ps.faction, seeds))
    io.stdout:flush()
  end
  if gameOverFrame and frame >= gameOverFrame + gameOverStop then summary("game over"); os.exit(0) end
  if tickMode and tick >= #masks + tonumber(os.getenv("ROUTE_EXTRA") or "0") then
    summary("end of the tick key file"); os.exit(0)
  end
  if frame >= stopFrame then summary("frame limit"); os.exit(0) end
end)
