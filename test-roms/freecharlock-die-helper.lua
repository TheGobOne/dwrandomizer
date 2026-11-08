-- Free Charlock death test helper for FCEUX, for DWR_FreeCharlock_music_test.nes.
-- Start a new game and walk out of Tantegel Castle. Select puts you just below Tantegel with 1 HP,
-- and HP stays at 1 until you die. After that the script stops touching HP, so the second fight is normal.
-- Random battles are off everywhere except Charlock's throne room, so you only die to the Dragonlord.
local PX, PY = 40, 60
local THRONE_ROOM, OVERWORLD = 6, 1
local held, armed, msg = false, false, "Select: 1 HP + go to Tantegel"
while true do
  local map = memory.readbyte(0x45)
  if map ~= THRONE_ROOM then
    if memory.readbyte(0x6bf3) == 1 then memory.writebyte(0x6b0b, 255) else memory.writebyte(0xdb, 255) end; memory.writebyte(0x94, 0x55); memory.writebyte(0x95, 0x55)
  end
  if armed then
    if map ~= OVERWORLD and map ~= THRONE_ROOM then
      armed = false; msg = "You died. Walk out and re-enter Charlock."
    elseif memory.readbyte(0xc5) > 1 then
      memory.writebyte(0xc5, 1)
    end
  end
  local sel = joypad.read(1).select
  if sel and not held and map == OVERWORLD and not armed then
    memory.writebyte(0xc5, 1)
    memory.writebyte(0x3a, PX); memory.writebyte(0x3b, PY); memory.writebyte(0x8e, PX); memory.writebyte(0x8f, PY)
    memory.writebyte(0x90, (PX * 16) % 256); memory.writebyte(0x91, math.floor(PX / 16))
    memory.writebyte(0x92, (PY * 16) % 256); memory.writebyte(0x93, math.floor(PY / 16))
    armed = true; msg = "1 HP. Walk left 2, then up 1."
  end
  held = sel
  gui.text(2, 2, msg)
  emu.frameadvance()
end
