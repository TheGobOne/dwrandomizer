-- Garinham warp helper for FCEUX. File > Lua > New Lua Script Window, load this, then start or load a game.
-- On the overworld, press Select: you are put one tile left of Garinham's entrance (read from this ROM's warp
-- table, so it works for any seed) and walked in. Full HP and MP, no random battles.
local GARINHAM = 9
local gx, gy
-- The ROM may not be loaded yet when the script starts (fceux -lua), so look the entrance up when needed.
local function find()
  for k = 0, 50 do
    local f, t = 0xf3c8 + 3 * k, 0xf461 + 3 * k
    if memory.readbyte(f) == 1 and memory.readbyte(t) == GARINHAM then
      gx, gy = memory.readbyte(f + 1), memory.readbyte(f + 2)
      return
    end
  end
end
local function go(x, y)
  memory.writebyte(0x3a, x); memory.writebyte(0x3b, y); memory.writebyte(0x8e, x); memory.writebyte(0x8f, y)
  memory.writebyte(0x90, (x * 16) % 256); memory.writebyte(0x91, math.floor(x / 16))
  memory.writebyte(0x92, (y * 16) % 256); memory.writebyte(0x93, math.floor(y / 16))
end
local held, hold = false, 0
local msg = "Select on the overworld: warp to Garinham"
while true do
  memory.writebyte(0xc5, 255); memory.writebyte(0xc6, 255)   -- HP, MP
  -- No random battles. While the music driver runs ($6BF3 = 1) it has swapped its own bytes into $D0-$DD and
  -- the game's $DB is parked at $6B0B; writing $DB then corrupts the driver and freezes the music.
  if memory.readbyte(0x6bf3) == 1 then memory.writebyte(0x6b0b, 255) else memory.writebyte(0xdb, 255) end
  local sel = joypad.read(1).select
  if sel and not held then
    if not gx then find() end
    if not gx then
      msg = "Garinham entrance not found"
    elseif memory.readbyte(0x45) == 1 then
      go(gx - 1, gy); hold = 40
      msg = string.format("Walking into Garinham (%d,%d). If it doesn't go in, step off and back on.", gx, gy)
    else
      msg = "Walk out to the overworld first"
    end
  end
  held = sel
  if hold > 0 then hold = hold - 1; joypad.set(1, {right = true}) end
  gui.text(2, 2, msg)
  emu.frameadvance()
end
