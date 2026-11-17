-- Phase 3 readiness checks for DWR AP, run in turbo (FCEUX mutes sound in turbo).
-- Run "one": open a throne room chest with TAKE, take the stairs to Tantegel, check the
--   chest bit is still set, then exit FCEUX cleanly so the .sav is written.
-- Run "two" (same .sav): read the AP block right after power on.
-- The run name is read from dwr-ap-testbed/fx/p3mode.txt.
local OUT = "C:/Users/Ryan/code/dwr-ap-testbed/fx/"
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
-- Run "five": the first Tantegel (map 4) chest that Random Chest Locations moved off the original
-- Tantegel chest spots. Read from the chest table (file offset $10 + $5DCD, 4 bytes: map, x, y, item).
local VANILLA_TGT = { ["1,13"] = true, ["1,15"] = true, ["2,14"] = true, ["3,15"] = true }
local function moved_tantegel_chest()
  for i = 0, 30 do
    local a = 0x10 + 0x5dcd + i * 4
    local m, x, y = rom.readbyte(a), rom.readbyte(a + 1), rom.readbyte(a + 2)
    if m == 4 and not VANILLA_TGT[x .. "," .. y] then return i, x, y end
  end
end
local TGT_I, TGT_X, TGT_Y = moved_tantegel_chest()
local last_frame, last_pad = -1, {}
local last_block = ""
local DIRS = { "down", "left", "right", "up" }
local mail_dir, mail_seen, mail_x, mail_y = 1, nil, 0, 0

local started = os.clock()
local function finish()
  local secs = math.max(os.clock() - started, 0.01)
  log(string.format("done, %.1f s, %.0f frames per second", secs, frame / secs)); set_step("idle")
  if emu.exit then emu.exit() else log("no emu.exit") end
end

local function driver()
  local fc = emu.framecount()
  if fc == last_frame then joypad.set(1, last_pad); return end
  last_frame = fc
  frame = frame + 1
  -- Set turbo inside the frame loop: a call at script load, before the ROM runs, does not
  -- stick. Turbo also mutes sound (muteTurbo in fceux.cfg).
  if frame % 300 == 1 then emu.speedmode("turbo") end
  local map = rd(0x45)

  if MODE == "two" and step == "boot" then
    if frame == 30 then log("power on, block " .. block()); finish() end
  elseif step == "boot" then
    -- Run "three" uses the .sav from run "one". DW writes the adventure log when a quest
    -- starts, so it continues that quest. Run "four" starts with no .sav (a new game).
    if MODE == "three" or MODE == "four" then
      local b = block()
      if b ~= last_block then log("block now " .. b .. ", map " .. map); last_block = b end
    end
    if map == 5 then in_throne = in_throne + 1 else in_throne = 0 end
    if in_throne >= 60 then set_step("intro"); log("in game, block " .. block())
    elseif MODE == "four" and frame == 10 then
      -- Stale bits in battery RAM, then a new game with a .sav present (the bug case).
      wr(0x6822, 0x10); wr(0x6826, 0x02); log("planted stale bits, block " .. block())
    elseif MODE == "four" then
      -- Title: start opens the menu (Continue, Change Message Speed, Begin a New Quest, ...).
      -- Begin is two steps down. Presses are 10 frames so the menu registers them.
      if frame <= 480 and frame > 200 and frame % 120 < 4 then press.start = 2 end
      if frame == 620 or frame == 660 then press.down = 10 end
      if frame == 700 then press.A = 10 end
      -- Then a slot (Adventure Log 2), name entry and the rest, as for a fresh game.
      if frame > 860 and frame % 120 < 4 then press.start = 2 end
      if frame > 860 and frame % 60 == 30 then press.A = 2 end
      if frame >= 600 and frame <= 900 and frame % 40 == 0 and gui.savescreenshotas then
        gui.savescreenshotas(OUT .. "menu_" .. frame .. ".png")
      end
    elseif frame > 200 and frame % 120 < 4 then press.start = 2
    elseif frame > 260 and frame % 60 == 30 then press.A = 2 end
  elseif step == "intro" and (MODE == "three" or MODE == "four") then
    -- $CA1A (game start) runs once the king is done talking, so check after the intro.
    if age() < 300 and age() % 30 == 0 then press.B = 2 end
    if age() == 320 then
      local b = block()
      log("after intro, block " .. b)
      if MODE == "three" then
        if AND(rd(0x6822), CHEST_BIT) ~= 0 and rd(0x6827) == 0 then log("PASS continue kept the bits and cleared AP_LOADED")
        else log("FAIL continue: expected chest bit kept and $6827 = 0") end
      else
        if b == "00 00 00 00 00 00 00 00" then log("PASS new game cleared the AP block")
        else log("FAIL new game kept AP bits") end
      end
      finish()
    end
  elseif step == "intro" then
    if age() < 400 and age() % 30 == 0 then press.B = 2 end
    if age() == 420 then
      if MODE == "five" or MODE == "six" then sx, sy = warp(5, 4); side = 1; set_step("stairs")
      else set_step("take") end
    end
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
      if MODE == "five" then set_step("tgt")
      elseif MODE == "six" then set_step("mail")
      else
        if AND(rd(0x6822), CHEST_BIT) ~= 0 then log("PASS chest bit kept across map change")
        else log("FAIL chest bit lost on map change") end
        set_step("save")
      end
    elseif age() % 120 == 1 then
      if side > 4 then log("stairs failed"); finish(); return end
      local s = SIDES[side]
      go(sx + s[1], sy + s[2]); press[s[3]] = 20; side = side + 1
    end
  elseif step == "tgt" then
    -- Run "five": open the moved Tantegel chest found by moved_tantegel_chest.
    if age() == 1 and not TGT_I then log("FAIL no moved Tantegel chest: this ROM needs Random Chest Locations"); finish(); return end
    if age() == 1 then go(TGT_X, TGT_Y); keys({ {1, "B"} }) end
    if age() == 30 then keys({ {20, "A"}, {45, "right"}, {45, "down"}, {45, "down"}, {45, "down"}, {45, "A"} }) end
    if age() == 25 then log(string.format("at %d,%d on map %d", rd(0x3a), rd(0x3b), map)) end
    if age() == 380 and gui.savescreenshotas then gui.savescreenshotas(OUT .. "five_" .. tries .. ".png") end
    if age() == 420 then keys({ {5, "A"}, {30, "A"}, {30, "B"}, {30, "B"}, {30, "B"} }) end
    if age() == 600 then
      local byte, bit = 0x6822 + math.floor(TGT_I / 8), 2 ^ (TGT_I % 8)
      if AND(rd(byte), bit) ~= 0 then log(string.format("PASS moved chest %d opened in Tantegel at %d,%d, block %s", TGT_I, TGT_X, TGT_Y, block())); finish()
      else
        tries = tries + 1
        if tries < 4 then log(string.format("chest %d bit not set, retry %d", TGT_I, tries)); set_step("tgt")
        else log(string.format("FAIL moved chest %d bit not set, block %s", TGT_I, block())); finish() end
      end
    end
  elseif step == "mail" then
    -- Run "six": put "Herb" in the mailbox, take a step, and check the message shows and play goes on.
    if age() == 1 then
      local text = { 0x2B, 0x0E, 0x1B, 0x0B, 0xFA }   -- "Herb" in DW tiles
      for i, v in ipairs(text) do wr(0x6850 + i - 1, v) end
      wr(0x682E, 1); log("mailbox set, flag " .. rd(0x682E))
      mail_x, mail_y = rd(0x3a), rd(0x3b)
    end
    if age() >= 30 and age() < 400 and age() % 90 == 30 and rd(0x682E) ~= 0 then
      press[DIRS[mail_dir]] = 18; mail_dir = mail_dir % 4 + 1
    end
    if rd(0x682E) == 0 and not mail_seen then
      mail_seen = frame; log(string.format("flag cleared after a step, at %d,%d", rd(0x3a), rd(0x3b)))
    end
    if mail_seen and frame == mail_seen + 90 and gui.savescreenshotas then gui.savescreenshotas(OUT .. "mail.png") end
    if mail_seen and frame == mail_seen + 120 then keys({ {1, "A"}, {20, "B"}, {20, "B"} }) end
    if mail_seen and frame >= mail_seen + 200 and frame < mail_seen + 560 and (frame - mail_seen) % 90 == 20 then
      press[DIRS[mail_dir]] = 18; mail_dir = mail_dir % 4 + 1
    end
    if mail_seen and frame == mail_seen + 600 then
      log(string.format("after closing, at %d,%d map %d, flag %d", rd(0x3a), rd(0x3b), rd(0x45), rd(0x682E)))
      if gui.savescreenshotas then gui.savescreenshotas(OUT .. "mail_after.png") end
      finish()
    end
    if age() == 1500 and not mail_seen then log("FAIL mailbox flag never cleared"); finish() end
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

emu.registerbefore(driver)
