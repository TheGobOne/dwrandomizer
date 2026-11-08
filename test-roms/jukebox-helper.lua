-- Jukebox helper for FCEUX: listen to every song slot of a custom-music ROM.
-- File > Lua > New Lua Script Window, load this, then start the ROM and stay on the title screen.
-- Select plays the next slot, B the previous one. The screen shows which slot is playing.
-- FCEUX rules: Lua 5.1 (no << or |), one hook per address, literal paths, no newlines inside strings.
local slots = {
  {0x01, "Title"}, {0x02, "Tantegel throne room"}, {0x03, "Tantegel"}, {0x04, "Town"},
  {0x05, "Overworld"}, {0x06, "Dungeon floor 1"}, {0x07, "Dungeon floor 2"}, {0x08, "Dungeon floor 3"},
  {0x09, "Dungeon floor 4"}, {0x0a, "Dungeon floor 5"}, {0x0b, "Dungeon floor 6"}, {0x0c, "Dungeon floor 7"},
  {0x0d, "Final floor"}, {0x0e, "Encounter"}, {0x0f, "Dragonlord"}, {0x10, "Ending"},
  {0x1c, "Hauksness"}, {0x1f, "Credits"},
}
local i, sel_held, b_held = 1, false, false
local function play(n)
  -- Ask for the song the way the game does: the raw request in WANT, then the flag that tells the
  -- wrapper to map it to this seed's song.
  memory.writebyte(0x6bfc, slots[n][1])
  memory.writebyte(0x6bf5, 1)
end
local msg = "Select: next song  B: previous song"
while true do
  local pad = joypad.read(1)
  if pad.select and not sel_held then
    play(i); msg = string.format("%d/%d  %s (request $%02x)", i, #slots, slots[i][2], slots[i][1])
    i = i % #slots + 1
  end
  if pad.B and not b_held then
    i = (i - 3) % #slots + 1
    play(i); msg = string.format("%d/%d  %s (request $%02x)", i, #slots, slots[i][2], slots[i][1])
    i = i % #slots + 1
  end
  sel_held, b_held = pad.select, pad.B
  gui.text(2, 2, msg)
  emu.frameadvance()
end
