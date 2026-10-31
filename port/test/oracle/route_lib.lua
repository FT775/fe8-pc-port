-- Shared helpers for the route tools (route_bot.lua, route_check.lua).
-- Not part of the Gate 0 trace: these only read game RAM in mGBA.
--
-- Addresses are from fireemblem8.elf of the pinned build (SHA-1
-- c25b145e37456171ada4b0d440bf88a19f4d509f); struct layouts are from the
-- decomp headers named next to each reader.

local L = {}

L.KEY = { A = 0x001, B = 0x002, SELECT = 0x004, START = 0x008, RIGHT = 0x010,
          LEFT = 0x020, UP = 0x040, DOWN = 0x080, R = 0x100, L = 0x200 }

L.ADDR = {
  gRNSeeds         = 0x03000000, -- u16[3]   (random.c)
  gLCGRNValue      = 0x03000008, -- u32
  gGameClock       = 0x03000014,
  gMainCallback    = 0x02024CB8,
  sProcArray       = 0x02024E68, -- struct Proc[64], 0x6C each (include/proc.h)
  gBmSt            = 0x0202BCB0, -- struct BmSt (include/types.h)
  gPlaySt          = 0x0202BCF0, -- struct PlaySt (include/types.h)
  gUnitArrayBlue   = 0x0202BE4C, -- struct Unit[62], 0x48 each (include/bmunit.h)
  gUnitArrayRed    = 0x0202CFBC, -- struct Unit[50]
  gUnitArrayGreen  = 0x0202DDCC, -- struct Unit[20]
  gBmMapSize       = 0x0202E4D4, -- struct Vec2
  gBmMapUnit       = 0x0202E4D8, -- u8 ** (rows)
  gBmMapTerrain    = 0x0202E4DC,
  gBmMapMovement   = 0x0202E4E0,
  gActiveUnit      = 0x03004E50, -- struct Unit *
  gItemData        = 0x08809B10, -- struct ItemData[], 0x24 each (include/bmitem.h)
}

-- Proc scripts and idle callbacks (Thumb addresses have bit 0 set in proc_idleCb).
L.PROC = {
  GameControl        = 0x085916D4,
  BMapMain           = 0x0859A1F0, -- gProc_BMapMain
  PlayerPhase        = 0x0859AAD8,
  EventEngine        = 0x08591AC0, -- ProcScr_StdEventEngine
  BattleEventEngine  = 0x08591AF8, -- battle quotes; Start cannot skip them
  TargetSelection    = 0x085B655C,
  BKSEL              = 0x0859E520,
  WorldMapMain       = 0x08A3D748,
  OpSubtitle         = 0x08AA21BC, -- opening story slides after New Game (Start skips)
  GmapBaseMenu       = 0x08A3EEEC, -- world map menu on a cleared node (shops, items)
  PrepItemScreen     = 0x08A189A4,
  PrepItemTradeScreen = 0x08A1901C,
  AtMenu             = 0x08A1829C, -- ProcScr_AtMenu: battle preparations main menu
  ConvoyMenu         = 0x0859AE38, -- gProcCmd_ConvoyMenu: inventory full after a drop (src/convoymenu.c)
}
-- World map (include/worldmap.h): struct GMapData gGMData, struct GMapNodeData gWMNodeData[].
L.GM = { data = 0x03005280, nodeData = 0x082060B0, nodeCount = 0x1D,
         Loop = 0x080B9805,  -- WorldMap_Loop: free cursor on the world map
         links = 0x0201AFF0 } -- gGmNodeLinks: struct GMapNodeLink[] (s8 numConnections, s8 connections[7]), by node
L.IDLE = {
  PlayerPhase_MainIdle          = 0x0801C941,
  PlayerPhase_RangeDisplayIdle  = 0x0801CD1D,
  Menu_OnIdle                   = 0x0804F165,
  TargetSelection_Loop          = 0x0804F96D,
  DifficultySelect_KeyHandler   = 0x080AC289,
  TalkWaitForInput              = 0x08007C4D,
  TalkChoice_OnIdle             = 0x08007E9D, -- [Yes]/[No]/[Buy/Sell] choice in a talk (struct TalkChoiceProc, include/scene.h)
  PrepItemScreen_UnitSelect     = 0x08099121, -- sub_8099120: the unit grid of the item screen (world-map shops pick their unit here)
}
-- Scenes (coverage 12, "Watching the scenes" in inputs/README-routes.md): the procs of a story
-- scene besides L.PROC's event engines and opening slides. route_bot.lua's ROUTE_WATCH waits
-- on them instead of skipping; route_check.lua's ROUTE_SCENES_OUT lists the ticks they run.
L.SCENE = {
  Talk                  = 0x08591358, -- gProcScr_Talk (src/scene.c): a talk box printing or waiting
  ChapterIntro          = 0x0859B1B0, -- gProcScr_ChapterIntro (src/chapterintrofx.c: A, B or Start end it)
  ChapterIntroTitleOnly = 0x0859B578, -- gProcScr_ChapterIntroTitleOnly
  CgTextMain            = 0x08A01DBC, -- gProcScr_CgTextMain (src/cgtext.c: the ending's and a trainee's CG text)
  FinScreen             = 0x08A3D498, -- gProcScr_FinScreen
  EndingCredits         = 0x08AA2084, -- ProcScr_EndingCredits (src/ending_credits.c)
  YesNoChoice           = 0x08090015, -- YesNoChoice_Loop_KeyHandler (src/cgtext.c), an idle callback
}
-- The scene kind of a tick (nil: none): the opening slides, a chapter's title, a battle quote
-- (its own event engine), an event, a talk or CG text outside one, the credits and Fin.
function L.sceneKind(procs)
  if L.findScript(procs, L.PROC.OpSubtitle) then return "opening" end
  if L.findScript(procs, L.SCENE.ChapterIntro) or L.findScript(procs, L.SCENE.ChapterIntroTitleOnly) then return "intro" end
  if L.findScript(procs, L.PROC.BattleEventEngine) then return "quote" end
  if L.findScript(procs, L.PROC.EventEngine) then return "event" end
  if L.findScript(procs, L.SCENE.Talk) or L.findScript(procs, L.SCENE.CgTextMain) then return "talk" end
  if L.findScript(procs, L.SCENE.EndingCredits) or L.findScript(procs, L.SCENE.FinScreen) then return "ending" end
  return nil
end
-- Shops (src/bmshop.c, include/bmshop.h): struct ProcShop unit +0x2C,
-- shopItems[20] +0x30, head_loc (the hand's line) +0x5C.
L.SHOP = { Proc = 0x08A39210, BuyLoop = 0x080B44A9, SellLoop = 0x080B47B5 }
-- struct TalkChoiceProc: selectedChoice (1 or 2) +0x2A, choices +0x34.
L.TALKCHOICE = { YesNo = 0x08591490, BuySell = 0x085914A0 }
-- struct PrepUnitList gPrepUnitList (include/prepscreen.h): units[0x40], max_num +0x100;
-- struct PrepItemScreenProc hoverUnitIdx +0x2A (three units a row).
L.PREPLIST = 0x020121CC
L.CB = {
  OnMain               = 0x080152F5,
  MainUpdate_8055C68   = 0x08055C69, -- battle animation screen (enter/leave)
  InBattleMainRoutine  = 0x0804FE41, -- battle animation main loop
}
L.MENUDEF = { UnitAction = 0x0859D1F0, MapMenu = 0x0859D214,
              ItemSelect = 0x0859D184,      -- gItemSelectMenuDef: the Item command's list (overrideId 0x43 + slot)
              ItemSub = 0x0859D118,         -- gItemSubMenuDef: Use, Equip, Trade, Discard
              StaffItemSelect = 0x0859D160, -- gStaffItemSelectMenuDef (overrideId 0x3E + slot)
              WeaponSelect = 0x0859D1CC, YesNo = 0x0859D0F4,
              WMNode = 0x08A3E200,          -- gMenu_WMNodeMenu: A on the lord's cleared world-map node (Armory 0, Shop 1, ? Shop 2, Manage Items 3)
              RouteSplit = 0x089F36A0 }     -- MenuDef_RouteSplit (src/events/ch8-eventscript.h): Eirika 0, Ephraim 1
-- MenuItemDef.overrideId (src/menu_def.c)
L.MENUITEM = { Seize = 0x4E, Attack = 0x4F, AttackBallista = 0x50, Staff = 0x51, Talk = 0x5A,
               Visit = 0x5C, Door = 0x5E, Item = 0x67, Trade = 0x68, Wait = 0x6B, EndPhase = 0x78,
               ItemUse = 0x34, ItemSelect0 = 0x43, StaffSelect0 = 0x3E,
               WeaponSelect0 = 0x49,    -- gWeaponSelectMenuItems: 0x49 + inventory slot
               Rescue = 0x63 }
-- Trade screen (src/bmtrade.c, include/bmtrade.h): struct TradeMenuProc
-- hoverColumn +0x41, hoverRow +0x42; column 0 is the active unit.
L.TRADE = { Proc = 0x0859BB1C, Unselected = 0x0802D835, Selected = 0x0802D981 }

L.US = { HIDDEN = 0x1, UNSELECTABLE = 0x2, DEAD = 0x4, NOT_DEPLOYED = 0x8,
         RESCUING = 0x10, RESCUED = 0x20, HAS_MOVED = 0x40, BIT16 = 0x10000 }

L.CHAR = { EIRIKA = 0x01, SETH = 0x02, GILLIAM = 0x03, FRANZ = 0x04, VANESSA = 0x06,
           ROSS = 0x07, GARCIA = 0x0A, EPHRAIM = 0x0F, BREGUET = 0x46, BONE = 0x47, BAZBA = 0x48,
           ONEILL = 0x68 }

function L.s8(v) if v >= 0x80 then return v - 0x100 end return v end
function L.s16(v) if v >= 0x8000 then return v - 0x10000 end return v end

function L.rng()
  local a = L.ADDR
  return string.format("%04x %04x %04x", emu:read16(a.gRNSeeds), emu:read16(a.gRNSeeds + 2),
    emu:read16(a.gRNSeeds + 4)), string.format("%08x", emu:read32(a.gLCGRNValue))
end

function L.playSt()
  local p = L.ADDR.gPlaySt
  return {
    chapter = L.s8(emu:read8(p + 0x0E)),
    faction = emu:read8(p + 0x0F),
    turn = emu:read16(p + 0x10),
    stateBits = emu:read8(p + 0x14),
    mode = emu:read8(p + 0x1B),
    gold = emu:read32(p + 0x08),
  }
end
-- (the milestones read gPlaySt itself, even where a tool wraps L.playSt: route_bot.lua's
-- ROUTE_SKIRMISH plays a skirmish with another chapter's rules)
local playStRaw = L.playSt

function L.cursor()
  local b = L.ADDR.gBmSt
  return L.s16(emu:read16(b + 0x14)), L.s16(emu:read16(b + 0x16)),
         L.s16(emu:read16(b + 0x20)), L.s16(emu:read16(b + 0x22))
end

-- All live procs: { addr, script, idle, lock }
function L.procs()
  local list = {}
  for i = 0, 63 do
    local p = L.ADDR.sProcArray + i * 0x6C
    local scr = emu:read32(p)
    if scr ~= 0 then
      list[#list + 1] = { addr = p, script = scr, idle = emu:read32(p + 0x0C), lock = emu:read8(p + 0x28) }
    end
  end
  return list
end

function L.findScript(list, scr)
  for _, p in ipairs(list) do if p.script == scr then return p end end
end

function L.findIdle(list, idle)
  for _, p in ipairs(list) do if p.idle == idle then return p end end
end

-- Map readers: the map variables hold a pointer to an array of row pointers.
function L.mapGet(var, x, y)
  local rows = emu:read32(var)
  return emu:read8(emu:read32(rows + 4 * y) + x)
end

function L.mapSize()
  return L.s16(emu:read16(L.ADDR.gBmMapSize)), L.s16(emu:read16(L.ADDR.gBmMapSize + 2))
end

function L.item(id)
  local d = L.ADDR.gItemData + id * 0x24
  return {
    id = id,
    weaponType = emu:read8(d + 0x07),
    attributes = emu:read32(d + 0x08),
    might = emu:read8(d + 0x15),
    hit = emu:read8(d + 0x16),
    weight = emu:read8(d + 0x17),
    crit = emu:read8(d + 0x18),
    minRange = emu:read8(d + 0x19) >> 4,
    maxRange = emu:read8(d + 0x19) & 0xF,
    rank = emu:read8(d + 0x1C),     -- weapon rank needed (GetItemRequiredExp)
    eff = emu:read32(d + 0x10),     -- effectiveness class list, 0-terminated (or 0)
    uses = emu:read8(d + 0x14),     -- uses of a new item
    cost = emu:read16(d + 0x1A),    -- price per use (GetItemCost = uses * cost)
    effect = emu:read8(d + 0x1F),   -- weaponEffectId (WPN_EFFECT_*, include/bmitem.h; 3 = HP halve, Eclipse)
  }
end

-- struct Unit (include/bmunit.h)
function L.readUnit(u)
  local cp = emu:read32(u)
  if cp == 0 then return nil end
  local clp = emu:read32(u + 4)
  local unit = {
    addr = u,
    id = emu:read8(u + 0x0B),
    char = emu:read8(cp + 4),
    class = emu:read8(clp + 4),
    classPtr = clp,
    level = emu:read8(u + 8),
    state = emu:read32(u + 0x0C),
    x = L.s8(emu:read8(u + 0x10)), y = L.s8(emu:read8(u + 0x11)),
    maxHp = emu:read8(u + 0x12), hp = emu:read8(u + 0x13),
    pow = emu:read8(u + 0x14), skl = emu:read8(u + 0x15), spd = emu:read8(u + 0x16),
    def = emu:read8(u + 0x17), res = emu:read8(u + 0x18), lck = emu:read8(u + 0x19),
    mov = L.s8(emu:read8(clp + 0x12)) + L.s8(emu:read8(u + 0x1D)),
    con = L.s8(emu:read8(clp + 0x11)) + L.s8(emu:read8(cp + 0x13)) + L.s8(emu:read8(u + 0x1A)),
    items = {},
  }
  for k = 0, 4 do
    local it = emu:read16(u + 0x1E + 2 * k)
    if it ~= 0 then unit.items[#unit.items + 1] = it end
  end
  -- Equipped weapon: the first weapon in the inventory (simplified; rank checks skipped).
  for _, it in ipairs(unit.items) do
    local d = L.item(it & 0xFF)
    if (d.attributes & 1) ~= 0 and (d.attributes & 4) == 0 then unit.weapon = d; break end
  end
  return unit
end

local GONE = L.US.DEAD | L.US.NOT_DEPLOYED | L.US.BIT16 | L.US.RESCUED

function L.units(base, n)
  local list = {}
  for i = 0, n - 1 do
    local u = L.readUnit(base + i * 0x48)
    if u and (u.state & GONE) == 0 and u.hp > 0 then list[#list + 1] = u end
  end
  return list
end

function L.blue() return L.units(L.ADDR.gUnitArrayBlue, 62) end
function L.red() return L.units(L.ADDR.gUnitArrayRed, 50) end
function L.green() return L.units(L.ADDR.gUnitArrayGreen, 20) end

-- Chapters whose map milestones are reported (include/constants/chapters.h):
-- the story chapters and the Tower of Valni (0x00 to 0x2B), the Lagdou Ruins
-- (0x2E to 0x37), Melkaen Coast (0x39) and the two Chapter 11s (Eirika's 0x3D,
-- Ephraim's 0x3E). Maps that only scenes borrow (Castle Frelia 0x38, 0x3F,
-- 0x40, 0x44 and the rest) stay left out. Until 2026-10-21 this was
-- chapterIndex 0 to 40 (0x28), which left out both Chapter 11s, Tower 6 to 8
-- and the Ruins; every index the older routes visit gives the same lines.
function L.milestoneChapter(ch)
  return (ch >= 0 and ch <= 0x2B) or (ch >= 0x2E and ch <= 0x37) or ch == 0x39 or ch == 0x3D or ch == 0x3E
end

-- Milestones shared by the bot and the checker. Call once per logic tick,
-- after the tick is sampled. Returns a list of milestone strings for this tick.
function L.newMilestones()
  local m = { seen = {}, lastChapter = nil }
  function m.step(tick)
    local out = {}
    local ps = playStRaw()
    local procs = L.procs()
    local cb = emu:read32(L.ADDR.gMainCallback)
    local function once(key, text)
      if not m.seen[key] then
        m.seen[key] = tick
        local seeds, lcg = L.rng()
        out[#out + 1] = string.format("tick %d: %s (chapterIndex %d, turn %d, phase %02x, gRNSeeds %s, gLCGRNValue %s)",
          tick, text, ps.chapter, ps.turn, ps.faction, seeds, lcg)
      end
    end
    if ps.mode ~= 0 then once("newgame", "new game started (chapterModeIndex set)") end
    if ps.mode ~= 0 and L.findScript(procs, L.PROC.BMapMain) then
      local ch = ps.chapter
      -- A chapter counts as loaded only when its map appears with turn 0.
      -- Ending scenes that borrow another chapter's map (e.g. 27 after
      -- Chapter 3) keep the old turn number and are not reported.
      if L.milestoneChapter(ch) and ps.turn == 0 then
        once("load" .. ch, string.format("chapter %d map loaded", ch))
      end
      if L.milestoneChapter(ch) and m.seen["load" .. ch] then
        if ps.turn >= 1 then once("turn1_" .. ch, string.format("chapter %d turn 1 begins", ch)) end
        local pp = L.findScript(procs, L.PROC.PlayerPhase)
        if pp and pp.idle == L.IDLE.PlayerPhase_MainIdle and ps.faction == 0 then
          once("pp" .. ch, string.format("chapter %d first player phase (cursor free)", ch))
        end
        if ps.faction == 0x80 then once("ep" .. ch, string.format("chapter %d first enemy phase", ch)) end
        if cb == L.CB.InBattleMainRoutine or cb == L.CB.MainUpdate_8055C68 then
          once("battle" .. ch, string.format("chapter %d first battle", ch))
        end
      end
    end
    -- Game saves: watch the SaveBlockInfo entries of the three game slots in
    -- SRAM (struct SaveBlocks at 0x0E000000, saveBlockInfo[] at +0x64, 0x10
    -- each; include/bmsave.h). The suspend slot (entry 3) changes after every
    -- action and is not watched. A chapter's end save is written once the next
    -- chapter's index is already in gPlaySt, so it is keyed by that index.
    local sbi = emu:readRange(0x0E000064, 0x30)
    if m.lastSbi ~= nil and sbi ~= m.lastSbi then
      once("save_to" .. ps.chapter, ps.chapter == 0 and "new game save written to SRAM"
        or string.format("game save written to SRAM (end of chapter %d)", m.lastPlayedChapter or -1))
    end
    m.lastSbi = sbi
    if m.seen["turn1_" .. ps.chapter] then m.lastPlayedChapter = ps.chapter end
    if m.lastChapter ~= nil and ps.mode ~= 0 and ps.chapter ~= m.lastChapter then
      once("chg" .. m.lastChapter .. "_" .. ps.chapter,
        string.format("chapterIndex %d -> %d", m.lastChapter, ps.chapter))
    end
    m.lastChapter = ps.chapter
    return out
  end
  return m
end

-- Tick rules (port/test/README.md, "Early flush VBlank"), chosen with
-- ROUTE_TICK:
--   "frame" (the default; every route up to route_e10.keys was made with it):
--     a tick is a frame callback that sees gBmSt.main_loop_ended set.
--   "capture": gate0_capture.lua's and sram_capture.lua's own rule: the tick
--     is sampled at the main loop's VBlankIntrWait (breakpoint gate0_wait,
--     flag set) or, after an early flush VBlank, at the wait that follows it.
--     Needs MGBA_RUN_BP with the gate0_wait and gate0_onvblank breakpoints
--     (port/test/Makefile GBA_BP).
-- The two agree except around an early flush VBlank, where the frame rule can
-- count the tick a frame late or not at all; the key mask for the next OnMain
-- then comes late, and the capture plays the file differently (route_e13.keys
-- at tick 19,196, a battle animation: cov9 bot step 3). Routes from
-- route_e11.keys on are made with "capture".
function L.tickRule() return os.getenv("ROUTE_TICK") or "frame" end
-- Test cheats (port/test/README.md, "Test cheats"; README-routes.md, "Cheat-assisted routes"):
-- ROUTE_CHEAT=durability, funds, convoy or several joined with "+" runs those cheats at each tick
-- sample of the capture rule, as gate0_capture.lua does with GATE0_CHEAT (the rule: test_cheat.lua,
-- found next to this file), before the tool's own work for that tick, so its milestones and its
-- decision for the next tick see what the game's next tick reads. L.CHEAT is the name in the
-- cheats' order. A route made with it is replayed with GATE0_CHEAT=L.CHEAT on both sides (its
-- "# Generated" line names ROUTE_CHEAT). Needs ROUTE_TICK=capture. In Eirika's Chapter 20 and
-- final chapter it stops the tool, at the first tick sample there, before the tool's decision
-- (test_cheat.lua). The scripts that cannot run a cheat (menu_monkey.lua, movie_check.lua,
-- movie_presses.lua) refuse ROUTE_CHEAT at once.
L.CHEAT = os.getenv("ROUTE_CHEAT") or ""
if L.CHEAT ~= "" then
  L.cheat = dofile(((os.getenv("ROUTE_LIB") or "port/test/oracle/route_lib.lua"):match("^(.*/)") or "")
    .. "test_cheat.lua")
  L.CHEAT = L.cheat.check(L.CHEAT, "ROUTE_CHEAT")
end
-- The capture rule's own state (below). route_bot.lua's search (ROUTE_SEARCH,
-- "Tactics v6") saves it with a save state and puts it back with one.
local cap = { outerWait = false, earlyFlush = false }
function L.captureState() return { outerWait = cap.outerWait, earlyFlush = cap.earlyFlush } end
function L.setCaptureState(s) cap.outerWait, cap.earlyFlush = s.outerWait, s.earlyFlush end
function L.onTick(rule, onFrame, onSample)
  assert(not L.cheat or rule == "capture", "ROUTE_CHEAT needs ROUTE_TICK=capture (the sample point gate0_capture.lua "
    .. "refills at; port/test/README.md, \"Test cheats\")")
  if rule == "frame" then
    callbacks:add("frame", function()
      onFrame()
      if emu:read8(L.ADDR.gBmSt) == 0 then return end
      onSample()
    end)
    return
  end
  assert(rule == "capture", "ROUTE_TICK must be frame or capture, not " .. tostring(rule))
  local bp = os.getenv("MGBA_RUN_BP") or ""
  assert(bp:find("gate0_wait@", 1, true) and bp:find("gate0_onvblank@", 1, true),
    "ROUTE_TICK=capture needs MGBA_RUN_BP with the gate0_wait and gate0_onvblank breakpoints (port/test/Makefile GBA_BP)")
  callbacks:add("frame", onFrame)
  callbacks:add("gate0_onvblank", function()
    if emu:read8(L.ADDR.gBmSt) ~= 0 and not cap.outerWait then cap.earlyFlush = true end
    cap.outerWait = false
  end)
  if L.cheat then L.cheat.auto() end
  callbacks:add("gate0_wait", function()
    if emu:read8(L.ADDR.gBmSt) == 0 then
      if not cap.earlyFlush then return end        -- a wait inside OnMain, or boot
      cap.earlyFlush = false                       -- the main loop's wait after an early flush
    else
      -- gate0_capture.lua stops here (README.md, "Early flush VBlank"); so do the route tools.
      assert(not cap.earlyFlush, "an early flush VBlank, then a wait with main_loop_ended set (gate0_capture.lua cannot follow this)")
      cap.outerWait = true
    end
    if L.cheat then L.cheat.atSample() end         -- the test cheat (above), before the tool's decision
    onSample()
  end)
end

-- Key file helpers (format: port/test/README.md, "Input file").
function L.readKeys(path)
  local masks = {}
  for line in io.lines(path) do
    line = line:gsub("#.*", ""):match("^%s*(.-)%s*$")
    if line ~= "" then
      local mk, n = line:match("^(%x+)%*(%d+)$")
      if not mk then mk, n = line:match("^(%x+)$"), "1" end
      assert(mk, "bad key file entry: " .. line)
      for _ = 1, tonumber(n) do masks[#masks + 1] = tonumber(mk, 16) end
    end
  end
  return masks
end

function L.writeKeys(path, masks, header)
  local f = assert(io.open(path, "w"))
  f:write(header)
  local i = 1
  while i <= #masks do
    local j = i
    while j + 1 <= #masks and masks[j + 1] == masks[i] do j = j + 1 end
    local n = j - i + 1
    if n == 1 then f:write(string.format("%03x\n", masks[i]))
    else f:write(string.format("%03x*%d\n", masks[i], n)) end
    i = j + 1
  end
  f:close()
end

return L
