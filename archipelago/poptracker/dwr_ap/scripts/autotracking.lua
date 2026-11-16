-- Archipelago autotracking for DWR. IDs come from scripts/mapping.lua.

local function reset()
  for _, value in pairs(ITEM_MAPPING) do
    local obj = Tracker:FindObjectForCode(value[1])
    if obj then
      if value[2] == "toggle" then obj.Active = false else obj.AcquiredCount = 0 end
    end
  end
  for _, path in pairs(LOCATION_MAPPING) do
    local obj = Tracker:FindObjectForCode(path)
    if obj then
      obj.AvailableChestCount = obj.ChestCount
      if Highlight then obj.Highlight = Highlight.None end
    end
  end
end

-- Hints: a location in this world that someone hinted gets a highlight until it is checked.
local hint_key = nil

-- Archipelago hint status: 0 unspecified, 10 no priority, 20 avoid, 30 priority, 40 found.
local function hint_highlight(hint)
  if hint.found or hint.status == 40 then return Highlight.None end
  if hint.status == 30 then return Highlight.Priority end
  if hint.status == 20 then return Highlight.Avoid end
  if hint.status == 10 then return Highlight.NoPriority end
  if hint.status == nil then return Highlight.Priority end  -- servers before hint status
  return Highlight.Unspecified
end

local function on_hints(key, value)
  if key ~= hint_key or type(value) ~= "table" or not Highlight then return end
  for _, hint in ipairs(value) do
    if hint.finding_player == Archipelago.PlayerNumber then
      local path = LOCATION_MAPPING[hint.location]
      local obj = path and Tracker:FindObjectForCode(path)
      if obj then obj.Highlight = hint_highlight(hint) end
    end
  end
end

local function on_clear(slot_data)
  reset()
  if Archipelago.TeamNumber and Archipelago.PlayerNumber then
    hint_key = "_read_hints_" .. Archipelago.TeamNumber .. "_" .. Archipelago.PlayerNumber
    Archipelago:SetNotify({hint_key})
    Archipelago:Get({hint_key})
  end
end

local function on_item(index, item_id, item_name, player_number)
  local value = ITEM_MAPPING[item_id]
  if not value then return end
  local obj = Tracker:FindObjectForCode(value[1])
  if not obj then return end
  if value[2] == "toggle" then obj.Active = true else obj.AcquiredCount = obj.AcquiredCount + obj.Increment end
end

local function on_location(location_id, location_name)
  local path = LOCATION_MAPPING[location_id]
  if not path then return end
  local obj = Tracker:FindObjectForCode(path)
  if obj and obj.AvailableChestCount > 0 then obj.AvailableChestCount = obj.AvailableChestCount - 1 end
  if obj and Highlight then obj.Highlight = Highlight.None end
end

Archipelago:AddClearHandler("dwr clear", on_clear)
Archipelago:AddItemHandler("dwr item", on_item)
Archipelago:AddLocationHandler("dwr location", on_location)
Archipelago:AddSetReplyHandler("dwr hints", function(key, value, old) on_hints(key, value) end)
Archipelago:AddRetrievedHandler("dwr hints get", function(key, value) on_hints(key, value) end)
