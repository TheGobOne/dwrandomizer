-- NPC Guillotine talk test for Mesen 2 (--testrunner). Run against a ROM made by make_npc_rom.py.
-- Boots a new game into the Tantegel throne room, then for each target NPC: holds the NPC beside Hero,
-- opens the menu, picks TALK, and logs the NPC's three RAM bytes at $51+offset before, during and after.
-- An ordinary NPC should end as 00 00 00 (removed). A quest NPC ($6B to $6E) should keep its bytes.
-- Output: <DWR_TALK_OUT>/talk.txt and <DWR_TALK_OUT>/shots/*.png (default ./talk_out). Create shots/ first.
-- Targets are set by make_npc_rom.py; they must match the offsets there.
local OUT = os.getenv("DWR_TALK_OUT") or "talk_out"
local M = emu.memType.nesMemory
local function rd(a) return emu.read(a, M) end
local function wr(a, v) emu.write(a, v, M) end
local log = io.open(OUT .. "/talk.txt", "w")
local function say(s) log:write(s .. "\n"); log:flush() end
local function shot(name)
  local f = io.open(OUT .. "/shots/" .. name .. ".png", "wb")
  f:write(emu.takeScreenshot()); f:close()
end
local function slot(o) return string.format("%02x %02x %02x", rd(0x51 + o), rd(0x52 + o), rd(0x53 + o)) end

-- {offset, name}: offset is the NPC's byte offset in the throne room table (3 bytes each)
local targets = {
  {15, "ordinary"}, {9, "quest6B"}, {12, "quest6C"}, {18, "quest6D"}, {21, "quest6E"},
}
local frame, inthrone, t0, state, ti = 0, 0, 0, "boot", 1
emu.addEventCallback(function()
  frame = frame + 1
  local pad = {}
  if state == "boot" then
    if rd(0x45) == 5 then inthrone = inthrone + 1 else inthrone = 0 end
    if inthrone < 60 then
      if frame > 200 and frame % 120 < 4 then pad.start = true end
      if frame > 260 and frame % 60 == 30 then pad.a = true end
    elseif inthrone < 400 then
      if inthrone % 30 < 3 then pad.b = true end
    else
      state = "talk"; t0 = frame
    end
  elseif state == "talk" then
    local o, name = targets[ti][1], targets[ti][2]
    local a = frame - t0
    if a < 239 then
      -- stand the NPC one tile below Hero (Hero y is $3b), facing up
      local x, y = rd(0x3a), rd(0x3b) + 1
      wr(0x51 + o, (rd(0x51 + o) & 0xe0) | (x & 0x1f))
      wr(0x52 + o, (rd(0x52 + o) & 0xe0) | (y & 0x1f))
      wr(0x602f, 2)
    end
    if a == 238 then say(name .. " before: " .. slot(o)) end
    if (a >= 20 and a < 24) or (a >= 130 and a < 134) or (a >= 240 and a < 244) then pad.a = true end
    if a == 200 then shot(name .. "_mid") end
    if a == 420 then shot(name .. "_talk"); say(name .. " during: " .. slot(o)) end
    if a > 460 and a < 600 and a % 40 < 3 then pad.b = true end
    if a == 620 then
      say(name .. " after: " .. slot(o)); shot(name .. "_after")
      ti = ti + 1; t0 = frame
      if ti > #targets then log:close(); emu.stop(0) end
    end
  end
  emu.setInput(pad, 0)
end, emu.eventType.inputPolled)
