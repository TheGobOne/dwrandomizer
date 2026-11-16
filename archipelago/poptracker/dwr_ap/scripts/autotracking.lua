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
    if obj then obj.AvailableChestCount = obj.ChestCount end
  end
end

local function on_clear(slot_data)
  reset()
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
end

Archipelago:AddClearHandler("dwr clear", on_clear)
Archipelago:AddItemHandler("dwr item", on_item)
Archipelago:AddLocationHandler("dwr location", on_location)
