-- Phase 3 readiness checks for DWR AP, run in turbo (FCEUX mutes sound in turbo).
-- Run "one": open a throne room chest with TAKE, take the stairs to Tantegel, check the
--   chest bit is still set, then exit FCEUX cleanly so the .sav is written.
-- Run "two" (same .sav): read the AP block right after power on.
-- The run name is read from scratchpad/fx/p3mode.txt.
local OUT = "C:/Users/Ryan/AppData/Local/Temp/claude/c710b416a9ee2736/942a7c2e-a5d3-415a-8c5b-87bc852c4860/scratchpad/fx/"
local rd, wr = memory.readbyte, memory.writebyte
local mf = io.open(OUT .. "p3mode.txt"); local MODE = mf and mf:read("*l") or "one"; if mf then mf:close() end
local frame, step, t0 = 0, "boot", 0
local lines = {}
local press, queue = {}, {}
local function log(s)
  lines[#lines + 1] = string.format("f%d [%s %s] %s", frame, MODE, step, s)
  local f = io.open(OUT .. "p3_" .. MODE .. ".txt", "w"); f:write(table.concat(lines, "\n") .. "\n"); f:close()
end
local function block()
  local t = {}
  for a = 0x6820, 0x6827 do t[#t + 1] = string.format("%02x", rd(a)) end
  return table.concat(t, " ")
end
local function go(x, y)
  wr(0x3a, x); wr(0x3b, y); wr(0x8e, x); wr(0x8f, y)
  wr(0x90, (x * 16) % 256); wr(0x91, math.floor(x / 16))
  wr(0x92, (y * 16) % 256); wr(0x93, math.floor(y / 16))
end
local function keys(list)
  local at = frame
  for _, k in ipairs(list) do at = at + k[1]; queue[#queue + 1] = {at = at, key = k[2]} end
end
local function set_step(s) step = s; t0 = frame end
local function age() return frame - t0 end
local function warp(from_map, to_map)
  for k = 0, 50 do
    local f, t = 0xf3c8 + 3 * k, 0xf461 + 3 * k
    if rd(f) == from_map and rd(t) == to_map then return rd(f + 1), rd(f + 2) end
  end
end
local SIDES = { {-1, 0, "right"}, {1, 0, "left"}, {0, -1, "down"}, {0, 1, "up"} }
local CHEST_X, CHEST_Y, CHEST_BIT = 4, 4, 0x10   -- throne room chest index 4: $6822 bit 4
local in_throne, tries, side, sx, sy = 0, 0, 1, 0, 0
local last_frame, last_pad = -1, {}

local function finish()
  log("done"); set_step("idle")
  if emu.exit then emu.exit() else log("no emu.exit") end
end

local function driver()
  local fc = emu.framecount()
  if fc == last_frame then joypad.set(1, last_pad); return end
  last_frame = fc
  frame = frame + 1
  local map = rd(0x45)

  if MODE == "two" and step == "boot" then
    if frame == 30 then log("power on, block " .. block()); finish() end
  elseif step == "boot" then
    if map == 5 then in_throne = in_throne + 1 else in_throne = 0 end
    if in_throne >= 60 then set_step("intro"); log("in game, block " .. block())
    elseif frame > 200 and frame % 120 < 4 then press.start = 2
    elseif frame > 260 and frame % 60 == 30 then press.A = 2 end
  elseif step == "intro" then
    if age() < 300 and age() % 30 == 0 then press.B = 2 end
    if age() == 320 then set_step("take") end
  elseif step == "take" then
    if age() == 1 then go(CHEST_X, CHEST_Y); keys({ {1, "B"} }) end
    if age() == 30 then keys({ {20, "A"}, {45, "right"}, {45, "down"}, {45, "down"}, {45, "down"}, {45, "A"} }) end
    if age() == 420 then keys({ {5, "A"}, {30, "A"}, {30, "B"}, {30, "B"}, {30, "B"} }) end
    if age() == 600 then
      if AND(rd(0x6822), CHEST_BIT) ~= 0 then
        log("chest bit set, block " .. block())
        sx, sy = warp(5, 4); side = 1; set_step("stairs")
      else
        tries = tries + 1
        if tries < 4 then log("chest bit not set, retry " .. tries); set_step("take")
        else log("TAKE failed, block " .. block()); finish() end
      end
    end
  elseif step == "stairs" then
    if map == 4 and age() > 30 then
      log("in Tantegel (map 4), block " .. block())
      if AND(rd(0x6822), CHEST_BIT) ~= 0 then log("PASS chest bit kept across map change")
      else log("FAIL chest bit lost on map change") end
      set_step("save")
    elseif age() % 120 == 1 then
      if side > 4 then log("stairs failed"); finish(); return end
      local s = SIDES[side]
      go(sx + s[1], sy + s[2]); press[s[3]] = 20; side = side + 1
    end
  elseif step == "save" then
    if age() == 60 then log("exiting so the .sav is written"); finish() end
  end

  local pad = {}
  for i = #queue, 1, -1 do
    if queue[i].at == frame then press[queue[i].key] = 4; table.remove(queue, i) end
  end
  for k, n in pairs(press) do
    if n > 0 then pad[k] = true; press[k] = n - 1 end
  end
  last_pad = pad
  joypad.set(1, pad)
end

emu.speedmode("turbo")
emu.registerbefore(driver)
