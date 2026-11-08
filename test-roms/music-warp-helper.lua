-- Music warp helper for FCEUX. File > Lua > New Lua Script Window, load this, then start or load a game.
-- On the overworld, Select puts you on the next entrance in the list on screen, read from this ROM's warp table,
-- so it works for any seed. Step off the entrance and back on to go in. HP and MP stay full, and you have max stats.
local NAMES = {
  [2] = "Charlock", [3] = "Hauksness", [4] = "Tantegel", [7] = "Kol", [8] = "Brecconary", [9] = "Garinham",
  [10] = "Cantlin", [11] = "Rimuldar", [12] = "Tantegel basement", [13] = "Northern shrine", [14] = "Southern shrine",
  [21] = "Swamp cave", [22] = "Mountain cave", [24] = "Garin's Grave", [28] = "Erdrick's Cave",
}
local WANT = {28, 22, 24, 21, 2, 12, 9}           -- the order Select goes through
local dests = {}
for _, m in ipairs(WANT) do
  for k = 0, 50 do
    local f, t = 0xf3c8 + 3 * k, 0xf461 + 3 * k
    if memory.readbyte(f) == 1 and memory.readbyte(t) == m then
      dests[#dests + 1] = {name = NAMES[m], x = memory.readbyte(f + 1), y = memory.readbyte(f + 2)}
      break
    end
  end
end
local i, held = 0, false
local msg = #dests > 0 and ("Select on the overworld: warp to " .. dests[1].name) or "No overworld entrances found"
local function go(x, y)
  memory.writebyte(0x3a, x); memory.writebyte(0x3b, y); memory.writebyte(0x8e, x); memory.writebyte(0x8f, y)
  memory.writebyte(0x90, (x * 16) % 256); memory.writebyte(0x91, math.floor(x / 16))
  memory.writebyte(0x92, (y * 16) % 256); memory.writebyte(0x93, math.floor(y / 16))
end
while true do
  memory.writebyte(0xc5, 255); memory.writebyte(0xc6, 255)        -- HP, MP
  local sel = joypad.read(1).select
  if sel and not held and memory.readbyte(0x45) == 1 and #dests > 0 then
    local d = dests[i % #dests + 1]
    memory.writebyte(0xc7, 30); memory.writebyte(0xca, 140); memory.writebyte(0xcb, 130)
    memory.writebyte(0xcc, 255); memory.writebyte(0xcd, 255); memory.writebyte(0xbe, 0xff)
    memory.writebyte(0xbf, 6); memory.writebyte(0xc0, 6); memory.writebyte(0xce, 0xff); memory.writebyte(0xcf, 0x03)
    go(d.x, d.y)
    i = i + 1
    msg = string.format("At %s (%d,%d). Step off and back on. Next Select: %s", d.name, d.x, d.y, dests[i % #dests + 1].name)
  end
  held = sel
  gui.text(2, 2, msg)
  emu.frameadvance()
end
