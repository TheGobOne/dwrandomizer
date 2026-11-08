-- Credits test helper for FCEUX. On the overworld, press Select to jump to the end of the
-- game: it skips the Dragonlord and the king's speech, then runs the rest of the ending.
local held = false
local function pressed() return joypad.read(1).select end
if HELPER_TEST_PRESSED then pressed = HELPER_TEST_PRESSED end
while true do
  memory.writebyte(0xc5, 200); if memory.readbyte(0x6bf3) == 1 then memory.writebyte(0x6b0b, 255) else memory.writebyte(0xdb, 255) end
  memory.writebyte(0x94, 0x55); memory.writebyte(0x95, 0x55)
  local sel = pressed()
  if sel and not held and memory.readbyte(0x45) == 1 then
    local e4 = memory.readbyte(0xe4)
    if e4 % 8 < 4 then memory.writebyte(0xe4, e4 + 4) end
    -- The ending needs bank 0, so switch to it and "return" into the ending code.
    local sp = memory.getregister("s")
    memory.writebyte(0x0100 + sp, 0xcc); memory.writebyte(0x0100 + sp - 1, 0xa3)
    memory.setregister("s", sp - 2)
    memory.writebyte(0x6004, 0)
    memory.setregister("a", 0)
    memory.setregister("pc", 0xff96)
  end
  held = sel
  if HELPER_TEST_FRAME then HELPER_TEST_FRAME() end
  emu.frameadvance()
end
