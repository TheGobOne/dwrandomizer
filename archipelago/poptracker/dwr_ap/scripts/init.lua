Tracker:AddItems("items/items.json")
Tracker:AddMaps("maps/maps.json")
Tracker:AddLocations("locations/locations.json")
Tracker:AddLayouts("layouts/tracker.json")
if Archipelago then
  ScriptHost:LoadScript("scripts/mapping.lua")
  ScriptHost:LoadScript("scripts/autotracking.lua")
end
