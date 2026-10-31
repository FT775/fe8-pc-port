-- Route check: replay a Gate 0 key file open-loop in mGBA and print the
-- route milestones (route_lib.lua) with their ticks, chapterIndex and RNG
-- state, then the chapter, turn and units at the last tick.
--
--   GATE0_KEYS=port/test/inputs/route_ch0_prologue.keys \
--     build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/route_check.lua 2000000
--
-- Environment: GATE0_KEYS (key file), ROUTE_EXTRA (ticks to run past the
-- last entry, default 0), ROUTE_LIB (path of route_lib.lua), ROUTE_TICK
-- ("frame" or "capture", as the file was made: its "# Generated" header line
-- names ROUTE_TICK=capture when it was; "capture" needs MGBA_RUN_BP, see
-- route_lib.lua "Tick rules"), ROUTE_CHEAT (a cheat-assisted file, whose
-- "# Generated" line names ROUTE_CHEAT=...: the same test cheats,
-- route_lib.lua; with ROUTE_TICK=capture; it stops the replay in Eirika's
-- Chapter 20 and final chapter, test_cheat.lua).
--
-- ROUTE_SCENES_OUT=FILE (coverage 12): also write one line per run of ticks
-- of one kind, "first last kind chapterIndex": the scene kinds of
-- route_lib.lua's L.sceneKind (opening, intro, quote, event, talk, ending),
-- else "battle" (the battle animation screen), "menu" (a menu is open) or "-".
-- The Gate 0 frames of a route's scenes come from it (GATE0_SHOT_TICKS).
--
-- Ticks and key timing: a tick is sampled when a frame ends with
-- gBmSt.main_loop_ended == 1 (with ROUTE_TICK=capture, exactly as
-- gate0_capture.lua samples it), and the mask for tick t+1 is set right after
-- tick t is sampled.

local L = dofile(os.getenv("ROUTE_LIB") or "port/test/oracle/route_lib.lua")
local keysPath = assert(os.getenv("GATE0_KEYS"), "GATE0_KEYS is required")
local extra = tonumber(os.getenv("ROUTE_EXTRA") or "0")
local masks = L.readKeys(keysPath)
local last = #masks + extra
local milestones = L.newMilestones()
local tick, frame = 0, 0
if L.cheat then L.cheat.tick = function() return tick end end   -- (named by the cheat's refusal, test_cheat.lua)

local function summary()
  local ps = L.playSt()
  local seeds, lcg = L.rng()
  print(string.format("route_check: %s: %d ticks (%d key entries) in %d frames", keysPath, tick, #masks, frame))
  print(string.format("route_check: final chapterIndex %d, turn %d, phase %02x, gRNSeeds %s, gLCGRNValue %s",
    ps.chapter, ps.turn, ps.faction, seeds, lcg))
  for _, set in ipairs({ { "blue", L.blue() }, { "red", L.red() }, { "green", L.green() } }) do
    for _, u in ipairs(set[2]) do
      print(string.format("route_check:   %s unit %02x char %02x class %02x lv %d at (%d,%d) hp %d/%d",
        set[1], u.id, u.char, u.class, u.level, u.x, u.y, u.hp, u.maxHp))
    end
  end
  io.stdout:flush()
end

local scenesPath = os.getenv("ROUTE_SCENES_OUT")
local scenesF = scenesPath and scenesPath ~= "" and assert(io.open(scenesPath, "w")) or nil
local runKind, runFirst, runCh = nil, 0, 0
local function sceneStep()
  local procs = L.procs()
  local kind = L.sceneKind(procs)
  if not kind then
    local cb = emu:read32(L.ADDR.gMainCallback)
    if cb == L.CB.InBattleMainRoutine or cb == L.CB.MainUpdate_8055C68 then kind = "battle"
    elseif L.findIdle(procs, L.IDLE.Menu_OnIdle) then kind = "menu"
    else kind = "-" end
  end
  local ch = L.playSt().chapter
  if kind ~= runKind or ch ~= runCh then
    if runKind then scenesF:write(string.format("%d %d %s %d\n", runFirst, tick - 1, runKind, runCh)) end
    runKind, runFirst, runCh = kind, tick, ch
  end
end
local function scenesEnd()
  if scenesF then
    if runKind then scenesF:write(string.format("%d %d %s %d\n", runFirst, tick - 1, runKind, runCh)) end
    scenesF:close()
  end
end

emu:setKeys(masks[1] or 0)
L.onTick(L.tickRule(), function() frame = frame + 1 end, function()
  for _, line in ipairs(milestones.step(tick)) do print(line) end
  io.stdout:flush()
  if scenesF then sceneStep() end
  tick = tick + 1
  if tick >= last then scenesEnd(); summary(); os.exit(0) end
  emu:setKeys(masks[tick + 1] or 0)
end)
