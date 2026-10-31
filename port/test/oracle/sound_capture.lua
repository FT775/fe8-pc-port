-- P3.3 / P3.4a sound capture from fireemblem8.gba in mGBA (port-plan/audio-design.md 5.2).
--
--   SND_OUT=build/gate0/title.gba.snd SND_FRAMES=7600 [SND_FROM=6000] \
--   [GATE0_KEYS=port/test/inputs/x.keys] \
--   [SND_CALLS=build/gate0/title.gba.calls SND_M4A_CODE=0x080CF4B8-0x080D166C,... \
--    MGBA_RUN_BP=snd_m4aSoundVSync@0x080CFB1C:ret,...,snd_wait@0x080D16DC] \
--     build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/sound_capture.lua 1000000
--
-- (port/test/Makefile snd-gba sets all of these; port/test/audio/snd_calls.py
-- makes the breakpoint list and SND_M4A_CODE from fireemblem8.elf/.map.)
--
-- One record per frame callback, which the runner fires when a frame ends at
-- line 160, before the CPU takes the VBlank IRQ (port/test/README.md): the
-- state after the previous VBlank's handler and the main thread's work since.
-- The PC writes the same records at the same point (port/audio/fifo_pc.c,
-- FE8PC_SND_RECORD). Record, 8,236 bytes, little-endian:
--   header 16: "FE8S", u16 version 1, u16 logic ticks so far (mod 65536; a
--              tick is a frame that ends with gBmSt.main_loop_ended set, so
--              a GBA lag VBlank repeats the previous count), u32 v (callback
--              index from 0), u32 gGameClock (0x03000014)
--   IWRAM 0x03001E38-0x03002A67 (track arrays)                       3,120
--   IWRAM 0x03005410-0x0300679F (gSoundInfo with both PCM lanes,
--         the players, gMPlayJumpTable, gCgbChans, gMPlayMemAccArea)  5,008
--   IWRAM 0x03007FF0 (SOUND_INFO_PTR)                                    4
--   I/O   0x04000060-0x040000AF (sound registers, as mGBA reads them)   80
--   I/O   DMA1CNT 0x040000C4, DMA2CNT 0x040000D0                         8
--
-- Keys (optional): GATE0_KEYS, one mask per logic tick. The mask for tick t+1
-- is set at the frame callback that ends tick t (main_loop_ended set there),
-- so OnMain t+1 reads it. gate0_capture.lua instead sets it at the
-- VBlankIntrWait breakpoint where it samples tick t (main_loop_ended set), as
-- the PC hook does (port/test/pc/gate0_hook.c). The two rules give every
-- OnMain the same mask unless a VBlank falls between OnMain setting
-- main_loop_ended and its VBlankIntrWait call. With the snd_wait breakpoint
-- (VBlankIntrWait) set, this script also counts ticks the gate0 way and
-- reports every frame where the two counts differ ("key rule" in the summary).
--
-- Sound calls (optional, SND_CALLS): the m4a functions in MGBA_RUN_BP
-- ("snd_NAME@0xADDR:ret"; the runner reports each entry and its return).
-- A call is top-level when its caller (LR) is outside the sound driver code
-- (SND_M4A_CODE: m4a_1.s, m4a.c and the IWRAM copy of SoundMainRAM); calls
-- the driver makes itself (MPlayStart from m4aSongNumStart ...) are only
-- counted. One line per top-level call, written at its return:
--   SEQ W_IN W_OUT CLEAN NAME R0 R1 R2 R3 LR
-- SEQ is the order of entry. W_IN and W_OUT are the windows of the entry and
-- the return: window w is the time between record w and record w+1 (-1:
-- before record 0; W_OUT -1: still open when the capture ended). CLEAN is 1
-- when nothing else happened during the call: no record was taken and no
-- other top-level call (an IRQ handler: OnVBlank's m4aSoundVSync and
-- m4aSoundMain, or a VSync proc) started before it returned. Registers are
-- read at the entry, before its first instruction. The last line is
-- "# end records N from F". port/test/audio/m4a_replay.py replays the clean
-- calls and scopes its excuse to what the others can touch.

local outPath = assert(os.getenv("SND_OUT"), "SND_OUT is not set")
local frames = tonumber(os.getenv("SND_FRAMES") or "600")
local from = tonumber(os.getenv("SND_FROM") or "0")
local keysPath = os.getenv("GATE0_KEYS")
local callsPath = os.getenv("SND_CALLS")
local bpSpec = os.getenv("MGBA_RUN_BP") or ""

local MAIN_LOOP_ENDED = 0x0202BCB0

local masks = {}
if keysPath then
  local f = assert(io.open(keysPath, "r"), "cannot open key file " .. keysPath)
  for line in f:lines() do
    line = line:gsub("#.*", ""):match("^%s*(.-)%s*$")
    if line ~= "" then
      local m, n = line:match("^(%x+)%*(%d+)$")
      if not m then m, n = line:match("^(%x+)$"), "1" end
      assert(m, "bad key entry " .. line)
      for _ = 1, tonumber(n) do masks[#masks + 1] = tonumber(m, 16) end
    end
  end
  f:close()
end
local function maskFor(t) return masks[t + 1] or 0 end

local out = assert(io.open(outPath, "wb"), "cannot open " .. outPath)
local v, tick = 0, 0
emu:setKeys(maskFor(0))

local function u16(x) return string.char(x & 0xFF, (x >> 8) & 0xFF) end
local function u32(x) return string.char(x & 0xFF, (x >> 8) & 0xFF, (x >> 16) & 0xFF, (x >> 24) & 0xFF) end

-- Sound calls ---------------------------------------------------------------
local calls = nil
local driver = {}                               -- {lo, hi} code ranges of the driver
local open = {}                                 -- open watched calls, innermost last
local seq = 0
local nTop, nNested, nUnclean = 0, 0, 0
local nested = {}                               -- name -> count
local watched = 0

local function reg(name) return emu:readRegister(name) & 0xFFFFFFFF end

local function inDriver(a)
  for _, r in ipairs(driver) do
    if a >= r[1] and a < r[2] then return true end
  end
  return false
end

local function writeCall(c, wOut)
  calls:write(string.format("%d %d %d %d %s %08x %08x %08x %08x %08x\n", c.seq, c.w, wOut,
    c.clean and 1 or 0, c.name, c.r[1], c.r[2], c.r[3], c.r[4], c.lr))
  if not c.clean then nUnclean = nUnclean + 1 end
end

if callsPath then
  local spec = assert(os.getenv("SND_M4A_CODE"), "SND_CALLS needs SND_M4A_CODE (port/test/Makefile)")
  for lo, hi in spec:gmatch("(0x%x+)%-(0x%x+)") do driver[#driver + 1] = { tonumber(lo), tonumber(hi) } end
  assert(#driver > 0, "bad SND_M4A_CODE " .. spec)
  calls = assert(io.open(callsPath, "w"), "cannot open " .. callsPath)
  calls:write("# fe8-sndcalls-1 SEQ W_IN W_OUT CLEAN NAME R0 R1 R2 R3 LR (port/test/oracle/sound_capture.lua)\n")
  calls:write("# driver " .. spec .. "\n")
  for name, addr in bpSpec:gmatch("snd_([%w_]+)@(0x%x+):ret") do
    if name ~= "wait" then
      watched = watched + 1
      calls:write(string.format("# watch %s %s\n", name, addr))
      callbacks:add("snd_" .. name, function()
        local lr = reg("lr")
        local c = { name = name, top = not inDriver(lr & ~1) }
        if c.top then
          -- Another context (an IRQ handler) entering the API interrupts
          -- every top-level call still open.
          for _, o in ipairs(open) do if o.top then o.clean = false end end
          seq = seq + 1
          c.seq, c.w, c.clean, c.lr = seq, v - 1, true, lr
          c.r = { reg("r0"), reg("r1"), reg("r2"), reg("r3") }
          nTop = nTop + 1
        else
          nNested = nNested + 1
          nested[name] = (nested[name] or 0) + 1
        end
        open[#open + 1] = c
      end)
      callbacks:add("snd_" .. name .. "_ret", function()
        local c = table.remove(open)
        if not (c and c.name == name) then
          -- A Lua error in a callback is only logged, so stop the run here.
          print("sound_capture: return of " .. name .. " does not match the open call "
            .. (c and c.name or "(none)") .. " at v " .. v)
          io.stdout:flush()
          os.exit(1)
        end
        if c.top then writeCall(c, v - 1) end
      end)
    end
  end
  assert(watched > 0, "SND_CALLS is set but MGBA_RUN_BP has no snd_NAME@ADDR:ret breakpoint")
end

-- Key rule check: ticks counted at VBlankIntrWait as gate0_capture.lua does.
local waitTicks, keyDiffer, keyFirst = nil, 0, nil
if bpSpec:find("snd_wait@", 1, true) then
  waitTicks = 0
  callbacks:add("snd_wait", function()
    if emu:read8(MAIN_LOOP_ENDED) ~= 0 then waitTicks = waitTicks + 1 end
  end)
end

callbacks:add("frame", function()
  local ended = emu:read8(MAIN_LOOP_ENDED) ~= 0
  if ended then tick = tick + 1 end
  if waitTicks and waitTicks ~= tick then
    keyDiffer = keyDiffer + 1
    keyFirst = keyFirst or v
  end
  for _, o in ipairs(open) do if o.top then o.clean = false end end   -- open across a record
  if v >= from then
    out:write("FE8S", u16(1), u16(tick & 0xFFFF), u32(v), u32(emu:read32(0x03000014)))
    out:write(emu:readRange(0x03001E38, 3120))
    out:write(emu:readRange(0x03005410, 5008))
    out:write(emu:readRange(0x03007FF0, 4))
    out:write(emu:readRange(0x04000060, 80))
    out:write(u32(emu:read32(0x040000C4)), u32(emu:read32(0x040000D0)))
  end
  v = v + 1
  if ended then emu:setKeys(maskFor(tick)) end
  if v >= frames then
    out:close()
    print(string.format("sound_capture: %d records (from %d), %d ticks -> %s", v - from, from, tick, outPath))
    if calls then
      local still = 0
      for _, o in ipairs(open) do
        if o.top then o.clean = false; writeCall(o, -1); still = still + 1 end
      end
      calls:write(string.format("# end records %d from %d\n", v - from, from))
      calls:close()
      local parts = {}
      for name, n in pairs(nested) do parts[#parts + 1] = string.format("%s %d", name, n) end
      table.sort(parts)
      print(string.format("sound_capture: %d watched m4a functions; %d top-level calls (%d not clean, %d still open)"
        .. " -> %s; %d calls from inside the driver (%s)", watched, nTop, nUnclean, still, callsPath, nNested,
        table.concat(parts, ", ")))
    end
    if waitTicks then
      print(string.format("sound_capture: key rule: %d ticks at frame callbacks, %d at VBlankIntrWait (gate0 rule);"
        .. " counts differ at %d frame callbacks%s", tick, waitTicks, keyDiffer,
        keyFirst and string.format(" (first at v %d)", keyFirst) or ""))
    end
    io.stdout:flush()
    os.exit(0)
  end
end)
