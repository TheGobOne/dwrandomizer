# UI/UX review 3: site/ (TheGobOne Remix), desktop 1000-1920

Date: 2026-11-06. Review only, nothing under site/ edited. Method: copy of `site/` served on 127.0.0.1:8853 with the correct wasm MIME type, headless Edge over DevTools, real screenshots at 1920x1080, 1440x900, 1280x720/800, 1100x800 and 1000x700/800. Screenshots and scripts: `C:\Users\Ryan\AppData\Local\Temp\claude\c710b416a9ee2736\ba1af868-e2d3-490f-9988-8904ec45a839\scratchpad\shots3\` and `r3*.mjs`. No emulator was launched. No console errors on load.
Items already in `reviews/uiux-review.md` are not repeated, except where status changed (see "Previous items").

Verdict: the new labels work and the flag string is correct for all 6 Run Mechanics choices. The new left-column order has a real cost: the Command menu is now cut off at common laptop heights, and the long Run Mechanics label makes the Battle tab scroll sideways below about 1050 px wide.

## Verified working (the things you asked about)

- Run Mechanics flag string: each choice changes the flag string to a distinct value, and pasting each string back selects the same option (all 6 round trips match). Strings (last 8 chars of the default-preset flags): Vanilla `AAICAAAA`, Safer DW1 `ABYCAAAA`, DW2 `AAYCAAAA`, DW4 `ABICAAAA`, Costs XP `ACICAAAA`, Random `ADYCAAAA`. The URL hash follows.
- Truncation in the select: none at 1100 or wider (select 451 px wide, longest label 369 px plus 16 px padding). At 1000 the label fits the select but the select is wider than its column (finding 2).
- Truncation in the Status list: none. "Run Mechanics: DW4 - 50% first 2, 75% 3rd, 100% 4th" wraps to 3 lines in the 209 px column. Readable, but it is the tallest entry in the list.
- Menu Color: select is in Visual > Look with REMIX badge; choosing Orange adds "Menu Color: Orange" to Status and bumps the Visual tab badge to 1. It does not change the flag string or URL hash (it is stored in localStorage `dwrMenuColor`), which matches its description "does not change the checksum".
- Biome "Continent (Original)": shows in full at every width tested; REMIX badge present.
- Search: "run" gives 5 matches, "color" 1, "biome" 1, "flash" 2, "zzz" shows "No flag matches "zzz". Try a shorter word." Escape clears and returns to the previous tab. Changing a select inside results works (flags update). Clicking a tab ends the search.
- Hero sprites now load from `site/sprites/` (naturalWidth 32).

## Findings, ranked

1. Command menu is clipped at normal laptop heights (cause: new left-column order).
   - Saw: Command list height is 318 px for 10 rows, but it only gets what is left after Search (67), Hero (181-277) and Site Color (45). Visible tabs: 10 at 1920x1080, 8 at 1440x900, 5 at 1280x720, 2 at 1000x700. The last visible row is cut in half (a row of dashes at 1440x900; "SHOPS & INNS" sliced at 1280x720), which looks like a rendering glitch. Visual (where Menu Color lives) and Sound are the hidden ones at 1440x900.
   - Fix: give the Command window `flex:none` so all 10 rows always show, and let the Hero window absorb the squeeze (hide the 140 px preview image under about 1000 px tall, or put Hero and Site Color in one row). Alternative: move Command back to the top and Search under it, Hero last.
   - Time: 15 minutes.

2. Battle tab scrolls sideways below about 1050 px wide (cause: new long Run Mechanics label).
   - Saw: at 1000 wide, `.opts` clientWidth 371 vs scrollWidth 425; horizontal scrollbar appears; Crit Chance select loses its arrow, description text is cut ("against t"), and the Run Mechanics select shows "100% on 4t". Gone at 1060 and wider (1040 still overflows by 14 px). The select takes the width of its longest option (409 px).
   - Fix: `.opt select{width:100%;max-width:100%;min-width:0}` so a select shrinks to the column. Optional: shorten to "DW4 - 50/50/75/100%".
   - Time: 5 minutes.

3. Sound tab also scrolls sideways at 1000-1040 wide (371/419 at 1000). Toggle rows and descriptions are wider than the panel. Same cure family as 2: put `min-width:0` on the grid children of `.opt` and let `.desc` wrap. Check the Custom Music Pack row. Time: 10 minutes.

4. Search does not match select option labels.
   - Saw: "DW4" gives 0 matches even though Run Mechanics has "DW4 - 50%...". Same for "Vanilla", "Costs XP", "Instakill", "Wasteland" in a select that does not mention it in the description. `matches()` (index.html line 604) only checks name and description.
   - Fix: add `||(it[4]&&Array.isArray(it[4])&&it[4].some(o=>o.toLowerCase().includes(q)))`. Time: 3 minutes.

5. REMIX badge is never explained.
   - Saw: 9 settings carry a green REMIX tag, but the legend line says only "[ ] off [x] on [?] random", the badge has no tooltip (`title=""`), and the text exists only as CSS `::after` content, so it is not reliably read aloud. A new user cannot tell it means "not in the original randomizer, so flags using it will not work there".
   - Fix: add "REMIX = added in this version" to the legend line, set a `title` on the badged `.opt` rows, and say it in the Quest/title text. Time: 10 minutes.

6. Two colour controls with confusing names. "Site Color" (page theme, bottom of left column) and "Menu Color" (game border colour, Visual tab) sit in different places and both change colours; changing Menu Color does not change the page at all, so there is no preview. Fix: rename Site Color to "Page Color" (or "Site Theme") and add a small swatch next to Menu Color's select showing the chosen colour. Time: 15 minutes.

7. Search results drop the section notes. Run Mechanics' key note "Run Mechanics overrides Alternate Running Algorithm and Easy Charlock" is shown in the Battle tab but not in search results, where the two overridden options appear right below it with no warning. Fix: for matched items that have a section note, show the note under the section heading in results, or repeat it in the item's description. Time: 10 minutes.

8. "Choose your Dragon Warrior ROM first." appears in small text at the bottom-left of the Quest window and the whole page jumps down about 18 px (Quest window grows, columns shrink, Command loses another row). The same message also appears top-right ("No ROM loaded..."). Fix: reserve the line (min-height) or show the message in place of the top-right status, and focus the Choose File button. Time: 10 minutes.

9. While searching, the World tab keeps `aria-selected="true"` and the panel says "Search". Screen readers announce the wrong current tab. Fix: set `aria-selected=false` on all tabs in `show()` when q is non-empty. Time: 3 minutes.

10. Keyboard: from the tab list, Tab now passes 6 stops (Search, Prev sprite, Sprite select, Next sprite, Animate, Site color) before the first option (was 5 in review 1). Search at least is worth keeping; fix with a "Skip to options" link or move Hero and Site Color after the panel in the DOM. Time: 15 minutes.

## Previous items with changed status

- Biome ignored when Random Map is off: fixed. With Random Map off, Biome is disabled and omitted from Status.
- Hero preview path: fixed for a `site/`-rooted host (sprites load from `/sprites/`).
- Not re-checked: the earlier clipped Randomize button. At 1440 the button reads "Randomize" with no clipping (scrollWidth 111 = clientWidth 111); the label no longer has "!" and shows as RANDOMIZE.

## Not tested

- Real ROM build (no ROM supplied), a real music pack, Firefox/Safari, screen readers.
- The native dropdown popup list (headless capture does not show it), so I did not see how the long Run Mechanics option looks inside the open list.
- Phone widths, by instruction.

## Next action (under 2 minutes)

In `site/index.html` add `width:100%;max-width:100%;min-width:0` to the `.opt select` rule, then reload at 1000 wide and confirm the Battle tab has no horizontal scrollbar.
