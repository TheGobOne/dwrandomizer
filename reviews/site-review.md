# Site review: built Command Window (site/index.html)

Reviewed from source only (no browser run). Compared against site-mockups/01-command-window.html, the Site brief, webui/ and the C flag code. No site or code files were edited.

## What matches the direction (keep)
- Blue double-border windows, black title tags, arrow-pointer command list, 9 category tabs, Status window, Hero window. Same look as the mockup.
- Real flags, ROM, seed, checksum, presets, saved presets, music pack with per-scene picks, Run Bank level/cap with tier tooltips.
- Keyboard: roving tabs with arrows/Home/End, toggles work on Enter, visible focus ring, labels and aria set. No flashing; transitions off under reduced motion.
- Comfort settings (flash, red flash, shake) sit first on Visual and persist across reloads.

## Biome (World tab)
Present and correct in the places that matter:
- In the Land section, right after Random Map Size, 5 choices (Continent, Caldera, Wasteland, Riverlands, Random). Text matches the feature.
- Flag layout checks out against common/dwr.h: Caldera = bit 0, Wasteland = bit 4, Riverlands = both, Random = bits 1 and 5 (two "maybe" pairs). Mask 0x33 does not overlap Stair Shuffle (0x0c) or Heal/Hurt (0xc0).
Gaps are in the ranked list below (items 2, 5, 11).

## Ranked findings (value for effort, best first)

| # | Finding | Value | Effort |
|---|---|---|---|
| 1 | Sprite images load from `../webui/sprites/`. If only `site/` is deployed (or the folder is served alone), every hero preview 404s. Copy sprites into `site/` or confirm the deploy root includes `webui/`. | High | Easy |
| 2 | Biome does nothing without Random Map, but the page does not say so beyond the description. Dim or tag Biome (and Map Size) when Random Map is off, or add a one-line "Needs Random Map" note under it. Same for dependent options (Level 1 Radiant, Only Healmore and others the descriptions mention). | High | Easy |
| 3 | The embedded "DWR" font is built from ROM tiles (about 12 KB base64 inline). Your own notes say it needs a decision before a public site. Either swap to an original pixel font or confirm it is acceptable. Blocks going public. | High | Medium |
| 4 | Text is forced to 12px everywhere (`body *{font-size:12px!important}`), including phones. Hard to read and fights browser zoom and font settings. Use 12px only on desktop short screens, 14-16px on phones, and drop the `!important` stack. | High | Easy |
| 5 | Biome and Encounter-style "REMIX" badges: the REMIX tag list covers Encounter Rate, Quick Spell, Run Bank, Key & Herb, Screen Shake, Music Pack but not Biome (or Random Map Size if new). New features are not flagged. Add Biome to the selector list. | Medium | Easy |
| 6 | No Reset-to-defaults, no Search/filter, and no share link (flags + seed in the URL hash). The mockup and webui users expect these; there are 100+ options across 9 tabs. Share link is the cheapest and most useful. | High | Medium |
| 7 | Dead and stacked CSS. The stylesheet is a pile of overrides (`.main` grid redefined about 8 times, `!important` layout rules, unused `.meter`/`.feel` rules). Any new layout change risks regressions. Consolidate into one pass. | Medium | Medium |
| 8 | Desktop locks the page (`html,body{overflow:hidden}` at 1000px wide and 500px tall) with three nested scroll areas. Works, but keyboard users hit scroll-in-scroll and browser find/zoom behave oddly. Test at 200% zoom and short windows. | Medium | Medium |
| 9 | Hero walk animation swaps frames every 300 ms (above 3 swaps per second) and ignores `prefers-reduced-motion` in JS (the CSS rule does not stop `setInterval`). It is opt-in, but gate it on reduced motion and slow it to 500 ms or more. | Medium | Easy |
| 10 | Loading a flag string with an unknown value for a dropdown (for example a Biome pattern from a future build) silently falls back to the first choice. Show a short "some values were not recognised" note. | Low | Easy |
| 11 | Biome has no preview or hint of what Random picks, and no presets use it. A small text sketch or a "Random picks one of the other four" line would help. | Low | Easy |
| 12 | Music picks are site-only choices pending custom_music.c support (see your notes); the site already builds a trimmed pack, so this works, but add a one-line note under the pack that picks apply only to this site's output. | Low | Easy |
| 13 | `window.confirm` for preset delete is the only native dialog; fine, but it breaks the retro look. A themed confirm window is polish. | Low | Medium |
| 14 | Mobile layout: command list becomes a 2-column grid above settings, so on a phone you scroll past 9 tabs before options. A horizontal tab strip or a "Menu" pop-up would save a screen of scrolling. | Medium | Medium |

## Not done by me
No edits to site/, webui/ or C code; nothing committed or pushed. Not run in a browser, so rendering and exact phone behaviour are from reading CSS only.
