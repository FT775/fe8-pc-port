-- Gate 0, stage 2: boot the decomp's fireemblem8.gba headless and stop.
--
--   GATE0_FRAMES=400 GATE0_SHOT=build/gate0/boot.png \
--     build/gate0/mgba_lua_run fireemblem8.gba port/test/oracle/boot_check.lua 100000
--
-- Runs GATE0_FRAMES frames (default 400) with no keys pressed, saves a
-- screenshot (GATE0_SHOT, default build/gate0/boot.png), prints a few
-- registers and exits. At frame 400 the health and safety screen is up:
-- DISPCNT = 0x0300 (BG0 and BG1 on) and gMainCallback = OnMain (0x080152F5).

local frames = tonumber(os.getenv("GATE0_FRAMES") or "400")
local shot = os.getenv("GATE0_SHOT") or "build/gate0/boot.png"
local n = 0

emu:setKeys(0)

callbacks:add("frame", function()
  n = n + 1
  if n < frames then return end
  emu:screenshot(shot)
  print(string.format(
    "boot_check: frame=%d gGameClock=%d gMainCallback=0x%08X DISPCNT=0x%04X screenshot=%s",
    n, emu:read32(0x03000014), emu:read32(0x02024CB8), emu:read16(0x04000000), shot))
  io.stdout:flush()
  os.exit(0)
end)
