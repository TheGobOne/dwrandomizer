-- Free Charlock test helper for FCEUX, for DWR_FreeCharlock_test.nes.
-- File > Lua > New Lua Script Window, then load this. Start a new game and walk out of Tantegel Castle.
-- Select gives max stats and puts you just below Tantegel, with Charlock reached by walking left 2, then up 1.
-- HP and MP stay full, so you can run from or beat the Dragonlord as often as you like.
local PX, PY = 40, 60
local held = false
while true do
  memory.writebyte(0xc5, 255); memory.writebyte(0xc6, 255)
  local sel = joypad.read(1).select
  if sel and not held and memory.readbyte(0x45) == 1 then
    memory.writebyte(0xc7, 30)                                    -- level
    memory.writebyte(0xba, 0xff); memory.writebyte(0xbb, 0xff)    -- experience
    memory.writebyte(0xbc, 0xff); memory.writebyte(0xbd, 0xff)    -- gold
    memory.writebyte(0xca, 140); memory.writebyte(0xcb, 130)      -- strength, agility
    memory.writebyte(0xcc, 255); memory.writebyte(0xcd, 255)      -- max HP, max MP
    memory.writebyte(0xbe, 0xff)                                  -- Erdrick's sword, armour, large shield
    memory.writebyte(0xbf, 6); memory.writebyte(0xc0, 6)          -- keys, herbs
    memory.writebyte(0xce, 0xff); memory.writebyte(0xcf, 0x03)    -- all ten spells
    memory.writebyte(0x3a, PX); memory.writebyte(0x3b, PY); memory.writebyte(0x8e, PX); memory.writebyte(0x8f, PY)
    memory.writebyte(0x90, (PX * 16) % 256); memory.writebyte(0x91, math.floor(PX / 16))
    memory.writebyte(0x92, (PY * 16) % 256); memory.writebyte(0x93, math.floor(PY / 16))
  end
  held = sel
  gui.text(2, 2, "Select: max stats + go to Tantegel")
  emu.frameadvance()
end
