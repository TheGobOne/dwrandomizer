# QoL review 3

Reviewed 2026-11-06, branch feature/custom-music, working tree included. Code and site data read only. Nothing built, no emulator run. "Unverified" marks claims I did not confirm. Byte costs for ROM code are my estimates (6502, fixed bank), not measured. Comparisons to other randomizers are from memory.

## Status of review 2 items (checked in code/site)
- Caldera: README now says Tantegel is always outside the ring (done).
- Retry cap: `map_tries > 2000` gives `dwr_give_up` (map.c:1301) (done; message text is generic).
- Safe Start exists: radius 3 swamp-to-grass around Tantegel plus its border (map.c:1247). Step 2 (safe path to Brecconary) is NOT done.
- Checksum vs music pack (item 1): not re-checked, unverified. Re-test.
- Biome has a site control and spoiler line (dwr.c:4706). CLI `--biome` unverified.

## Ranked suggestions (max 15)

1. **Safe path from Tantegel to Brecconary.** Why: radius 3 only clears the doorstep; Big Swamp or Wasteland can still wall the first town in swamp. Proposal: after placement, BFS over non-swamp walkable tiles; if none, carve grass along the shortest walkable path. Run after `safe_start`, before `map_encode`. Setting: fold into Safe Start (no new bit). Race: deterministic from seed, in checksum. Bytes: 0 ROM. Difficulty: Hard.

2. **Music pack must not change the checksum.** Why: racers with the same seed and flags see different checksums if a big pack forces tight packing. Proposal: on a music-only failure, drop pack songs and say so, never repack game code. Bytes: 0 (removes work). Easy. Re-test first, status unknown.

3. **Race mode and run receipt (site).** Why: one toggle hides spoiler log and map preview, prints version, flags, seed, checksum, Bank settings, Biome, Safe Start, Menu Color note. Default off. Bytes: 0. Easy to Medium.

4. **Biome-aware hints on the site.** Why: Wasteland and Caldera are mostly desert, which is the fastest Encounter tier. Show "Pair with Encounter Rate Low or Medium" when those biomes are selected; grey out Biome, Map Size and Safe Start text when Random Map is off (the descriptions say "Needs Random Map" but nothing is disabled). Advice only, no auto-change. Bytes: 0. Easy.

5. **Check Corrected Hints against biomes.** Why: `fix_geography_talk` hints give directions; Caldera/Riverlands layouts may make "north/west" or reachability hints misleading. Unverified, needs a test with Hints=Corrected plus each biome. Fix by computing from final coordinates. Bytes: likely 0 (text rewritten at generation, but longer strings may cost a few bytes of NPC text space). Medium.

6. **Master "Reduce all flashing" switch (site).** Why: Spell, Red Flash and Screen Shake are three separate checkboxes; battle flash, level-up flash, death fade and Noir transitions have no option. Phase 1: one site toggle that sets the three existing flags (0 bytes, Easy). Phase 2: new ROM options for the rest, about 10 to 25 bytes each if it is a palette write patch (estimate; Hard because bank space is the issue). Cosmetic, keep out of the checksum like Menu Color.

7. **Share link and seed history.** Why: copy a URL with flags, seed and settings; remember last seeds in localStorage. Bytes: 0. Easy.

8. **Map preview (non-race).** Why: biomes make people want to see the map; also lets racers confirm the same map. One pixel per tile from wasm. Spoiler, so hidden in Race mode. Bytes: 0 ROM. Medium.

9. **Better generation failure messages.** Why: "Could not draw a map that works with these flags" does not say what to change. Name the combination (for example Very Small plus Riverlands, or Caldera plus Very Small) and suggest Normal size. The ROM-space message already names flags; do the same here. Unverified which biome and size combos fail often; generate 50 seeds each. Bytes: 0. Easy.

10. **Live ROM space meter or "will this fit" check.** Why: flag combinations fail for space, and the static `SPACE_FLAGS` list is a guess. Proposal: dry-run generation in wasm to report remaining bytes per bank before download. Bytes: 0. Hard.

11. **Flag search upgrade.** Why: add a "changed only" filter (reuse `isChanged`), search descriptions, `/` shortcut. Bytes: 0. Easy.

12. **Preset versioning note.** Why: Biome and Safe Start took flag bits, so old preset strings still load but cannot select them; freeze presets per version and show the build/version beside the checksum. Bytes: 0. Easy.

13. **Auto-equip best gear after purchase (parked).** Why: saves menu trips. About 60 to 100 bytes estimated in fixed bank, which likely will not fit with Warp Whistle, Run Bank and Quick Spell. Look at expansion banks first. Hard. Optional flag, default off; racing-neutral since it is only convenience.

14. **Door and chest overworld hint markers (optional "Show Search Spot Count").** Why: low priority. Skipped detail; an on-screen counter would cost 30 to 50 bytes and the stats window is already crowded (Death Counter, Key and Herb Counter). Unverified space. Hard. Not recommended now.

15. **Doc and CLI parity.** Why: confirm README, CLI table and `webui/index.js` all cover Biome, Safe Start, Menu Color, Encounter Rate, Quick Spell. `DEFAULT_FLAGS` in `common/dwr.h` and `webui/index.js` differed in review 2; unverified whether still true. Bytes: 0. Easy.

## Space and menu limits
- Fixed bank is the hard limit. Everything in items 1 to 12 and 15 is generator or site-only, so prefer them.
- Flag byte 2 is full after Biome (review 2); Safe Start took byte 8 bit 0x20. New flags need a spare bit; adding a byte breaks old seeds.
- Command window and stats window have no spare rows.
- Menu Color is stored outside flags; keep new cosmetic options the same way so they never affect the checksum.

## Top picks
1. Safe path to Brecconary (Hard, 0 bytes).
2. Checksum independent of music pack (Easy, 0 bytes).
3. Race mode and receipt (Easy to Medium, 0 bytes).
4. Biome/Encounter hint and disabled Biome controls (Easy).
5. Master flash toggle, phase 1 (Easy).
