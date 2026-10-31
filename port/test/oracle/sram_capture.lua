-- Chapter saves (port/test/README.md, "Chapter saves"): play a key file on
-- fireemblem8.gba in mGBA and write the game's SRAM (32 KiB, FE8's whole
-- save area, D-18) at a chosen point, so a later Gate 0 run can boot both the
-- GBA and the PC from it (GATE0_SRAM_IN) instead of replaying from power-on.
--
--   MGBA_RUN_BP=gate0_onmain@0x080152F4,gate0_wait@0x080D16DC,gate0_onvblank@0x080152A4 \
--   GATE0_KEYS=port/test/inputs/route_ch1.keys SRAM_AT=save:2 SRAM_OUT=port/test/saves/ch2.sav \
--     build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/sram_capture.lua 3000000
--
-- (make -f port/test/Makefile sram-capture does this.)
--
-- SRAM_AT picks the point:
--   tick:T     after T ticks (ticks 0 to T-1 played): the bytes gate0_capture.lua
--              writes with GATE0_TICKS=T and GATE0_SRAM (its first 32 KiB).
--   save:C     the end of the first tick in which the game writes a game save
--              (slots 0-2) whose gPlaySt.chapterIndex is C: a chapter save,
--              which the title menu's Continue loads (the routes write one at
--              each chapter change, README-routes.md "Milestones").
--   suspend:C  the SRAM right after the last suspend save the game wrote while
--              gPlaySt.chapterIndex was C, written once chapterIndex changes:
--              the title menu's "Resume Chapter" then starts at the last
--              action of chapter C (for chapter 1 of the routes, Eirika's
--              seize, just before the Castle Frelia scene).
-- Ticks and keys are exactly gate0_capture.lua's: a tick is one completed
-- OnMain, sampled at its VBlankIntrWait with gBmSt.main_loop_ended set (after
-- an early flush VBlank, at the main loop's VBlankIntrWait that follows it;
-- README.md, "Early flush VBlank"), and the mask for tick t+1 is set right
-- after tick t is sampled.
--
-- Output: SRAM_OUT (32 KiB, the usual .sav size for FE8) and SRAM_OUT.json
-- (where it was taken). Both are game-save data made from play with the ROM:
-- keep them out of git (port/test/saves/ ignores everything but its README).
--
-- GATE0_CHEAT=durability, funds, convoy or several joined with "+" (README.md,
-- "Test cheats"): the test cheats at every tick's sample, as gate0_capture.lua
-- has them (oracle/test_cheat.lua), for a save taken from a cheat-assisted
-- route (port/test/Makefile chain-save passes the key file's own cheat). The
-- .json then says "cheat". It says so too when the start save's .json
-- (GATE0_SRAM_IN.json) does, with or without a cheat in this run: "cheat" is
-- every cheat of the run and of the saves before it, so the mark goes on
-- through every later save of a chain.

local keysPath = assert(os.getenv("GATE0_KEYS"), "GATE0_KEYS: the key file to play")
local at = assert(os.getenv("SRAM_AT"), "SRAM_AT: tick:T, save:C or suspend:C")
local outPath = assert(os.getenv("SRAM_OUT"), "SRAM_OUT: the .sav to write")
local rule, arg = at:match("^(%a+):(%d+)$")
assert(rule == "tick" or rule == "save" or rule == "suspend", "bad SRAM_AT '" .. at .. "' (tick:T, save:C, suspend:C)")
arg = tonumber(arg)
assert(rule ~= "tick" or arg >= 1, "SRAM_AT=tick:T needs T >= 1")

local bp = os.getenv("MGBA_RUN_BP") or ""
assert(bp:find("gate0_wait@", 1, true), "MGBA_RUN_BP must set the gate0_wait breakpoint (port/test/Makefile)")
assert(bp:find("gate0_onvblank@", 1, true), "MGBA_RUN_BP must set the gate0_onvblank breakpoint (port/test/Makefile)")
local cheat = (os.getenv("GATE0_CHEAT") or "") ~= ""
  and dofile(os.getenv("GATE0_CHEAT_LIB") or "port/test/oracle/test_cheat.lua") or nil
if cheat then
  cheat.name = cheat.check(os.getenv("GATE0_CHEAT"), "GATE0_CHEAT")
  cheat.auto()
end

local MAIN_LOOP_ENDED = 0x0202BCB0               -- gBmSt.main_loop_ended
local PLAYST = 0x0202BCF0                        -- gPlaySt: +0x0E chapterIndex (s8), +0x10 chapterTurnNumber
local SRAM, SRAM_SIZE = 0x0E000000, 0x8000
local BLOCKINFO = SRAM + 0x64                    -- struct SaveBlockInfo[7], 0x10 bytes each (include/bmsave.h)
local SAVEMAGIC16 = 0x200A

local function readKeys(path)                    -- as gate0_capture.lua (README.md, "Input file")
  local masks = {}
  local f = assert(io.open(path, "r"), "cannot open key file " .. path)
  local lineNo = 0
  for line in f:lines() do
    lineNo = lineNo + 1
    line = line:gsub("#.*", ""):match("^%s*(.-)%s*$")
    if line ~= "" then
      local m, n = line:match("^(%x+)%*(%d+)$")
      if not m then m, n = line:match("^(%x+)$"), "1" end
      assert(m, string.format("%s:%d: bad entry '%s'", path, lineNo, line))
      local mask = tonumber(m, 16)
      assert(mask <= 0x3FF, string.format("%s:%d: mask %s is wider than 10 bits", path, lineNo, m))
      for _ = 1, tonumber(n) do masks[#masks + 1] = mask end
    end
  end
  f:close()
  return masks
end

local masks = readKeys(keysPath)
local function maskFor(t) return masks[t + 1] or 0 end

local function chapter()
  local c = emu:read8(PLAYST + 0x0E)
  return c >= 0x80 and c - 0x100 or c
end

-- One SaveBlockInfo: valid when magic16 is right (the game also checks magic32
-- and the checksum; port/test/tools/sram_info.py checks all of it).
local function blockValid(blocks, i)
  local b = i * 0x10
  local magic16 = blocks:byte(b + 5) | (blocks:byte(b + 6) << 8)
  return magic16 == SAVEMAGIC16
end

local function blockChapter(blocks, i)
  local b = i * 0x10
  local offset = blocks:byte(b + 9) | (blocks:byte(b + 10) << 8)
  local c = emu:read8(SRAM + offset + 0x0E)     -- the saved gPlaySt.chapterIndex
  return c >= 0x80 and c - 0x100 or c
end

local frame, tick = 0, 0
if cheat then cheat.tick = function() return tick end end   -- (named by the cheat's refusal, test_cheat.lua)
local lastBlocks, snapshot, snapTick, snapFrame, snapTurn

-- The .json's "cheat" (the header): this run's cheats and the start save's mark, in the cheats' order.
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

local function write(bytes, t, f, why)
  local s = assert(io.open(outPath, "wb"), "cannot open " .. outPath)
  s:write(bytes)
  s:close()
  local mark = cheatMark()
  local m = assert(io.open(outPath .. ".json", "w"), "cannot open " .. outPath .. ".json")
  m:write(string.format('{"keys":"%s","sram_at":"%s","tick":%d,"frame":%d,"what":"%s"%s}\n',
    keysPath, at, t, f, why, mark and (',"cheat":"' .. mark .. '"') or ""))
  m:close()
  print(string.format("sram_capture: %s: %s, the SRAM at the end of tick %d (frame %d) -> %s%s%s",
    at, why, t, f, outPath, cheat and (" (cheat-assisted: GATE0_CHEAT=" .. cheat.name .. ")") or "",
    mark and mark ~= (cheat and cheat.name) and (" (made with the test cheat: \"cheat\":\"" .. mark .. "\", from "
      .. "its start save)") or ""))
  io.stdout:flush()
  os.exit(0)
end

-- Tick t has just ended (the same sample point as gate0_capture.lua).
local function check(t)
  local blocks = emu:readRange(BLOCKINFO, 0x70)
  local changed = lastBlocks ~= nil and blocks ~= lastBlocks
  if rule == "tick" and t + 1 == arg then
    write(emu:readRange(SRAM, SRAM_SIZE), t, frame + 1, string.format("after %d ticks, chapterIndex %d turn %d",
      arg, chapter(), emu:read16(PLAYST + 0x10)))
  elseif rule == "save" and changed then
    for i = 0, 2 do
      if blocks:sub(i * 0x10 + 1, i * 0x10 + 0x10) ~= lastBlocks:sub(i * 0x10 + 1, i * 0x10 + 0x10)
          and blockValid(blocks, i) and blockChapter(blocks, i) == arg then
        write(emu:readRange(SRAM, SRAM_SIZE), t, frame + 1,
          string.format("game save slot %d written with chapterIndex %d", i, arg))
      end
    end
  elseif rule == "suspend" then
    local c = chapter()
    if changed and c == arg and blocks:sub(0x31, 0x50) ~= lastBlocks:sub(0x31, 0x50)
        and (blockValid(blocks, 3) or blockValid(blocks, 4)) then
      snapshot, snapTick, snapFrame, snapTurn = emu:readRange(SRAM, SRAM_SIZE), t, frame + 1, emu:read16(PLAYST + 0x10)
    elseif c ~= arg and snapshot ~= nil then
      write(snapshot, snapTick, snapFrame, string.format(
        "last suspend save of chapterIndex %d (turn %d; chapterIndex is %d from tick %d)", arg, snapTurn, c, t))
    end
  end
  lastBlocks = blocks
end

emu:setKeys(maskFor(0))

-- An early flush VBlank (as gate0_capture.lua): OnVBlank with main_loop_ended
-- set before the main loop's VBlankIntrWait. That wait, entered with the flag
-- clear, then ends the tick.
local outerWait, earlyFlush = false, false
callbacks:add("gate0_onvblank", function()
  if emu:read8(MAIN_LOOP_ENDED) ~= 0 and not outerWait then earlyFlush = true end
  outerWait = false
end)

callbacks:add("gate0_wait", function()
  if emu:read8(MAIN_LOOP_ENDED) == 0 then
    if not earlyFlush then return end              -- a wait inside OnMain, or boot
    earlyFlush = false                             -- the main loop's wait after an early flush
  else
    outerWait = true
  end
  check(tick)
  if cheat then cheat.atSample() end             -- the test cheat, after the tick's sample (README.md, "Test cheats")
  tick = tick + 1
  emu:setKeys(maskFor(tick))
end)

callbacks:add("frame", function()
  frame = frame + 1
end)
