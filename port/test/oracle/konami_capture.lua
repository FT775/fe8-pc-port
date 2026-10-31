-- Gate 0 capture (gate0_capture.lua, unchanged) plus the debug build's way
-- into the debug menu, for the test-build shortcut check
-- (port/test/konami.mk; port-plan/DIVERGENCES.md TB-1 to TB-6).
--
-- The test build opens the debug menu with the game's own code: the player
-- phase's A path opens the map menu, then StartDebugMenu(mapMenu), the map
-- menu's R handler in the debug build, swaps it for the debug menu
-- (src/playerphase.c, PortTest_OpenDebugMenu). Retail has no R handler for it,
-- so on the GBA this script makes that one call: in tick KONAMI_TICK, when
-- PlayerPhase_MainIdle has taken the A on an empty tile (map menu opened) and
-- returns to the proc runner, it calls StartDebugMenu with that map menu and
-- the runner's return address, so StartDebugMenu returns where MainIdle would
-- have. Everything else is the retail ROM, and the trace is gate0_capture's.
--
-- Breakpoints (MGBA_RUN_BP): gate0_capture's eight plus
--   konami_idle@<PlayerPhase_MainIdle>:ret   its return to the proc runner
--   konami_menu@<StartOrphanMenuAdjusted>:ret its return value, the map menu
-- Environment: gate0_capture's, plus
--   KONAMI_TICK          the tick whose A opens the map menu (the sequence's A)
--   KONAMI_DEBUG_MENU    StartDebugMenu's address (Thumb, without bit 0)
--
-- Optional, KONAMI_JUMP="NULLSUB9,ENDEACH,START,GOTO,GAMECTRL" (addresses of
-- nullsub_9, Proc_EndEach, Proc_Start, Proc_Goto and gProcScr_GameControl)
-- with the breakpoint konami_null@<nullsub_9>: the test build's stand-in for
-- the empty nullsub_9 at the end of the debug menu's chapter jump
-- (src/gamecontrol.c) is made on the GBA too, as three calls chained through
-- nullsub_9 itself (its body is bx lr): Proc_EndEach(gProcScr_GameControl),
-- Proc_Start(gProcScr_GameControl, PROC_TREE_3), Proc_Goto(that proc,
-- LGAMECTRL_EXEC_BM = 7), then back to nullsub_9's caller.

dofile("port/test/oracle/gate0_capture.lua")

local konamiTick = tonumber(os.getenv("KONAMI_TICK") or "")
local startDebugMenu = tonumber(os.getenv("KONAMI_DEBUG_MENU") or "")
assert(konamiTick and startDebugMenu, "konami_capture: set KONAMI_TICK and KONAMI_DEBUG_MENU")
local bp = os.getenv("MGBA_RUN_BP") or ""
for _, name in ipairs({ "konami_idle", "konami_menu" }) do
  assert(bp:find(name .. "@", 1, true), "MGBA_RUN_BP must set the " .. name .. " breakpoint (port/test/konami.mk)")
end

local onmain = 0          -- OnMain entries: OnMain t is the (t+1)-th
local menu = nil          -- the map menu opened in tick konamiTick
local done = false

local function tickNow() return onmain - 1 end

callbacks:add("gate0_onmain", function() onmain = onmain + 1 end)

callbacks:add("konami_menu_ret", function()
  if not done and tickNow() == konamiTick then
    menu = emu:readRegister("r0")
  end
end)

callbacks:add("konami_idle_ret", function()
  if done or tickNow() ~= konamiTick then return end
  done = true
  if menu == nil then
    io.stderr:write(string.format("konami_capture: tick %d: the A did not open the map menu "
      .. "(the cursor must be on an empty tile)\n", konamiTick))
    io.stderr:flush()
    os.exit(6)
  end
  -- Stopped before the instruction MainIdle returned to (the runner's next
  -- instruction is pc - 2 in Thumb state).
  local ret = emu:readRegister("pc") - 2
  emu:writeRegister("r0", menu)
  emu:writeRegister("lr", ret | 1)
  emu:writeRegister("pc", startDebugMenu)
  print(string.format("konami_capture: tick %d: StartDebugMenu(0x%08x) called, returning to 0x%08x",
    konamiTick, menu, ret))
  io.stdout:flush()
end)

local jump = os.getenv("KONAMI_JUMP")
if jump and jump ~= "" then
  local a = {}
  for v in jump:gmatch("[^,]+") do a[#a + 1] = tonumber(v) end
  assert(#a == 5, "konami_capture: KONAMI_JUMP wants five addresses")
  local nullsub9, endEach, start, goTo, gameCtrl = a[1], a[2], a[3], a[4], a[5]
  assert(bp:find("konami_null@", 1, true), "KONAMI_JUMP needs the konami_null breakpoint")
  local stage, origLr = 0, nil
  callbacks:add("konami_null", function()
    if stage == 0 then
      if not done then return end                 -- only after the debug menu was opened
      origLr = emu:readRegister("lr")
      emu:writeRegister("r0", gameCtrl)
      emu:writeRegister("lr", nullsub9 | 1)
      emu:writeRegister("pc", endEach)
      stage = 1
    elseif stage == 1 then                       -- Proc_EndEach returned into nullsub_9
      emu:writeRegister("r0", gameCtrl)
      emu:writeRegister("r1", 3)                  -- PROC_TREE_3
      emu:writeRegister("lr", nullsub9 | 1)
      emu:writeRegister("pc", start)
      stage = 2
    elseif stage == 2 then                       -- Proc_Start returned the new game control
      local proc = emu:readRegister("r0")
      emu:writeRegister("r1", 7)                  -- LGAMECTRL_EXEC_BM
      emu:writeRegister("lr", origLr)
      emu:writeRegister("pc", goTo)
      stage = 3
      print(string.format("konami_capture: tick %d: nullsub_9 stand-in: game control 0x%08x restarted at "
        .. "label 7, back to 0x%08x", tickNow(), proc, origLr & ~1))
      io.stdout:flush()
    end
  end)
end
