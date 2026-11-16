"""Builds the DWR Archipelago PopTracker pack from the APWorld's tables.

usage: python build_pack.py
Writes the pack to archipelago/poptracker/dwr_ap/ and a zip next to it. Item and location IDs
come from archipelago/dwr/data.py, so the pack always matches the APWorld.
"""
import json
import os
import re
import shutil
import sys
import types
import zipfile

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PACK = os.path.join(HERE, "dwr_ap")
DATA = os.path.join(HERE, "..", "dwr", "data.py")


def load_data():
    """Run data.py with a stand-in for BaseClasses, so this needs no Archipelago install."""
    fake = types.ModuleType("BaseClasses")

    class ItemClassification:
        progression, useful, filler = "progression", "useful", "filler"

    fake.ItemClassification = ItemClassification
    sys.modules["BaseClasses"] = fake
    scope = {}
    exec(compile(open(DATA).read(), DATA, "exec"), scope)
    return scope


def code_of(name):
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def area_of(name):
    """'Garin's Grave Chest 3' -> ("Garin's Grave", "Chest 3")."""
    m = re.match(r"(.*?) (Chest(?: \d+)?|Search Spot)$", name)
    return (m.group(1), m.group(2)) if m else (name, "Event")


def font(size):
    try:
        return ImageFont.truetype("arial.ttf", size)
    except OSError:
        return ImageFont.load_default()


def icon_text(label):
    """Two letters for an item tile: initials without small words ("Staff of Rain" -> "SR")."""
    words = [w for w in label.replace("'s", "").split() if w.lower() not in ("of", "the")]
    return "".join(w[0] for w in words[:2]).upper() if len(words) > 1 else label[:2].upper()


def item_image(path, text, color):
    img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((1, 1, 30, 30), 6, fill=color, outline=(255, 255, 255, 255))
    d.text((16, 16), text, fill=(255, 255, 255, 255), font=font(13), anchor="mm")
    img.save(path)


def main():
    data = load_data()
    if os.path.isdir(PACK):
        shutil.rmtree(PACK)
    for sub in ("items", "locations", "layouts", "maps", "scripts", "images/items", "images/maps"):
        os.makedirs(os.path.join(PACK, sub), exist_ok=True)

    # Items
    colors = {"progression": (176, 112, 32, 255), "useful": (48, 96, 176, 255), "filler": (80, 128, 80, 255)}
    items, item_map = [], {}
    texts = {name: icon_text(name) for name in data["ITEMS"]}
    clash = {t for t in texts.values() if list(texts.values()).count(t) > 1}
    if clash:
        raise SystemExit(f"Item tiles share letters: {sorted(clash)}")
    for name, item in data["ITEMS"].items():
        code = code_of(name)
        img = f"images/items/{code}.png"
        item_image(os.path.join(PACK, img), texts[name], colors[item.classification])
        # Items you do not have yet show grayed out.
        entry = {"name": name, "codes": code, "img": img, "disabled_img_mods": "@disabled"}
        if item.count > 1:
            entry.update(type="consumable", max_quantity=item.count * 2)
        else:
            entry["type"] = "toggle"
        items.append(entry)
        item_map[item.code] = (code, entry["type"])
    json.dump(items, open(os.path.join(PACK, "items", "items.json"), "w"), indent=2)

    # Locations, grouped by area, laid out as rows on a plain checks map.
    charlock = data["CHARLOCK_CHESTS"]
    rainbow = "stones_of_sunlight,staff_of_rain,erdrick_s_token"
    areas = {}
    for name, loc_id in data["LOCATION_NAME_TO_ID"].items():
        area, section = area_of(name)
        areas.setdefault(area, []).append((name, section, loc_id))

    row_h, left, top, dot = 40, 340, 20, 30
    width = left + 8 * (dot + 8) + 20
    height = top + (len(areas) + 1) * row_h + 10   # one more row for the legend
    board = Image.new("RGBA", (width, height), (24, 24, 40, 255))
    d = ImageDraw.Draw(board)
    locations, loc_map = [], {}
    for row, (area, entries) in enumerate(areas.items()):
        y = top + row * row_h
        kinds = {section for _, section, _ in entries}
        if kinds == {"Search Spot"}:
            label = f"{area} (search)"
        elif all(k.startswith("Chest") for k in kinds):
            label = f"{area} ({len(entries)} chest{'s' if len(entries) > 1 else ''})"
        else:
            label = area
        d.text((10, y + dot // 2), label, fill=(230, 230, 230, 255), font=font(20), anchor="lm")
        children = []
        for col, (name, section, loc_id) in enumerate(entries):
            x = left + col * (dot + 8) + dot // 2
            child = {
                "name": name,
                "sections": [{"name": section, "item_count": 1}],
                "map_locations": [{"map": "Checks", "x": x, "y": y + dot // 2}],
            }
            if name in charlock:
                child["access_rules"] = [rainbow]
            if name == "Staff of Rain Shrine Chest":
                child["access_rules"] = ["silver_harp"]
            children.append(child)
            loc_map[loc_id] = f"@{area}/{name}/{section}"
        locations.append({"name": area, "children": children})
    d.text((10, top + len(areas) * row_h + dot // 2),
           "Green: can check now.  Red: needs items.  Gray: checked.",
           fill=(180, 180, 180, 255), font=font(16), anchor="lm")
    board.save(os.path.join(PACK, "images", "maps", "checks.png"))
    json.dump(locations, open(os.path.join(PACK, "locations", "locations.json"), "w"), indent=2)
    json.dump([{"name": "Checks", "img": "images/maps/checks.png", "location_size": dot,
                "location_border_thickness": 2}],
              open(os.path.join(PACK, "maps", "maps.json"), "w"), indent=2)

    # Layout: item grid on the left, checks map on the right.
    codes = [code_of(n) for n in data["ITEMS"]]
    rows = [codes[i:i + 5] for i in range(0, len(codes), 5)]
    layout = {"tracker_default": {"type": "container", "background": "#181828", "content": {
        "type": "array", "orientation": "horizontal", "content": [
            {"type": "group", "header": "Items", "content": {
                "type": "itemgrid", "item_size": 32, "rows": rows}},
            {"type": "map", "maps": ["Checks"]}]}}}
    json.dump(layout, open(os.path.join(PACK, "layouts", "tracker.json"), "w"), indent=2)

    manifest = {"name": "DWR Archipelago", "game_name": "DWR", "package_version": "0.1.0",
                "package_uid": "dwr_archipelago", "author": "TheGobOne", "platform": "nes",
                "versions_url": "", "variants": {"standard": {"display_name": "Standard", "flags": ["ap"]}}}
    json.dump(manifest, open(os.path.join(PACK, "manifest.json"), "w"), indent=2)

    # Autotracking: AP item and location IDs map to pack codes and section paths.
    lua = ["-- Generated by build_pack.py from archipelago/dwr/data.py. Do not edit.", "ITEM_MAPPING = {"]
    lua += [f'  [{i}] = {{"{c}", "{t}"}},' for i, (c, t) in sorted(item_map.items())]
    lua += ["}", "LOCATION_MAPPING = {"]
    lua += [f'  [{i}] = "{p}",' for i, p in sorted(loc_map.items())]
    lua += ["}"]
    open(os.path.join(PACK, "scripts", "mapping.lua"), "w").write("\n".join(lua) + "\n")
    open(os.path.join(PACK, "scripts", "autotracking.lua"), "w").write(AUTOTRACKING)
    open(os.path.join(PACK, "scripts", "init.lua"), "w").write(INIT)

    out = os.path.join(HERE, "dwr_ap.zip")
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for root, _, files in os.walk(PACK):
            for f in files:
                full = os.path.join(root, f)
                z.write(full, os.path.relpath(full, PACK))
    print("items", len(items), "locations", len(loc_map), "->", out)


INIT = """Tracker:AddItems("items/items.json")
Tracker:AddMaps("maps/maps.json")
Tracker:AddLocations("locations/locations.json")
Tracker:AddLayouts("layouts/tracker.json")
if Archipelago then
  ScriptHost:LoadScript("scripts/mapping.lua")
  ScriptHost:LoadScript("scripts/autotracking.lua")
end
"""

AUTOTRACKING = """-- Archipelago autotracking for DWR. IDs come from scripts/mapping.lua.

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
"""

if __name__ == "__main__":
    main()
