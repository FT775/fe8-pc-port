-- Test-only cheats for the Gate 0 harness and the route tools (port/test/README.md, "Test
-- cheats"; port/test/inputs/README-routes.md, "Cheat-assisted routes"). The GBA side of the rule;
-- port/test/pc/gate0_hook.c (CheatRefill) is the PC side, the same rule at the same point. The
-- idea of "durability" is FE8 Reforged's unlimited-durability option (a romhack); nothing of the
-- romhack is used, and the game, the ROM and every played build stay as they are: this file is
-- only loaded by the test scripts when a cheat is asked for, and the PC side is in the Gate 0 test
-- binary only.
--
-- Three cheats, combinable with "+" (GATE0_CHEAT=... for gate0_capture.lua, sram_capture.lua and
-- sram_boost.lua; ROUTE_CHEAT=... for route_bot.lua and route_check.lua; e.g.
-- GATE0_CHEAT=durability+funds+convoy). Any order is taken; the name is always written in this
-- order: durability, funds, convoy (C.check). All act at every tick boundary:
--   - The point: the sample point of a tick (gate0_capture.lua's sample at the main loop's
--     VBlankIntrWait call, README.md "Logic tick"; the PC hook's __wrap_VBlankIntrWait), right
--     after the tick's line is sampled and before the VBlank the tick waits for. So the traced
--     fields of tick T are the game's own, and OnMain T+1 reads what the cheats left on both sides.
--     In this order: the refusal (below), convoy, durability, funds.
--   - "durability": every item in every unit's inventory gets its uses back to the item's maximum.
--     The items: the five slots of every struct Unit (+0x1E, include/bmunit.h) whose
--     pCharacterData (+0x00) is set, in gUnitArrayBlue[62], gUnitArrayRed[50],
--     gUnitArrayGreen[20] and gUnitArrayPurple[5] (src/bmunit.c), and the 100 slots of the convoy,
--     gConvoyItemArray (include/bmcontainer.h CONVOY_ITEM_COUNT). An empty slot (0) stays empty.
--     The maximum: GetItemMaxUses (src/bmitem.c), struct ItemData maxUses (+0x14) of gItemData
--     in the ROM. An item with IA_UNBREAKABLE (attributes +0x08, bit 3) or a maximum of 0 is left
--     as it is. The word becomes ITEM_INDEX | max << 8 (also when its uses were above the max).
--     Nothing else: not the battle units' copies (gBattleActor, gBattleTarget), not stats, not
--     the RNG. A battle's own uses are taken from the battle unit during the battle; the next
--     tick boundary after the game writes them back fills them again. A single-use item (a
--     key, a stat booster, a promotion item) is still used up: its slot empties inside the
--     tick, and an empty slot stays empty.
--   - "funds": the party's gold, gPlaySt.partyGoldAmount (+0x08, include/types.h), becomes the
--     game's maximum, 999,999 (SetPartyGoldAmount and AddPartyGoldAmount cap it there,
--     src/bmgold.c; the shops buy with GetPartyGoldAmount - price and sell with
--     SetPartyGoldAmount(gold + price), src/bmshop.c). At every tick sample, as written.
--   - "convoy": a rolling convoy. The convoy's 100 slots are the ROM's own limit (the game's code,
--     which the GBA oracle runs unchanged), so the cheat does not raise it: it keeps the slots
--     full instead. Every empty convoy slot (word 0, as AddItemToConvoy sees it) gets the next
--     item of the list, made as the game makes a new item (MakeNewItem, src/bmitem.c: its
--     maximum uses, 0 for an unbreakable item). The list: the item ids 0x01 to 0xCD of gItemData
--     (0xCE entries, ITEM_NONE to ITEM_UNK_CD, src/data_items.c) whose name is not the
--     placeholder text MSG_403 "Dummy" (nameTextId +0x00): 177 items; the 28 left out are
--     ITEM_SWORD_MKATTI, ITEM_ANIMA_FORBLAZE, ITEM_LIGHT_LUCE, ITEM_MINE, ITEM_LIGHTRUNE, the four
--     FE7 blessings (0x7D to 0x80), ITEM_HEAVENSEAL and ITEM_UNK_BC to ITEM_UNK_CD. So the first
--     time the convoy is empty its 100 slots get the first 100 items (0x01 to 0x67, ITEM_SWORD_IRON
--     to ITEM_ELYSIANWHIP, without 0x0A, 0x3D and 0x44), and whenever a slot empties afterwards
--     (an item taken out: RemoveItemFromConvoy shrinks the list, so the empty slot is the last)
--     the next one goes in. The cursor counts the items placed so far: item number
--     cursor % 177 of the list is next, so after the 177th the list goes round again from
--     ITEM_SWORD_IRON (and the convoy never runs dry). It starts at 0 at power-on (a resumed run
--     takes its photo's), and acts only while an army is loaded (some unit of gUnitArrayBlue has
--     pCharacterData set): before a save loads, the convoy in RAM is not the game's yet, and the
--     load (ReadSupplyItems) overwrites all 100 slots. The game's own items stay where they are.
--   - Not in Eirika's Chapter 20 and final chapter (the user's decision of 2026-10-22: they are
--     played fair). Each refill first reads gPlaySt.chapterIndex; at 0x14 (Chapter 20, and the
--     world map before it), 0x15 or 0x16 (the final chapter's two parts) the tool stops with a
--     message (exit 5) before it changes anything, whatever else the run is (ROUTE_E7 or not, the
--     real army or a boosted start, any cheat). The PC hook refuses at the same point
--     (gate0_hook.c). So the first tick sample in those chapters stops the run: a save that loads
--     into them (e20_start, e21_start, e22_start, b20_start to b22_start) and a chapter change
--     into them (the end of Chapter 19, before its end save).
--     Two exceptions. On Ephraim's route (chapterModeIndex 3, CHAPTER_MODE_EPHRAIM) such an index
--     is a scene's map, not Eirika's chapter (his Chapter 20 ends with a scene on map 0x15:
--     route_i20.keys, ticks 163,673 to 163,689). And once the game is complete
--     (gPlaySt.chapterStateBits +0x14 has PLAY_FLAG_COMPLETE, 0x20; set by
--     GameCtrl_DeclareCompletedChapter, src/bmio.c, when the final chapter is won), the index is
--     the post-game's: a clear save (e_clear, b_clear) loads with 0x16 and that flag, for the
--     ending it replays or the post-game world map (the user's decision of 2026-10-23: cheat-
--     assisted post-game runs from Eirika's clear saves are allowed). In the final chapter itself
--     the flag is only set after its boss falls, and every tick sample of the chapter before that
--     refuses.
-- refill() also gives the fields of the tick line (README.md, "Test cheats"), compared like any
-- traced field: "cheat_items", the FNV-1a 32 of the item bytes after the cheats, of all 137 unit
-- slots (10 bytes each, at +0x1E, whatever pCharacterData is) and then the convoy's 200 bytes;
-- with durability "cheat_refills", the item words it changed; with funds "cheat_funds", 1 when it
-- changed the gold (0 when it was already 999,999); with convoy "cheat_convoy", the cursor after
-- this tick's placements.

local C = {}

C.NAMES = { "durability", "funds", "convoy" }    -- the cheats, in the order of their names
local MAIN_LOOP_ENDED = 0x0202BCB0               -- gBmSt.main_loop_ended
local ITEMDATA = 0x08809B10                      -- gItemData: struct ItemData[], 0x24 each (include/bmitem.h)
local ITEM_COUNT = 0xCE                          -- its entries, ITEM_NONE to ITEM_UNK_CD (src/data_items.c)
local MSG_DUMMY = 0x0403                         -- MSG_403, "Dummy" (texts/texts.txt): a placeholder item's name
local IA_UNBREAKABLE = 0x8
local UNITS = { { 0x0202BE4C, 62 },              -- gUnitArrayBlue
                { 0x0202CFBC, 50 },              -- gUnitArrayRed
                { 0x0202DDCC, 20 },              -- gUnitArrayGreen
                { 0x0202E36C, 5 } }              -- gUnitArrayPurple; struct Unit is 0x48 bytes
local CONVOY, CONVOY_COUNT = 0x0203A81C, 100     -- gConvoyItemArray
local CHAPTER = 0x0202BCFE                       -- gPlaySt.chapterIndex (gPlaySt 0x0202BCF0, +0x0E; include/types.h)
local MODE, MODE_EPHRAIM = 0x0202BD0B, 3         -- gPlaySt.chapterModeIndex (+0x1B), CHAPTER_MODE_EPHRAIM
local STATEBITS, PLAY_FLAG_COMPLETE = 0x0202BD04, 0x20   -- gPlaySt.chapterStateBits (+0x14)
local GOLD, GOLD_MAX = 0x0202BCF8, 999999        -- gPlaySt.partyGoldAmount (+0x08); src/bmgold.c's cap

-- The value of GATE0_CHEAT or ROUTE_CHEAT (var names it): nil when unset or empty, else its name
-- in the order of C.NAMES ("funds+durability" is "durability+funds"). C.on[name] is set for each.
C.on = {}
function C.check(value, var)
  if value == nil or value == "" then return nil end
  local want = {}
  for part in (value .. "+"):gmatch("(.-)%+") do
    local known = false
    for _, n in ipairs(C.NAMES) do known = known or part == n end
    assert(known, string.format("%s=%s: the test cheats are durability, funds and convoy, joined with + "
      .. "(port/test/README.md, \"Test cheats\")", var, value))
    assert(not want[part], string.format("%s=%s: %s twice", var, value, part))
    want[part] = true
  end
  local list = {}
  for _, n in ipairs(C.NAMES) do if want[n] then list[#list + 1] = n end end
  C.var, C.on, C.name = var, want, table.concat(list, "+")
  return C.name
end

-- What the cheats do, for a key file's "# Cheat-assisted" line (route_bot.lua). For durability
-- alone, the words every older cheat-assisted file has.
function C.describe()
  local d = {}
  if C.on.durability then d[#d + 1] = "every item's uses back to full" end
  if C.on.funds then d[#d + 1] = "the party's gold at the game's maximum, 999,999" end
  if C.on.convoy then d[#d + 1] = "every empty convoy slot filled with the next of the game's items (a rolling convoy)" end
  return table.concat(d, ", ") .. " at each tick"
end

-- The tool's tick, for the refusal's message: each tool sets C.tick to a function that returns it.
C.tick = nil

-- Eirika's Chapter 20 and final chapter (the header): chapterIndex 0x14 to 0x16, the indices
-- route_bot.lua's L.E7.chapter plays with the real army, off Ephraim's route and before the game is
-- complete. Stops the tool there.
local function refuse()
  local ch = emu:read8(CHAPTER)
  if ch < 0x14 or ch > 0x16 or emu:read8(MODE) == MODE_EPHRAIM then return end
  if (emu:read8(STATEBITS) & PLAY_FLAG_COMPLETE) ~= 0 then return end
  io.stderr:write(string.format("test cheat: %s=%s refused at tick %s: chapterIndex 0x%02x is Eirika's "
    .. "Chapter 20 or final chapter (0x14 to 0x16, the game not complete), which are played fair (the user's "
    .. "decision of 2026-10-22; port/test/README.md, \"Test cheats\")\n", C.var or "GATE0_CHEAT", C.name or "?",
    C.tick and tostring(C.tick()) or "?", ch))
  io.stderr:flush()
  os.exit(5)
end

local maxOf = {}                                 -- item index -> maximum uses, or false (left as it is)
local function maxUses(id)
  local m = maxOf[id]
  if m == nil then
    local d = ITEMDATA + id * 0x24
    if (emu:read32(d + 0x08) & IA_UNBREAKABLE) ~= 0 then
      m = false
    else
      m = emu:read8(d + 0x14)
      if m == 0 then m = false end
    end
    maxOf[id] = m                                -- (the ROM does not change)
  end
  return m
end

-- n item words at address a: refilled when live, then hashed (memory order, little-endian).
local function slots(a, n, live, undo, h)
  local s = emu:readRange(a, 2 * n)
  for k = 0, n - 1 do
    local w = s:byte(2 * k + 1) | (s:byte(2 * k + 2) << 8)
    if live and w ~= 0 then
      local m = maxUses(w & 0xFF)
      if m and (w >> 8) ~= m then
        local v = (w & 0xFF) | (m << 8)
        emu:write16(a + 2 * k, v)
        undo[#undo + 1] = a + 2 * k
        undo[#undo + 1] = w
        w = v
      end
    end
    h = ((h ~ (w & 0xFF)) * 0x01000193) & 0xFFFFFFFF
    h = ((h ~ (w >> 8)) * 0x01000193) & 0xFFFFFFFF
  end
  return h
end

-- The convoy cheat's list (the header): read once from the ROM's gItemData.
local list = nil
function C.items()
  if not list then
    list = {}
    for id = 1, ITEM_COUNT - 1 do
      local d = ITEMDATA + id * 0x24
      assert(emu:read8(d + 0x06) == id, string.format("gItemData[0x%02x].number is 0x%02x", id, emu:read8(d + 0x06)))
      if emu:read16(d) ~= MSG_DUMMY then list[#list + 1] = id end
    end
    assert(#list == 177, "the convoy cheat's list has " .. #list .. " items, not the 177 the header names")
  end
  return list
end
local cursor = 0                                 -- the convoy cheat's items placed so far
function C.cursor() return cursor end
function C.setCursor(n) cursor = n end
local function newItem(id)                       -- MakeNewItem (src/bmitem.c)
  local d = ITEMDATA + id * 0x24
  local uses = emu:read8(d + 0x14)
  if (emu:read32(d + 0x08) & IA_UNBREAKABLE) ~= 0 then uses = 0 end
  return (uses << 8) + id
end
local function convoy(undo)
  local loaded = false
  for i = 0, UNITS[1][2] - 1 do
    if emu:read32(UNITS[1][1] + i * 0x48) ~= 0 then loaded = true; break end
  end
  if not loaded then return 0 end
  local items, s, placed = C.items(), emu:readRange(CONVOY, 2 * CONVOY_COUNT), 0
  for k = 0, CONVOY_COUNT - 1 do
    if s:byte(2 * k + 1) == 0 and s:byte(2 * k + 2) == 0 then
      emu:write16(CONVOY + 2 * k, newItem(items[cursor % #items + 1]))
      undo[#undo + 1] = CONVOY + 2 * k
      undo[#undo + 1] = 0
      cursor = cursor + 1
      placed = placed + 1
    end
  end
  return placed
end

-- The cheats at this tick boundary (the header). Returns the undo list (address, old halfword,
-- ...; .cursor the convoy cursor before), the tick line's fields (a string) and the counts
-- { words = durability's item words changed, placed = items the convoy cheat put in, funds = 1 when
-- the gold was changed }. (In Eirika's Chapter 20 and final chapter it stops the tool instead.)
function C.refill()
  refuse()
  local undo, h = { cursor = cursor }, 0x811C9DC5
  local placed = C.on.convoy and convoy(undo) or 0
  local n0, live = #undo, C.on.durability == true
  for _, arr in ipairs(UNITS) do
    for i = 0, arr[2] - 1 do
      local u = arr[1] + i * 0x48
      h = slots(u + 0x1E, 5, live and emu:read32(u) ~= 0, undo, h)
    end
  end
  h = slots(CONVOY, CONVOY_COUNT, live, undo, h)
  local words, funds = (#undo - n0) // 2, 0
  if C.on.funds then
    local g = emu:read32(GOLD)
    if g ~= GOLD_MAX then
      undo[#undo + 1] = GOLD; undo[#undo + 1] = g & 0xFFFF
      undo[#undo + 1] = GOLD + 2; undo[#undo + 1] = g >> 16
      emu:write32(GOLD, GOLD_MAX)
      funds = 1
    end
  end
  local fields = string.format('"cheat_items":"%08x"', h)
  if C.on.durability then fields = fields .. string.format(',"cheat_refills":%d', words) end
  if C.on.funds then fields = fields .. string.format(',"cheat_funds":%d', funds) end
  if C.on.convoy then fields = fields .. string.format(',"cheat_convoy":%d', cursor) end
  return undo, fields, { words = words, placed = placed, funds = funds }
end

function C.undo(list)
  for i = #list - 1, 1, -2 do emu:write16(list[i], list[i + 1]) end
  if list.cursor then cursor = list.cursor end
end

-- The run's totals, for a tool's end line: add(counts) for each tick line written, then line().
local totals = { words = 0, wordTicks = 0, placed = 0, funds = 0 }
function C.add(c)
  totals.words, totals.placed, totals.funds = totals.words + c.words, totals.placed + c.placed, totals.funds + c.funds
  if c.words > 0 then totals.wordTicks = totals.wordTicks + 1 end
end
function C.line()
  local d = {}
  if C.on.durability then d[#d + 1] = string.format("%d item words refilled, on %d ticks", totals.words, totals.wordTicks) end
  if C.on.funds then d[#d + 1] = string.format("the gold set to 999,999 on %d ticks", totals.funds) end
  if C.on.convoy then
    d[#d + 1] = string.format("%d items put in the convoy (cursor %d, %d items in the list)", totals.placed, cursor,
      #C.items())
  end
  return table.concat(d, "; ")
end

-- For the tools that take a tick's sample at the gate0_wait report and never take it back
-- (sram_capture.lua, sram_boost.lua, route_lib.lua's capture rule): C.atSample() at the sample,
-- after the tool's own work for the tick, and C.auto() once. When the IRQ is taken at
-- VBlankIntrWait's first instruction and its OnVBlank flushes (README.md, "Early flush VBlank",
-- the IRQ at the call), gate0_capture.lua takes the tick's sample back and takes it again when
-- the CPU is back at the call, so its refill comes after that OnVBlank, as on the PC (whose lag
-- replay runs that VBlank before the sample). These callbacks do the same here: the words (and
-- the convoy cursor) are put back when that OnVBlank begins and the cheats run again at
-- "gate0_wait_resume", so every tool leaves the game exactly as gate0_capture.lua does.
local st = { armed = false, irq = false, redo = false, undo = nil }
function C.atSample()
  local undo = C.refill()
  st.armed = emu:read8(MAIN_LOOP_ENDED) ~= 0     -- (the sample after an early flush VBlank stands)
  st.irq, st.redo, st.undo = false, false, st.armed and undo or nil
end
function C.auto()
  callbacks:add("gate0_wait_irq", function() if st.armed then st.irq = true end end)
  callbacks:add("gate0_onvblank", function()
    if st.armed and st.irq and emu:read8(MAIN_LOOP_ENDED) ~= 0 then
      C.undo(st.undo)
      st.armed, st.irq, st.undo, st.redo = false, false, nil, true
    end
  end)
  callbacks:add("gate0_wait_resume", function()
    st.armed, st.irq, st.undo = false, false, nil
    if st.redo then
      st.redo = false
      C.refill()
    end
  end)
  callbacks:add("frame", function() st.armed, st.irq, st.undo = false, false, nil end)
end
-- route_bot.lua's search (save states at a tick sample) keeps this state with its own, the
-- convoy cursor included.
function C.save() return { armed = st.armed, irq = st.irq, redo = st.redo, undo = st.undo, cursor = cursor } end
function C.load(s) st.armed, st.irq, st.redo, st.undo, cursor = s.armed, s.irq, s.redo, s.undo, s.cursor or 0 end

return C
