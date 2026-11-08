# UI/UX review: site/index.html (TheGobOne Remix v1.0)

Date: 2026-11-06. Branch feature/custom-music, uncommitted changes. Review only, nothing edited.
Method: headless Edge driven over DevTools against `site/` served by a local Python server. Widths 1920, 1440, 1280, 1024, 768, 390, 320. Screenshots and scripts are in `C:\Users\Ryan\AppData\Local\Temp\claude\c710b416a9ee2736\ba1af868-e2d3-490f-9988-8904ec45a839\scratchpad\` (shots\ and t1-t8.mjs).

Verdict: no crashes and no console errors. The flag round trip, ROM build and keyboard flow all work. The problems are one misleading Biome behaviour, three visible glitches and small text.

## Errors (most severe first)

1. Biome is silently ignored when Random Map is off, but it still shows as active.
   - Steps: World tab, leave Random Map `[ ]`, set Biome to Wasteland.
   - Result: Biome stays enabled and the Status list says "Biome: Wasteland". The flag string changes, but the generated world is the vanilla map.
   - Evidence: `common/map.c` only reads BIOME inside RANDOM_MAP paths (lines 490-498, 1260). The select's `disabled` was `false` in the test. The only hint is "Needs Random Map" in the middle of the description.
   - Fix: disable Biome (or grey it with the note "Turn on Random Map") while `State.v.map===0`, and skip it in the Status list. Alternative: show "Biome: Wasteland (needs Random Map)".

2. The Randomize! button text is clipped on desktop (verified).
   - At 1440x900 the button has scrollWidth 125 against clientWidth 110. The "!" is cut off and the text touches the edge (`after-build.png`).
   - Cause: `.idfields .row .btn.go{flex:2 1 0}` with `white-space:nowrap`.
   - Fix: give `.btn.go` a `min-width:max-content` or `padding-inline:.8rem`, or put the button on its own row.

3. The result message is stale and shifts the layout.
   - After a build, "ROM downloaded: DWRando.<seed>.<flags>.nes." stays on screen. When I changed Biome the checksum cleared, but the message still looked like it belonged to the new flags.
   - The message is about 2 lines long, so the whole page below it jumps about 24px (compare `d-sound.png` with `after-build.png`).
   - Fix: clear `.result` in the same listener that clears the checksum (`State.on` in `flagsBlock`), and reserve space for the message or truncate the filename.

4. Track slot dropdowns are clipped at 1440 wide (verified in `sound-pack.png`).
   - "Random tagged track" shows as "Random tagge". "Dungeon Floor 1" wraps and leaves the "1" alone on a second line.
   - Cause: `.trackrow{grid-template-columns:9.5rem minmax(0,1fr) 2rem}` is too narrow in the 2-column layout.
   - Fix: shorten the option to "Random tagged" (or "Random"), widen the label column to 11rem, and use "Floor 1" to "Floor 7" under a "Dungeon" sub-heading.

5. A track slot with no tagged tracks looks dead.
   - With "Only show tagged tracks" checked (the default), Title, Castle, Hauksness, Inn and others show only "Game default". There is no hint that unchecking the filter unlocks more songs.
   - Fix: when the filter is on and the slot has none, add a disabled-style first option "No tagged tracks (untick filter)".

6. The hero preview path `../webui/sprites/` only works if the site is hosted from the repo root.
   - Served with `site/` as the web root, the image URL was `/webui/sprites/...` and `naturalWidth` was 0, so a broken image shows.
   - The path is not new (it is in HEAD). I could not check the real hosting, so treat this as a risk. Fix: copy the sprites into `site/` or document the hosting root.

## Smaller errors

- Pasted flags with an odd Biome bit pattern (for example one "maybe" bit only) silently become Continent. Checked for raw values 2, 3, 18, 32, 33, 35, 48, 50 and 51. Only the five encodings Continent=0, Caldera=1, Wasteland=16, Riverlands=17 and Random=34 round-trip. That is fine for the UI's own flags.
- An empty or non-numeric seed is cleared with no message, and Randomize then uses seed 0. Fix: show "Seed must be a number" or auto-fill a new seed.
- Two things are named "Quest": the top window and the Command tab. Rename the window to "Setup" or "Your Quest".
- The tab "Speed & Help" has nothing that is help. It holds Fast Text, credits and counters. Rename it to "Speed & Display" or "Speed & Info".

## Verified working

- **Biome:** all 5 values round-trip through the flag string and back (IVIAAV, IVIACV, IVIBAV, IVIBCV, IVICEV). Real builds of Caldera, and of Caldera with Very Small, succeeded (checksums f83a0e91623ce172 and others). The Random Map toggle also works as a tri-state.
- **Keyboard:** Up, Down, Home and End move through the tabs. Enter and Space both change a toggle (off, on, random). A label click toggles. The focus ring is visible. Bad flags and bad characters show a clear error and reset the box. Seed is clamped to 2^64-1.
- **Track slots:** a synthetic pack loaded (6 tracks, SFX excluded as designed). Slots with tagged tracks default to "Random tagged". Picking an untagged song, then re-ticking the filter, shows "Untagged Jam (not tagged)". The pack and picks survive reload. The Sound tab badge and the Status "Music pack: x (13 chosen)" are correct.
- **Contrast:** white on all 5 site colours is 8.4:1 or better. The muted text is 5.8:1 or better. The gold section titles are 5.8:1 or better.
- **Layout:** no horizontal overflow at 320 to 1920 wide. Small text sizes are listed under improvements.
- **Spelling and help text:** no typos found in the option text. The Biome description matches the C code (Caldera, Wasteland and Riverlands all exist).

## Improvements, ranked by value for effort

1. Fix errors 1 to 3 together. About 25 minutes, all in `index.html`.
2. Text size. A forced `body *{font-size:12px!important}` makes nearly all text 12px, and the pixel font is built for 8px multiples. On phones I measured text at 12px.
   - Fix: raise to 16px under `@media (max-width:720px)`. About 10 minutes plus a check of the layout.
3. Tab order. From the tab list a keyboard user passes 5 stops (Site colour, three hero controls, Animate) before reaching the first option. Move Site Color and Hero after the panel in the DOM, or add a "Skip to options" link. About 15 minutes.
4. Phone: "Choose File / No file chosen" is cut to "No fi...osen" at 320. The Randomize button sits about 700px down, below the ROM, flags and seed. Consider a sticky Randomize bar. About 30 minutes.
5. At 1024x768 three scrollbars appear at once (Command list, options, Status), and only 7 of 10 tabs are visible. Hide the Status panel under 1100px or put it in the panel footer. About 20 minutes.
6. Add `aria-live="polite"` to the Status panel, add a `<main>` landmark, and add a skip link. About 10 minutes.
7. Biome select: add a one-line note per option, for example "Continent: the usual map". The current description is three sentences long. About 5 minutes.

## Could not test

- Real screen readers, real touch devices, Safari and Firefox (Edge only).
- A real music pack with FamiTracker data. I used a synthetic pack with dummy data and did not build a ROM with it.
- The actual deployment (hosting root, wasm MIME type). My Python server sent the wrong MIME type and the page fell back to ArrayBuffer instantiation. That is the test server, not the site.
- Whether the built ROMs play correctly. No emulator was launched, as instructed. Test builds used the reduce-flashing option and default music.
- Colour picker (Custom site colour) interaction and the delete-preset confirm dialog.

## Next action (under 2 minutes)

Open `site/index.html`, search for `.btn.go{white-space:nowrap}` and add `min-width:max-content` to it. That fixes the clipped "Randomize!" label.
