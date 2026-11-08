-- Flute sell test for FCEUX. On the overworld, press Select: the Fairy Flute goes in your first item slot,
-- you get 500 gold, and you are put on Brecconary's entrance (read from this ROM's warp table).
-- Step off the entrance and back on to go in, then sell the flute at the item shop.
local BRECCONARY = 8
local dest
for k = 0, 50 do
  local f, t = 0xf3c8 + 3 * k, 0xf461 + 3 * k
  if memory.readbyte(f) == 1 and memory.readbyte(t) == BRECCONARY then
    dest = {x = memory.readbyte(f + 1), y = memory.readbyte(f + 2)}
    break
  end
end
local function go(x, y)
  memory.writebyte(0x3a, x); memory.writebyte(0x3b, y); memory.writebyte(0x8e, x); memory.writebyte(0x8f, y)
  memory.writebyte(0x90, (x * 16) % 256); memory.writebyte(0x91, math.floor(x / 16))
  memory.writebyte(0x92, (y * 16) % 256); memory.writebyte(0x93, math.floor(y / 16))
end
local held = false
local msg = dest and "Select on the overworld: get the flute and warp to Brecconary" or "Brecconary entrance not found"
while true do
  local sel = joypad.read(1).select
  if sel and not held and memory.readbyte(0x45) == 1 and dest then
    -- Item slots are nibbles in $C1-$C4; the Fairy Flute is item 5. Keep the other half of the byte.
    memory.writebyte(0xc1, math.floor(memory.readbyte(0xc1) / 16) * 16 + 5)
    memory.writebyte(0xbc, 0xf4); memory.writebyte(0xbd, 0x01)    -- 500 gold
    go(dest.x, dest.y)
    msg = string.format("Flute given. At Brecconary (%d,%d): step off and back on, then sell it.", dest.x, dest.y)
  end
  held = sel
  gui.text(2, 2, msg)
  emu.frameadvance()
end
