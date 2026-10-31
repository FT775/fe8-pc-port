-- Menu monkey (README.md, "Menu monkey"): a seeded, structured button
-- sequence that visits the screens the bot routes never open, written as an
-- ordinary Gate 0 key file. It runs fireemblem8.gba in mGBA like
-- route_bot.lua does (no breakpoints, the same tick and key timing), reads
-- game RAM to aim (the cursor on a unit, the unit menu's commands, the map
-- menu's entries, a target, an option row), and picks among the choices with
-- its own seeded random numbers, including timing: fast double taps, holds,
-- and presses during transitions. The GBA oracle is the referee: the key file
-- is replayed by the normal Gate 0 capture and the PC lag replay, and the
-- run does not have to make sense as play.
--
--   GATE0_SRAM_IN=~/fe8-chapsaves/ch2_start.sav MONKEY_OUT=build/gate0/monkey/x.keys \
--   MONKEY_SYMS=build/gate0/fireemblem8.nm MONKEY_SEED=1 MONKEY_TICKS=6000 \
--   MONKEY_PREFIX=port/test/inputs/resume.keys \
--     build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/menu_monkey.lua 3000000
--
-- (make -f port/test/Makefile monkey START=... SEED=... TICKS=... does this.)
--
-- Environment:
--   MONKEY_OUT          key file to write (required)
--   MONKEY_SYMS         `arm-none-eabi-nm fireemblem8.elf` output (required): procs,
--                       menus and RAM are found by name, not by hard-coded address
--   MONKEY_SEED         seed (default 1); same seed, start and ROM = same file
--   MONKEY_TICKS        ticks the monkey plays after the prefix (default 6000)
--   MONKEY_PREFIX       key file played open-loop first (resume.keys for a .sav start,
--                       a route for a power-on or snapshot start); empty = none
--   MONKEY_PREFIX_TICKS how many of its ticks (default: all of them)
--   MONKEY_START        label of the start for the header (the make target sets it)
--   MONKEY_COVERAGE     coverage JSON (default MONKEY_OUT .coverage.json)
--   MONKEY_TRACE        optional: per-tick digest in the Gate 0 trace's fields, for
--                       tools/monkey_check.py (the capture must give the same)
--   MONKEY_LIB          route_lib.lua (default port/test/oracle/route_lib.lua)
--   MONKEY_VERBOSE=1    log each decision
--   ROUTE_TICK          frame (default) or capture (needs MGBA_RUN_BP; see Determinism)
--   GATE0_SRAM_IN       read by the runner: boot with that save (a .sav start)
--
-- Kept off on purpose: the map menu's Suspend, Retreat, and the Arena
-- command. Suspend and the Arena were known stops on the PC when the monkey
-- was made (src/uiarena.c was a generated stub; built since P-24); they stay
-- off so the committed shard files keep their meaning. No soft-reset
-- combination (A+B+Start+Select) and no opposite d-pad pairs are ever pressed.
--
-- Determinism: decisions use only the seed, game RAM and their own state
-- (no time, no pairs() order). Tick semantics are route_bot.lua's: a tick is
-- sampled at the "frame" callback that ends with gBmSt.main_loop_ended set
-- (ROUTE_TICK=frame, the default) or at the Gate 0 capture's sample point
-- (ROUTE_TICK=capture, with MGBA_RUN_BP: route_lib.lua, "Tick rules"), and
-- the mask for tick t+1 is set right after tick t is sampled.

-- No test cheat here (port/test/README.md, "Test cheats"): its key files are replayed without one
-- (monkey-check), so ROUTE_CHEAT, which route_lib.lua would act on, stops the monkey at once.
assert((os.getenv("ROUTE_CHEAT") or "") == "", "menu_monkey.lua: ROUTE_CHEAT is not supported here: the menu "
  .. "monkey has no test cheat (port/test/README.md, \"Test cheats\")")
local L = dofile(os.getenv("MONKEY_LIB") or "port/test/oracle/route_lib.lua")
local K = L.KEY

local outPath = assert(os.getenv("MONKEY_OUT"), "MONKEY_OUT is required")
local symsPath = assert(os.getenv("MONKEY_SYMS"), "MONKEY_SYMS (arm-none-eabi-nm fireemblem8.elf output) is required")
local seed = tonumber(os.getenv("MONKEY_SEED") or "1")
local bodyTicks = tonumber(os.getenv("MONKEY_TICKS") or "6000")
local prefixPath = os.getenv("MONKEY_PREFIX") or ""
local prefixTicksEnv = tonumber(os.getenv("MONKEY_PREFIX_TICKS") or "")
local startLabel = os.getenv("MONKEY_START") or "power-on"
local covPath = os.getenv("MONKEY_COVERAGE") or (outPath:gsub("%.keys$", "") .. ".coverage.json")
local tracePath = os.getenv("MONKEY_TRACE") or ""
local verbose = (os.getenv("MONKEY_VERBOSE") or "0") ~= "0"
assert(seed and seed >= 0, "MONKEY_SEED must be a number >= 0")
assert(bodyTicks and bodyTicks >= 1, "MONKEY_TICKS must be >= 1")
-- MONKEY_WM=post (coverage 12, the post-game; README.md "Menu monkey", "The post-game"): on
-- the world map the monkey picks a node of its own (Tower of Valni, Lagdou Ruins, a node with
-- monsters, a node with shops and then its menu, the lord's node, any node) instead of the
-- node of gPlaySt.chapterIndex, and on the title menu it mostly takes Extras, Map, the slot
-- (the post-game's way back from a Game Over; Continue on a clear save plays the ending
-- again). Unset (the default), nothing changes: every older file comes out byte for byte.
local wmMode = os.getenv("MONKEY_WM") or ""
assert(wmMode == "" or wmMode == "post", "MONKEY_WM must be empty or post")

-- menu-monkey-1: the first version (commit 6606a839; inputs/monkey/ were made with it).
-- menu-monkey-2: attack positions by the unit's weapon range, End a little sooner, the
-- save menu's post-chapter proc in the screen list. (MONKEY_WM=post adds a header line.)
local VERSION = "menu-monkey-2"

---------------------------------------------------------------------------
-- Symbols (MONKEY_SYMS: "ADDR TYPE NAME" lines, as nm prints them). Thumb
-- functions are listed with bit 0 clear; proc idle callbacks hold bit 0 set.

local SYM, NAME = {}, {}
for line in io.lines(symsPath) do
  local a, n = line:match("^(%x+) %a ([%w_%.%$]+)$")
  if a then
    local v = tonumber(a, 16)
    if SYM[n] == nil then SYM[n] = v end
    if NAME[v] == nil then NAME[v] = n end
  end
end
local function sym(n) return assert(SYM[n], "symbol " .. n .. " is not in MONKEY_SYMS " .. symsPath) end
local function fn(n) return sym(n) | 1 end            -- a Thumb function as a proc idle holds it
local function nameOf(v) return NAME[v] or NAME[v & ~1] end

local A = {
  procArray   = sym("sProcArray"),
  bmSt        = sym("gBmSt"),
  playSt      = sym("gPlaySt"),
  activeUnit  = sym("gActiveUnit"),
  mainCb      = sym("gMainCallback"),
  genericBuf  = sym("gGenericBuffer"),
  bmMapUnit   = sym("gBmMapUnit"),
  bmMapTerr   = sym("gBmMapTerrain"),
  bmMapMove   = sym("gBmMapMovement"),
}
local IDLE = {
  menu        = fn("Menu_OnIdle"),
  menuFrozen  = fn("Menu_Frozen_OnLoop"),
  menuFrozenH = fn("Menu_FrozenHelpBox_OnLoop"),
  ppMain      = fn("PlayerPhase_MainIdle"),
  ppRange     = fn("PlayerPhase_RangeDisplayIdle"),
  wmLoop      = fn("WorldMap_Loop"),
  talkWait    = fn("TalkWaitForInput_OnIdle"),
  prepMenu    = fn("PrepMenu_CtrlLoop"),
  sallyMap    = fn("PrepScreenProc_MapIdle"),
  -- (MONKEY_WM=post) the title menu's main list and its Extras list (ProcScr_SaveMenu)
  titleMain   = fn("SameMenu_CtrlLoop"),
  titleExtra  = fn("SaveMenu_ExtraMenuLoop"),
}
local CB ={ battle1 = fn("MainUpdate_8055C68"), battle2 = fn("InBattleMainRoutine") }
-- gConfigUiState points into gGenericBuffer (uiconfig.c): selectedOptionIdx at +0x2A.
local CONFIG_SEL = A.genericBuf + 0x2A

-- Commands by MenuItemDef.overrideId (src/menu_def.c).
local UNIT_CMD = {
  [0x4E] = "Seize", [0x4F] = "Attack", [0x50] = "Attack (ballista)", [0x51] = "Staff", [0x52] = "Ride",
  [0x53] = "Exit", [0x54] = "Play", [0x55] = "Dance", [0x56] = "Steal", [0x57] = "Summon", [0x58] = "Summon (DK)",
  [0x59] = "Pick", [0x5A] = "Talk", [0x5B] = "Support", [0x5C] = "Visit", [0x5D] = "Chest", [0x5E] = "Door",
  [0x5F] = "Armory", [0x60] = "Vendor", [0x61] = "Secret shop", [0x62] = "Arena", [0x63] = "Rescue",
  [0x64] = "Drop", [0x65] = "Take", [0x66] = "Give", [0x67] = "Item", [0x68] = "Trade", [0x69] = "Supply",
  [0x6B] = "Wait",
}
local UNIT_CMD_W = {
  [0x4E] = 4, [0x4F] = 45, [0x50] = 20, [0x51] = 25, [0x52] = 6, [0x53] = 6, [0x54] = 15, [0x55] = 15,
  [0x56] = 15, [0x57] = 10, [0x58] = 10, [0x59] = 10, [0x5A] = 25, [0x5B] = 25, [0x5C] = 25, [0x5D] = 20,
  [0x5E] = 20, [0x5F] = 25, [0x60] = 25, [0x61] = 25, [0x62] = 0, [0x63] = 15, [0x64] = 20, [0x65] = 15,
  [0x66] = 15, [0x67] = 25, [0x68] = 20, [0x69] = 20, [0x6B] = 12,
}
local MAP_CMD = { [0x6E] = "Unit", [0x6F] = "Status", [0x74] = "Guide", [0x70] = "Records", [0x71] = "Options",
                  [0x72] = "Retreat", [0x73] = "Suspend", [0x78] = "End" }
local ITEMSUB_CMD = { [0x34] = "Use", [0x35] = "Equip", [0x36] = "Trade", [0x37] = "Discard" }

---------------------------------------------------------------------------
-- Seeded random numbers (xorshift32), integer only.

local rs = (seed * 2654435761 + 0x9E3779B9) & 0xFFFFFFFF
if rs == 0 then rs = 0x1234567 end
for _ = 1, 8 do rs = rs ~ ((rs << 13) & 0xFFFFFFFF); rs = rs ~ (rs >> 17); rs = rs ~ ((rs << 5) & 0xFFFFFFFF) end
local function rnd()
  rs = rs ~ ((rs << 13) & 0xFFFFFFFF)
  rs = rs ~ (rs >> 17)
  rs = rs ~ ((rs << 5) & 0xFFFFFFFF)
  return rs
end
local function rint(a, b) if b <= a then return a end return a + rnd() % (b - a + 1) end
local function pct(p) return rnd() % 100 < p end       -- p in percent, integer
-- Weighted pick: list of { value, weight } (integer weights); nil when all are 0.
local function wpick(list)
  local total = 0
  for _, e in ipairs(list) do total = total + e[2] end
  if total <= 0 then return nil end
  local r = rnd() % total
  for _, e in ipairs(list) do
    if r < e[2] then return e[1] end
    r = r - e[2]
  end
end
local function choose(arr) if #arr == 0 then return nil end return arr[rint(1, #arr)] end

---------------------------------------------------------------------------
-- Ticks and keys.

local prefix = {}
if prefixPath ~= "" then prefix = L.readKeys(prefixPath) end
local prefixTicks = prefixTicksEnv or #prefix
assert(prefixTicks <= #prefix or prefixPath == "", "MONKEY_PREFIX_TICKS is longer than the prefix key file")
local totalTicks = prefixTicks + bodyTicks

local masks = {}          -- masks[t + 1] is the mask for tick t
local queue = {}
local tick, frame = 0, 0  -- tick: ticks completed so far
local lastTickFrame = 0
-- Tick rule (route_lib.lua, "Tick rules"; ROUTE_TICK): "frame" (the default,
-- every file in inputs/monkey/ was made with it) or "capture", the Gate 0
-- capture's own rule, which also follows an early flush VBlank (cov9 H-B:
-- with the frame rule the monkey's ticks ran one frame behind the capture's
-- from ch5_start_s5's early flush at 9,306 on). It needs MGBA_RUN_BP
-- (make monkey MONKEY_TICK=capture sets it).
local tickRule = L.tickRule()
local sampleFrame = 0     -- the frame of the tick being sampled: the VBlank its wait waits for

local function log(fmt, ...)
  if verbose then print(string.format("[%d] " .. fmt, tick, ...)); io.stdout:flush() end
end

local function push(mask, hold, gap)
  for _ = 1, hold do queue[#queue + 1] = mask end
  for _ = 1, gap do queue[#queue + 1] = 0 end
end
local function wait(n) push(0, 0, n or 1) end

local DPAD = { K.UP, K.DOWN, K.LEFT, K.RIGHT }
local TRANSITION_KEYS = { K.A, K.B, K.START, K.R, K.L, K.UP, K.DOWN, K.LEFT, K.RIGHT }
local stats = { taps = 0, doubles = 0, holds = 0, transition = 0 }
local noStartNow = false  -- set while the preparations are open

-- One press with seeded timing: mostly a tap, sometimes a fast double tap or a
-- hold; now and then one more key right after it, while the screen changes.
local function press(mask, gapLo, gapHi, plain)
  gapLo, gapHi = gapLo or 2, gapHi or gapLo or 6
  local style = plain and 99 or rnd() % 100
  if style < 6 then
    push(mask, 1, 1); push(mask, 1, rint(gapLo, gapHi)); stats.doubles = stats.doubles + 1
  elseif style < 11 then
    push(mask, rint(3, 18), rint(gapLo, gapHi)); stats.holds = stats.holds + 1
  else
    push(mask, 1, rint(gapLo, gapHi)); stats.taps = stats.taps + 1
  end
  if not plain and pct(3) then
    local k = choose(TRANSITION_KEYS)
    if k == K.START and noStartNow then k = K.B end     -- Start in the preparations would begin the chapter
    push(k, 1, rint(0, 3)); stats.transition = stats.transition + 1
  end
end

---------------------------------------------------------------------------
-- Game state readers.

local function s8(v) return L.s8(v) end
local function playSt() return L.playSt() end

local function procScan()
  local list, first, active = {}, {}, {}
  for i = 0, 63 do
    local p = A.procArray + i * 0x6C
    local scr = emu:read32(p)
    if scr ~= 0 then
      local idle = emu:read32(p + 0x0C)
      if idle ~= 0 then idle = idle | 1 end        -- compared as Thumb addresses (IDLE, fn())
      local e = { addr = p, script = scr, idle = idle, lock = emu:read8(p + 0x28), name = nameOf(scr) }
      list[#list + 1] = e
      if e.name then
        first[e.name] = first[e.name] or e
        if e.lock == 0 then active[e.name] = active[e.name] or e end
      end
    end
  end
  return { list = list, first = first, active = active }
end

local function findIdle(P, idle, needActive)
  for _, e in ipairs(P.list) do
    if e.idle == idle and (not needActive or e.lock == 0) then return e end
  end
end

local function anyOf(P, names)
  for _, n in ipairs(names) do if P.first[n] then return P.first[n], n end end
end

local function activeMenu(P)
  for _, e in ipairs(P.list) do
    if e.idle == IDLE.menu and e.lock == 0 and (emu:read8(e.addr + 0x63) & 0x40) == 0 then return e end
  end
end

local function menuInfo(m)
  local def = emu:read32(m.addr + 0x30)
  local n = emu:read8(m.addr + 0x60)
  local cur = emu:read8(m.addr + 0x61)
  local items = {}
  for i = 0, n - 1 do
    local ip = emu:read32(m.addr + 0x34 + 4 * i)
    local idef = ip ~= 0 and emu:read32(ip + 0x30) or 0
    items[i] = { id = idef ~= 0 and emu:read8(idef + 9) or 0, avail = ip ~= 0 and emu:read8(ip + 0x3D) or 0 }
  end
  return def, n, cur, items, nameOf(def) or string.format("menudef_%08x", def)
end

local function mapGet(var, x, y) return L.mapGet(var, x, y) end
local function mapSize() return L.mapSize() end
local function cursor() return L.cursor() end

local function onMap(u)
  local w, h = mapSize()
  return u.x >= 0 and u.y >= 0 and u.x < w and u.y < h
end

local function activeUnit()
  local p = emu:read32(A.activeUnit)
  if p == 0 then return nil end
  return L.readUnit(p)
end

local function isLord(u) return u and (u.char == L.CHAR.EIRIKA or u.char == L.CHAR.EPHRAIM) end

local function dist(ax, ay, bx, by) return math.abs(ax - bx) + math.abs(ay - by) end

-- Enemy reach on (x, y), as route_bot.lua's estimate (Manhattan, terrain ignored).
local function threatAt(u, x, y, enemies)
  local t = 0
  for _, e in ipairs(enemies) do
    if e.weapon and dist(e.x, e.y, x, y) <= e.mov + e.weapon.maxRange then
      t = t + math.max(1, e.pow + e.weapon.might - u.def)
    end
  end
  return t
end

---------------------------------------------------------------------------
-- Coverage (written as JSON at the end; every list sorted, so it is
-- deterministic too).

local cov = { procs = {}, menus = {}, menuFirst = {}, commands = {}, targets = {}, activities = {}, states = {} }
local function covCount(tab, key) tab[key] = (tab[key] or 0) + 1 end
local procsAtStart = nil   -- procs already there when the monkey starts (not counted as "opened")

local function covProcs(P)
  local seen = {}
  for _, e in ipairs(P.list) do
    local n = e.name or string.format("proc_%08x", e.script)
    if not seen[n] then
      seen[n] = true
      local c = cov.procs[n]
      if not c then c = { first = tick, ticks = 0 }; cov.procs[n] = c end
      c.ticks = c.ticks + 1
    end
  end
end

---------------------------------------------------------------------------
-- Cursor aiming on the battle map (route_bot.lua's way: one press per tile,
-- after the cursor sprite has caught up).

local function cursorTo(tx, ty)
  local cx, cy, dx, dy = cursor()
  if dx ~= cx * 16 or dy ~= cy * 16 then wait(1); return false end
  if cx < tx then push(K.RIGHT, 1, 1); return false end
  if cx > tx then push(K.LEFT, 1, 1); return false end
  if cy < ty then push(K.DOWN, 1, 1); return false end
  if cy > ty then push(K.UP, 1, 1); return false end
  return true
end

local function emptyTileNear(x0, y0)
  local w, h = mapSize()
  if x0 >= 0 and y0 >= 0 and x0 < w and y0 < h and mapGet(A.bmMapUnit, x0, y0) == 0 then return x0, y0 end
  local best, bx, by
  for y = 0, h - 1 do
    for x = 0, w - 1 do
      if mapGet(A.bmMapUnit, x, y) == 0 then
        local d = dist(x, y, x0, y0)
        if best == nil or d < best then best, bx, by = d, x, y end
      end
    end
  end
  return bx or 0, by or 0
end

-- Tiles the active unit can end its move on (route_bot.lua's reachable()).
local function reachable(u)
  local w, h = mapSize()
  local list = {}
  for y = 0, h - 1 do
    for x = 0, w - 1 do
      if mapGet(A.bmMapMove, x, y) < 120 then
        local occ = mapGet(A.bmMapUnit, x, y)
        if occ == 0 or (x == u.x and y == u.y) then list[#list + 1] = { x = x, y = y } end
      end
    end
  end
  return list
end

local SPECIAL_TERRAIN = { [0x03] = true, [0x05] = true, [0x06] = true, [0x07] = true, [0x1F] = true,
                          [0x21] = true, [0x24] = true, [0x38] = true }

---------------------------------------------------------------------------
-- Per-turn bookkeeping.

local turnKey, turnActs, wantEnd = nil, 0, false
local optionsSwept = false
local unitSteps = 0       -- menu/target decisions since the last unit was picked
local defVisits = {}      -- menu def / target kind -> times opened since then
local function newTurnCheck()
  local ps = playSt()
  local k = ps.chapter .. ":" .. ps.turn .. ":" .. ps.faction
  if k ~= turnKey then turnKey, turnActs, wantEnd = k, 0, false end
end

---------------------------------------------------------------------------
-- Policies. Each queues at least one tick.

local act = nil           -- free-cursor activity
local rPlan = nil         -- move-range plan
local menuPlan = nil
local tPlan = nil
local screen = nil        -- { name, budget, n }
local prepBudget = nil    -- ticks of preparations left to explore
local wmBudget = nil      -- world-map exploration decisions left
local wmPlan = nil        -- (MONKEY_WM=post) the world-map node the monkey is going for
local titlePlan = nil     -- (MONKEY_WM=post) the title menu: Extras, Map (true) or the usual presses

local function screenStep(name, lo, hi)
  if not screen or screen.name ~= name then
    screen = { name = name, budget = rint(lo, hi), n = 0 }
    log("screen %s, budget %d", name, screen.budget)
  end
  screen.n = screen.n + 1
  return screen.n > screen.budget, screen
end

local function freeActivity()
  local blue, others = {}, {}
  for _, u in ipairs(L.blue()) do
    if onMap(u) and (u.state & (L.US.UNSELECTABLE | L.US.HIDDEN)) == 0 then blue[#blue + 1] = u end
  end
  local all = {}
  for _, u in ipairs(L.blue()) do if onMap(u) and (u.state & L.US.HIDDEN) == 0 then all[#all + 1] = u end end
  for _, u in ipairs(L.red()) do if onMap(u) and (u.state & L.US.HIDDEN) == 0 then others[#others + 1] = u; all[#all + 1] = u end end
  for _, u in ipairs(L.green()) do if onMap(u) and (u.state & L.US.HIDDEN) == 0 then others[#others + 1] = u; all[#all + 1] = u end end
  local endW = math.min(45, 1 + 3 * turnActs)
  local kind = wpick({
    { "unit", #blue > 0 and 36 or 0 }, { "enemy", #others > 0 and 8 or 0 }, { "stat", #all > 0 and 9 or 0 },
    { "mapmenu", 24 }, { "minimap", 6 }, { "lswitch", 4 }, { "wander", 8 }, { "end", endW },
  })
  local a = { kind = kind, steps = 0 }
  local cx, cy = cursor()
  if kind == "unit" then
    local u = choose(blue); a.tx, a.ty, a.key = u.x, u.y, K.A
  elseif kind == "enemy" then
    local u = choose(others); a.tx, a.ty, a.key = u.x, u.y, K.A
  elseif kind == "stat" then
    local u = choose(all); a.tx, a.ty, a.key = u.x, u.y, K.R
  elseif kind == "mapmenu" or kind == "end" then
    local w, h = mapSize()
    local x0, y0 = cx, cy
    if pct(40) then x0, y0 = rint(0, w - 1), rint(0, h - 1) end
    a.tx, a.ty = emptyTileNear(x0, y0); a.key = K.A
  end
  covCount(cov.activities, kind)
  log("activity %s%s", kind, a.tx and string.format(" at (%d,%d)", a.tx, a.ty) or "")
  return a
end

local function wanderStep()
  local dir = choose(DPAD)
  local r = rnd() % 100
  if r < 35 then push(dir, 1, rint(1, 4))
  elseif r < 55 then push(dir, rint(8, 30), rint(1, 4))                 -- hold: key repeat
  elseif r < 70 then push(K.B | dir, rint(4, 20), rint(1, 4))           -- B held: fast cursor
  elseif r < 80 then push(dir, 1, 1); push(dir, 1, rint(1, 4))          -- fast double tap
  elseif r < 88 then push(K.B, 1, rint(2, 6))
  else wait(rint(4, 20)) end
end

local function freePolicy()
  newTurnCheck()
  rPlan, menuPlan, tPlan, screen = nil, nil, nil, nil
  if act == nil or act.done then act = freeActivity(); turnActs = turnActs + 1; unitSteps = 0; defVisits = {} end
  act.steps = act.steps + 1
  if act.steps > 400 then act.done = true; wait(1); return end
  local k = act.kind
  if k == "unit" or k == "enemy" or k == "stat" or k == "mapmenu" or k == "end" then
    if not cursorTo(act.tx, act.ty) then return end
    if k == "end" then wantEnd = true end
    press(act.key, 3, 8, true)
    act.done = true
  elseif k == "minimap" then
    press(K.START, 4, 10, true); act.done = true
  elseif k == "lswitch" then
    for _ = 1, rint(1, 3) do push(K.L, 1, rint(1, 8)) end
    act.done = true
  elseif k == "wander" then
    act.n = act.n or rint(1, 5)
    wanderStep()
    act.n = act.n - 1
    if act.n <= 0 then act.done = true end
  else
    wait(1); act.done = true
  end
end

local function chooseDest(u)
  local tiles = reachable(u)
  if #tiles == 0 then return { x = u.x, y = u.y } end
  local reds, blues = L.red(), L.blue()
  local near, ally, special = {}, {}, {}
  -- Attack positions: the unit's weapon range from a red unit (1 without a weapon).
  local rmin, rmax = 1, 1
  if u.weapon then rmin, rmax = math.max(1, u.weapon.minRange), math.max(1, u.weapon.maxRange) end
  for _, t in ipairs(tiles) do
    for _, e in ipairs(reds) do
      local d = dist(t.x, t.y, e.x, e.y)
      if d >= rmin and d <= rmax then near[#near + 1] = t; break end
    end
    for _, b in ipairs(blues) do
      if b.id ~= u.id and dist(t.x, t.y, b.x, b.y) == 1 then ally[#ally + 1] = t; break end
    end
    local terr = mapGet(A.bmMapTerr, t.x, t.y)
    if SPECIAL_TERRAIN[terr] then special[#special + 1] = t end
    local w, h = mapSize()
    for _, dv in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
      local nx, ny = t.x + dv[1], t.y + dv[2]
      if nx >= 0 and ny >= 0 and nx < w and ny < h and mapGet(A.bmMapTerr, nx, ny) == 0x1E then
        special[#special + 1] = t; break
      end
    end
  end
  local lord = isLord(u)
  -- Toward the enemy: the reachable tile nearest to any red unit (so later turns have fights).
  local toward, tbest = nil, nil
  for _, t in ipairs(tiles) do
    for _, e in ipairs(reds) do
      local d = dist(t.x, t.y, e.x, e.y)
      if d >= 1 and (tbest == nil or d < tbest) then tbest, toward = d, t end
    end
  end
  local kind = wpick({ { "stay", 10 }, { "special", #special > 0 and 25 or 0 }, { "enemy", #near > 0 and (lord and 4 or 30) or 0 },
                       { "toward", (toward and #near == 0 and not lord) and 25 or 0 },
                       { "ally", #ally > 0 and 20 or 0 }, { "any", 15 } })
  local t
  if kind == "stay" then t = { x = u.x, y = u.y }
  elseif kind == "special" then t = choose(special)
  elseif kind == "enemy" then t = choose(near)
  elseif kind == "toward" then t = toward
  elseif kind == "ally" then t = choose(ally)
  else t = choose(tiles) end
  if lord and pct(70) then
    -- Keep the lord alive more often than not: the safest of a few candidates.
    local best, bestT = nil, t
    for _ = 1, 6 do
      local c = choose(tiles)
      local th = threatAt(u, c.x, c.y, reds)
      if best == nil or th < best then best, bestT = th, c end
    end
    if threatAt(u, t.x, t.y, reds) > best then t = bestT end
  end
  return t, kind
end

local function rangePolicy(P)
  local u = activeUnit()
  if not u then press(K.B, 4, 8, true); return end
  local moved = (u.state & L.US.HAS_MOVED) ~= 0
  local key = u.id .. ":" .. (turnKey or "") .. ":" .. tostring(moved)
  if not rPlan or rPlan.key ~= key then
    rPlan = { key = key, steps = 0, aTries = 0 }
    if u.id >= 0x40 then
      rPlan.mode = "look"; rPlan.wait = rint(10, 90)
    else
      local r = rnd() % 100
      if r < 6 then rPlan.mode = "cancel"
      elseif r < 10 then rPlan.mode = "info"
      elseif r < 13 then rPlan.mode = "reset"
      else rPlan.mode = "move" end
      rPlan.dest, rPlan.destKind = chooseDest(u)
      if moved then rPlan.mode = "move" end
    end
    log("range: unit %02x mode %s%s", u.id, rPlan.mode,
      rPlan.dest and string.format(" dest (%d,%d) %s", rPlan.dest.x, rPlan.dest.y, tostring(rPlan.destKind)) or "")
  end
  rPlan.steps = rPlan.steps + 1
  if rPlan.steps > 300 then press(K.B, 4, 8, true); rPlan = nil; return end
  if rPlan.mode == "look" then
    if rPlan.wait > 0 then local n = math.min(rPlan.wait, 8); rPlan.wait = rPlan.wait - n; wait(n); return end
    if pct(15) then press(K.A, 6, 12) else press(K.B, 6, 12) end
    return
  elseif rPlan.mode == "cancel" then
    press(K.B, 6, 12); rPlan.mode = "move"; return
  elseif rPlan.mode == "info" then
    press(K.R, 6, 12); rPlan.mode = "move"; return
  elseif rPlan.mode == "reset" then
    press(K.L, 4, 8); rPlan.mode = "move"; return
  end
  if cursorTo(rPlan.dest.x, rPlan.dest.y) then
    rPlan.aTries = rPlan.aTries + 1
    if rPlan.aTries > 3 then rPlan.dest = { x = u.x, y = u.y }; rPlan.aTries = 0 end
    press(K.A, 6, 10, true)
  end
end

local function menuPolicy(m)
  local def, n, cur, items, dname = menuInfo(m)
  local key = m.addr .. ":" .. def
  if n == 0 then press(K.B, 4, 8, true); return end
  unitSteps = unitSteps + 1
  if not menuPlan or menuPlan.key ~= key then
    covCount(cov.menus, dname)
    cov.menuFirst[dname] = cov.menuFirst[dname] or tick
    local visits = defVisits[dname] or 0                    -- this menu again in the same unit's action
    defVisits[dname] = visits + 1
    local cands = {}
    for i = 0, n - 1 do
      local it, w = items[i], 10
      if dname == "gUnitActionMenuDef" then
        w = UNIT_CMD_W[it.id] or 5
        if it.id == 0x6B then w = w + 15 * visits end
        if it.id == 0x6B and unitSteps > 30 then w = 1000 end
      elseif dname == "gMapMenuDef" then
        if it.id == 0x72 or it.id == 0x73 then w = 0
        elseif it.id == 0x78 then w = wantEnd and 1000 or math.min(15, 1 + turnActs)
        elseif it.id == 0x6E then w = 20 elseif it.id == 0x6F then w = 12 elseif it.id == 0x74 then w = 14
        elseif it.id == 0x71 then w = 16 elseif it.id == 0x70 then w = 5 end
      elseif dname == "gItemSubMenuDef" then
        w = ({ [0x34] = 15, [0x35] = 25, [0x36] = 15, [0x37] = 4 })[it.id] or 10
      elseif dname == "gYesNoSelectionMenuDef" then
        w = it.id == 0x32 and 40 or 60
      end
      if it.avail == 2 and w > 0 then w = math.min(w, 3) end     -- greyed: a few, for the frozen help box
      cands[#cands + 1] = { i, w }
    end
    local cancelP = (dname == "gUnitActionMenuDef") and 7 or (dname == "gMapMenuDef" and (wantEnd and 0 or 9)
      or math.min(80, 12 + 18 * visits))
    if dname == "gUnitActionMenuDef" and unitSteps > 30 then cancelP = 0 end
    local idx = wpick(cands)
    menuPlan = { key = key, idx = idx, cancel = (idx == nil) or pct(cancelP), help = pct(10), after = 0, name = dname }
    log("menu %s: %d items, cur %d, plan %s%s%s", dname, n, cur, tostring(idx), menuPlan.cancel and " cancel" or "",
      menuPlan.help and " help" or "")
  end
  local mp = menuPlan
  if mp.cancel then press(K.B, 4, 10); mp.after = mp.after + 1; if mp.after > 3 then menuPlan = nil end; return end
  if mp.help then mp.help = false; press(K.R, 10, 40, true); return end
  if cur ~= mp.idx then
    local down = (mp.idx - cur) % n
    local up = (cur - mp.idx) % n
    press(down <= up and K.DOWN or K.UP, 2, 5)
    return
  end
  local it = items[cur]
  local label
  if mp.name == "gUnitActionMenuDef" then label = "unit menu: " .. (UNIT_CMD[it.id] or string.format("0x%02x", it.id))
  elseif mp.name == "gMapMenuDef" then label = "map menu: " .. (MAP_CMD[it.id] or string.format("0x%02x", it.id))
  elseif mp.name == "gItemSubMenuDef" then label = "item submenu: " .. (ITEMSUB_CMD[it.id] or string.format("0x%02x", it.id))
  else label = mp.name .. string.format(": item %d", cur) end
  if mp.after == 0 then covCount(cov.commands, label); log("select %s", label) end
  if mp.name == "gMapMenuDef" and it.id == 0x78 then wantEnd = false end
  press(K.A, 6, 12, true)
  mp.after = mp.after + 1
  if mp.after > 2 then menuPlan = nil end
end

local function targetPolicy(ts)
  local info = emu:read32(ts.addr + 0x2C)
  local iname = nameOf(info) or string.format("selectinfo_%08x", info)
  local key = ts.addr .. ":" .. info
  unitSteps = unitSteps + 1
  if not tPlan or tPlan.key ~= key then
    covCount(cov.targets, iname)
    defVisits[iname] = (defVisits[iname] or 0) + 1
    tPlan = { key = key, cycles = rint(0, 3), help = pct(20), steps = 0, name = iname, visits = defVisits[iname] - 1 }
    log("target %s: %d cycles%s", iname, tPlan.cycles, tPlan.help and ", help" or "")
  end
  tPlan.steps = tPlan.steps + 1
  if tPlan.steps > 25 then press(K.B, 6, 10, true); return end
  if tPlan.cycles > 0 then tPlan.cycles = tPlan.cycles - 1; press(choose(DPAD), 4, 10); return end
  if tPlan.help then tPlan.help = false; press(K.R, 20, 60, true); return end
  local pA = 65
  if iname == "gSelectInfo_Attack" then pA = isLord(activeUnit()) and 10 or math.min(85, 40 + 15 * tPlan.visits) end
  if unitSteps > 40 then pA = 0 end
  if pct(pA) then press(K.A, 8, 12, true) else press(K.B, 8, 12, true) end
end

-- Options screen: on the first visit, every option changed and changed back
-- (Right, or Left at the last value), then the battle animation option is
-- sometimes left changed so later battles run on the map instead.
local function optionsPolicy(P)
  local over, sc = screenStep("options", optionsSwept and 30 or 140, optionsSwept and 70 or 220)
  local cp = P.active["ProcScr_Config_Field"] or P.active["ProcScr_Config_PrepMapMenu"] or P.active["ProcScr_Config_WorldMap"]
  if not cp then wait(2); return end
  if emu:read16(cp.addr + 0x30) ~= 0 then wait(1); return end       -- rows still scrolling
  local sel = L.s16(emu:read16(CONFIG_SEL))
  if not sc.plan then
    sc.plan = {}
    if not optionsSwept then
      for r = 0, 12 do sc.plan[#sc.plan + 1] = { row = r, swap = true } end
      optionsSwept = true
    else
      for _ = 1, rint(1, 3) do sc.plan[#sc.plan + 1] = { row = rint(0, 12), swap = pct(50) } end
    end
    if pct(50) then sc.plan[#sc.plan + 1] = { row = 0, swap = false } end   -- battle animations: leave it changed
    sc.i = 1
  end
  local step = sc.plan[sc.i]
  if over or step == nil then press(K.B, 10, 20, true); return end
  if sel ~= step.row then press(sel < step.row and K.DOWN or K.UP, 2, 4, true); return end
  if not step.before then
    step.before = emu:readRange(A.playSt, 0x4C)
    step.dir = K.RIGHT
    if pct(15) then press(K.R, 20, 40, true); return end                -- the row's help
    press(step.dir, 6, 10, true)
    return
  end
  if not step.checked then
    step.checked = true
    if emu:readRange(A.playSt, 0x4C) == step.before then step.dir = K.LEFT; press(K.LEFT, 6, 10, true); return end
  end
  if step.swap and not step.back then
    step.back = true
    press(step.dir == K.RIGHT and K.LEFT or K.RIGHT, 6, 10, true)
    return
  end
  sc.i = sc.i + 1
  wait(rint(2, 8))
end

local function genericScreen(name, keys, lo, hi, exitKey)
  local over = screenStep(name, lo, hi)
  if over then press(exitKey or K.B, 8, 16, true); return end
  local k = wpick(keys)
  if k == 0 then wait(rint(4, 20)) else press(k, 4, 14) end
end

local function eventPolicy(ev, P)
  local bits = emu:read16(ev.addr + 0x3C)
  if (bits & 0x04) ~= 0 then wait(1); return end
  if P.first["gProcScr_TalkChoice"] then
    if pct(60) then press(pct(50) and K.LEFT or K.RIGHT, 4, 8) else press(K.A, 6, 10) end
    return
  end
  if (bits & 0x02) ~= 0 and (bits & 0x10) == 0 then
    local r = rnd() % 100
    if r < 70 then press(K.START, 4, 8, true) elseif r < 92 then press(K.A, 4, 8) else press(K.B, 4, 8) end
    return
  end
  if pct(90) then press(K.A, 3, 8) else press(K.B, 3, 8) end
end

local function prepPolicy(P)
  if prepBudget == nil then prepBudget = rint(900, 2400); log("preparations: budget %d ticks", prepBudget) end
  if os.getenv("MONKEY_VERBOSE") == "2" then
    local names = {}
    for _, e in ipairs(P.list) do
      names[#names + 1] = (e.name or string.format("%08x", e.script)) .. (e.lock > 0 and "*" or "") .. "/" ..
        (nameOf(e.idle) or string.format("%08x", e.idle))
    end
    log("prep procs: %s", table.concat(names, " "))
  end
  local pm = findIdle(P, IDLE.prepMenu, true)
  local leaving = prepBudget <= 0
  if leaving then
    if pm and emu:read32(pm.addr + 0x5C) ~= 0 then press(K.START, 20, 30, true); return end
    if findIdle(P, IDLE.sallyMap, true) then press(K.B, 10, 16, true); return end
    press(K.B, 8, 14, true)
    return
  end
  if pm then
    local cur, max = emu:read8(pm.addr + 0x2A), emu:read8(pm.addr + 0x2B)
    if emu:read8(pm.addr + 0x29) ~= 0 then press(pct(50) and K.B or K.R, 8, 16, true); return end   -- help open
    if not screen or screen.name ~= "prepmenu:" .. pm.addr then
      screen = { name = "prepmenu:" .. pm.addr, target = max > 0 and rint(0, max - 1) or 0, n = 0, help = pct(15) }
    end
    screen.n = screen.n + 1
    if screen.n > 30 then screen = nil; press(K.B, 8, 12, true); return end
    if cur ~= screen.target then press(cur < screen.target and K.DOWN or K.UP, 3, 6); return end
    if screen.help then screen.help = false; press(K.R, 20, 50, true); return end
    if pct(12) then press(K.B, 8, 14, true) else press(K.A, 10, 16, true) end
    screen = nil
    return
  end
  if findIdle(P, IDLE.sallyMap, true) then
    genericScreen("checkmap", { { K.UP, 10 }, { K.DOWN, 10 }, { K.LEFT, 10 }, { K.RIGHT, 10 }, { K.A, 10 },
                                { K.R, 6 }, { K.L, 4 }, { K.START, 3 }, { 0, 6 } }, 10, 40)
    return
  end
  genericScreen("prepsub", { { K.UP, 12 }, { K.DOWN, 12 }, { K.LEFT, 8 }, { K.RIGHT, 8 }, { K.A, 14 },
                             { K.R, 6 }, { K.L, 3 }, { K.SELECT, 2 }, { K.B, 6 }, { 0, 6 } }, 8, 30)
end

local function worldMapPolicy(P, wm)
  if wmBudget == nil then wmBudget = rint(4, 20); log("world map: %d exploring steps", wmBudget) end
  if wmBudget > 0 then
    wmBudget = wmBudget - 1
    local k = wpick({ { K.L, 25 }, { K.UP, 10 }, { K.DOWN, 10 }, { K.LEFT, 10 }, { K.RIGHT, 10 }, { K.SELECT, 6 },
                      { K.A, 10 }, { K.B, 5 }, { K.R, 4 }, { 0, 10 } })
    if k == 0 then wait(rint(10, 40)) elseif k == K.UP or k == K.DOWN or k == K.LEFT or k == K.RIGHT then
      push(k, rint(1, 30), rint(4, 12))
    else press(k, 12, 24) end
    return
  end
  if wmMode == "post" then
    -- (MONKEY_WM=post) a node of the monkey's own: the Tower of Valni (node 0x1A) or the
    -- Lagdou Ruins (0x1B), a node with monsters (gGMData.units[4..6], GetGmSkirmishUnitAtNode),
    -- a node with an armory, vendor or secret shop (struct GMapNodeData +0x0C/+0x10/+0x14;
    -- A walks the lord there, A again opens its menu), the lord's own node (units[0]: its menu)
    -- or any node on the map. L moves the cursor to the next node, as route_bot.lua does.
    local d = L.GM.data
    local cx, cy = emu:read32(d + 8) >> 8, emu:read32(d + 12) >> 8
    local lord = emu:read8(d + 0x11)
    local cur, best
    local vis, dung, mon, shop = {}, {}, {}, {}
    for i = 0, L.GM.nodeCount - 1 do
      if (emu:read8(d + 0x30 + 4 * i) & 1) ~= 0 then
        local nd = L.GM.nodeData + i * 0x20
        local dd = math.abs(L.s16(emu:read16(nd + 0x18)) - cx) + math.abs(L.s16(emu:read16(nd + 0x1A)) - cy)
        if best == nil or dd < best then best, cur = dd, i end
        vis[#vis + 1] = i
        if i == 0x1A or i == 0x1B then dung[#dung + 1] = i end
        if emu:read32(nd + 0x0C) ~= 0 or emu:read32(nd + 0x10) ~= 0 or emu:read32(nd + 0x14) ~= 0 then shop[#shop + 1] = i end
        for k = 4, 6 do
          if emu:read16(d + 0x12 + 4 * k) ~= 0 and emu:read8(d + 0x11 + 4 * k) == i then mon[#mon + 1] = i; break end
        end
      end
    end
    if not wmPlan then
      local kind = wpick({ { "dungeon", #dung > 0 and 30 or 0 }, { "monster", #mon > 0 and 25 or 0 },
                           { "shop", #shop > 0 and 25 or 0 }, { "here", 10 }, { "any", #vis > 0 and 10 or 0 } })
      local node = lord
      if kind == "dungeon" then node = choose(dung) elseif kind == "monster" then node = choose(mon)
      elseif kind == "shop" then node = choose(shop) elseif kind == "any" then node = choose(vis) end
      wmPlan = { kind = kind, node = node, ls = 0, a = 0, want = (kind == "shop" and node ~= lord) and 2 or 1 }
      covCount(cov.activities, "wm " .. kind)
      log("world map: %s, node %d (lord at %d; %d nodes, dungeons %d, monsters %d, shops %d)", kind, node, lord,
        #vis, #dung, #mon, #shop)
    end
    local pl = wmPlan
    if pl.a >= pl.want or pl.ls > 30 then
      log("world map: %s plan for node %d done (%d A, %d L)", pl.kind, pl.node, pl.a, pl.ls)
      wmPlan = nil; wmBudget = rint(2, 10); wait(rint(2, 10))
      return
    end
    if cur == pl.node and best <= 16 then
      if pl.a == 1 and lord ~= pl.node then pl.a = pl.want; wait(rint(4, 12)); return end   -- the walk stopped short
      pl.a = pl.a + 1
      press(K.A, 20, 40, true)
    else
      pl.ls = pl.ls + 1
      press(K.L, 20, 20, true)
    end
    return
  end
  -- Then as route_bot.lua: the node of gPlaySt.chapterIndex, A there.
  local ps = playSt()
  local d = L.GM.data
  local cx, cy = emu:read32(d + 8) >> 8, emu:read32(d + 12) >> 8
  local target, cur, best
  for i = 0, L.GM.nodeCount - 1 do
    local st = emu:read8(d + 0x30 + 4 * i)
    if (st & 1) ~= 0 then
      local nd = L.GM.nodeData + i * 0x20
      if emu:read8(nd + 4) == ps.chapter and target == nil then target = i end
      local dd = math.abs(L.s16(emu:read16(nd + 0x18)) - cx) + math.abs(L.s16(emu:read16(nd + 0x1A)) - cy)
      if best == nil or dd < best then best, cur = dd, i end
    end
  end
  if target ~= nil and cur == target and best <= 16 then press(K.A, 20, 20, true) else press(K.L, 20, 20, true) end
end

-- Which screen is in front: the first rule that matches, in this order.
local function classify(P)
  local ps = playSt()
  local function is(n) return P.first[n] ~= nil end
  local menu = activeMenu(P)
  local pp = P.first["gProcScr_PlayerPhase"]
  local cb = emu:read32(A.mainCb)
  -- Battle preparations: the AtMenu, or its Check Map mode (PLAY_FLAG_PREPSCREEN in
  -- chapterStateBits); the chapter's event engine waits under them.
  local inPrep = is("ProcScr_AtMenu") or ((ps.stateBits & 0x10) ~= 0 and is("gProcScr_SALLYCURSOR"))
  local ev = P.first["ProcScr_StdEventEngine"]

  if is("ProcScr_GameOverScreen") or is("ProcScr_BmGameOver") then return "gameover" end
  if is("ProcScr_NewGameDifficultySelect") then return "difficulty" end
  if ev and not menu and not inPrep then return "event", ev end
  if is("ProcScr_BattleEventEngine") or findIdle(P, IDLE.talkWait) then return "talk" end
  if findIdle(P, IDLE.menuFrozen, true) or findIdle(P, IDLE.menuFrozenH, true) then return "menu-frozen" end
  if is("gProcScr_HelpBox") then return "helpbox" end
  if is("gProcScr_StatScreen") then return "statscreen" end
  if is("ProcScr_UnitListScreen_Field") or is("ProcScr_UnitListScreen_WorldMap") or is("ProcScr_UnitListScreen_PrepMenu") then
    return "unitlist"
  end
  if is("ProcScr_TradeMenu") then return "trade" end
  if is("ProcScr_BmSupplyScreen") then return "supply" end
  if is("gProcScr_Shop") then return "shop" end
  if is("ProcScr_E_Guide1") or is("ProcScr_E_Guide2") then return "guide" end
  if is("ProcScr_Config_Field") or is("ProcScr_Config_PrepMapMenu") or is("ProcScr_Config_WorldMap") then return "options" end
  if is("gProcScr_ChapterStatusScreen") or is("ProcScr_ChapterStatusScreen_FromPrep") or is("ProcScr_WorldmapStatusUi") then
    return "chapstatus"
  end
  if is("gProcScr_Minimap") or is("ProcScr_WorldmapMinimapWrapper") then return "minimap" end
  if is("ProcScr_SaveMenu") or is("gProcScr_SaveMenuPostChapter") then return "savemenu" end
  if is("gProcScr_SupportScreen") then return "supportscreen" end
  if menu then return "menu", menu end
  if P.first["gProcScr_TargetSelection"] then return "target", P.first["gProcScr_TargetSelection"] end
  if inPrep then return "prep" end
  if pp and pp.idle == IDLE.ppMain then return "free" end
  if P.first["ProcScr_SaveMenu"] == nil and findIdle(P, IDLE.prepMenu, true) then return "prep" end
  if pp and pp.idle == IDLE.ppRange then return "range" end
  if cb == CB.battle1 or cb == CB.battle2 or is("gProc_ekrBattle") then return "battleanim" end
  if pp then return "pp-busy" end
  if P.first["gProc_BMapMain"] and ps.faction ~= 0 then return "otherphase" end
  if findIdle(P, IDLE.wmLoop, true) then return "worldmap" end
  if is("ProcScr_GmapBaseMenu") then return "wmbase" end
  return "generic"
end

local idleRun = 0
local lastKind = nil
local function decide()
  local P = procScan()
  local kind, obj = classify(P)

  -- Plans belong to one screen: leaving it drops them.
  local inPrep = P.first["ProcScr_AtMenu"] ~= nil or ((playSt().stateBits & 0x10) ~= 0 and P.first["gProcScr_SALLYCURSOR"] ~= nil)
  if kind ~= lastKind then screen = nil end
  if kind ~= "menu" then menuPlan = nil end
  if kind ~= "target" then tPlan = nil end
  if not inPrep then prepBudget = nil end
  if not P.first["ProcScr_WorldMapMain"] then wmBudget = nil; wmPlan = nil end
  if kind ~= "savemenu" then titlePlan = nil end
  noStartNow = inPrep

  if kind == "gameover" then press(pct(70) and K.A or K.START, 20, 40)
  elseif kind == "difficulty" then press(K.A, 10, 20, true)
  elseif kind == "event" then eventPolicy(obj, P)
  elseif kind == "talk" then if pct(90) then press(K.A, 3, 8) else press(K.B, 3, 8) end
  elseif kind == "menu-frozen" then press(pct(60) and K.B or K.A, 10, 30)
  elseif kind == "helpbox" then
    local over = screenStep("helpbox", 1, 6)
    if over then press(pct(60) and K.B or K.R, 8, 14, true)
    elseif pct(35) then press(choose(DPAD), 6, 14) else wait(rint(4, 24)) end
  elseif kind == "statscreen" then
    genericScreen("statscreen", { { K.LEFT, 12 }, { K.RIGHT, 14 }, { K.UP, 10 }, { K.DOWN, 10 }, { K.R, 10 },
                                  { K.A, 3 }, { 0, 8 } }, 3, 10)
  elseif kind == "unitlist" then
    genericScreen("unitlist", { { K.UP, 12 }, { K.DOWN, 14 }, { K.LEFT, 12 }, { K.RIGHT, 14 }, { K.R, 8 }, { K.A, 3 },
                                { K.L | K.DOWN, 3 }, { 0, 6 } }, 6, 24)
  elseif kind == "trade" then
    genericScreen("trade", { { K.UP, 12 }, { K.DOWN, 12 }, { K.LEFT, 10 }, { K.RIGHT, 10 }, { K.A, 14 }, { K.R, 6 },
                             { 0, 4 } }, 4, 18)
  elseif kind == "supply" then
    genericScreen("supply", { { K.UP, 12 }, { K.DOWN, 12 }, { K.LEFT, 10 }, { K.RIGHT, 10 }, { K.A, 10 }, { K.R, 6 },
                              { 0, 4 } }, 4, 18)
  elseif kind == "shop" then
    genericScreen("shop", { { K.UP, 10 }, { K.DOWN, 10 }, { K.LEFT, 8 }, { K.RIGHT, 8 }, { K.A, 18 }, { K.R, 4 },
                            { 0, 4 } }, 6, 22)
  elseif kind == "guide" then
    genericScreen("guide", { { K.UP, 12 }, { K.DOWN, 14 }, { K.LEFT, 8 }, { K.RIGHT, 8 }, { K.A, 12 }, { K.B, 4 },
                             { 0, 4 } }, 8, 28)
  elseif kind == "options" then optionsPolicy(P)
  elseif kind == "chapstatus" then
    genericScreen("chapstatus", { { 0, 10 }, { K.A, 2 }, { K.R, 3 }, { K.UP, 2 }, { K.DOWN, 2 } }, 1, 5)
  elseif kind == "minimap" then
    local over = screenStep("minimap", 2, 8)
    if over then press(wpick({ { K.B, 5 }, { K.START, 3 }, { K.A, 2 } }), 10, 20, true)
    elseif pct(30) then press(choose(DPAD), 4, 10) else wait(rint(6, 20)) end
  elseif kind == "savemenu" then
    -- (MONKEY_WM=post) the title menu: mostly Extras, then Map, then the slot (route_bot.lua's
    -- ROUTE_POSTGAME rule: main_select +0x2B over the set bits of main_options +0x30,
    -- extra_select +0x34 over extra_options +0x32), else the usual presses.
    local sm = wmMode == "post" and P.first["ProcScr_SaveMenu"] or nil
    if sm and (sm.idle == IDLE.titleMain or sm.idle == IDLE.titleExtra) and titlePlan == nil then
      titlePlan = pct(80); log("title menu: %s", titlePlan and "Extras, Map" or "the usual presses")
    end
    if sm and titlePlan and (sm.idle == IDLE.titleMain or sm.idle == IDLE.titleExtra) then
      local main = sm.idle == IDLE.titleMain
      local bits = emu:read8(sm.addr + (main and 0x30 or 0x32)) & (main and 0x1F or 0x0F)
      local want = 0
      while bits ~= 0 do want = want + (bits & 1); bits = bits >> 1 end
      local cur = emu:read8(sm.addr + (main and 0x2B or 0x34))
      if cur < want then push(K.DOWN, 1, rint(3, 8)) elseif cur > want then push(K.UP, 1, rint(3, 8))
      else push(K.A, 1, rint(6, 14)) end
    else
      local r = rnd() % 100
      if r < 70 then press(K.A, 10, 20) elseif r < 85 then press(pct(50) and K.UP or K.DOWN, 6, 12) else press(K.B, 10, 20) end
    end
  elseif kind == "supportscreen" then
    genericScreen("supportscreen", { { K.UP, 10 }, { K.DOWN, 10 }, { K.LEFT, 6 }, { K.RIGHT, 6 }, { K.A, 10 }, { K.R, 5 },
                                     { 0, 4 } }, 6, 20)
  elseif kind == "menu" then menuPolicy(obj)
  elseif kind == "target" then targetPolicy(obj)
  elseif kind == "prep" then prepPolicy(P)
  elseif kind == "free" then freePolicy()
  elseif kind == "range" then rangePolicy(P)
  elseif kind == "battleanim" then
    if pct(6) then press(choose(TRANSITION_KEYS), 2, 6, true) else wait(rint(4, 20)) end
  elseif kind == "pp-busy" then
    if pct(4) then press(choose(TRANSITION_KEYS), 1, 4, true) else wait(rint(1, 3)) end
  elseif kind == "otherphase" then
    if pct(5) then press(wpick({ { K.A, 4 }, { K.B, 3 }, { K.START, 2 }, { K.R, 1 } }), 2, 6, true) else wait(rint(5, 30)) end
  elseif kind == "worldmap" then worldMapPolicy(P)
  elseif kind == "wmbase" then
    genericScreen("wmbase", { { K.UP, 10 }, { K.DOWN, 10 }, { K.A, 8 }, { K.R, 4 }, { 0, 4 } }, 3, 12)
  else
    if verbose then
      local names = {}
      for _, e in ipairs(P.list) do names[#names + 1] = (e.name or string.format("%08x", e.script)) .. (e.lock > 0 and "*" or "") end
      log("generic: %s", table.concat(names, " "))
    end
    local r = rnd() % 100
    if r < 45 then press(K.A, 10, 20) elseif r < 70 then wait(rint(5, 20)) elseif r < 85 then press(K.B, 10, 20)
    else press(K.START, 10, 20) end
  end

  -- The preparations budget runs in ticks.
  if prepBudget and inPrep then prepBudget = prepBudget - #queue end
  covCount(cov.states, kind)

  -- Watchdog: one screen for very long (the screen budgets normally end it first).
  if kind == lastKind then idleRun = idleRun + #queue else idleRun = 0 end
  lastKind = kind
  if idleRun > 3000 and kind ~= "otherphase" and kind ~= "battleanim" and kind ~= "event" and kind ~= "free"
      and kind ~= "prep" then
    log("watchdog: %s for %d ticks: B, B, A", kind, idleRun)
    covCount(cov.states, "watchdog")
    queue = {}
    push(K.B, 1, 10); push(K.B, 1, 10); push(K.A, 1, 10)
    idleRun = 0; screen = nil; menuPlan = nil; tPlan = nil; rPlan = nil; act = nil
  end
  if #queue == 0 then wait(1) end
  if os.getenv("MONKEY_VERBOSE") == "2" then
    local keys = {}
    for _, m in ipairs(queue) do if m ~= 0 then keys[#keys + 1] = string.format("%03x", m) end end
    log("%s: %d ticks, keys %s", kind, #queue, table.concat(keys, " "))
  end
  return kind
end

---------------------------------------------------------------------------
-- Optional per-tick digest (MONKEY_TRACE), the fields gate0_capture.lua
-- writes, for tools/monkey_check.py.

local traceOut = nil
local function fnv1a(s)
  local h = 0x811C9DC5
  for i = 1, #s do h = ((h ~ s:byte(i)) * 0x01000193) & 0xFFFFFFFF end
  return h
end
local function tohex(s) return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end)) end
local T_SCALARS = { { "gGameClock", 0x03000014, 4 }, { "gKeyStatusIgnoredSt", 0x03000010, 2 },
                    { "gKeyComboResetEN", 0x0300000C, 1 }, { "gSoftwareResetFlag", 0x0300001A, 1 },
                    { "gMainCallback", 0x02024CB8, 4 }, { "gLCGRNValue", 0x03000008, 4 } }
local T_RAWS = { { "gBmSt", 0x0202BCB0, 0x40 }, { "gPlaySt", 0x0202BCF0, 0x4C }, { "gRNSeeds", 0x03000000, 0x06 },
                 { "sKeyStatusBuffer", 0x02024CC0, 0x14 } }
local T_HASHED = { { "gLCDControlBuffer", 0x03003080, 0x69 }, { "gPaletteBuffer", 0x020228A8, 0x400 },
                   { "gBG0TilemapBuffer", 0x02022CA8, 0x800 }, { "gBG1TilemapBuffer", 0x020234A8, 0x800 },
                   { "gBG2TilemapBuffer", 0x02023CA8, 0x800 }, { "gBG3TilemapBuffer", 0x020244A8, 0x800 },
                   { "gOam", 0x03003140, 0x400 } }
local function traceTick(t, keys, state, P)
  local parts = { string.format('"tick":%d,"frame":%d,"vblanks":%d,"keys":"%03x"', t, sampleFrame,
    sampleFrame - lastTickFrame, keys) }
  -- The monkey's own view (not Gate 0 fields; monkey_check.py frames labels frames with it).
  parts[#parts + 1] = string.format('"monkey":"%s"', state)
  if P and P.first["ProcScr_PhaseIntro"] then parts[#parts + 1] = '"phase_intro":1' end
  if P and (P.first["gProcScr_Minimap"] or P.first["ProcScr_WorldmapMinimapWrapper"]) then parts[#parts + 1] = '"minimap":1' end
  for _, f in ipairs(T_SCALARS) do
    local v
    if f[3] == 4 then v = emu:read32(f[2]) elseif f[3] == 2 then v = emu:read16(f[2]) else v = emu:read8(f[2]) end
    parts[#parts + 1] = string.format('"%s":"%0' .. (f[3] * 2) .. 'x"', f[1], v)
  end
  for _, f in ipairs(T_RAWS) do parts[#parts + 1] = string.format('"%s":"%s"', f[1], tohex(emu:readRange(f[2], f[3]))) end
  for _, f in ipairs(T_HASHED) do parts[#parts + 1] = string.format('"%s":"%08x"', f[1], fnv1a(emu:readRange(f[2], f[3]))) end
  traceOut:write("{" .. table.concat(parts, ",") .. "}\n")
end

---------------------------------------------------------------------------

local sramFnv = string.format("%08x", fnv1a(emu:readRange(0x0E000000, 0x8000)))
if tracePath ~= "" then
  traceOut = assert(io.open(tracePath, "w"))
  traceOut:write(string.format('{"source":"menu_monkey","version":"%s","seed":%d,"ticks":%d,"sram_fnv":"%s"}\n',
    VERSION, seed, totalTicks, sramFnv))
end

local function sortedKeys(t)
  local ks = {}
  for k in pairs(t) do ks[#ks + 1] = k end
  table.sort(ks)
  return ks
end

local function jstr(s) return '"' .. tostring(s):gsub('[\\"]', "\\%0") .. '"' end

-- Curated screen list for the report: label, then proc script or menu-def names.
local SCREENS = {
  { "map menu", "menu:gMapMenuDef" }, { "unit menu", "menu:gUnitActionMenuDef" },
  { "item menu", "menu:gItemMenuDef", "menu:gItemSelectMenuDef" },
  { "item submenu (use/equip/trade/discard)", "menu:gItemSubMenuDef" },
  { "weapon select", "menu:gWeaponSelectMenuDef" }, { "staff select", "menu:gStaffItemSelectMenuDef" },
  { "yes/no menu", "menu:gYesNoSelectionMenuDef" }, { "convoy menus", "menu:gConvoyMenuDef", "menu:gSendToConvoyMenuDef" },
  { "battle forecast", "gProcScr_BKSEL" }, { "target selection", "gProcScr_TargetSelection" },
  { "stat screen", "gProcScr_StatScreen" }, { "help box", "gProcScr_HelpBox" },
  { "unit list", "ProcScr_UnitListScreen_Field", "ProcScr_UnitListScreen_PrepMenu", "ProcScr_UnitListScreen_WorldMap" },
  { "guide", "ProcScr_E_Guide1", "ProcScr_E_Guide2" },
  { "options", "ProcScr_Config_Field", "ProcScr_Config_PrepMapMenu", "ProcScr_Config_WorldMap" },
  { "chapter status", "gProcScr_ChapterStatusScreen", "ProcScr_ChapterStatusScreen_FromPrep", "ProcScr_WorldmapStatusUi" },
  { "minimap", "gProcScr_Minimap", "ProcScr_WorldmapMinimapWrapper" },
  { "trade screen", "ProcScr_TradeMenu" }, { "supply screen", "ProcScr_BmSupplyScreen" }, { "shop", "gProcScr_Shop" },
  { "battle animation", "gProc_ekrBattle" }, { "map battle", "ProcScr_MapAnimBattle" },
  { "level up", "ProcScr_EkrLevelup", "ProcScr_MapAnimExpBar" },
  { "enemy/other phase", "gProcScr_CpPhase" }, { "phase intro", "ProcScr_PhaseIntro" },
  { "event engine", "ProcScr_StdEventEngine" }, { "talk", "gProcScr_Talk" }, { "talk choice", "gProcScr_TalkChoice" },
  { "game over", "ProcScr_GameOverScreen" }, { "save menu", "ProcScr_SaveMenu", "gProcScr_SaveMenuPostChapter" },
  { "preparations", "ProcScr_AtMenu" }, { "prep: pick units", "ProcScr_PrepUnitScreen" },
  { "prep: items", "ProcScr_PrepItemScreen" }, { "prep: trade", "ProcScr_PrepItemTradeScreen" },
  { "prep: supply", "ProcScr_PrepItemSupplyScreen" }, { "prep: list", "ProcScr_PrepItemListScreen" },
  { "prep: use", "ProcScr_PrepItemUseScreen" }, { "prep: check map", "gProcScr_SALLYCURSOR" },
  { "support screen", "gProcScr_SupportScreen" },
  { "world map", "ProcScr_WorldMapMain" }, { "world map base menu", "ProcScr_GmapBaseMenu" },
}

local function screenFirst(entry)
  local first, ticks = nil, 0
  for i = 2, #entry do
    local n = entry[i]
    local mn = n:match("^menu:(.*)$")
    if mn then
      if cov.menus[mn] then
        ticks = ticks + cov.menus[mn]
        if first == nil or cov.menuFirst[mn] < first then first = cov.menuFirst[mn] end
      end
    else
      local c = cov.procs[n]
      if c then ticks = ticks + c.ticks; if first == nil or c.first < first then first = c.first end end
    end
  end
  return first, ticks
end

local function finish(reason)
  -- Key file.
  local lines = {}
  local reached = {}
  for _, e in ipairs(SCREENS) do
    local first = screenFirst(e)
    if first then reached[#reached + 1] = e[1] end
  end
  -- Header lines stay short (the PC hook read key files with a 256-byte line buffer until cov9 H-A;
  -- kept so the files regenerate byte for byte).
  local function wrap(items)
    local lines, cur = {}, "#  "
    for i, it in ipairs(items) do
      local piece = " " .. it .. (i < #items and "," or "")
      if #cur + #piece > 90 then lines[#lines + 1] = cur; cur = "#  " end
      cur = cur .. piece
    end
    lines[#lines + 1] = cur
    return table.concat(lines, "\n") .. "\n"
  end
  local header = string.format(
    "# FE8 Gate 0 key file (format: port/test/README.md, \"Input file\").\n" ..
    "# Generated by port/test/oracle/menu_monkey.lua (%s; README.md, \"Menu monkey\").\n" ..
    "# Seed %d, start %s (SRAM FNV-1a %s).\n# Prefix %s (%d ticks), then %d monkey ticks.\n" ..
    "%s%s# %d ticks. Stop reason: %s\n" ..
    "# Screens opened by the monkey:\n%s#\n",
    VERSION, seed, startLabel:sub(1, 120), sramFnv, (prefixPath ~= "" and prefixPath or "none"):sub(1, 120), prefixTicks,
    bodyTicks, tickRule == "capture" and "# Tick rule: capture (ROUTE_TICK=capture, make monkey MONKEY_TICK=capture).\n" or "",
    wmMode == "post" and "# World map: post-game (MONKEY_WM=post: its own nodes; the title's Extras, Map).\n" or "",
    #masks, reason, wrap(reached))
  L.writeKeys(outPath, masks, header)

  -- Coverage JSON.
  local f = assert(io.open(covPath, "w"))
  f:write("{\n")
  f:write(string.format('  "version": %s, "seed": %d, "start": %s, "sram_in": %s, "sram_fnv": %s, "keys": %s,\n',
    jstr(VERSION), seed, jstr(startLabel), jstr(os.getenv("GATE0_SRAM_IN") or ""), jstr(sramFnv), jstr(outPath)))
  f:write(string.format('  "prefix": %s, "prefix_ticks": %d, "monkey_ticks": %d, "ticks": %d, "stop": %s,\n',
    jstr(prefixPath), prefixTicks, bodyTicks, #masks, jstr(reason)))
  f:write(string.format('  "presses": {"taps": %d, "double_taps": %d, "holds": %d, "during_transitions": %d},\n',
    stats.taps, stats.doubles, stats.holds, stats.transition))
  f:write('  "screens": [\n')
  for i, e in ipairs(SCREENS) do
    local first, ticks = screenFirst(e)
    f:write(string.format('    {"screen": %s, "reached": %s, "first_tick": %s, "count": %d}%s\n', jstr(e[1]),
      first and "true" or "false", (first and first >= 0) and tostring(first) or "null", ticks, i < #SCREENS and "," or ""))
  end
  f:write('  ],\n')
  local function dump(name, tab, last)
    f:write(string.format('  "%s": {', name))
    local ks = sortedKeys(tab)
    for i, k in ipairs(ks) do f:write(string.format('%s%s: %d', i > 1 and ", " or "", jstr(k), tab[k])) end
    f:write("}" .. (last and "\n" or ",\n"))
  end
  dump("states", cov.states); dump("activities", cov.activities); dump("menus", cov.menus)
  dump("commands", cov.commands); dump("targets", cov.targets)
  f:write('  "procs": {')
  local ks = sortedKeys(cov.procs)
  local first = true
  for _, k in ipairs(ks) do
    if not (procsAtStart and procsAtStart[k]) then
      f:write(string.format('%s\n    %s: {"first_tick": %d, "ticks": %d}', first and "" or ",", jstr(k), cov.procs[k].first, cov.procs[k].ticks))
      first = false
    end
  end
  f:write("\n  }\n}\n")
  f:close()
  if traceOut then traceOut:close() end
  print(string.format("menu_monkey: %s; %d ticks (prefix %d, monkey %d) in %d frames -> %s", reason, #masks, prefixTicks,
    #masks - prefixTicks, frame, outPath))
  print("menu_monkey: screens opened: " .. table.concat(reached, ", "))
  io.stdout:flush()
  os.exit(0)
end

masks[1] = prefix[1] or 0
if prefixTicks == 0 then masks[1] = 0 end
emu:setKeys(masks[1])

-- A tick is sampled at the frame callback that sees main_loop_ended set (the
-- frame rule), or at the capture's sample point (ROUTE_TICK=capture, before the
-- VBlank its wait waits for: that VBlank is the tick's frame).
L.onTick(tickRule, function() frame = frame + 1 end, function()
  sampleFrame = tickRule == "capture" and frame + 1 or frame
  local t = tick                                   -- tick t has just completed
  local P = procScan()
  if traceOut then traceTick(t, masks[t + 1], t < prefixTicks and "prefix" or (classify(P)), P) end
  lastTickFrame = sampleFrame
  if t >= prefixTicks then
    if not procsAtStart then
      procsAtStart = {}
      for _, e in ipairs(P.list) do if e.name then procsAtStart[e.name] = true end end
    end
    covProcs(P)
  end
  tick = t + 1
  if tick >= totalTicks then finish("reached " .. totalTicks .. " ticks") end
  local m
  if tick < prefixTicks then
    m = prefix[tick + 1] or 0
  else
    if #queue == 0 then decide() end
    m = table.remove(queue, 1) or 0
  end
  masks[tick + 1] = m
  emu:setKeys(m)
end)
