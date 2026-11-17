-- DWR AP test for FCEUX: runs the AP connector and, alongside it, a test driver that
-- uses Wings and teleports to reach each search spot, searches, then sets the
-- Gwaelin and Dragonlord bits so the client sends those too.
-- Log: dwr-ap-testbed/fx/aptest.txt, screenshots: dwr-ap-testbed/fx/*.png
local OUT = "C:/Users/Ryan/code/dwr-ap-testbed/fx/"
local CONNECTOR = "C:/ProgramData/Archipelago/data/lua/connector_fceux_generic.lua"

local OVERWORLD, KOL, HAUKSNESS = 1, 7, 3
local rd, wr = memory.readbyte, memory.writebyte
local frame, step, t0 = 0, "boot", 0
local lines = {}
local press = {}       -- key -> frames left
local queue = {}       -- {at = frame, key = name}

local function log(s)
  lines[#lines + 1] = string.format("f%d [%s] %s", frame, step, s)
  local f = io.open(OUT .. "aptest.txt", "w"); f:write(table.concat(lines, "\n") .. "\n"); f:close()
end
local function ap()
  local t = {}
  for a = 0x6820, 0x6827 do t[#t + 1] = string.format("%02x", rd(a)) end
  return table.concat(t, " ")
end
local function shot(name) if gui.savescreenshotas then gui.savescreenshotas(OUT .. name .. ".png") end end
local function go(x, y)
  if not x or not y then log("go: no target"); return end
  wr(0x3a, x); wr(0x3b, y); wr(0x8e, x); wr(0x8f, y)
  wr(0x90, (x * 16) % 256); wr(0x91, math.floor(x / 16))
  wr(0x92, (y * 16) % 256); wr(0x93, math.floor(y / 16))
end
-- keys = list of {delay, key}; delays are frames after the previous key
local function keys(list)
  local at = frame
  for _, k in ipairs(list) do at = at + k[1]; queue[#queue + 1] = {at = at, key = k[2]} end
  return at
end
local function set_step(s) step = s; t0 = frame end
local function age() return frame - t0 end

-- Warp tables: from (map, x, y) at $F3C8, to (map, x, y) at $F461, 3 bytes each.
local function warp(from_map, to_map)
  for k = 0, 50 do
    local f, t = 0xf3c8 + 3 * k, 0xf461 + 3 * k
    if rd(f) == from_map and rd(t) == to_map then return rd(f + 1), rd(f + 2), rd(t + 1), rd(t + 2) end
  end
end
local function warp_entry(town) local x, y = warp(OVERWORLD, town); return x, y end
local DIRS = { "down", "left", "right", "up" }
local exit_from, exit_dir = nil, 1
local SIDES = { {-1, 0, "right"}, {1, 0, "left"}, {0, -1, "down"}, {0, 1, "up"} }

local function use_wings()
  wr(0xc1, 0x33); wr(0xc2, 0); wr(0xc3, 0); wr(0xc4, 0)   -- only Wings in the bag
  return keys({ {10, "A"}, {30, "right"}, {30, "down"}, {30, "A"}, {40, "A"} })  -- ITEM, first item
end
local function stairs()
  return keys({ {10, "A"}, {30, "down"}, {30, "down"}, {30, "A"} })  -- STAIRS
end
local function search()
  return keys({ {20, "A"}, {45, "down"}, {45, "down"}, {45, "down"}, {45, "A"} })  -- SEARCH
end

local plan = {
  {town = KOL, idx = 1, name = "kol"},
  {town = HAUKSNESS, idx = 2, name = "hauksness"},
  {town = OVERWORLD, idx = 0, name = "overworld"},
}
local p, side, done_at, in_throne = 1, 1, nil, 0
local sx, sy = 0, 0
local tries = 0
local tgt_x, tgt_y = 0, 0

-- FCEUX is Lua 5.1: no | or // operators. Bits use FCEUX's OR(). registerbefore can run more
-- than once per frame, so the test steps run once per emu.framecount() and repeats only re-send input.
local last_frame, last_pad = -1, {}
local deliver_t0, last_count, last_change = 0, -1, 0
local function driver()
  local fc = emu.framecount()
  if fc == last_frame then joypad.set(1, last_pad); return end
  last_frame = fc
  frame = frame + 1
  -- Turbo, set inside the frame loop (a call at script load does not stick). Turbo also mutes
  -- sound (muteTurbo in fceux.cfg).
  if frame % 300 == 1 then emu.speedmode("turbo") end
  local map = rd(0x45)
  -- No fights: build the test ROM with the Permanent Repel flag (IVICEV2U...).

  if step == "boot" then
    -- RAM holds junk (map 255) before the game starts. A new game and a continue both start in the
    -- throne room (map 5), so wait until the map reads 5 for a full second.
    if map == 5 then in_throne = in_throne + 1 else in_throne = 0 end
    if in_throne >= 60 then set_step("intro"); log("in game, map " .. map .. ", AP " .. ap())
    elseif frame > 200 and frame % 120 < 4 then press.start = 2
    elseif frame > 260 and frame % 60 == 30 then press.A = 2 end
  elseif step == "intro" then
    if age() < 400 and age() % 30 == 0 then press.B = 2 end
    if age() == 420 then
      local x, y = warp(5, 4)   -- throne room stairs down to Tantegel
      if not x then log("throne room stairs not found"); set_step("report"); return end
      sx, sy = x, y; side = 1; log(string.format("throne room stairs at %d,%d", x, y)); set_step("stairs")
    end
  elseif step == "stairs" then
    -- Auto Stairs takes the stairs on a step, so stand next to them and walk on.
    if map == 4 and age() > 30 then exit_from = 4; exit_dir = 1; side = 1; log("in Tantegel"); set_step("exit")
    elseif age() % 120 == 1 then
      if side > 4 then shot("stairs_fail"); log("stairs failed, map " .. map); set_step("report"); return end
      local s = SIDES[side]
      go(sx + s[1], sy + s[2]); press[s[3]] = 20; side = side + 1
    end
  elseif step == "exit" then
    -- Stand on the town's arrival tile (its edge) and step off the map in each direction in turn.
    if map == OVERWORLD and age() > 30 then log("on overworld"); set_step("next")
    elseif age() % 200 == 1 then
      -- Close any window left open (a message or the command menu) before moving.
      if exit_dir > 4 then shot("exit_fail"); log("could not leave map " .. map); set_step("report"); return end
      keys({ {1, "B"}, {25, "B"}, {25, "B"} })
    elseif age() % 200 == 80 then
      local _, _, tx, ty = warp(OVERWORLD, exit_from)
      go(tx, ty); press[DIRS[exit_dir]] = 110; exit_dir = exit_dir + 1
    end
  elseif step == "next" then
    local spot = plan[p]
    if not spot then set_step("events")
    elseif spot.town == OVERWORLD then
      local x, y = rd(0xe139), rd(0xe13c)
      go(x, y); tgt_x, tgt_y = x, y; log(string.format("teleported to overworld spot %d,%d", x, y)); set_step("search")
    else
      local ex, ey = warp_entry(spot.town)
      if not ex then log("no entrance for map " .. spot.town); p = p + 1; return end
      local s = SIDES[side]
      go(ex + s[1], ey + s[2]); press[s[3]] = 40
      log(string.format("walking into map %d from %d,%d", spot.town, ex + s[1], ey + s[2]))
      set_step("enter")
    end
  elseif step == "enter" then
    local spot = plan[p]
    if map == spot.town and age() > 60 then
      local x, y = rd(0xe139 + spot.idx), rd(0xe13c + spot.idx)
      go(x, y); tgt_x, tgt_y = x, y; log(string.format("in town, teleported to %d,%d", x, y)); set_step("search")
    elseif age() > 150 then
      side = side + 1
      if side > 4 then log("could not enter map " .. spot.town); shot(spot.name .. "_enter_fail"); side = 1; p = p + 1 end
      set_step("next")
    end
  elseif step == "search" then
    -- The search keys take 200 frames, so look at the result well after that. If the
    -- search bit is not set (a press was dropped), close any window and try again.
    -- Re-teleport and clear any window before each attempt: a missed A press lets the
    -- down presses walk the player off the search tile.
    if age() == 1 then go(tgt_x, tgt_y); keys({ {1, "B"} }) end
    if age() == 30 then search() end
    if age() == 330 then shot(plan[p].name); log("after search, AP " .. ap()) end
    if age() == 420 then keys({ {5, "A"}, {30, "A"}, {30, "B"}, {30, "B"}, {30, "B"} }) end
    if age() == 600 and AND(rd(0x6826), 2 ^ plan[p].idx) == 0 then
      tries = tries + 1
      if tries < 3 then log("search bit not set, retry " .. tries); set_step("search"); return end
      log("search failed for " .. plan[p].name)
    end
    if age() == 600 then
      tries = 0
      p = p + 1; side = 1
      if map ~= OVERWORLD then exit_from = map; exit_dir = 1; set_step("exit") else set_step("next") end
    end
  elseif step == "events" then
    if age() == 1 then wr(0xdf, OR(rd(0xdf), 0x01)); log("set Gwaelin carried bit") end
    if age() == 180 then wr(0xe4, OR(rd(0xe4), 0x04)); log("set Dragonlord defeated bit") end
    if age() == 400 then log("AP after events " .. ap()); deliver_t0 = os.time(); set_step("deliver") end
  elseif step == "deliver" then
    -- Turbo runs the script in seconds, but the client polls in real time (about every 0.5 s),
    -- so wait in real time for the items. Take a step now and then: a received item's message
    -- shows only after a step, and the next item waits for it.
    local count = rd(0x6820) + 256 * rd(0x6821)
    if count ~= last_count then
      last_count = count; last_change = os.time()
      log(string.format("received %d, mail flag %d", count, rd(0x682e)))
    end
    -- Walk out and back in each direction in turn, so some step lands even next to water.
    if age() % 90 == 10 then
      local turn = math.floor(age() / 90) % 8
      press[({ "left", "right", "up", "down", "right", "left", "down", "up" })[turn + 1]] = 18
    end
    if age() % 90 == 50 then press.B = 4 end
    if age() % 1800 == 900 then
      log(string.format("waiting: map %d at %d,%d, mail flag %d", map, rd(0x3a), rd(0x3b), rd(0x682e)))
      shot("deliver_" .. age())
    end
    local now = os.time()
    if (now - deliver_t0 >= 20 and now - last_change >= 15) or now - deliver_t0 >= 120 then
      log("final AP " .. ap() .. string.format(", mail flag %d, seed hash %02x%02x%02x%02x, slot tags %02x %02x %02x",
        rd(0x682e), rd(0x6842), rd(0x6843), rd(0x6844), rd(0x6845), rd(0x6846), rd(0x6848), rd(0x684a)))
      set_step("report")
    end
  elseif step == "report" then
    if not done_at then done_at = frame; log("done") end
  end

  -- inputs
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

-- Log the PRG bank in use ($6004) the first few times each AP hook site runs, so a hook can
-- move out of the fixed bank only where one bank is always in use.
local bank_hits = {}
for _, site in ipairs({ 0xCA1A, 0xCDBF, 0xE221, 0xE12D, 0xF6EA, 0xF8BE, 0xF8CE, 0xFA18, 0xFB6B }) do
  memory.registerexecute(site, function()
    bank_hits[site] = (bank_hits[site] or 0) + 1
    if bank_hits[site] <= 3 then log(string.format("bank at $%04X: %d", site, rd(0x6004))) end
  end)
end

emu.registerbefore(driver)
-- Not dofile: it is a C function, and the connector's emu.frameadvance() can't yield through it.
local connector = assert(loadfile(CONNECTOR))
connector()
