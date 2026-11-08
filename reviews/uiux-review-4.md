# UI/UX review 4: site/index.html (DWR, Command Window design)

Date: 2026-11-06. Branch feature/custom-music, HEAD f15b152. Review only: nothing under `site/` was edited, nothing committed.

Screenshots: YES, real ones. Headless Chrome driven with puppeteer-core against `site/` served by `python -m http.server` on 127.0.0.1:48213 (stopped afterwards). The test server sent the wrong wasm MIME type, so the page fell back to ArrayBuffer loading; that is the test server, not the site. Desktop sizes only: 1920x1080, 1440x900, 1280x720, 1024x768. No emulator was launched. The local ROM `C:/ROMs/Dragon_warrior.nes` was used only in the local page; nothing was copied into the repo. Scripts, a synthetic music pack and screenshots are in the scratchpad `C:\Users\Ryan\AppData\Local\Temp\claude\c710b416a9ee2736\ba1af868-e2d3-490f-9988-8904ec45a839\scratchpad\` (`shots\`, `flow*.js`).

## What I saw vs. only read

Saw in screenshots (and drove): home at 1920/1440/1280/1024; Randomize with no ROM; bad flags; ROM load, bad ROM, remembered-after-reload; Sound tab with no pack, with a synthetic pack, with a corrupt pack; "Only show tagged tracks" and "Same track for dungeon floors 1-7" on and off; sprite select and Animate with the ROM loaded (all 87 sprite images load, no 404s); Run Bank block and tier hover; Search; Random Map off (dependent options dimmed); presets save and delete; Site Color Green; a full Randomize and download message (page reported success; headless Chrome did not write the file, so I did not inspect the .nes on disk).

Only read in the source: Copy button clipboard path, custom colour picker, the open native dropdown list, narrow-sprite edge cases, real FamiTracker song data (my pack used dummy data, so the "6 of 8 music picks did not fit" message I got is from fake data and is not a finding), Firefox/Safari, screen readers.

## Verdict

The Command Window layout is solid at 1280 and wider: all 10 tabs now show, keyboard flow works (Tab order, Arrow/Home/End on tabs, Enter/Space on toggles, `/` focuses search, Escape clears it), and the flag string round-trips. The weakest spots are first-run feedback (Randomize with no ROM), flag pasting, and the Sound tab's pack flow.

Counts: 2 High, 8 Medium, 5 Low (15 total). Tags: [NEW], [REPEAT] = in an earlier review and still unfixed (review file named), [PARTIAL] = earlier item only partly fixed.

## High

1. [NEW] Randomize with no ROM looks like nothing happened. `site/index.html:714`
   - Experience: click RANDOMIZE before loading a ROM. The only change is the existing line "No ROM loaded. Choose your uncompressed Dragon Warrior ROM." turning yellow (`.romalert`, line 302), at the top of the window, far from the button. The result area under the button stays empty. Focus moves to the file input but, after a mouse click, no focus ring shows (screenshot `a-noRomRandomize.png`). Yellow is the only signal.
   - Fix: also write "Load your Dragon Warrior ROM first (see ROM above)." into the result line (`o.textContent=`) and add a `.romalert` style that is not colour-only (for example `outline:2px solid #ffd23f` on the ROM row). Call `f.focus({focusVisible:true})` or `f.scrollIntoView()`.
   - Effort: 10 min.

2. [NEW] Pasting flags often fails and the error does not say why. `site/index.html:645-646`
   - Experience: the Flags box has `maxlength=40` and the code trims only after the browser has already cut the text. Pasting `Flags: IVIA...` (the first line of what Copy puts on the clipboard, line 651) gives `Flags: IVIAAVCFKACACQAAKAAAAZAUAAQBAAAAA` and then "Those flags are not valid...". Pasting ` ivIAA...AAAA ` with a leading space also fails (tested with Chrome's insert-text, which obeys maxlength). Lowercase alone is fine.
   - Fix: remove `maxlength` from `#flags` (keep the 40-character check in `setFlags`), strip everything but A-Z and 2-7 after an optional `Flags:` prefix, then validate. Change the message to say how many valid characters it found ("Found 33 of 40 characters").
   - Effort: 15 min.

## Medium

3. [NEW] A bad pack file wipes the pack you already loaded. `site/index.html:603-604`
   - Experience: with a good pack loaded, choosing a wrong file runs `setPack(null); rememberPack(null)`. Every track select goes back to disabled "Game default" and the saved copy is deleted. The status line reads `"badpack.json5" could not be read...`, but nothing says the old pack is gone (`h-badpack.png`).
   - Fix: on failure, keep the current `State.pack`, only set the status text ("...could not be read. Your pack X is still loaded.") and restore `inp.value=''`.
   - Effort: 5 min.

4. [REPEAT, review 1 #4] Track dropdowns are truncated at 1440 and 1024. `site/index.html:175, 179-180`
   - Experience: at 1440x900 the selects read "Random tagg" and "Game defaul" (`e-pack-loaded.png`, `z-reload-sound.png`). The selected value cannot be read. Fine at 1920. The "Dungeon Floors 1-7" label wraps.
   - Fix: change the option text to "Random" and "Default" (lines 575-576), and let `.trackcols` go to one column under about 1500 px (`minmax(min(100%,22rem),1fr)` to `minmax(min(100%,30rem),1fr)`), or drop the label column to 9rem.
   - Effort: 10 min.

5. [NEW] "Same track for dungeon floors 1-7" (on by default) plus "Only show tagged tracks" (on by default) hides songs tagged Floor2 to Floor7. `site/index.html:561-564, 584-585`
   - Experience: my pack has a song tagged `Floor3`. With both boxes ticked the combined row offers only "Game default | Random tagged | Cave Of Doom" (the Floor3 song is missing). A pack author who tagged every floor sees only Floor 1's songs and gets no hint that the rest exist. They must find and untick the checkbox.
   - Fix: when same-floors is on, treat any `Floor1`-`Floor7` tag as tagged for the combined row (change `hasTag(s,slot[1])` for that row to include `Floor2..7`), or add a note under the row: "Floor 2-7 songs hidden: untick Same track to pick them."
   - Effort: 15 min.

6. [PARTIAL, review 1 #5] The rules for the music pack sit in one long paragraph below 22 dropdowns, and the tag-count column has no heading. `site/index.html:588, 592`
   - Experience: the key warning "Original Credits needs Stat Scroll set too" is the last sentence of a four-sentence block at the very bottom of the pack box, scrolled out of view at 1440x900. The column of small "1" and "0" numbers has only a hover title. Slots with 0 tagged tracks show a plain disabled-looking "Game default" with no reason.
   - Fix: split the paragraph into three short lines; move the credits warning to appear under the Original Credits row when Stat Scroll is on "Game default"; give the number column a header ("Tagged") in each `.trackcol` heading; for a slot with 0 tagged tracks and the filter on, use the first option text "Game default (no tagged tracks)".
   - Effort: 25 min.

7. [NEW] After a reload the file inputs say "No file chosen" while the status says "Loaded". `site/index.html:663-674, 788`
   - Experience: ROM and pack are restored from IndexedDB, so "Loaded: Dragon_warrior.nes (remembered in this browser)" and "Loaded 8 tracks" show next to a native control that says "No file chosen" (`z-reload.png`, `z-reload-sound.png`). Before reload the filename also appears twice on the ROM row (native label plus status).
   - Fix: hide the native filename text (`input[type=file]{color:transparent}` with the button restyled) or, simpler, set the status to "Loaded from this browser: NAME" and add a visible "Replace" label on the button. Remove the repeated name by trimming the native one.
   - Effort: 20 min.

8. [REPEAT, review 1 #3 and review 3 #8] The Quest window grows and pushes the page down when a message appears, and long messages break mid-word. `site/index.html:41, 237-238`
   - Experience: bad flags, a build error or "ROM downloaded..." adds about 36 px and everything below jumps (`b-badflags.png` vs `a-noRomRandomize.png`, `i-randomized.png`). `.result{word-break:break-all}` breaks words in the middle ("did not cha / nge").
   - Fix: reserve two lines (`.idfields .result{min-height:2.7em}` and drop the `:empty{display:none}` rule), and use `overflow-wrap:anywhere` instead of `word-break:break-all`.
   - Effort: 5 min.

9. [NEW] The hero picture is tiny or missing on ordinary laptop heights, so sprite choice has little feedback. `site/index.html:296-299`
   - Experience: size is `clamp(48px, 100vh - 855px, 190px)`. At 1440x900 it is 48 px; at 1280x720 and any height under 860 px the picture is hidden (`display:none`), leaving only the select (`home-1280.png`). It is full size only at 1080 high. The sprite list has 87 entries and "Random" shows a "?".
   - Fix: keep the 48 px preview on short screens (remove `.heropic{display:none}` at line 298) and put the preview beside the select instead of above it so it costs width, not height.
   - Effort: 15 min.

10. [NEW] Copy is disabled until after a successful Randomize, with no reason shown. `site/index.html:651, 655`
    - Experience: the greyed Copy button sits between Checksum and RANDOMIZE. A player who wants to share flags and seed (the link is already in the address bar hash) cannot. The disabled button's title is the same "Copies the link, flags, seed and checksum".
    - Fix: enable Copy always; leave the "Checksum:" line out of the text when there is none. Or keep it disabled and change the title to "Available after Randomize".
    - Effort: 5 min.

## Low

11. [REPEAT, review 1 improvement 2] All text is forced to 12 px. `site/index.html:110` (`body *{font-size:12px!important}`). Computed sizes on the page are only 12, 24 and 32 px. Browser minimum-font settings are overridden. Fix: use `rem` for the base size (for example 14 px on screens wider than 1280) and remove `!important`. Effort: 20 min plus a layout check.

12. [NEW] Off toggles look like plain text, and turning one off takes three clicks. `site/index.html:18-20, 48, 449`. `--tog-bd:0` gives an "off" button no border, so `[ ]` reads as text. A toggle goes off to on to random to off; I measured map going on, random, off. The legend line explains the marks but not the cycle. Fix: restore a 1 px border on `.tog` for state 0 (`--tog-bd:1px solid var(--soft)`) and add "click to cycle" to the legend. Effort: 5 min.

13. [REPEAT, review 1 smaller errors] Two things are called "Quest" (top window `site/index.html:912`, tab id `quest` in `TABS`), "Speed & Help" has no help items, and there is no `<main>` landmark (only `<nav>`, `<section>`, `<div id=app>`; line 818). Fix: rename the top window "Setup", the tab "Speed & Display", and make the panel `<main>`. Effort: 5 min.

14. [PARTIAL, review 3 #5] REMIX badges are explained only by a hover tooltip (`root.title`, line 635) while the badge text is CSS `::after` (line 227). Fix: add "REMIX = only works in this version" to the legend line (line 619 `LEGEND`). Effort: 5 min.

15. [NEW] Options that need Random Map are dimmed to 55 percent (`.opt.needsoff`, line 277). Dimmed description text falls to about 3.3:1 against the blue (my estimate from the colours, not measured with a tool), and the reason is only in the small description ("Needs Random Map") (`t-mapoff.png`). Fix: add a visible "Needs Random Map" tag in the label row, and keep opacity at .7. Effort: 10 min.

## Checked and fine

- Keyboard: tab order from page start is ROM, Forget, Flags, Seed, Random Seed, Checksum, Randomize, presets, preset name, Save, tab list, "Skip to options", Search, Changed, Remix, hero controls, Site Color. Arrow keys on the tab list change panels (checked: 2 Down moves two tabs). Space and Enter both toggle.
- Errors: bad flags show a clear message; a non-numeric seed is replaced with a message (earlier review's silent-seed item is fixed); a non-ROM file shows a specific message.
- Command window: all 10 tabs show at 1440x900 and 1280x720 (earlier review-3 #1 is fixed). No horizontal overflow at 1024.
- Contrast: body and description text is about rgb(199,199,229) on blue (high). REMIX green on blue is readable. `aria-selected` is cleared while searching (review-3 #9 fixed).
- Full Randomize with ROM and no pack: 95 ms in-page build, checksum shown, success message names the DWR file.

## Next action (under 2 minutes)

In `site/index.html` line 645, remove `maxlength:40` from the Flags input and reload; paste `Flags: ` plus a flag string and confirm the box keeps all of it.
