-- PPU test ROM (README.md, "PPU test ROM"): the ROM draws one still screen and
-- loops; shoot frame 30 to PPUTEST_PNG and stop.
local out = assert(os.getenv("PPUTEST_PNG"), "PPUTEST_PNG")
local n = 0
callbacks:add("frame", function()
  n = n + 1
  if n == 30 then
    emu:screenshot(out)
    os.exit(0)
  end
end)
