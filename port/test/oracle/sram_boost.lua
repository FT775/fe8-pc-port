-- Boosted starts (test coverage only, not story play): play a key file on
-- fireemblem8.gba in mGBA as sram_capture.lua does, but at the end of one tick
-- raise every living blue unit's stats to its class caps (and, with
-- BOOST_USES=1, every item's uses to a new item's), then let the game go on and
-- write its own saves; the SRAM is written at SRAM_AT as sram_capture.lua's
-- "tick:T" rule writes it. The game computes every checksum itself, so the
-- save is a normal one; only the units' numbers were changed, in RAM, before
-- the game saved them.
--
-- Use: an army that can win the last chapters, so the bot can reach the
-- final chapter, the ending, the credits, the clear save and the post-game
-- (port/test/inputs/README-routes.md, "Boosted starts"). Both sides boot from
-- the save it writes, so a Gate 0 compare from it is a fair GBA-vs-PC check;
-- the run itself is labelled "boosted start" everywhere.
--
--   MGBA_RUN_BP=$(GBA_BP) GATE0_SRAM_IN=e20_start.sav GATE0_KEYS=x.keys \
--   BOOST_AT=1900 SRAM_AT=tick:2721 SRAM_OUT=b20_start.sav \
--     build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/sram_boost.lua 3000000
--
-- BOOST_AT=T: the poke happens at the end of tick T (after OnMain T, before
-- the mask for tick T+1). Pick a tick after the units are in RAM (the title
-- menu's Continue or Resume) and before the save you want them in (a chapter's
-- turn-1 suspend is written before its first free cursor).
-- Stats: maxHP = class maxHP, HP = maxHP, pow/skl/spd/def/res = the class's
-- caps (struct ClassData +0x13..+0x18, include/bmunit.h), luck 30 (the game's
-- luck cap); level, items, ranks, con and move are left as they are.
-- BOOST_LEVEL=C:L:E instead (coverage 11, a "level-boosted" start): only the unit with character
-- C (hex) gets level L and experience E (struct Unit +0x08, +0x09); no stat, no item and no other unit
-- is changed (with BOOST_CAPS=1, that unit's stats also go to its class caps, as above). Use: a
-- trainee (Ross, Amelia, Ewan) at level 9 with 99 experience, so its next battle reaches level
-- 10 and the game's trainee promotion (classchg-event.c) runs.
-- Output: SRAM_OUT (32 KiB) and SRAM_OUT.json. Game-save data: never commit it.
--
-- Edited starts (test coverage only, not story play; round 10 "paths",
-- port/test/inputs/paths/README.md): BOOST_STATS=0 leaves the stats alone, and
-- BOOST_EDIT_FILE=FILE sets single fields of single units at the same tick, so a
-- test can reach a code path no story save reaches (a Bishop, a staff, a Devil
-- Axe next to a cracked wall). FILE has one line per unit (# comments):
--   PID FIELD=VALUE ...
-- PID is the character number (hex) of the first unit with it, blue units first,
-- then green, then red; or RED, every red unit. FIELD (decimal VALUE unless said): class (hex: pClassData
-- = that gClassData entry), lv, hp (current HP), maxhp, pow, skl, spd, def, res,
-- lck, x, y, st (hex byte 0x30: statusIndex | statusDuration << 4), r0..r7
-- (weapon rank exp: 4 is staves), i1..i5 (hex item word: id | uses << 8),
-- addstate (hex bits ORed into the state word +0x0C, such as 1000 for
-- US_DROP_ITEM: a suspend save keeps the whole word; integration 11). A
-- line "RN advance=N" draws N random numbers (src/rng.c NextRN), as if N had
-- been used before the save (a suspend save keeps the RN state, bmsave.c), so a
-- test can reach a roll's other outcome. A line "FLAGS set=N,N,..." (coverage 12)
-- sets those permanent event flags (101 and up), as SetPermanentFlag does, so a
-- test can reach the post-game recruits (flags 103-112). The game writes its own save
-- afterwards, as for the boost.
-- Coverage 12's item gallery (port/test/inputs/gallery/README.md) added: PID "U<hex>", the unit whose
-- index byte (+0x0B) is hex, any faction (blue 01-3F, green 41-7F, red 81-BF); the fields state (hex:
-- the whole state word +0x0C, so state=0 puts a dead unit back on the map at its x and y), caps (any
-- value: maxhp, hp, pow, skl, spd, def, res to the unit's class caps and luck 30, as the army boost;
-- the fields after it on the line still apply), ai1 and ai2 (decimal, +0x42 and +0x44); and two lines
-- of their own: "PLAY vision=N" (gPlaySt.chapterVisionRange +0x0D: fog of war with vision N, 0 none)
-- and "CONVOY clear" (all 100 words of gConvoyItemArray to 0), "CONVOY add XX" (item XX, hex, with a new
-- item's uses, in the first empty slot). A file that uses caps makes a
-- "boosted and edited" start.

local keysPath = assert(os.getenv("GATE0_KEYS"), "GATE0_KEYS: the key file to play")
local at = assert(os.getenv("SRAM_AT"), "SRAM_AT: tick:T")
local outPath = assert(os.getenv("SRAM_OUT"), "SRAM_OUT: the .sav to write")
local boostAt = tonumber((assert(os.getenv("BOOST_AT"), "BOOST_AT: the tick whose end gets the poke")))
local boostUses = os.getenv("BOOST_USES") == "1"
local boostStats = os.getenv("BOOST_STATS") ~= "0"
local editPath = os.getenv("BOOST_EDIT_FILE") or ""
local levelSpec = os.getenv("BOOST_LEVEL") or ""
local lvChar, lvLevel, lvExp
if levelSpec ~= "" then
  local c, l, e = levelSpec:match("^(%w+):(%d+):(%d+)$")
  lvChar, lvLevel, lvExp = tonumber(c, 16), tonumber(l), tonumber(e)
  assert(lvChar and lvLevel and lvExp, "BOOST_LEVEL=char:level:exp")
end
-- BOOST_ITEMS=C:i,i,...;C:i,... instead (coverage 11, a "refit" of a boosted army): the unit with
-- character C (hex) gets exactly the items i (hex ids, each with a new item's uses) in its five slots;
-- no stat and no other unit is changed. Use: the boosted army's weapons are worn out after Tower
-- floors 1 to 7 or Lagdou Ruins floor 1, and nine more floors need some.
local itemSpec = os.getenv("BOOST_ITEMS") or ""
local kit = nil
if itemSpec ~= "" then
  kit = {}
  for part in itemSpec:gmatch("[^;]+") do
    local c, list = part:match("^(%w+):([%x,]+)$")
    assert(c and list, "BOOST_ITEMS=char:item,item,...;char:...")
    local ids = {}
    for id in list:gmatch("%x+") do ids[#ids + 1] = tonumber(id, 16) end
    assert(#ids <= 5, "BOOST_ITEMS: at most five items a unit")
    kit[tonumber(c, 16)] = ids
  end
end
local rule, arg = at:match("^(%a+):(%d+)$")
assert(rule == "tick", "bad SRAM_AT '" .. at .. "' (tick:T)")
arg = tonumber(arg)
assert(arg > boostAt + 1, "SRAM_AT must come after BOOST_AT")

local bp = os.getenv("MGBA_RUN_BP") or ""
assert(bp:find("gate0_wait@", 1, true), "MGBA_RUN_BP must set the gate0_wait breakpoint (port/test/Makefile)")
assert(bp:find("gate0_onvblank@", 1, true), "MGBA_RUN_BP must set the gate0_onvblank breakpoint (port/test/Makefile)")
-- GATE0_CHEAT=durability, funds, convoy or several joined with "+" (README.md, "Test cheats"): the
-- test cheats at every tick's sample, as gate0_capture.lua has them (oracle/test_cheat.lua), when
-- KEYS is a cheat-assisted route (port/test/Makefile chain-save passes the key file's own cheat);
-- after the poke at BOOST_AT. The .json's "cheat" is every cheat of this run and of the start
-- save (GATE0_SRAM_IN.json's "cheat"), as sram_capture.lua writes it, so the mark goes on through
-- every later save of a chain.
local cheat = (os.getenv("GATE0_CHEAT") or "") ~= ""
  and dofile(os.getenv("GATE0_CHEAT_LIB") or "port/test/oracle/test_cheat.lua") or nil
if cheat then
  cheat.name = cheat.check(os.getenv("GATE0_CHEAT"), "GATE0_CHEAT")
  cheat.auto()
end

local MAIN_LOOP_ENDED = 0x0202BCB0               -- gBmSt.main_loop_ended
local PLAYST = 0x0202BCF0                        -- gPlaySt: +0x0E chapterIndex (s8), +0x10 chapterTurnNumber
local BLUE = 0x0202BE4C                          -- gUnitArrayBlue: struct Unit[62], 0x48 each
local ITEMDATA = 0x08809B10                      -- gItemData: struct ItemData[], 0x24 each (uses +0x14, attributes +0x08)
local SRAM, SRAM_SIZE = 0x0E000000, 0x8000

local function readKeys(path)                    -- as gate0_capture.lua (README.md, "Input file")
  local masks = {}
  local f = assert(io.open(path, "r"), "cannot open key file " .. path)
  for line in f:lines() do
    line = line:gsub("#.*", ""):match("^%s*(.-)%s*$")
    if line ~= "" then
      local m, n = line:match("^(%x+)%*(%d+)$")
      if not m then m, n = line:match("^(%x+)$"), "1" end
      assert(m, "bad key file entry '" .. line .. "'")
      for _ = 1, tonumber(n) do masks[#masks + 1] = tonumber(m, 16) end
    end
  end
  f:close()
  return masks
end

local masks = readKeys(keysPath)
local function maskFor(t) return masks[t + 1] or 0 end
local function s8(v) return v >= 0x80 and v - 0x100 or v end

local log = {}
local GREEN, RED = 0x0202DDCC, 0x0202CFBC        -- gUnitArrayGreen (20 units), gUnitArrayRed (50 units)
local CLASSDATA = 0x08807164                     -- gClassData: struct ClassData[], 0x54 each, from class 1
local EDIT_U8 = { lv = 0x08, maxhp = 0x12, hp = 0x13, pow = 0x14, skl = 0x15, spd = 0x16, def = 0x17, res = 0x18,
                  lck = 0x19, x = 0x10, y = 0x11, ai1 = 0x42, ai2 = 0x44 }
local editCaps = false                           -- (a "caps" field was used: a boosted start)
local function edit()                            -- BOOST_EDIT_FILE (the header above)
  local f = assert(io.open(editPath, "r"), "cannot open " .. editPath)
  for line in f:lines() do
    line = line:gsub("#.*", ""):match("^%s*(.-)%s*$")
    local vis = line:match("^PLAY%s+vision=(%d+)$")
    if vis then                                  -- gPlaySt.chapterVisionRange (include/types.h)
      log[#log + 1] = string.format("edit PLAY: chapterVisionRange %d -> %s", emu:read8(PLAYST + 0x0D), vis)
      emu:write8(PLAYST + 0x0D, tonumber(vis))
      print("sram_boost: " .. log[#log]); io.stdout:flush()
      line = ""
    end
    if line == "CONVOY clear" then               -- gConvoyItemArray (u16[100], src/bmcontainer.c)
      local n = 0
      for i = 0, 99 do if emu:read16(0x0203A81C + 2 * i) ~= 0 then n = n + 1 end; emu:write16(0x0203A81C + 2 * i, 0) end
      log[#log + 1] = string.format("edit CONVOY: %d items cleared", n)
      print("sram_boost: " .. log[#log]); io.stdout:flush()
      line = ""
    end
    local add = line:match("^CONVOY%s+add%s+(%x+)$")
    if add then                                  -- the first empty slot gets a new item (MakeNewItem: its uses)
      local id = tonumber(add, 16)
      local d = ITEMDATA + id * 0x24
      local uses = (emu:read32(d + 0x08) & 0x8) ~= 0 and 0 or emu:read8(d + 0x14)   -- (IA_UNBREAKABLE: 0)
      local slot
      for i = 0, 99 do if emu:read16(0x0203A81C + 2 * i) == 0 then slot = i; break end end
      assert(slot, "edit CONVOY add: the convoy is full")
      emu:write16(0x0203A81C + 2 * slot, id | (uses << 8))
      log[#log + 1] = string.format("edit CONVOY: item %02x (%d uses) in slot %d", id, uses, slot)
      print("sram_boost: " .. log[#log]); io.stdout:flush()
      line = ""
    end
    local adv = line:match("^RN%s+advance=(%d+)$")
    if adv then
      -- "RN advance=N": draw N random numbers (src/rng.c NextRN on gRNSeeds, IWRAM 0x03000000),
      -- as if N had been used before the save; the suspend save keeps the state (bmsave.c).
      local s0, s1, s2 = emu:read16(0x03000000), emu:read16(0x03000002), emu:read16(0x03000004)
      local before = string.format("%04x %04x %04x", s0, s1, s2)
      for _ = 1, tonumber(adv) do
        local rn = (((s1 << 11) & 0xFFFF) + (s0 >> 5)) & 0xFFFF
        local t2 = (s2 * 2) & 0xFFFF
        if (s1 & 0x8000) ~= 0 then t2 = (t2 + 1) & 0xFFFF end
        rn = rn ~ t2
        s0, s1, s2 = rn, s0, s1
      end
      emu:write16(0x03000000, s0); emu:write16(0x03000002, s1); emu:write16(0x03000004, s2)
      log[#log + 1] = string.format("edit RN: advanced %s steps, seeds %s -> %04x %04x %04x", adv, before, s0, s1, s2)
      print("sram_boost: " .. log[#log]); io.stdout:flush()
      line = ""
    end
    local flags = line:match("^FLAGS%s+set=([%d,]+)$")
    if flags then
      -- "FLAGS set=N,N,...": permanent event flags N (101 and up) set, as SetPermanentFlag
      -- (src/eventinfo.c) does: gPermanentFlagBits (IWRAM 0x03005250) bit gFlagBitMaskLut
      -- (0x089E857C) of N - 101. A game save written afterwards keeps them. Coverage 12: the
      -- post-game recruits' flags 103-112 (EVFLAG_EXTRA_UNIT, src/bmdifficulty.c), so the next
      -- post-game preparations add those units (PrepScreenProc_AddPostgameUnits).
      local set = {}
      for n in flags:gmatch("%d+") do
        local f = tonumber(n)
        assert(f > 100, "FLAGS: permanent flags are 101 and up")
        local k = f - 101
        local a = 0x03005250 + (k >> 3)
        emu:write8(a, emu:read8(a) | emu:read8(0x089E857C + (k & 7)))
        set[#set + 1] = n
      end
      log[#log + 1] = "edit flags: permanent flags " .. table.concat(set, " ") .. " set"
      print("sram_boost: " .. log[#log]); io.stdout:flush()
      line = ""
    end
    if line ~= "" then
      local who, rest = line:match("^(%w+)%s+(.*)$")
      assert(who, "bad edit line '" .. line .. "'")
      local us = {}                                -- the units the line edits
      if who == "RED" then                         -- "RED ...": every red unit
        for i = 0, 49 do
          if emu:read32(RED + i * 0x48) ~= 0 then us[#us + 1] = RED + i * 0x48 end
        end
      elseif who:match("^U%x%x$") then             -- "U<hex>": the unit with that index byte, any faction
        local id = tonumber(who:sub(2), 16)
        for _, arr in ipairs({ { BLUE, 62 }, { GREEN, 20 }, { RED, 50 } }) do
          for i = 0, arr[2] - 1 do
            local p = arr[1] + i * 0x48
            if emu:read32(p) ~= 0 and emu:read8(p + 0x0B) == id then us[#us + 1] = p end
          end
        end
        assert(#us == 1, string.format("edit: no unit with index %02x", id))
      else
        local pid = tonumber(who, 16)
        assert(pid, "bad edit line '" .. line .. "'")
        for _, arr in ipairs({ { BLUE, 62 }, { GREEN, 20 }, { RED, 50 } }) do
          for i = 0, arr[2] - 1 do
            local p = arr[1] + i * 0x48
            if #us == 0 and emu:read32(p) ~= 0 and emu:read8(emu:read32(p) + 4) == pid then us[1] = p end
          end
        end
        assert(#us == 1, string.format("edit: no unit with character %02x", pid))
      end
      for _, u in ipairs(us) do
        local done = {}
        for key, val in rest:gmatch("(%w+)=(%x+)") do
          if key == "class" then
            emu:write32(u + 4, CLASSDATA + (tonumber(val, 16) - 1) * 0x54)
          elseif key == "st" then
            emu:write8(u + 0x30, tonumber(val, 16))
          elseif key == "addstate" then
            emu:write32(u + 0x0C, emu:read32(u + 0x0C) | tonumber(val, 16))
          elseif key == "state" then
            emu:write32(u + 0x0C, tonumber(val, 16))
          elseif key == "caps" then                -- (the army boost's numbers, for this unit)
            local clp = emu:read32(u + 4)
            local cap = {}
            for k = 0, 5 do cap[k] = s8(emu:read8(clp + 0x13 + k)) end
            emu:write8(u + 0x12, cap[0]); emu:write8(u + 0x13, cap[0])
            for k = 1, 5 do emu:write8(u + 0x13 + k, cap[k]) end
            emu:write8(u + 0x19, 30)
            editCaps = true
          elseif key:match("^r[0-7]$") then
            emu:write8(u + 0x28 + tonumber(key:sub(2)), tonumber(val))
          elseif key:match("^i[1-5]$") then
            emu:write16(u + 0x1E + 2 * (tonumber(key:sub(2)) - 1), tonumber(val, 16))
          else
            assert(EDIT_U8[key], "edit: unknown field " .. key)
            emu:write8(u + EDIT_U8[key], tonumber(val) & 0xFF)
          end
          done[#done + 1] = key .. "=" .. val
        end
        log[#log + 1] = string.format("edit unit %02x char %02x: %s", emu:read8(u + 0x0B), emu:read8(emu:read32(u) + 4),
          table.concat(done, " "))
        print("sram_boost: " .. log[#log]); io.stdout:flush()
      end
    end
  end
  f:close()
end

local function boostLevel()
  for i = 0, 61 do
    local u = BLUE + i * 0x48
    local cp, clp = emu:read32(u), emu:read32(u + 4)
    if cp ~= 0 and clp ~= 0 and emu:read8(cp + 4) == lvChar then
      log[#log + 1] = string.format("unit %02x char %02x class %02x: level %d exp %d -> level %d exp %d", emu:read8(u + 0x0B),
        lvChar, emu:read8(clp + 4), emu:read8(u + 8), emu:read8(u + 9), lvLevel, lvExp)
      emu:write8(u + 8, lvLevel); emu:write8(u + 9, lvExp)
      if os.getenv("BOOST_CAPS") == "1" then   -- (and that unit's stats to its class caps, as the army boost)
        local cap = {}
        for k = 0, 5 do cap[k] = s8(emu:read8(clp + 0x13 + k)) end
        emu:write8(u + 0x12, cap[0]); emu:write8(u + 0x13, cap[0])
        for k = 1, 5 do emu:write8(u + 0x13 + k, cap[k]) end
        log[#log] = log[#log] .. string.format(", hp %d pow %d skl %d spd %d def %d res %d (class caps)", cap[0], cap[1], cap[2], cap[3], cap[4], cap[5])
      end
      print("sram_boost: " .. log[#log]); io.stdout:flush()
    end
  end
  assert(#log > 0, "BOOST_LEVEL: no blue unit with character " .. tostring(lvChar))
end
local function boostItems()
  for i = 0, 61 do
    local u = BLUE + i * 0x48
    local cp, clp = emu:read32(u), emu:read32(u + 4)
    local ids = (cp ~= 0 and clp ~= 0) and kit[emu:read8(cp + 4)] or nil
    if ids and (emu:read32(u + 0x0C) & 0x4) == 0 then
      local before, after = {}, {}
      for k = 0, 4 do
        local it = emu:read16(u + 0x1E + 2 * k)
        if it ~= 0 then before[#before + 1] = string.format("%02x:%d", it & 0xFF, it >> 8) end
        local id = ids[k + 1]
        local v = 0
        if id then v = id | (emu:read8(ITEMDATA + id * 0x24 + 0x14) << 8); after[#after + 1] = string.format("%02x:%d", id, v >> 8) end
        emu:write16(u + 0x1E + 2 * k, v)
      end
      log[#log + 1] = string.format("unit %02x char %02x class %02x: items %s -> %s", emu:read8(u + 0x0B), emu:read8(cp + 4),
        emu:read8(clp + 4), table.concat(before, " "), table.concat(after, " "))
      print("sram_boost: " .. log[#log]); io.stdout:flush()
    end
  end
  assert(#log > 0, "BOOST_ITEMS: no living blue unit with those characters")
end
local function boost()
  if lvChar then return boostLevel() end
  if kit then return boostItems() end
  if editPath ~= "" then edit() end
  if not boostStats then return end
  for i = 0, 61 do
    local u = BLUE + i * 0x48
    local cp, clp = emu:read32(u), emu:read32(u + 4)
    local state = emu:read32(u + 0x0C)
    if cp ~= 0 and clp ~= 0 and (state & 0x4) == 0 and emu:read8(u + 0x13) > 0 then   -- not US_DEAD
      local before = string.format("hp %d/%d pow %d skl %d spd %d def %d res %d lck %d", emu:read8(u + 0x13),
        emu:read8(u + 0x12), emu:read8(u + 0x14), emu:read8(u + 0x15), emu:read8(u + 0x16), emu:read8(u + 0x17),
        emu:read8(u + 0x18), emu:read8(u + 0x19))
      local cap = {}
      for k = 0, 5 do cap[k] = s8(emu:read8(clp + 0x13 + k)) end   -- maxHP, maxPow, maxSkl, maxSpd, maxDef, maxRes
      emu:write8(u + 0x12, cap[0]); emu:write8(u + 0x13, cap[0])
      for k = 1, 5 do emu:write8(u + 0x13 + k, cap[k]) end
      emu:write8(u + 0x19, 30)
      local uses = ""
      if boostUses then
        for k = 0, 4 do
          local it = emu:read16(u + 0x1E + 2 * k)
          if it ~= 0 then
            local d = ITEMDATA + (it & 0xFF) * 0x24
            local full = emu:read8(d + 0x14)
            if (emu:read32(d + 0x08) & 0x8) == 0 and full > 0 then   -- not IA_UNBREAKABLE
              emu:write16(u + 0x1E + 2 * k, (it & 0xFF) | (full << 8))
              uses = uses .. string.format(" %02x:%d->%d", it & 0xFF, it >> 8, full)
            end
          end
        end
      end
      log[#log + 1] = string.format("unit %02x char %02x class %02x lv %d: %s -> hp %d pow %d skl %d spd %d def %d res %d lck 30%s",
        emu:read8(u + 0x0B), emu:read8(cp + 4), emu:read8(clp + 4), emu:read8(u + 8), before,
        cap[0], cap[1], cap[2], cap[3], cap[4], cap[5], uses)
      print("sram_boost: " .. log[#log]); io.stdout:flush()
    end
  end
end

local frame, tick = 0, 0
if cheat then cheat.tick = function() return tick end end   -- (named by the cheat's refusal, test_cheat.lua)
-- The .json's "cheat" (above): this run's cheats and the start save's mark, in the cheats' order.
local function cheatMark()
  local names, out = {}, {}
  local function add(s) for n in (s or ""):gmatch("[^+]+") do names[n] = true end end
  add(cheat and cheat.name)
  local p = os.getenv("GATE0_SRAM_IN")
  local j = p and p ~= "" and io.open(p .. ".json", "r")
  if j then add(j:read("a"):match('"cheat":"([^"]*)"')); j:close() end
  for _, n in ipairs({ "durability", "funds", "convoy" }) do
    if names[n] then out[#out + 1] = n; names[n] = nil end
  end
  assert(next(names) == nil, "GATE0_SRAM_IN's .json names an unknown test cheat: " .. tostring(next(names)))
  return #out > 0 and table.concat(out, "+") or nil
end
local function check(t)
  if t == boostAt then boost() end
  if t + 1 == arg then
    local mark = cheatMark()
    local s = assert(io.open(outPath, "wb"), "cannot open " .. outPath)
    s:write(emu:readRange(SRAM, SRAM_SIZE)); s:close()
    local m = assert(io.open(outPath .. ".json", "w"), "cannot open " .. outPath .. ".json")
    m:write(string.format('{"what":"%s start (test coverage only, not story play)",' ..
      (lvChar and ('"boost_level":"' .. levelSpec .. '",') or '') .. (kit and ('"boost_items":"' .. itemSpec .. '",') or '') ..
      '"keys":"%s","boost_at":%d,' ..
      '"boost_uses":%s,"sram_at":"%s","tick":%d,"frame":%d,"chapterIndex":%d,"turn":%d,"units":[%s]%s}\n',
      editPath ~= "" and ((boostStats or editCaps) and "boosted and edited" or "edited") or "boosted",
      keysPath, boostAt, boostUses and "true" or "false", at, t, frame + 1, s8(emu:read8(PLAYST + 0x0E)),
      emu:read16(PLAYST + 0x10), '"' .. table.concat(log, '","') .. '"',
      mark and (',"cheat":"' .. mark .. '"') or ""))
    m:close()
    print(string.format("sram_boost: the SRAM at the end of tick %d (frame %d, chapterIndex %d turn %d) -> %s",
      t, frame + 1, s8(emu:read8(PLAYST + 0x0E)), emu:read16(PLAYST + 0x10), outPath))
    io.stdout:flush()
    os.exit(0)
  end
end

emu:setKeys(maskFor(0))
local outerWait, earlyFlush = false, false
callbacks:add("gate0_onvblank", function()
  if emu:read8(MAIN_LOOP_ENDED) ~= 0 and not outerWait then earlyFlush = true end
  outerWait = false
end)
callbacks:add("gate0_wait", function()
  if emu:read8(MAIN_LOOP_ENDED) == 0 then
    if not earlyFlush then return end
    earlyFlush = false
  else
    outerWait = true
  end
  check(tick)
  if cheat then cheat.atSample() end             -- the test cheat, after the tick's sample (README.md, "Test cheats")
  tick = tick + 1
  emu:setKeys(maskFor(tick))
end)
callbacks:add("frame", function() frame = frame + 1 end)
