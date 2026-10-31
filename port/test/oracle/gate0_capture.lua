-- Gate 0, stage 4: per-logic-tick capture from fireemblem8.gba in mGBA.
--
--   MGBA_RUN_BP=<GBA_BP in port/test/Makefile: the eight breakpoints below> \
--   GATE0_KEYS=port/test/inputs/boot300.keys GATE0_TICKS=300 \
--   GATE0_OUT=build/gate0/gba.jsonl GATE0_SHOTDIR=build/gate0/gba_frames \
--     build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/gate0_capture.lua 100000
--
-- Trace format: port/test/README.md, "Trace". The PC hook
-- (port/test/pc/gate0_hook.c) writes the same format.
--
-- The runner (oracle/mgba_lua_run.c) sets libmgba debugger breakpoints and
-- calls back here each time the CPU is about to run the instruction there:
--   gate0_onmain    OnMain (0x080152F4): counts logic ticks started.
--   gate0_wait      VBlankIntrWait (0x080D16DC): counts the game's VBlank
--                   waits, and is where a tick is sampled.
--   gate0_onvblank  OnVBlank (0x080152A4): counts VBlank handler runs, which
--                   is what gGameClock counts (IncrementGameClock, bm.c:307).
-- and six for the lag position (README.md, "Lag position"): the main-loop
-- events, counted per tick, that both sides can count in the same order, and
-- one place inside an event's call (gate0_rpscmd).
--   gate0_rpr       RunProcessRecursive (0x08002E28): a proc tree node is
--                   entered (proc.c:212).
--   gate0_rpr_end   RunProcessRecursive's only epilogue (0x08002E7E, both
--                   returns go there): that node's step (and subtree) is done.
--   gate0_rps       RunProcessScript (0x08003394): a proc script runs, from
--                   Proc_Start or a tree walk (proc.c:84, 221). A lag VBlank
--                   after it and before the script's first command (the
--                   "head") gets "lag_head" [i]: the PC runs it at
--                   RunProcessScript's entry (lagHeadAt below; cov9 A2).
--   gate0_rpscmd    RunProcessScript+0x3C (0x080033D0), after the call of a
--                   command: not an event; it tells the loop's later passes
--                   from its head.
--   gate0_clock     GetGameClock's bx lr (0x08000D2C): a clock read, the
--                   value already in r0.
--   gate0_settime   SetGameTime's bx lr (0x08000D38): a clock write, done.
-- and the BIOS copy and decompression calls whose destination (r1) is video
-- memory (palette RAM, VRAM or OAM, 0x05000000-0x07FFFFFF), made outside
-- OnVBlank: the libagbsyscall wrappers' swi (gate0_cpuset 0x080D1678,
-- gate0_cpufastset 0x080D1674, gate0_lz77wram 0x080D1694, gate0_lz77vram
-- 0x080D1690; RLUnComp* and HuffUnComp, which only Decompress's table names
-- and no route calls, are left out on both sides). How a lag VBlank can fall
-- inside one depends on the call (mGBA 0.10.5's HLE BIOS, checked on its BIOS
-- image, port/test/README.md "Lag position", kind 6):
--   LZ77UnCompWram/Vram: the SWI table sends them to the HLE in C, which
--     writes all of the data at once, then the BIOS stall loop (0x348) runs
--     with the caller's IRQ bit: a lag VBlank in the call comes after every
--     write, and the replay, which runs it before the next event, does too.
--   CpuSet (BIOS 0x214) and CpuFastSet (0x2A8): ARM copy loops in the BIOS
--     image, run in System mode with the caller's IRQ bit, so a lag VBlank can
--     come between any two of their instructions, part way through the copy.
--     For such a VBlank the tick line gets "lag_split" [i, written, read]: lag
--     VBlank i (of "lag_at") came after `written` bytes were stored and
--     `read` bytes of the source were loaded (CpuFastSet loads 8 words, then
--     stores them; a fill loads its value once, read 0 before that load), from
--     the interrupted registers (runner_irq_sp: the BIOS IRQ vector's push;
--     runner_irq_reg: r4-r11 as the runner kept them at that IRQ's entry, also
--     when the VBlank came inside a line handler, H-col-2).
--     The PC's lag replay splits the call there (port/test/pc/gate0_hook.c).
-- A lag VBlank inside a main-loop BIOS copy that is not an event (another
-- destination) is listed in "lag_in_other_copy" (GBA only, not compared): the
-- replay runs it at the next event, after the copy.
-- The tree-0 tasks that copy into VRAM themselves
-- (BMapVSync_UpdateMapImgAnimations) are ordered against these events. "Made
-- by the main loop" is: no IRQ frame on the IRQ stack (runner_irq_sp() is
-- __sp_irq, 0x03007FA0), so the calls of every IRQ handler (OnVBlank's flush
-- and tree-0 tasks, the HBlank and VCount handlers) are left out, as the PC
-- leaves out every call made while gPortIrqDepth is not 0.
-- and a line handler's hand-over: gate0_flipscroll, FlipBgVerticalScroll's
-- bx lr (0x08014EB8, the flip already stored), which gives
-- HBlank_BgVerticalScroll the next frame's scroll table; the handler takes it
-- at line 159 of every frame, lag frames too, and the PC's lag replay runs a
-- lag frame's line interrupts.
--
-- Raster-timing frames (README.md, "Raster-timing frames"): with the runner's
-- raster log (MGBA_RUN_RASTERLOG, oracle/mgba_lua_run.c) each frame15 tick line
-- gets "raster", the CRC-32 of that frame's block in the log (the line events
-- and the watched stores of the frame), and the header "raster_log", the
-- watched regions.
-- Events inside OnVBlank (its tree-0 Proc_Run and the clock reads of tree-0
-- procs) are not main-loop events. A lag VBlank (OnVBlank entered with
-- gBmSt.main_loop_ended == 0 and no inner VBlankIntrWait pending) is recorded
-- as the number of main-loop events of its tick that came before it.
--
-- Early flush VBlank (README.md, "Early flush VBlank"): OnVBlank entered with
-- main_loop_ended == 1 before the main loop's VBlankIntrWait, that is, in the
-- few instructions between `gBmSt.main_loop_ended = 1` and the call
-- (`gBmSt.prevVCount = REG_VCOUNT`; OnMain, MainUpdate_8055C68 and
-- InBattleMainRoutine all end that way). It flushes and clears the flag, so
-- the main loop's VBlankIntrWait is entered with main_loop_ended == 0 and
-- waits for one more VBlank, which does not flush. Such a VBlank is recorded
-- as a lag VBlank at the tick's last event ("lag_at"), the tick line gets
-- "early_flush":1 (that last "lag_at" entry is it), and the tick is sampled at
-- that following VBlankIntrWait call. Route ch2 has one at tick 28,865 and
-- route ch5 two more (75,618, 108,956).
-- The IRQ can also be taken at the call itself: the gate0_wait breakpoint
-- (VBlankIntrWait's first instruction, `movs r2, #0`) is reported, and the IRQ
-- comes before that instruction runs (the runner's "gate0_wait_irq"). The call
-- has not been entered then either: when that IRQ's OnVBlank flushes, it is an
-- early flush as above (the BIOS's IntrWait, entered after it, discards that
-- VBlank's flag and waits for the next one). The sample taken at the report is
-- withdrawn (waitUndo, before anything else ran) and taken again when the CPU
-- is back at the call ("gate0_wait_resume"), so the tick is recorded as any
-- early flush. Route ch7 has one at tick 41,152 (H-story-2, 2026-10-22: the IRQ
-- at 0x080D16DC, after the frame callback of that VBlank; the tick had been
-- sampled before the flush, and VBlank 43,301, the one its wait returned on,
-- was taken for a lag VBlank of tick 41,153 at event 0, so the sound index of
-- tick 41,152 was one record off the PC's). An IRQ there whose handler does
-- not flush (HBlank, VCount) leaves the sample as it is.
--
-- Logic tick: one completed OnMain. OnMain sets gBmSt.main_loop_ended and then
-- calls VBlankIntrWait; OnVBlank clears the flag after flushing the shadow
-- state. A tick is sampled at the VBlankIntrWait call made with
-- main_loop_ended == 1, before the call runs: the same program point as the
-- PC hook (__wrap_VBlankIntrWait). After an early flush VBlank (above) it is
-- sampled at the main loop's VBlankIntrWait call that follows it; the PC's
-- lag replay runs that OnVBlank at its sample point first, so both sides
-- sample after the flush. The JSON line is written at the next
-- "frame" callback (the VBlank that call waits for), so frame15 can be the
-- screenshot of that frame, as before.
--
-- Keys: the mask for tick t+1 is set right after tick t is sampled (tick 0's
-- before the first frame), so OnMain t+1 reads it in UpdateKeyStatus.
--
-- Frame mode (README.md, "TAS route"): GATE0_FRAME_KEYS=FILE plays FILE as a
-- movie player does, one entry per emulated FRAME instead of per tick:
-- interval n (power-on to the first "frame" callback for n = 0, then callback
-- n to callback n+1) plays entry n + GATE0_FRAME0 (default 0; an entry before
-- the file's start is 000), set at callback n, VBlank start, as movie_check.lua
-- does. The key file named in the header is FILE. Each tick line's "keys" is
-- then the mask the game read in that tick (REG_KEYINPUT at UpdateKeyStatus's
-- load, the gate0_keyread breakpoint, which frame mode needs), and
-- "key_reads" counts the reads (a tick with no read repeats the last mask;
-- "key_reads_differ":1 marks a tick whose reads saw different masks, which a
-- tick key file cannot replay). The boot-time read before tick 0 is checked
-- against tick 0's ("boot_keys" in the header's end line on stdout).
-- tools/tas_route.py turns such a trace into a tick key file.
--
-- Save states (README.md, "Save states"): GATE0_STATE_OUT=DIR and
-- GATE0_STATE_AT=T[,T...] save DIR/tick<T>/gba/ at the "frame" callback that
-- writes tick T-1's line: mGBA's state with its SRAM (the runner's
-- runner_save_state), every counter of this script (capture.lua), tick T-1's
-- line and raw record (prologue.*) and meta.json. GATE0_STATE_IN=DIR/tick<T>
-- resumes from there: the runner loads the state before the first
-- instruction (MGBA_RUN_STATE_IN, set by port/test/Makefile), this script
-- takes its counters back, writes the header with a "resume" block and tick
-- T-1's line and record, then goes on with tick T.
--
-- Sound (optional; README.md, "Sound on any route"): SND_OUT=FILE writes one
-- sound record per "frame" callback, the 8,236-byte record of
-- sound_capture.lua (line 160, before the VBlank IRQ; record v is taken at
-- callback v, v counted from power-on, the same count as "frame" - 1), except
-- that its u16 at bytes 6-7 is the OnMain entries so far (mod 65536), what the
-- PC record holds there (port/audio/fifo_pc.c). SND_CALLS=FILE (with
-- SND_M4A_CODE and the snd_NAME@ADDR:ret breakpoints of audio/snd_calls.py
-- setup-gate0 in MGBA_RUN_BP) writes the m4a calls between the records, as
-- sound_capture.lua does: the same rule for top-level calls, windows and
-- "clean". SND_INDEX=FILE writes "TICK V" for each tick line: V is the
-- record taken at the callback that writes the line (the VBlank the tick
-- waits for). The keys and the ticks are this script's own, so the records
-- follow any route or monkey key file exactly as the trace does. With
-- SND_CALLS the trace also gets sound positions: the header has "lag_snd":1
-- and each tick with "lag_at" has "lag_snd", per lag VBlank the number of the
-- main loop's top-level m4a calls (no IRQ frame) since the event its "lag_at"
-- entry counts up to (or since the previous sample); a call whose entry the
-- IRQ came at, before its first instruction, is not counted. The PC's lag
-- replay runs each such VBlank before the main loop's next m4a call after
-- them, as on the GBA (an m4a call is no main-loop event, so "lag_at" alone
-- cannot place it: route_ch1 has four such ticks, README.md "Sound on any
-- route"). A lag VBlank whose IRQ came inside an IRQ handler's top-level m4a
-- call (an OnVBlank that ran into the next frame, its m4aSoundMain still
-- mixing: IrqMain runs every handler with IRQs on) gets "lag_snd_nested"
-- [i, locks], the driver's locks its handler found (as in "lag_snd_split"):
-- its m4aSoundMain finds SoundMain locked and mixes nothing (route_e15 tick
-- 1,301). The PC's lag replay runs it with those locks set. A resumed
-- capture writes the record of its start point first (the photo's own
-- callback, the last record of the run before it) and a "# resumed" line in
-- SND_CALLS; no m4a call is open there (the main loop is halted in
-- VBlankIntrWait and the VBlank IRQ has not come).
--
-- Test cheats (README.md, "Test cheats"; test coverage only, never a played
-- build): GATE0_CHEAT=durability, funds, convoy or several joined with "+"
-- (every item's uses back to full; the party's gold at 999,999; a rolling
-- convoy) at each tick's sample, after the sample is taken
-- (oracle/test_cheat.lua; the PC hook does the same at the same point). The
-- header gets "cheat", each tick line "cheat_items" and the cheats' own fields
-- ("cheat_refills", "cheat_funds", "cheat_convoy"). A sample taken back (an
-- early flush at the call, above) takes the cheats' changes back too, before
-- that OnVBlank runs. A save state keeps the convoy cursor (capture.lua).
-- Refused in Eirika's Chapter 20 and final chapter (chapterIndex 0x14 to 0x16
-- before the game is complete: the run stops there, test_cheat.lua). Unset or
-- empty: nothing of it runs.

local keysPath = os.getenv("GATE0_KEYS") or "port/test/inputs/boot300.keys"
local ticks = tonumber(os.getenv("GATE0_TICKS") or "300")
local outPath = os.getenv("GATE0_OUT") or "build/gate0/gba.jsonl"
local rawPath = os.getenv("GATE0_RAW")          -- optional: raw bytes of the hashed buffers
local shotDir = os.getenv("GATE0_SHOTDIR")      -- optional: PNG every 60 ticks for frame15
local sramPath = os.getenv("GATE0_SRAM")        -- optional: the 64 KiB at 0x0E000000 at the end
-- optional: the test cheat (see the top), its rule in oracle/test_cheat.lua (cheat.name: its name)
local cheat = (os.getenv("GATE0_CHEAT") or "") ~= ""
  and dofile(os.getenv("GATE0_CHEAT_LIB") or "port/test/oracle/test_cheat.lua") or nil
if cheat then cheat.name = cheat.check(os.getenv("GATE0_CHEAT"), "GATE0_CHEAT") end
local frameEvery = 60
-- Optional extra frame15 ticks, "a-b,c,..." (the PC hook reads the same variable).
local shotRanges = {}
for a, b in (os.getenv("GATE0_SHOT_TICKS") or ""):gmatch("(%d+)%-?(%d*)") do
  shotRanges[#shotRanges + 1] = { tonumber(a), tonumber(b ~= "" and b or a) }
end
local function isShotTick(t)
  if t % frameEvery == 0 then return true end
  for _, r in ipairs(shotRanges) do
    if t >= r[1] and t <= r[2] then return true end
  end
  return false
end

local bp = os.getenv("MGBA_RUN_BP") or ""
local biosBreakpoints = { "gate0_cpuset", "gate0_cpufastset", "gate0_lz77wram", "gate0_lz77vram" }
for _, name in ipairs({ "gate0_onmain", "gate0_wait", "gate0_onvblank", "gate0_rpr", "gate0_rpr_end",
                        "gate0_rps", "gate0_rpscmd", "gate0_clock", "gate0_settime", "gate0_flipscroll",
                        table.unpack(biosBreakpoints) }) do
  assert(bp:find(name .. "@", 1, true), "MGBA_RUN_BP must set the " .. name .. " breakpoint (port/test/Makefile)")
end
-- Optional: one line per lag VBlank with the event it followed (diagnostic).
local lagLogPath = os.getenv("GATE0_LAGLOG")
-- Raster-timing frames: the runner's raster log (see the top).
local rasterLog = (os.getenv("MGBA_RUN_RASTERLOG") or "") ~= ""
local rasterHeader = ""
if rasterLog then
  local ram = os.getenv("MGBA_RUN_RASTER_RAM") or ""
  rasterHeader = string.format(',"raster_log":"pltt@0x05000000+0x400,vram@0x06000000+0x18000,oam@0x07000000+0x400%s"',
    ram ~= "" and ("," .. ram) or "")
end

-- Addresses: fireemblem8.map / sym_iwram.txt / fireemblem8.elf of the pinned
-- commit (PLAN.md Gate 0; the static RNG and key buffers from the ELF).
local MAIN_LOOP_ENDED = 0x0202BCB0              -- gBmSt.main_loop_ended
local PROC_TREE_0 = 0x02026A70                  -- gProcTreeRootArray[0], what OnVBlank's Proc_Run gets
local scalars = {
  { "gGameClock",          0x03000014, 4 },
  { "gKeyStatusIgnoredSt", 0x03000010, 2 },
  { "gKeyComboResetEN",    0x0300000C, 1 },
  { "gSoftwareResetFlag",  0x0300001A, 1 },
  { "gMainCallback",       0x02024CB8, 4 },
  { "gLCGRNValue",         0x03000008, 4 },     -- rng.c, static int
}
local raws = {                                   -- small structs, kept as hex
  { "gBmSt",            0x0202BCB0, 0x40 },
  { "gPlaySt",          0x0202BCF0, 0x4C },
  { "gRNSeeds",         0x03000000, 0x06 },     -- rng.c, static u16[3]
  { "sKeyStatusBuffer", 0x02024CC0, 0x14 },     -- hardware.c, static struct KeyStatusBuffer
}
local hashed = {                                 -- FNV-1a 32 of the bytes
  { "gLCDControlBuffer", 0x03003080, 0x69 },     -- struct LCDControlBuffer, 0x00-0x68
  { "gPaletteBuffer",    0x020228A8, 0x400 },
  { "gBG0TilemapBuffer", 0x02022CA8, 0x800 },
  { "gBG1TilemapBuffer", 0x020234A8, 0x800 },
  { "gBG2TilemapBuffer", 0x02023CA8, 0x800 },
  { "gBG3TilemapBuffer", 0x020244A8, 0x800 },
  { "gOam",              0x03003140, 0x400 },    -- 128 OAM entries
}
-- FNV-1a 32 only, no raw dump (integration 11; port/test/README.md, "Trace"): the trap pool,
-- bmtrap.c's file-static struct Trap sTrapPool[TRAP_MAX_COUNT] (address from the ELF). Round 10's
-- C10-10 (a) (RemoveTrap) changed it with no traced field showing it.
local hashedOnly = {
  { "sTrapPool",         0x0203A614, 0x200 },
}

local function readKeys(path)
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

-- Frame mode (see the top): GATE0_FRAME_KEYS replaces GATE0_KEYS.
local frameKeysPath = os.getenv("GATE0_FRAME_KEYS")
if frameKeysPath == "" then frameKeysPath = nil end
local frame0 = tonumber(os.getenv("GATE0_FRAME0") or "0")
local frameMasks
if frameKeysPath then
  keysPath = frameKeysPath
  frameMasks = readKeys(frameKeysPath)
  assert(bp:find("gate0_keyread@", 1, true), "GATE0_FRAME_KEYS needs the gate0_keyread breakpoint (MGBA_RUN_BP)")
  assert((os.getenv("GATE0_STATE_OUT") or "") == "" and (os.getenv("GATE0_STATE_IN") or "") == "",
    "GATE0_FRAME_KEYS: save states are made from the tick key file (tools/tas_route.py), not in frame mode")
  assert(not cheat, "GATE0_CHEAT goes with a tick key file (GATE0_KEYS), not with frame mode (GATE0_FRAME_KEYS)")
end
local function frameMaskFor(n) return frameMasks[n + frame0 + 1] or 0 end
local masks = frameKeysPath and {} or readKeys(keysPath)
local tickRead = nil                             -- frame mode: the mask this tick's reads saw (the last one)
local function maskFor(t)
  if frameKeysPath then return tickRead or 0 end
  return masks[t + 1] or 0
end

local function fnv1a(s)
  local h = 0x811C9DC5
  for i = 1, #s do
    h = ((h ~ s:byte(i)) * 0x01000193) & 0xFFFFFFFF
  end
  return h
end

local function tohex(s)
  return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end))
end

-- Save states (see the top).
local stateOut, stateIn = os.getenv("GATE0_STATE_OUT"), os.getenv("GATE0_STATE_IN")
if stateOut == "" then stateOut = nil end
if stateIn == "" then stateIn = nil end
assert(not (stateOut and stateIn), "GATE0_STATE_OUT and GATE0_STATE_IN together are not supported")
local stateAt = {}
if stateOut then
  for t in (os.getenv("GATE0_STATE_AT") or ""):gmatch("%d+") do stateAt[tonumber(t)] = true end
  assert(next(stateAt), "GATE0_STATE_OUT needs GATE0_STATE_AT=T[,T...]")
end

local crcTable = {}
for i = 0, 255 do
  local v = i
  for _ = 1, 8 do v = (v & 1) ~= 0 and (0xEDB88320 ~ (v >> 1)) or (v >> 1) end
  crcTable[i] = v
end
local function crc32(s)
  local c = 0xFFFFFFFF
  for i = 1, #s do c = crcTable[(c ~ s:byte(i)) & 0xFF] ~ (c >> 8) end
  return c ~ 0xFFFFFFFF
end
local function readFile(path)
  local f = assert(io.open(path, "rb"), "cannot open " .. path)
  local s = f:read("a")
  f:close()
  return s
end
local function jsonString(s)
  return '"' .. s:gsub('[\\"]', "\\%0"):gsub("[%c]", function(c) return string.format("\\u%04x", c:byte()) end) .. '"'
end
local keysCrc = string.format("%08x", crc32(readFile(keysPath)))
local romCrc = string.format("%08x", runner_rom_crc32 or 0)

-- Start from a save (README.md, "Chapter saves"): the runner gave the core
-- the first 32 KiB of GATE0_SRAM_IN before reset. Check that the game sees
-- exactly those bytes, and name them in the header ("sram_in", "sram_fnv").
-- A resumed run's SRAM comes from the state; GATE0_SRAM_IN may then only name
-- the save the snapshot's run booted from.
local sramIn, sramHeader = os.getenv("GATE0_SRAM_IN"), ""
if sramIn and sramIn ~= "" then
  local f = assert(io.open(sramIn, "rb"), "cannot open GATE0_SRAM_IN " .. sramIn)
  local want = f:read(0x8000)
  f:close()
  assert(want and #want == 0x8000, "GATE0_SRAM_IN " .. sramIn .. " is shorter than 32 KiB")
  assert(stateIn or emu:readRange(0x0E000000, 0x8000) == want,
    "SRAM at boot is not GATE0_SRAM_IN (the runner must load it: oracle/mgba_lua_run.c)")
  sramHeader = string.format(',"sram_in":"%s","sram_fnv":"%08x"', sramIn, fnv1a(want))
end

local out = assert(io.open(outPath, "w"), "cannot open " .. outPath)
local raw = rawPath and assert(io.open(rawPath, "wb"), "cannot open " .. rawPath)
local lagLog = lagLogPath and assert(io.open(lagLogPath, "w"), "cannot open " .. lagLogPath)

local frame, lastTickFrame, tick = 0, 0, 0
local onmain, waits, onvblank = 0, 0, 0
local pending = nil                              -- sampled tick waiting for its frame
if cheat then cheat.tick = function() return tick end end   -- (named by the cheat's refusal, test_cheat.lua)

local function fail(msg)
  io.stderr:write("gate0_capture: " .. msg .. "\n")
  io.stderr:flush()
  os.exit(5)
end

-- Lag position (README.md, "Lag position").
local events = 0          -- main-loop events of this tick so far
local lagAt = {}          -- this tick's lag VBlanks: events before each
local lagPrev = 0         -- lag counter (onvblank - (waits - 1)) at the previous sample
local depth = 0           -- RunProcessRecursive nesting, every context
local vbBase = nil        -- set while OnVBlank's tree-0 Proc_Run runs: the nesting it started at
local innerPending = false -- an inner VBlankIntrWait (main_loop_ended == 0) waits for the next VBlank
local preempt = 0         -- the last event's breakpoint was hit, but an IRQ came before its instruction
local outerWait = false   -- the main loop's VBlankIntrWait (main_loop_ended == 1) was entered: the next VBlank is its own
local earlyFlush = false  -- this tick's flush VBlank came before the main loop's VBlankIntrWait (an early flush)
local earlyFlushes = 0    -- early flush VBlanks so far
local waitUndo = nil      -- the main loop's sample at its VBlankIntrWait report, as it was before (see the top)
local waitRetake = false  -- that sample was withdrawn (an early flush at the call): take it at "gate0_wait_resume"
local waitRetakes = 0     -- early flush VBlanks taken at the call itself so far
local lastEvent = ""      -- GATE0_LAGLOG only: the last main-loop event
local biosCounted = false -- the last BIOS breakpoint report was a main-loop event
local lastBios = nil      -- the main loop's last BIOS copy call: { name, src, dest, control, counted }
local lagSplit = {}       -- this tick's lag VBlanks inside a counted CpuSet/CpuFastSet: "[i,written,read]"
local lagOther = {}       -- this tick's lag VBlanks inside a main-loop BIOS copy that is no event
local rpsHead = false     -- the last main-loop event is a RunProcessScript entry and none of its commands has returned
local rpsProc = 0         -- that RunProcessScript's proc
local lagHead = {}        -- this tick's lag VBlanks at a RunProcessScript's head, before its first command: "i"
local headCheck = nil     -- the last such VBlank: the proc's fields when it came, checked after its tree-0 walk

-- Save states: the header (with the "resume" block on a resume), then on a
-- resume tick T-1's line and raw record, and every counter as it was.
local resumeHeader = ""
if stateIn then
  local dir = stateIn .. "/gba"
  if runner_state_loaded ~= dir .. "/state.ss" then
    fail("GATE0_STATE_IN=" .. stateIn .. ": the runner did not load " .. dir .. "/state.ss (MGBA_RUN_STATE_IN; "
      .. "port/test/Makefile sets it)")
  end
  local meta = readFile(dir .. "/meta.json")
  local function metaField(k) return meta:match('"' .. k .. '":"?([^",}]*)') end
  if metaField("keys_crc32") ~= keysCrc then
    fail(string.format("the key file %s (CRC-32 %s) is not the snapshot's (%s)", keysPath, keysCrc, metaField("keys_crc32")))
  end
  if metaField("rom_crc32") ~= romCrc then
    fail(string.format("the ROM (CRC-32 %s) is not the snapshot's (%s)", romCrc, metaField("rom_crc32")))
  end
  if (metaField("cheat") or "") ~= (cheat and cheat.name or "") then
    fail(string.format("the snapshot's run had GATE0_CHEAT=%s, this one has GATE0_CHEAT=%s (README.md, \"Test cheats\")",
      metaField("cheat") or "", cheat and cheat.name or ""))
  end
  local st = dofile(dir .. "/capture.lua")
  if cheat and cheat.on.convoy then                 -- the convoy cheat's cursor at the photo (test_cheat.lua)
    if not st.cheatConvoy then fail("the snapshot has no convoy cursor (cheatConvoy in capture.lua)") end
    cheat.setCursor(st.cheatConvoy)
  end
  if sramHeader ~= "" and sramHeader ~= st.sramHeader then
    fail("GATE0_SRAM_IN is not the save the snapshot's run booted from")
  end
  if emu:currentFrame() ~= st.emuFrame then
    fail(string.format("the loaded state is at frame %d, the snapshot was taken at %d", emu:currentFrame(), st.emuFrame))
  end
  frame, lastTickFrame, tick = st.frame, st.lastTickFrame, st.tick
  onmain, waits, onvblank = st.onmain, st.waits, st.onvblank
  events, lagAt, lagPrev, depth = st.events, st.lagAt, st.lagPrev, st.depth
  vbBase, innerPending, preempt, outerWait = st.vbBase or nil, st.innerPending, st.preempt, st.outerWait  -- false: nil
  earlyFlush, earlyFlushes, lastEvent = st.earlyFlush, st.earlyFlushes, st.lastEvent
  sramHeader = st.sramHeader
  resumeHeader = string.format(',"resume":{"tick":%d,"set":%s,"part":"gba","rom_crc32":"%s","keys_crc32":"%s"}',
    tick, jsonString(st.set), romCrc, keysCrc)
  if ticks <= tick then
    fail(string.format("GATE0_TICKS=%d must be past the snapshot's tick %d (the last tick + 1, as from power-on)",
      ticks, tick))
  end
elseif runner_state_loaded and runner_state_loaded ~= "" then
  fail("the runner loaded the save state " .. runner_state_loaded .. " (MGBA_RUN_STATE_IN), but GATE0_STATE_IN "
    .. "is not set: a resumed capture needs both (port/test/Makefile sets them)")
end
for t in pairs(stateAt) do
  if t < 1 or t > ticks then
    fail(string.format("GATE0_STATE_AT tick %d is outside this capture (ticks 1 to GATE0_TICKS=%d)", t, ticks))
  end
end
local frameHeader = frameKeysPath and string.format(',"frame_keys":true,"frame0":%d,"frame_keys_crc32":"%s"', frame0,
  keysCrc) or ""
-- Sound positions (see "Sound" at the top and the sound block below): with
-- SND_CALLS each tick with lag VBlanks also gets "lag_snd".
local sndPositions = (os.getenv("SND_CALLS") or "") ~= ""
out:write(string.format('{"source":"gba","format":"fe8-gate0-trace-4","keys":"%s","ticks":%d%s%s%s%s%s%s}\n',
  keysPath, ticks, sramHeader, resumeHeader, frameHeader, rasterHeader, sndPositions and ',"lag_snd":1' or "",
  cheat and (',"cheat":"' .. cheat.name .. '"') or ""))
if stateIn then
  out:write(readFile(stateIn .. "/gba/prologue.jsonl"))
  out:flush()
  if raw then
    local rec = readFile(stateIn .. "/gba/prologue.raw")
    if #rec == 0 then fail("GATE0_RAW is set, but the snapshot's run wrote no raw dump") end
    raw:write(rec)
  end
  print(string.format("gate0_capture: resumed at tick %d (frame %d, %d OnMain calls) from %s", tick, frame, onmain,
    stateIn))
end
if frameKeysPath then emu:setKeys(frameMaskFor(0)) else emu:setKeys(maskFor(tick)) end

-- Sound (see the top): records, m4a calls and the tick index. -----------------
local sndOutPath, sndCallsPath, sndIndexPath = os.getenv("SND_OUT"), os.getenv("SND_CALLS"), os.getenv("SND_INDEX")
if sndOutPath == "" then sndOutPath = nil end
if sndCallsPath == "" then sndCallsPath = nil end
if sndIndexPath == "" then sndIndexPath = nil end
-- SND_CALLS alone (no records): the sound positions for a save state's run
-- (port/test/Makefile snapshot SNAP_SND=1).
assert(not sndIndexPath or sndOutPath, "SND_INDEX goes with SND_OUT")
local snd = sndOutPath and assert(io.open(sndOutPath, "wb"), "cannot open SND_OUT " .. sndOutPath)
local sndCalls = sndCallsPath and assert(io.open(sndCallsPath, "w"), "cannot open SND_CALLS " .. sndCallsPath)
local sndIndex = sndIndexPath and assert(io.open(sndIndexPath, "w"), "cannot open SND_INDEX " .. sndIndexPath)
local sndFirst, sndCount = nil, 0          -- first record v, records written
local sndDriver, sndOpen = {}, {}          -- driver code ranges; open watched calls, innermost last
local sndSeq, sndTop, sndNested, sndUnclean, sndWatched = 0, 0, 0, 0, 0
local sndNestedBy = {}
-- Sound positions ("lag_snd"): the main loop's top-level m4a calls since the
-- tick's last main-loop event (or its sample), and the count before that event
-- (for a lag VBlank that preempted the event); sndPreempt: an IRQ came at the
-- entry of the last such call, before its first instruction (the call has not
-- run when that lag VBlank comes).
local sndSince, sndPrevSince, sndPreempt, sndLastMain = 0, 0, 0, nil
local SND_SP_IRQ_TOP = 0x03007FA0                -- __sp_irq: no IRQ frame on the IRQ stack = the main loop
-- A lag VBlank inside the main loop's open top-level m4a call ("lag_snd_split"):
-- the TrackStop calls that call has made (the sndpos_trackstop breakpoint; an
-- IRQ at TrackStop's entry, before its first instruction, comes before that
-- one), and the driver's locks the VBlank handler finds: bit k for player k of
-- gMPlayTable whose ident is not ID_NUMBER, and gSoundInfo.ident - ID_NUMBER in
-- bits 16-23 (SND_MPLAY=table:count, from audio/snd_calls.py setup-gate0).
local sndMainOpen, sndStops, sndStopCounted, sndStopPreempt = nil, 0, false, 0
local SND_ID = 0x68736D53
local sndPlayers = {}
do
  local tab, count = (os.getenv("SND_MPLAY") or ""):match("^(0x%x+):(%d+)$")
  if tab then
    for k = 0, tonumber(count) - 1 do sndPlayers[#sndPlayers + 1] = emu:read32(tonumber(tab) + 12 * k) end
  end
end
local function sndLocks()
  local m = 0
  for k, p in ipairs(sndPlayers) do
    if emu:read32(p + 0x34) ~= SND_ID then m = m | (1 << (k - 1)) end
  end
  return m | (((emu:read32(0x03005410) - SND_ID) & 0xFF) << 16)
end

local function sndU16(x) return string.char(x & 0xFF, (x >> 8) & 0xFF) end
local function sndU32(x) return string.char(x & 0xFF, (x >> 8) & 0xFF, (x >> 16) & 0xFF, (x >> 24) & 0xFF) end

-- The record of callback v (sound_capture.lua's layout; bytes 6-7: OnMain entries).
local function sndRecord(v)
  -- A watched call still open at a record is not clean (the GBA ran part of it on each side).
  for _, o in ipairs(sndOpen) do if o.top then o.clean = false end end
  snd:write("FE8S", sndU16(1), sndU16(onmain & 0xFFFF), sndU32(v), sndU32(emu:read32(0x03000014)))
  snd:write(emu:readRange(0x03001E38, 3120))
  snd:write(emu:readRange(0x03005410, 5008))
  snd:write(emu:readRange(0x03007FF0, 4))
  snd:write(emu:readRange(0x04000060, 80))
  snd:write(sndU32(emu:read32(0x040000C4)), sndU32(emu:read32(0x040000D0)))
  sndFirst = sndFirst or v
  sndCount = sndCount + 1
end

local function sndInDriver(a)
  for _, r in ipairs(sndDriver) do
    if a >= r[1] and a < r[2] then return true end
  end
  return false
end

local function sndWriteCall(c, wOut)
  sndCalls:write(string.format("%d %d %d %d %s %08x %08x %08x %08x %08x\n", c.seq, c.w, wOut,
    c.clean and 1 or 0, c.name, c.r[1], c.r[2], c.r[3], c.r[4], c.lr))
  if not c.clean then sndUnclean = sndUnclean + 1 end
end

if sndCalls then
  local spec = os.getenv("SND_M4A_CODE") or ""
  for lo, hi in spec:gmatch("(0x%x+)%-(0x%x+)") do sndDriver[#sndDriver + 1] = { tonumber(lo), tonumber(hi) } end
  if #sndDriver == 0 then fail("SND_CALLS needs SND_M4A_CODE (port/test/Makefile, audio/snd_calls.py)") end
  sndCalls:write("# fe8-sndcalls-1 SEQ W_IN W_OUT CLEAN NAME R0 R1 R2 R3 LR (port/test/oracle/gate0_capture.lua)\n")
  sndCalls:write("# driver " .. spec .. "\n")
  local function reg(name) return emu:readRegister(name) & 0xFFFFFFFF end
  for name, addr in bp:gmatch("snd_([%w_]+)@(0x%x+):ret") do
    sndWatched = sndWatched + 1
    sndCalls:write(string.format("# watch %s %s\n", name, addr))
    callbacks:add("snd_" .. name, function()
      local lr = reg("lr")
      local c = { name = name, top = not sndInDriver(lr & ~1) }
      if c.top then
        -- Another context (an IRQ handler) entering the API interrupts every
        -- top-level call still open.
        for _, o in ipairs(sndOpen) do if o.top then o.clean = false end end
        sndSeq = sndSeq + 1
        -- Window w: between record w and record w + 1; the next record is v = frame.
        c.seq, c.w, c.clean, c.lr = sndSeq, frame - 1, true, lr
        c.r = { reg("r0"), reg("r1"), reg("r2"), reg("r3") }
        sndTop = sndTop + 1
        if runner_irq_sp() == SND_SP_IRQ_TOP then  -- made by the main loop (no IRQ frame)
          sndSince = sndSince + 1
          sndLastMain = c
          sndMainOpen, sndStops, sndStopPreempt = c, 0, 0
        else
          sndLastMain = nil
          c.irq = true                             -- made by an IRQ handler (lag_snd_nested)
        end
      else
        sndNested = sndNested + 1
        sndNestedBy[name] = (sndNestedBy[name] or 0) + 1
      end
      sndOpen[#sndOpen + 1] = c
    end)
    callbacks:add("snd_" .. name .. "_ret", function()
      local c = table.remove(sndOpen)
      if not (c and c.name == name) then
        fail("return of " .. name .. " does not match the open call " .. (c and c.name or "(none)")
          .. " at frame " .. frame)
      end
      if c.top then sndWriteCall(c, frame - 1) end
      if c == sndMainOpen then sndMainOpen = nil end
    end)
    -- An IRQ taken at the entry of the main loop's last call, before its first
    -- instruction: a lag VBlank in it comes before that call.
    callbacks:add("snd_" .. name .. "_irq", function()
      local o = sndOpen[#sndOpen]
      if o and o == sndLastMain and o.name == name then sndPreempt = 1 end
    end)
    callbacks:add("snd_" .. name .. "_resume", function() sndPreempt = 0 end)
  end
  if sndWatched == 0 then fail("SND_CALLS is set but MGBA_RUN_BP has no snd_NAME@ADDR:ret breakpoint") end
  if bp:find("sndpos_trackstop@", 1, true) then
    if #sndPlayers == 0 then fail("the sndpos_trackstop breakpoint needs SND_MPLAY (audio/snd_calls.py setup-gate0)") end
    callbacks:add("sndpos_trackstop", function()
      sndStopCounted = sndMainOpen ~= nil and runner_irq_sp() == SND_SP_IRQ_TOP
      if sndStopCounted then sndStops = sndStops + 1 end
    end)
    callbacks:add("sndpos_trackstop_irq", function() if sndStopCounted then sndStopPreempt = 1 end end)
    callbacks:add("sndpos_trackstop_resume", function() sndStopPreempt = 0 end)
  end
  if bp:find("snd_wait@", 1, true) then
    fail("snd_wait is sound_capture.lua's: this capture samples ticks at gate0_wait (audio/snd_calls.py setup-gate0)")
  end
end
if sndIndex then
  sndIndex:write("# fe8-sndindex-1 TICK V: V the sound record of the VBlank the tick waits for (gba)\n")
end
if snd and stateIn then
  -- The photo's own callback (the one that wrote tick T-1's line): its record
  -- comes first, the last record of the run the snapshot was taken in.
  sndRecord(frame - 1)
  if sndCalls then sndCalls:write(string.format("# resumed tick %d v %d\n", tick, frame - 1)) end
  if sndIndex then sndIndex:write(string.format("%d %d\n", tick - 1, frame - 1)) end
end

local function sndFinish()
  if snd then
    snd:close()
    print(string.format("gate0_capture: sound: %d records from v %d -> %s", sndCount, sndFirst or -1, sndOutPath))
  end
  if sndIndex then sndIndex:close() end
  if sndCalls then
    local still = 0
    for _, o in ipairs(sndOpen) do
      if o.top then o.clean = false; sndWriteCall(o, -1); still = still + 1 end
    end
    sndCalls:write(string.format("# end records %d from %d\n", sndCount, sndFirst or 0))
    sndCalls:close()
    local parts = {}
    for name, n in pairs(sndNestedBy) do parts[#parts + 1] = string.format("%s %d", name, n) end
    table.sort(parts)
    print(string.format("gate0_capture: sound: %d watched m4a functions; %d top-level calls (%d not clean, %d still "
      .. "open) -> %s; %d calls from inside the driver (%s)", sndWatched, sndTop, sndUnclean, still, sndCallsPath,
      sndNested, table.concat(parts, ", ")))
  end
end

-- Frame mode: the reads of REG_KEYINPUT (UpdateKeyStatus's ldrh). A read
-- before the first OnMain is the boot-time call (main.c:53). An IRQ taken at
-- the breakpoint, before the load ran, can come with a frame callback that
-- set new keys, so the load's value is taken again when the CPU is back there
-- ("_resume").
local tickReadList, lastRead, bootRead = {}, 0, nil
local keyReadsTotal, differTicks, noReadTicks = 0, 0, 0
local function keyRead(again)
  local k = emu:getKeys() & 0x3FF
  if onmain == 0 then bootRead = k; return end
  if again and #tickReadList > 0 then
    tickReadList[#tickReadList] = k
  else
    tickReadList[#tickReadList + 1] = k
    keyReadsTotal = keyReadsTotal + 1
  end
end
-- At a tick's sample: the mask its reads saw, the read count, and whether they differ.
local function closeTickReads()
  local n = #tickReadList
  local differ = false
  if n > 0 then
    tickRead = tickReadList[n]
    for i = 1, n - 1 do if tickReadList[i] ~= tickRead then differ = true end end
    lastRead = tickRead
  else
    tickRead = lastRead
    noReadTicks = noReadTicks + 1
  end
  if differ then differTicks = differTicks + 1 end
  tickReadList = {}
  return n, differ
end
if frameKeysPath then
  callbacks:add("gate0_keyread", function() keyRead(false) end)
  callbacks:add("gate0_keyread_resume", function() keyRead(true) end)
end

-- GATE0_STATE_OUT: called at the "frame" callback that has just written tick
-- T-1's line (line, with its raw record rec).
local function saveSnapshot(line, rec)
  local dir = string.format("%s/tick%d/gba", stateOut, tick)
  if os.execute("mkdir -p '" .. dir .. "'") ~= true then fail("cannot make " .. dir) end
  if runner_save_state(dir .. "/state.ss") ~= 1 then fail("the runner refused or failed the save state at tick " .. tick) end
  -- A frame15 PNG of tick T-1 is copied into the snapshot, and its line points there.
  local png = line:match('"frame15":"png:([^"]*)"')
  if png then
    local copy = dir .. "/prologue.png"
    local f = assert(io.open(copy, "wb"))
    f:write(readFile(png))
    f:close()
    line = line:gsub('"frame15":"png:[^"]*"', function() return '"frame15":"png:' .. copy .. '"' end)
  end
  local function w(name, s)
    local f = assert(io.open(dir .. "/" .. name, "wb"), "cannot write " .. dir .. "/" .. name)
    f:write(s)
    f:close()
  end
  w("prologue.jsonl", line)
  w("prologue.raw", rec or "")
  local set = os.getenv("GATE0_STATE_SET") or ""
  local lagList = {}
  for i, v in ipairs(lagAt) do lagList[i] = tostring(v) end
  w("capture.lua", string.format(
    "return { frame = %d, lastTickFrame = %d, tick = %d, onmain = %d, waits = %d, onvblank = %d, events = %d, "
    .. "lagAt = {%s}, lagPrev = %d, depth = %d, vbBase = %s, innerPending = %s, preempt = %d, outerWait = %s, "
    .. "earlyFlush = %s, earlyFlushes = %d, lastEvent = %q, sramHeader = %q, set = %q, emuFrame = %d%s }\n",
    frame, lastTickFrame, tick, onmain, waits, onvblank, events, table.concat(lagList, ","), lagPrev, depth,
    vbBase == nil and "false" or tostring(vbBase), tostring(innerPending), preempt, tostring(outerWait),
    tostring(earlyFlush), earlyFlushes, lastEvent, sramHeader, set, emu:currentFrame(),
    (cheat and cheat.on.convoy) and string.format(", cheatConvoy = %d", cheat.cursor()) or ""))
  w("meta.json", string.format(
    '{"format":"fe8-gba-snapshot-1","part":"gba","tick":%d,"set":%s,"keys":%s,"keys_crc32":"%s",'
    .. '"rom_crc32":"%s","sram_header":%s,"frame":%d,"emu_frame":%d,"onmain_calls":%d,"raw":%s%s%s}\n',
    tick, jsonString(set), jsonString(keysPath), keysCrc, romCrc, jsonString(sramHeader), frame, emu:currentFrame(),
    onmain, rec and "true" or "false", sndPositions and ',"lag_snd":true' or "",
    cheat and (',"cheat":"' .. cheat.name .. '"') or ""))
  print(string.format("gate0_capture: tick %d saved to %s (frame %d, %d OnMain calls)", tick, dir, frame, onmain))
  io.stdout:flush()
end

local function describe(kind)
  -- GATE0_LAGLOG only. rpr/rps: r0 is the proc (proc_script at +0);
  -- rpr_end: r4 is; clock/settime: lr is the caller.
  local function reg(name)
    local ok, v = pcall(function() return emu:readRegister(name) end)
    return ok and v or 0
  end
  if kind == "rpr" or kind == "rps" then
    return string.format("%s proc %08x script %08x", kind, reg("r0"), emu:read32(reg("r0")))
  elseif kind == "rpr_end" then
    return string.format("%s proc %08x script %08x", kind, reg("r4"), emu:read32(reg("r4")))
  elseif kind:sub(1, 6) == "gate0_" then          -- a BIOS call: its destination, its caller
    return string.format("%s dest %08x lr %08x", kind:sub(7), reg("r1"), reg("lr"))
  end
  return string.format("%s lr %08x value %08x", kind, reg("lr"), emu:read32(0x03000014))
end

local function mainEvent(kind)
  if vbBase ~= nil then return end               -- inside OnVBlank: not a main-loop event
  preempt = 0
  rpsHead = false                                -- (set again by the rps callback for its own event)
  events = events + 1
  sndPrevSince, sndSince = sndSince, 0          -- sound positions: calls since this event
  if lagLog then lastEvent = describe(kind) end
end

-- Sound positions: the main loop's m4a calls before a lag VBlank, since the
-- event it came after (the one before a preempted event's entry).
local lagSnd, lagSndSplit, lagSndNested = {}, {}, {}
local function lagSndNow()
  local j = (preempt > 0 and sndPrevSince or sndSince) - (preempt > 0 and 0 or sndPreempt)
  lagSnd[#lagSnd + 1] = tostring(j)
  -- Inside the main loop's open call (entered and started: not preempted at
  -- its entry): where in it, as the handler can tell (see sndMainOpen).
  if sndMainOpen ~= nil then
    if preempt == 0 and sndPreempt == 0 then
      lagSndSplit[#lagSndSplit + 1] = string.format("[%d,%d,%d]", #lagSnd - 1, sndStops - sndStopPreempt, sndLocks())
    end
    return
  end
  -- Inside an IRQ handler's top-level m4a call ("lag_snd_nested", see "Sound"
  -- at the top): an OnVBlank that ran into the next frame, this VBlank's IRQ
  -- taken inside its m4aSoundMain (IrqMain runs every handler with IRQs on).
  -- The locks that call holds are what this VBlank's own m4a calls find.
  for _, o in ipairs(sndOpen) do
    if o.top and o.irq then
      lagSndNested[#lagSndNested + 1] = string.format("[%d,%d]", #lagSnd - 1, sndLocks())
      return
    end
  end
end

-- A lag VBlank inside the main loop's BIOS copy (README.md, "Lag position",
-- kind 6): the interrupted registers. The main loop runs at IRQ depth 0, so
-- its registers are in the outermost IRQ frame: at the top of the IRQ stack
-- (__sp_irq, 0x03007FA0) the BIOS vector (0x184) pushed {r0-r3, r12, lr}, lr
-- being the interrupted instruction + 4 (IrqMain's push {spsr, IE, r3, lr}
-- follows below it; 40 bytes an IRQ). r4-r11 are not on the IRQ stack: they
-- are the main loop's as the runner kept them when that outermost IRQ was
-- entered (runner_irq_reg(__sp_irq, k), oracle/mgba_lua_run.c). The live
-- r4-r11 at OnVBlank's entry are the main loop's only when OnVBlank is that
-- outermost IRQ: IrqMain runs every handler in System mode with IRQs on, so
-- the VBlank can come inside an HBlank handler that had itself interrupted the
-- copy (2026-10-21, H-col-2: ch4_start.sav + mkB_ch4_start_s2.keys tick 3,527,
-- the VBlank inside line 159's HBlank_BgVerticalScroll, whose r5 then read as
-- a 16-bit CpuSet's destination). BIOS addresses: mGBA 0.10.5's HLE BIOS image
-- (SWI handler from 0x40, in System mode with the caller's IRQ bit from 0x78,
-- the routine called at 0xA0; CpuSet 0x214-0x2A7, CpuFastSet 0x2A8-0x2FF).
local SP_IRQ_TOP = 0x03007FA0
local IRQ_FRAME = 40
local function irqSaved(k) return emu:read32(SP_IRQ_TOP - 24 + 4 * k) end   -- 0-3 r0-r3, 4 r12, 5 lr
-- r4-r11 of the main loop where the outermost IRQ interrupted it (see above).
local function mainReg(k)
  local v = runner_irq_reg(SP_IRQ_TOP, k)
  if v < 0 then fail(string.format("tick %d: the runner kept no r4-r11 for the IRQ at __sp_irq", tick)) end
  return v
end
local function irqDepth() return (SP_IRQ_TOP - runner_irq_sp()) // IRQ_FRAME end
-- At each lag VBlank: the runner's record is there and agrees with what the
-- stacks hold: r12 with the BIOS vector's push, and when OnVBlank is the
-- outermost IRQ itself, r4-r11 with the live registers and sp and lr with
-- IrqMain's System-mode push {lr} (at sp, the interrupted sp less 4).
local function checkIrqRecord()
  if runner_irq_sp() > SP_IRQ_TOP - IRQ_FRAME then fail(string.format("tick %d: OnVBlank outside an IRQ frame", tick)) end
  local want = { [12] = irqSaved(4) }
  if irqDepth() == 1 then
    for k = 4, 11 do want[k] = emu:readRegister("r" .. k) & 0xFFFFFFFF end   -- readRegister: a signed value
    local sp = emu:readRegister("sp") & 0xFFFFFFFF
    want[13], want[14] = sp + 4, emu:read32(sp)
  end
  for k = 4, 14 do
    local v = mainReg(k)
    if want[k] ~= nil and want[k] ~= v then
      fail(string.format("tick %d: the runner's r%d at the IRQ entry (%08x) is not the stacks' (%08x), IRQ depth %d",
        tick, k, v, want[k], irqDepth()))
    end
  end
end
local function biosCopyAt(i)
  local pc = (irqSaved(5) - 4) & 0xFFFFFFFF
  local routine
  if pc >= 0x78 and pc < 0xA4 then                 -- the SWI handler, before it calls the routine
    routine = irqSaved(4)                          -- ip: the routine from the SWI table
    if routine ~= 0x214 and routine ~= 0x2A8 then return end
  elseif pc >= 0x214 and pc < 0x2A8 then routine = 0x214
  elseif pc >= 0x2A8 and pc < 0x300 then routine = 0x2A8
  else return end                                  -- not in a BIOS copy (the LZ77 stall loop 0x348: all written)
  local b = lastBios
  local want = routine == 0x214 and "gate0_cpuset" or "gate0_cpufastset"
  if b == nil or b.name ~= want then
    fail(string.format("tick %d: a lag VBlank inside the BIOS routine 0x%x (pc 0x%x), but the main loop's last "
      .. "BIOS call was %s", tick, routine, pc, b and b.name or "none"))
  end
  local fill = b.control & 0x1000000 ~= 0
  local written, read = 0, 0
  if pc < 0xA4 then
    -- before the routine: nothing stored or loaded
  elseif routine == 0x2A8 then                     -- CpuFastSet: r0 source, r1 destination
    if pc >= 0x2FC then return end                 -- its pop: all written
    -- r0 and r1 step from the call's own values (ldm/stm ignore their low bits)
    written = irqSaved(1) - b.dest
    if fill then read = pc > 0x2BC and 4 or 0 else read = irqSaved(0) - b.src end
  else                                             -- CpuSet
    if pc >= 0x2A0 then return end                 -- its epilogue: all written
    if pc > 0x220 then                             -- r5 = r1, ip = r0 set
      if b.control & 0x4000000 ~= 0 then           -- 32-bit: r0 source, r1 destination
        written = irqSaved(1) - b.dest
        if fill then read = pc > 0x238 and 4 or 0 else read = irqSaved(0) - b.src end
      else                                         -- 16-bit: ip source, r5 destination
        written = (mainReg(5) & ~1) - (b.dest & ~1)
        if fill then read = pc > 0x258 and 2 or 0 else read = (irqSaved(4) & ~1) - (b.src & ~1) end
      end
    end
  end
  local unit = (routine == 0x2A8 or b.control & 0x4000000 ~= 0) and 4 or 2
  local block = routine == 0x2A8 and 32 or unit    -- what one store instruction writes
  if written < 0 or read < 0 or written % block ~= 0 or read % unit ~= 0
      or (not fill and (read < written or read > written + block)) then
    fail(string.format("tick %d: lag VBlank inside %s at BIOS 0x%x: progress %d written, %d read does not add up",
      tick, b.name, pc, written, read))
  end
  if b.counted then
    lagSplit[#lagSplit + 1] = string.format("[%d,%d,%d]", i, written, read)
  else
    lagOther[#lagOther + 1] = tostring(i)
  end
  if lagLog then
    lagLog:write(string.format("tick %d lag %d inside %s (dest %08x control %08x) at BIOS 0x%x: %d bytes written, "
      .. "%d read%s, IRQ depth %d\n", tick, i + 1, b.name, b.dest, b.control, pc, written, read,
      b.counted and "" or " (not an event)", irqDepth()))
  end
end

-- A lag VBlank at the head of a main-loop RunProcessScript (README.md, "Lag
-- position", kind 3, "At a script's head"): after its entry (the event) and
-- before the first instruction of its first command. RunProcessScript calls
-- a command with `bl _call_via_r1` (0x080033CC, two halves) and the veneer's
-- `bx r1` (0x080D18C4) jumps to it, so the head is RunProcessScript up to
-- 0x080033CE, then the veneer's bx and the command's first instruction with
-- lr still the call's return address (0x080033D1; runner_irq_reg(.., 14),
-- the main loop's own lr). Nothing of the script has run there, so the PC
-- runs that VBlank at RunProcessScript's entry, before the script's first
-- command, not before the next event, after it (cov9 A2: Ch2's Supply screen
-- starts ProcScr_SlidingWallBg on tree 0 with Proc_Start; the GBA's VBlank
-- came at the veneer's bx, so tree 0's walk skipped the new proc,
-- PROC_FLAG_STARTING; the PC ran it after Init, REPEAT and Proc_Start's end,
-- and the walk stepped the mural once more). The loop's later passes go
-- through the same instructions, so a head is also "no command of this call
-- has returned yet" (gate0_rpscmd, RunProcessScript+0x3C, after the call).
-- The PC runs the VBlank before anything RunProcessScript reads of the proc.
-- The GBA's may have come after some of those reads, or after all of them
-- with the command already chosen: at the veneer's bx, or at the command's
-- first instruction (every ProcCmd_* handler, 0x080030E8-0x0800337C, sits
-- below RunProcessScript, so the pc's order does not tell). The reads, in
-- order (fireemblem8.elf of this commit): proc_script (+0, the ldr at
-- RunProcessScript+4, 0x08003398, the first), proc_lockCnt (+0x28,
-- 0x080033A2), proc_idleCb (+0x0C, 0x080033A8), proc_scrCur (+4, 0x080033BE),
-- that command's opcode (the ldrsh at 0x080033C2), then its handler from
-- sProcessCmdTable (ROM, 0x080033C8). pc is the instruction the IRQ came
-- before (not run yet), so inside RunProcessScript the reads have begun when
-- pc > RPS_FIRST_READ, and in the call all of them are done. Whenever they
-- have begun, the capture stops if the VBlank's tree-0 walk changed what they
-- read (checkHead), so a "lag_head" record is exact.
local RPS_ENTRY, RPS_HEAD_HI, RPS_FIRST_READ = 0x08003394, 0x080033CE, 0x08003398
local RPS_CMD_RET, CALL_VIA_R1_BX = 0x080033D1, 0x080D18C4
-- The opcode RunProcessScript reads at the command (ProcCmd.opcode, a halfword
-- at +0): only a script in RAM could change it (none seen; scripts in ROM
-- cannot, and an unmapped or BIOS address would read open bus), nil otherwise.
local function headOpcode(cur)
  if cur >= 0x02000000 and cur < 0x04000000 then return emu:read16(cur) end
  return nil
end
local function lagHeadAt(i)
  if not rpsHead or preempt ~= 0 then return end
  local pc = (irqSaved(5) - 4) & 0xFFFFFFFF
  local inRps = pc > RPS_ENTRY and pc <= RPS_HEAD_HI       -- at the entry itself the IRQ came first (preempt)
  local inCall = mainReg(14) == RPS_CMD_RET and (pc == CALL_VIA_R1_BX or pc == (irqSaved(1) & ~1))
  if not inRps and not inCall then return end
  lagHead[#lagHead + 1] = tostring(i)
  -- Only proc code can change those fields: OnVBlank's tree-0 walk, when tree
  -- 0 has a root (checkHead runs after it).
  local readsBegun = inCall or pc > RPS_FIRST_READ
  if readsBegun and emu:read32(PROC_TREE_0) ~= 0 then
    local cur = emu:read32(rpsProc + 4)
    headCheck = { i = i, pc = pc, inCall = inCall, proc = rpsProc, script = emu:read32(rpsProc), cur = cur,
                  op = headOpcode(cur), idle = emu:read32(rpsProc + 0x0C), lock = emu:read8(rpsProc + 0x28) }
  end
  if lagLog then
    lagLog:write(string.format("tick %d lag %d at the head of RunProcessScript (proc %08x script %08x, pc %08x%s)\n", tick,
      i + 1, rpsProc, emu:read32(rpsProc), pc,
      inCall and ", in the call: every read done" or readsBegun and ", reads begun" or ", before its first read"))
  end
end
-- After that VBlank's tree-0 walk (lagHeadAt noted it only when the GBA's
-- VBlank came after RunProcessScript's first read of the proc): if the walk
-- changed what RunProcessScript reads (the proc's script, lock count, idle
-- callback, current command or, for a script in RAM, that command's opcode),
-- the PC's placement (before those reads) is not exact.
local function checkHead()
  local h = headCheck
  headCheck = nil
  local script, cur, idle, lock = emu:read32(h.proc), emu:read32(h.proc + 4), emu:read32(h.proc + 0x0C),
    emu:read8(h.proc + 0x28)
  local op = cur == h.cur and headOpcode(cur) or nil
  if script ~= h.script or cur ~= h.cur or idle ~= h.idle or lock ~= h.lock or op ~= h.op then
    fail(string.format("tick %d: lag VBlank %d at the head of RunProcessScript (pc %08x%s) changed proc %08x (script "
      .. "%08x->%08x, command %08x->%08x, opcode %s->%s, idle %08x->%08x, lock %d->%d) after its reads began: the lag "
      .. "replay cannot place it exactly", tick, h.i + 1, h.pc, h.inCall and ", in the command's call" or "", h.proc,
      h.script, script, h.cur, cur, tostring(h.op), tostring(op), h.idle, idle, h.lock, lock))
  end
  if lagLog then
    lagLog:write(string.format("tick %d lag %d head check (pc %08x): proc %08x's script, lock, idle, command%s unchanged "
      .. "by the tree-0 walk\n", tick, h.i + 1, h.pc, h.proc, h.op and ", opcode" or ""))
  end
end

-- An early flush at the call itself (see "Early flush" at the top): the IRQ was
-- taken at the main loop's VBlankIntrWait report, before its first instruction
-- ("gate0_wait_irq" right after the report: the runner fires it at the next
-- step, so nothing of the game ran in between), and its OnVBlank flushes.
-- The sample taken at the report is put back as it was; this OnVBlank is then
-- the tick's early flush VBlank, and the sample is taken again at the call's
-- "gate0_wait_resume". The raw dump is rewound to before the sample's record.
local function withdrawWaitSample()
  local u = waitUndo
  waitUndo = nil
  if u.cheat then cheat.undo(u.cheat) end        -- the test cheat's refill too, before this OnVBlank runs
  waits, outerWait, lagPrev, lastTickFrame, tick = u.waits, u.outerWait, u.lagPrev, u.lastTickFrame, u.tick
  events, lagAt, preempt, earlyFlush, lagSplit, lagOther = u.events, u.lagAt, u.preempt, u.earlyFlush, u.lagSplit,
    u.lagOther
  lagHead, rpsHead, headCheck = u.lagHead, u.rpsHead, u.headCheck
  lagSnd, lagSndSplit, lagSndNested, sndSince, sndPrevSince = u.lagSnd, u.lagSndSplit, u.lagSndNested, u.sndSince,
    u.sndPrevSince
  tickReadList, tickRead, lastRead, noReadTicks, differTicks = u.tickReadList, u.tickRead, u.lastRead, u.noReadTicks,
    u.differTicks
  if raw then raw:seek("set", u.rawPos) end
  pending = nil
  if not frameKeysPath then emu:setKeys(maskFor(tick)) end
  waitRetake = true
  waitRetakes = waitRetakes + 1
  if lagLog then
    lagLog:write(string.format("tick %d: the IRQ came at VBlankIntrWait's first instruction and its OnVBlank flushes: "
      .. "the sample taken at the call is withdrawn (an early flush at the call)\n", tick))
  end
end

callbacks:add("gate0_onmain", function() onmain = onmain + 1 end)
callbacks:add("gate0_onvblank", function()
  if waitUndo ~= nil and waitUndo.irq and pending ~= nil and pending == waitUndo.pending
      and emu:read8(MAIN_LOOP_ENDED) ~= 0 then
    withdrawWaitSample()
  end
  onvblank = onvblank + 1
  if emu:read8(MAIN_LOOP_ENDED) == 0 then
    if innerPending then
      innerPending = false                       -- the VBlank an inner wait waited for: not lag
    else
      lagAt[#lagAt + 1] = events - preempt       -- a lag VBlank
      checkIrqRecord()
      if sndPositions then lagSndNow() end
      biosCopyAt(#lagAt - 1)
      lagHeadAt(#lagAt - 1)
      if lagLog then
        lagLog:write(string.format("tick %d lag %d at %d vcount %d %s %s\n", tick, #lagAt,
          events - preempt, emu:read16(0x04000006),
          preempt > 0 and "before (IRQ at its first instruction)" or "after", lastEvent))
      end
    end
  elseif not outerWait then
    -- An early flush VBlank: the main loop set main_loop_ended and has not
    -- called VBlankIntrWait yet. This OnVBlank flushes and clears the flag; it
    -- is a lag VBlank at the tick's last event, and the wait that follows is
    -- the sample.
    if earlyFlush then fail(string.format("tick %d: a second early flush VBlank before the sample", tick)) end
    lagAt[#lagAt + 1] = events - preempt
    if sndPositions then lagSndNow() end
    earlyFlush = true
    earlyFlushes = earlyFlushes + 1
    if lagLog then
      lagLog:write(string.format("tick %d lag %d at %d vcount %d early flush (main_loop_ended set, before its wait) %s\n",
        tick, #lagAt, events - preempt, emu:read16(0x04000006), lastEvent))
    end
  end
  outerWait = false
  if emu:read32(PROC_TREE_0) ~= 0 then vbBase = depth end  -- OnVBlank's Proc_Run(tree 0) follows
end)
callbacks:add("gate0_rpr", function() mainEvent("rpr"); depth = depth + 1 end)
callbacks:add("gate0_rpr_end", function()
  depth = depth - 1
  if vbBase ~= nil then
    if depth == vbBase then                      -- OnVBlank's tree-0 run is over
      vbBase = nil
      if headCheck then checkHead() end
    end
    return
  end
  mainEvent("rpr_end")
end)
callbacks:add("gate0_rps", function()
  mainEvent("rps")
  if vbBase == nil then rpsHead, rpsProc = true, emu:readRegister("r0") & 0xFFFFFFFF end
end)
-- RunProcessScript+0x3C, after the call of a command: a main-loop command has
-- returned, so its RunProcessScript is past its head (lagHeadAt). Not an event.
callbacks:add("gate0_rpscmd", function()
  if runner_irq_sp() == SP_IRQ_TOP then rpsHead = false end
end)
callbacks:add("gate0_clock", function() mainEvent("clock") end)
callbacks:add("gate0_settime", function() mainEvent("settime") end)
-- FlipBgVerticalScroll's bx lr (the flip stored): the hand-over
-- HBlank_BgVerticalScroll takes at line 159. An exit, like the clock events:
-- an IRQ taken at it comes after the flip. Its only caller is the main loop
-- (savedraw.c), as for the BIOS calls below the IRQ stack shows it.
callbacks:add("gate0_flipscroll", function()
  if runner_irq_sp() == SP_IRQ_TOP then mainEvent("flipscroll") end
end)
-- A BIOS copy or decompression into video memory by the main loop: no IRQ
-- frame on the IRQ stack (IrqMain calls every handler, OnVBlank's flush and
-- tree-0 tasks too, inside one), the same rule as the PC's gPortIrqDepth == 0.
for _, name in ipairs(biosBreakpoints) do
  callbacks:add(name, function()
    local ok, dest = pcall(function() return emu:readRegister("r1") end)
    biosCounted = false
    if ok and runner_irq_sp() == SP_IRQ_TOP then
      local counted = dest >= 0x05000000 and dest < 0x08000000
      lastBios = { name = name, src = emu:readRegister("r0"), dest = dest, control = emu:readRegister("r2"),
                   counted = counted }
      if counted then
        mainEvent(name)
        biosCounted = true
      end
    end
  end)
  -- An IRQ before the swi ran: the event has not started (as at the other entry breakpoints, below).
  -- The runner fires _irq right after the report it belongs to, so biosCounted is that report's.
  callbacks:add(name .. "_irq", function() if biosCounted and vbBase == nil then preempt = 1 end end)
  callbacks:add(name .. "_resume", function() preempt = 0 end)
end
-- The runner fires "_irq" when an IRQ is taken at the breakpoint, before its
-- instruction, and "_resume" when the CPU is back there. For the two entry
-- breakpoints the event has then not started yet: a lag VBlank in between
-- came before it. (At the other three the event's work is done.) The runner
-- knows the CPU is back by the interrupted code's stack pointer, so when the
-- IRQ handler reaches the same function first (OnVBlank's tree-0 walk calls
-- RunProcessRecursive and RunProcessScript), that call comes here as a call
-- of its own, inside OnVBlank, and mainEvent leaves it out (README.md, "Lag
-- position").
local function preempted() if vbBase == nil then preempt = 1 end end
local function resumed() preempt = 0 end
callbacks:add("gate0_rpr_irq", preempted)
callbacks:add("gate0_rps_irq", preempted)
callbacks:add("gate0_rpr_resume", resumed)
callbacks:add("gate0_rps_resume", resumed)

local function waitCall()
  waitUndo = nil
  if emu:read8(MAIN_LOOP_ENDED) ~= 0 and not earlyFlush then
    -- The main loop's sample below, as it was before it (withdrawWaitSample).
    waitUndo = { waits = waits, outerWait = outerWait, lagPrev = lagPrev, lastTickFrame = lastTickFrame, tick = tick,
                 events = events, lagAt = lagAt, preempt = preempt, earlyFlush = earlyFlush, lagSplit = lagSplit,
                 lagOther = lagOther, lagHead = lagHead, rpsHead = rpsHead, headCheck = headCheck, lagSnd = lagSnd,
                 lagSndSplit = lagSndSplit, lagSndNested = lagSndNested, sndSince = sndSince, sndPrevSince = sndPrevSince,
                 tickReadList = tickReadList, tickRead = tickRead, lastRead = lastRead, noReadTicks = noReadTicks,
                 differTicks = differTicks, rawPos = raw and raw:seek() or 0 }
  end
  waits = waits + 1
  if emu:read8(MAIN_LOOP_ENDED) == 0 then        -- a wait inside OnMain, or boot, or after an early flush
    innerPending = true                          -- the VBlank it waits for is not lag
    if not earlyFlush then return end
    -- The main loop's own wait after an early flush VBlank: the tick's sample.
  else
    if earlyFlush then fail(string.format("tick %d: an early flush VBlank, then a wait with main_loop_ended set", tick)) end
    outerWait = true
  end
  assert(pending == nil, "two tick samples before one VBlank")
  if vbBase ~= nil then fail(string.format("tick %d sampled inside OnVBlank's tree-0 run", tick)) end
  local lagNow = onvblank - (waits - 1)
  if #lagAt ~= lagNow - lagPrev then
    fail(string.format("tick %d: %d lag VBlanks placed, the counters say %d", tick, #lagAt, lagNow - lagPrev))
  end
  lagPrev = lagNow
  local sampleFrame = frame + 1                  -- the VBlank this call waits for
  local nReads, readsDiffer
  if frameKeysPath then nReads, readsDiffer = closeTickReads() end
  local parts = {
    string.format('"tick":%d,"frame":%d,"vblanks":%d,"keys":"%03x"',
      tick, sampleFrame, sampleFrame - lastTickFrame, maskFor(tick)),
  }
  if frameKeysPath then
    parts[#parts + 1] = string.format('"key_reads":%d%s', nReads, readsDiffer and ',"key_reads_differ":1' or "")
  end
  for _, f in ipairs(scalars) do
    local v
    if f[3] == 4 then v = emu:read32(f[2]) elseif f[3] == 2 then v = emu:read16(f[2]) else v = emu:read8(f[2]) end
    parts[#parts + 1] = string.format('"%s":"%0' .. (f[3] * 2) .. 'x"', f[1], v)
  end
  for _, f in ipairs(raws) do
    parts[#parts + 1] = string.format('"%s":"%s"', f[1], tohex(emu:readRange(f[2], f[3])))
  end
  local rec = {}
  for _, f in ipairs(hashed) do
    local bytes = emu:readRange(f[2], f[3])
    parts[#parts + 1] = string.format('"%s":"%08x"', f[1], fnv1a(bytes))
    if raw then raw:write(bytes) end
    rec[#rec + 1] = bytes
  end
  for _, f in ipairs(hashedOnly) do
    parts[#parts + 1] = string.format('"%s":"%08x"', f[1], fnv1a(emu:readRange(f[2], f[3])))
  end
  parts[#parts + 1] = string.format('"onmain_calls":%d,"vblank_waits":%d,"onvblank_calls":%d,"events":%d',
    onmain, waits, onvblank, events)
  if #lagAt > 0 then
    parts[#parts + 1] = '"lag_at":[' .. table.concat(lagAt, ",") .. "]"
    if sndPositions then
      -- Sound positions: per lag VBlank, the main loop's top-level m4a calls
      -- since the event of its "lag_at" entry (or the previous sample).
      parts[#parts + 1] = '"lag_snd":[' .. table.concat(lagSnd, ",") .. "]"
      if #lagSndSplit > 0 then
        parts[#parts + 1] = '"lag_snd_split":[' .. table.concat(lagSndSplit, ",") .. "]"
      end
      if #lagSndNested > 0 then
        parts[#parts + 1] = '"lag_snd_nested":[' .. table.concat(lagSndNested, ",") .. "]"
      end
    end
  end
  if earlyFlush then
    parts[#parts + 1] = '"early_flush":1'       -- the last lag_at entry is an early flush VBlank
  end
  if #lagSplit > 0 then
    parts[#parts + 1] = '"lag_split":[' .. table.concat(lagSplit, ",") .. "]"
  end
  if #lagOther > 0 then
    parts[#parts + 1] = '"lag_in_other_copy":[' .. table.concat(lagOther, ",") .. "]"
  end
  if #lagHead > 0 then
    parts[#parts + 1] = '"lag_head":[' .. table.concat(lagHead, ",") .. "]"
  end
  local cheatCounts = nil
  if cheat then
    -- The test cheats (see the top): this tick boundary's, after the sample.
    local undo, fields
    undo, fields, cheatCounts = cheat.refill()
    parts[#parts + 1] = fields
    if waitUndo then waitUndo.cheat = undo end
  end
  events, lagAt, preempt, earlyFlush, lagSplit, lagOther = 0, {}, 0, false, {}, {}
  lagHead, rpsHead, headCheck = {}, false, nil
  lagSnd, lagSndSplit, lagSndNested, sndSince, sndPrevSince = {}, {}, {}, 0, 0  -- sound positions start again with the tick
  pending = { parts = parts, tick = tick, rec = raw and table.concat(rec) or nil, cheatCounts = cheatCounts }
  if waitUndo then waitUndo.pending = pending end
  lastTickFrame = sampleFrame
  tick = tick + 1
  if not frameKeysPath then emu:setKeys(maskFor(tick)) end
end
callbacks:add("gate0_wait", waitCall)
-- An IRQ taken at the call's first instruction (see "Early flush" at the top):
-- the sample just taken can still be withdrawn, if that IRQ's OnVBlank flushes.
callbacks:add("gate0_wait_irq", function()
  if waitUndo ~= nil and pending ~= nil and pending == waitUndo.pending then waitUndo.irq = true end
end)
-- The CPU back at the call: after a withdrawn sample this is the main loop's
-- own wait after its early flush VBlank, the tick's sample.
callbacks:add("gate0_wait_resume", function()
  waitUndo = nil
  if waitRetake then
    waitRetake = false
    waitCall()
  end
end)

callbacks:add("frame", function()
  frame = frame + 1
  if snd then sndRecord(frame - 1) end                          -- sound: record v = frame - 1
  if frameKeysPath then emu:setKeys(frameMaskFor(frame)) end   -- frame mode: the next interval's keys
  if pending == nil then return end
  waitUndo = nil                                                -- the line is written: the sample stands
  if sndIndex then sndIndex:write(string.format("%d %d\n", pending.tick, frame - 1)) end
  local frame15 = "null"
  if shotDir and isShotTick(pending.tick) then
    local png = string.format("%s/tick%06d.png", shotDir, pending.tick)
    emu:screenshot(png)
    frame15 = string.format('"png:%s"', png)
  end
  local parts = pending.parts
  if rasterLog and frame15 ~= "null" then
    -- This frame's block in the raster log (the runner empties the log after this callback).
    parts[#parts + 1] = string.format('"raster":"%08x"', math.tointeger(runner_raster_write(pending.tick)))
  end
  parts[#parts + 1] = '"frame15":' .. frame15
  local line = "{" .. table.concat(parts, ",") .. "}\n"
  out:write(line)
  local rec = pending.rec
  if pending.cheatCounts then cheat.add(pending.cheatCounts) end -- the test cheats' totals (the end line)
  pending = nil
  if stateOut and stateAt[tick] then
    out:flush()
    if raw then raw:flush() end
    saveSnapshot(line, rec)
  end
  if tick >= ticks then
    out:close()
    if raw then raw:close() end
    if lagLog then lagLog:close() end
    sndFinish()
    if sramPath then
      local s = assert(io.open(sramPath, "wb"), "cannot open " .. sramPath)
      s:write(emu:readRange(0x0E000000, 0x10000))
      s:close()
    end
    print(string.format("gate0_capture: %d ticks in %d frames, %d OnMain calls, %d early flush VBlanks -> %s",
      tick, frame, onmain, earlyFlushes, outPath))
    if waitRetakes > 0 then
      print(string.format("gate0_capture: %d of the early flush VBlanks came at the main loop's VBlankIntrWait call "
        .. "(the IRQ before its first instruction; the tick sampled again after it)", waitRetakes))
    end
    if cheat then
      print(string.format("gate0_capture: test cheat GATE0_CHEAT=%s (README.md, \"Test cheats\"): %s", cheat.name,
        cheat.line()))
    end
    if frameKeysPath then
      print(string.format("gate0_capture: frame mode: %d key reads, %d ticks with no read, %d ticks whose reads "
        .. "differ; boot read %s", keyReadsTotal, noReadTicks, differTicks,
        bootRead and string.format("%03x", bootRead) or "none"))
    end
    io.stdout:flush()
    os.exit(0)
  end
end)
