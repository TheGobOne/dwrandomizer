-- Music test helper for FCEUX: File > Lua > New Lua Script Window, then load this.
-- Full HP, no random battles. Select jumps into the next location: from the overworld
-- directly, and from anywhere else by first going back to where you last left the overworld.
local targets = {
  {"Swamp Cave", 5}, {"Erdrick's Cave", 13}, {"Charlock", 6},
  {"Hauksness", 10}, {"Staff Shrine (old man)", 1}, {"Jerk Cave (old man)", 12},
}
local nxt, held, hold, msg = 1, false, 0, ""
local outside = savestate.object()
local saved = false
local function pressed() return joypad.read(1).select end
if HELPER_TEST_PRESSED then pressed = HELPER_TEST_PRESSED end
while true do
  memory.writebyte(0xc5, 200); if memory.readbyte(0x6bf3) == 1 then memory.writebyte(0x6b0b, 255) else memory.writebyte(0xdb, 255) end
  memory.writebyte(0x94, 0x55); memory.writebyte(0x95, 0x55)
  local sel = pressed()
  if sel and not held then
    if memory.readbyte(0x45) == 1 then
      savestate.save(outside); saved = true
    elseif saved then
      savestate.load(outside)
    end
    if memory.readbyte(0x45) == 1 then
      local t = targets[nxt]; local a = 0xf3c8 + 3 * t[2]
      local x, y = memory.readbyte(a + 1), memory.readbyte(a + 2)
      memory.writebyte(0x8e, x - 1); memory.writebyte(0x8f, y)
      memory.writebyte(0x3a, x - 1); memory.writebyte(0x3b, y)
      hold = 40; msg = t[1]; nxt = nxt % #targets + 1
    else
      msg = "Walk out to the overworld first"
    end
  end
  held = sel
  if hold > 0 then hold = hold - 1; joypad.set(1, {right = true}) end
  if msg ~= "" then gui.text(8, 8, msg) end
  if HELPER_TEST_FRAME then HELPER_TEST_FRAME() end
  emu.frameadvance()
end
