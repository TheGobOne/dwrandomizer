# Flag and feature reference

Detail for the features in the [README](../README.md). Seed numbers and flags are **not** compatible with mcgrew's official randomizer or with juef17's fork. Names match the labels on the website, whose tabs are World, Treasure, Hero & Magic, Monsters, Battle, Shops & Inns, Quest, Speed & Info, Visual and Sound.

The first part covers what this remix added. The second part is inherited from juef17's fork and was kept because it is still accurate.

- [Remix additions](#remix-additions)
- [Inherited from the fork](#inherited-from-the-fork)
- [Command line options](#command-line-options)
- [Known issues](#known-issues)

## Remix additions ##

#### Run Bank ####

Each win earns points toward a guaranteed escape, and the `BANK` battle command spends one. It can't be used on the Dragonlord. A dot after a one digit bank means a third of the way to the next run, and two dots mean two thirds.

Recommended: use the `Vanilla` Run Mechanics setting with Run Bank. The banked escapes are the only thing that changes how a run works, so the bank stays predictable.

Stronger monsters earn more. Kills per run, from the weakest tier of monster to the strongest:

| Earn level | Kills per run |
|---|---|
| Easy | 12, 8, 6, 4, 3, 2, 1 |
| Normal | 16, 12, 8, 6, 4, 3, 2 |
| Strict | 24, 16, 12, 8, 6, 4, 3 |
| Very Strict | 32, 24, 16, 12, 8, 6, 4 |

The bank holds 9 runs unless a cap of 1 to 99 is chosen.


#### Quick Spell ####

Adds a battle command that casts your last spell again, without opening the spell menu.


#### Biome ####

Needs Random Map. Chooses the shape of the generated overworld. The choice is part of the flags, so it is part of the checksum.

- **Continent**: the usual random map.
- **Caldera**: a mountain ring with two passes. Charlock sits in the middle of the basin and Tantegel is always outside the ring.
- **Wasteland**: mostly desert and swamp, with few forests. Pair it with a lower Encounter Rate.
- **Riverlands**: one large land cut up by rivers, joined by bridges.
- **Lake**: a ring of land around a lake. Charlock sits on an island in the middle, reached by 2 to 4 long bridges. You still need the Rainbow Drop to cross from the island to the castle. Tantegel is never on the island.
- **Earth**: a map of the world. Europe, Asia and Africa are one continent, joined by bridges (straight or bent once) to Australia, Japan, the Arctic and Antarctica. North and South America are reached only by raft: each has a dock (a cave tile on the coast) that takes you to a dock in Europe, Asia or Africa, a different one for each, and back. Always normal size.
- **Westeros**: one long continent. The Wall, a line of mountains across the North, has a single pass, and Charlock is always beyond it while Tantegel is always south of it. The Neck, partway down, is swamp. Always normal size.
- **Random**: picks one of the seven.

Lake, Earth, Westeros and later biomes are stored in a 26th flag byte, so their flag codes are 42 characters. Codes for the first four biomes stay 40 characters. Old codes with Biome set to Random keep picking from the first four.


#### Safe Start ####

Swamp within 3 tiles of Tantegel becomes grass, so the first steps of the game are safe. It works with every map and Biome, and it is part of the flags and the checksum.


#### Encounter Rate ####

`Default` keeps the original random roll on every step. `Low`, `Medium` and `High` use a danger meter: each step adds points by terrain, and a fight check happens when the meter reaches a hidden random target. Steps in towns, castles and shrines do not count. Streaks and long droughts go away, and Repel still works.

| Rate | Steps on grass | Forest, swamp, brick, barrier | Hills, desert |
|---|---|---|---|
| Low | 12-32 | 10-26 | 6-16 |
| Medium | 8-24 | 7-20 | 4-12 |
| High | 4-16 | 4-13 | 2-8 |

The meter also removes the original game's hidden checkerboard on grass, where neighbouring tiles have different odds.


#### Auto Return ####

When the Dragonlord's second form dies, Return is cast for you and you arrive in front of Tantegel, or at your Return to Town point if that flag is on, instead of standing at Charlock. Command line: `--auto-return on`.


#### Responsive Controls ####

On the overworld, walking, the command window and pause answer a press at once instead of waiting up to 15 frames. In towns and dungeons a short press is kept until the game can act on it, because NPCs move in time with that wait. A button newly pressed during a step is kept for when the step ends. Hills no longer slow you down: the original game waits 3 extra frames on every hill step. Very full ROMs may not build with it on. It is part of the checksum. Command line: `--responsive-controls on`.


#### Auto Stairs ####

Stepping onto stairs takes them at once, the way stepping onto a town enters it, so there is no need to open the command window and choose `STAIRS`. It happens before any fight check on that step. Arriving on stairs, from other stairs or a warp, does nothing until you step off and back on. Charlock's hidden stairs work once you have found them by searching, and then you step off and back on. The `STAIRS` command still works, and Stair Shuffle is followed. It is part of the checksum. Command line: `--auto-stairs on`.


#### Free Charlock ####

Entering Charlock from the overworld puts you beside the Dragonlord and starts the fight, with no dialogue. Run away and you stay in his room; the fight starts by itself again only after you leave and come back. Short Charlock, Easy Charlock, Three's Company, Cursed Princess, Radish Finish and Disguised Dragonlord are turned off and disabled while it is on. On a very full ROM the build stops, as it does for other flags that need space. It is part of the checksum, along with Auto Return. Command line: `--free-charlock on`.


#### Show Key & Herb Counter ####

Adds a row to the bottom of the stats window that reads `K n  H n`: keys and herbs. Command line: `--key-herb on`.


#### Disable Screen Shake ####

Keeps the screen still when you take damage.


#### Custom Music Packs ####

Pick a pack in the Custom Music Pack option on the website, or pass `--music pack.json5` on the command line. Packs hold FamiTracker modules and play through the Dn-FamiTracker driver. Up to 24 songs and about 17 KB fit, and songs that don't fit are skipped. A scene with no tagged song keeps its default music. Sound effects can't be replaced.

Scene tags: `Title`, `Tantegel`, `Town`, `Overworld`, `Charlock`, `Cave`, `Encounter`, `Dragonlord`, `CharlockFinalFloor`, `Hauksness`.
One `Charlock` song plays on all seven floors of Charlock, to save music space. `CharlockFinalFloor` and `Hauksness` use it if they have no song of their own. `Cave` plays in Erdrick's Cave, the mountain cave, Garin's Grave and the swamp cave; without a `Cave` song those keep the game's own floor music. The old man caves (the Tantegel basement and the shrines) have no song of their own: whatever was playing keeps playing, so the overworld song from an overworld tile, or the town or cave song when the cave is joined to one.

Event tags: `StatScroll` (stat scroll), `OriginalCredits` (original credits, or the stat scroll if the pack has two; with Skip Original Credits on the stat scroll plays only its own `StatScroll` track). A song with an event tag is used only for that event. Jingles always play the game's own music. With a pack loaded, the built-in random credits song is left out so music bank 5 can hold pack songs: the stat scroll plays the pack's `StatScroll` song, or is silent without one. If the pack's music does not fit at all, the built-in credits song comes back.



## Inherited from the fork ##

#### New post-credit music ####

Credits go to bran, auriplane, nicetas and juef17. See `./expansion/music` for a list of songs. Open them with FamiStudio for more information.


#### Disguised Dragonlord ####

- A random NPC will have the Dragonlord dialogue;
- The NPC at the bottom of Charlock, still with the Dragonlord sprite, will tell the player the location and disguise (or *sprite*) of the new Dragonlord.
- Upon getting the Ball of Light, the player is teleported outside of the area the Dragonlord was fought in.

Every NPC can be the Dragonlord, except for:

- the King;
- the coordinates old man;
- the uncurse old man;
- the Rimuldar key vendor.

Additionally, if the `Open Charlock` flag is off, it cannot be:

- the Staff of Rain old man;
- the Rainbow Drop old man.

Also, if the `Don't Require Magic Keys` flag is on, it obviously cannot be a key vendor, and if the `Radish Finish` flag is on, it cannot be the radish vendor.

This makes an approximate probability of 1/120 that a specific NPC is the Dragonlord.

In order to make room for the hint, one of the merchants in the bottom left corner of Tantegel has had his text hijacked and will be the same as the other merchant. Therefore, if they both have the same text, the flag is active (but the Dragonlord may still be in his vanilla location with a small probability).


#### Randomized Flute Music ####

Randomizes the Fairy Flute song that is played when using the flute in a battle or on the overworld. The new song is chosen at random from over 60 possible songs (including the original). Since their lengths are variable, the song is tied to the seed number, not to give an advantage in a race situation. The songs are all between 2.4 and 8.2 seconds long, averaging about 5 seconds, and the original song is about 5 seconds long. For a preview of the songs or to know where they are from, see #community-contributions channel on Discord (Flute Toots thread). Major thanks to AsmadiGames for the initial work on this, as well as a handful of flute songs.


#### Vendor Shuffle ####

This shuffles every weapon, tool, fairy water and key vendor, as well as the radish vendor. The locations, positions, sprites and prices are not modified. If the `Don't Require Magic Keys` flag is on, then the Rimuldar key vendor and the two other key vendors, wherever they are, are removed as they would ordinarily be. Otherwise, the Rimuldar key vendor will stay in Rimuldar, so the key logic does not have to change.


#### Charlock Inn ####

There is now an innkeeper in Charlock, near the treasury. Why an inn in Charlock? Why not?


#### Only Healmore ####

Never learn any spell other than Healmore (and Radiant, should the `Level 1 Radiant` flag be on). Like the No Hurtmore flag, this only works with `Random Spell Learning` on.


#### Magic Herbs ####

Herbs refill MP rather than HP.


#### Inn & Key Prices ####

Inn and key prices can now be shuffled or randomized (1G - 255G). If you sell a key to an item vendor, you will get half the amount needed to buy a key in Rimuldar.


#### Dragonlord Crits ####

You can now allow critical hits against the Dragonlord. There is a separate flag for each of his two forms (`DL1 Crits`, `DL2 Crits`).


#### Crit Chance ####

The Excellent Move rate can be modified.


#### Bonk Damage ####

Walking into walls can deal damage to the player. The amount is chosen on the website: None, 1 HP, 2 HP, 20 HP, Instakill, Random (0-80), Current HP / 2, or one of those at random.


#### Disable Red Flashes ####

Prevents the screen from flashing when walking on damage tiles. If bonk damage is on, the usual swamp damage sound is used instead of the bonking sound.


#### DK Flute ####

Make it possible to drop the flute on finding a new item with a full inventory, or sell to a shop for 1g.


#### Return Escapes ####

The Return spell can be used in battle for a guaranteed escape.


#### Return To Town ####

Wings and Return send you to the last place you saved or used an inn at.


#### Warp Whistle ####

The Fairy Flute will work as a warp whistle outside of battle, cycling between places you saved or used an inn at. The order is:

- Tantegel
- Brecconary
- Kol
- Garinham
- Cantlin
- Rimuldar

The list is kept for each save slot, so resetting or turning off the game no longer loses it.


#### Level-Up Refill ####

Have HP and MP refilled after leveling up.


#### Random Key Carry and Random Herb Carry ####

The maximum number of keys and herbs will be randomized.

- Herbs: between 0 and 9;
- Keys: between 2 and 9 (or 1 and 9 if `Unbreakable Keys` is on, or between 3 and 9 if `Random Princess Location` is on, or between 4 and 9 if `Stair Shuffle` and `Short Charlock` are on, or between 5 and 9 if `Stair Shuffle`, `Short Charlock` and `Random Princess Location` are on).


#### Flute Is For ####

The Fairy Flute can now put all enemies to sleep. The Dragonlord's second form can be excluded or not.


#### Level 1 Radiant / Repel ####

Makes sure the player starts with Radiant and/or Repel and some MP. Like the No Hurtmore flag, this only works with `Random Spell Learning` on.


#### Hurtmore Doors ####

Blast doors open with a cast of Hurtmore.


#### Winter Theme ####

Thanks to Cyberdark86, Angel_FM and friends, you can now save a very wintery-looking Alefgard. Newly implemented: this will not change a seed's checksum!


#### Radish Finish ####

A new way to win the game: talk to the radish vendor while carrying Gwaelin so you can share those sweet, sweet radishes with her.


#### Run Mechanics ####

Make the game use alternate mechanics for running away from an enemy from another game in the series. For Safer Dragon Warrior I, mechanics are untouched but a 4th run attempt will always be successful. For Dragon Warrior II, it's a flat 2/3 chance every time. For Dragon Warrior IV, it goes like this:
- On your first and second run attempt in a battle, you have a 50% chance to get away.
- On your third run attempt in a battle, you have a 75% chance to get away.
- On your fourth run attempt, or if the enemy's asleep, you're guaranteed to get away.

For `Costs XP`, you are guaranteed to run away if your XP is at least 1/8th of the enemy's XP drop: in that case, that amount is deducted from your XP. Yes, this means you can lose levels! Also, note that running from sleeping enemies won't cost you XP, nor will running from either Dragonlord form (as they don't give experience).

This flag overrides `Alternate Running Algorithm`. `Easy Charlock` is turned off and disabled while any Run Mechanics option is on, including Random.


#### Unbreakable Keys ####

Any key used outside of the throne room will not be removed from your inventory.


#### Ascetic King ####

King Lorik is feeling fair today and will let you keep your hard-earned gold when you die.


#### Show Death Counter ####

Adds a row to the stats window showing how many times you've died. With Show Key & Herb Counter on as well, that row sits under it.


#### Step Counter ####

The post-credits sequence will show how many steps you've taken in your entire playthrough.


#### Differences from Standard ####

The `Summary` tab will now show differences between the chosen flagset and the standard flagset. Active cosmetic flags will also be displayed separately there.


#### Modifiable Tristate Summer Sale chance ####

Normally, having `Summer Sale` on tristate will make each piece of gear have a 50% chance to have a reduced price. This probability can now be modified (12.5%, 25%, 50% or 75%).


#### Unguarded Overworld Search Spot ####

There is now a flag to ensure the overworld search spots, a.k.a. coordinates, will be unguarded, even if `Treasure Guards` is on.


#### Gold Chest Contents ####

The amount of gold that gold chests hold can now be modified.


#### Random Princess Location ####

Gwaelin might not be in her usual Swamp Cave today. Look for her in [one of these](https://juef.free.nf/dwr/random_princess_location/) locations (or in her vanilla spot). If `Stair Shuffle` is on, she is only placed where she can be reached without going through Charlock.


#### Fast Flute ####

The Fairy Flute music plays much faster (about 0.7 seconds, down from 4.5), with or without `Speed Hacks`. `Speed Hacks` no longer speeds the flute on its own. Older flag codes keep their flute: the old `Normal Flute Speed` bit is this flag turned off.


#### Hints ####

In the vanilla game, some NPCs give directions that become just plain wrong when randomizing the map. `Hints` set to `Corrected` is an attempt at fixing them, making the detour to some NPCs somehow worth it.

Here's a list of changed dialog:
* NPC in Tantegel: `East of this castle is a town…` (this is a throne room guard's dialogue about Brecconary... but only before you leave it the first time!)
* NPC in Brecconary: `Within sight of Tantegel Castle to the south is Charlock…`
* NPC in Brecconary: `Go north to the seashore, then follow the coastline west until thou hath reached Garinham…` (the message is changed to give just one generic direction)
* NPC in Kol: `Dreadful is the South island.` (now says `entire world`, because there's not always 2 continents, and it may be confusing to give a single direction from one to the other)
* NPC in Kol: `Hast thou been to the southern island?` (changed to `outside of this hamlet`, for the same reason)
* Same NPC in Kol, if you say 'No': `To the south, I believe, there is a town called Rimuldar.`
* NPC in Kol: `East of Hauksness there is a town, 'tis said, where one may purchase weapons of extraordinary quality.`
* NPC in Garinham: `Once there was a town called Hauksness far to the south…`
* NPC in Garinham: `It is said that the Princess was kidnapped and taken eastward.` (for this one, the direction was dropped: it might get confusing with 2 entrances to the cave, and with any or both of them being possibly in a town. Instead it gives a hint about which cave she is in, in case you're playing with `Random Princess Location`.)
* NPC in Rimuldar: `Heed my warning! Travel not to the south` (changed to `inside caves`, to avoid messing with zones and enemies to find out the most difficult one.)
* NPC in Rimuldar: `Hast thou found a magic temple? (No:) Go to the south.`
* NPC in Rimuldar: `Over the western part of this island Erdrick created a rainbow.` (again, the island thing is bad, so a more generic message without a direction is used)

In most of the above, just the direction is changed. For instance, if you're in Kol, and Hauksness is at (50, 43) and Cantlin is at (44, 19), the NPC in the weapon shop will say: `North of Hauksness, 'tis said there is a town where one may purchase weapons of extraordinary quality.`, since Cantlin is just a bit West but a whole lot North of Hauksness.

Also, the NPC in Rimuldar hinting towards the Rainbow Drop cave will give directions to Tantegel or Garinham, should that cave's entrance be there.


#### Normalized Monster XP/Gold ####

XP and Gold gained from monsters will be randomized, but within [pre-set, hardcoded limits](https://juef.free.nf/dwr/Normalized_XP_Gold.html). These limits have been determined by aaron2u2 with help from omegastrep. This flag will override others that modify the same values.


#### Disable Music / Don't Mute Jingles ####

While `Disable Music` exists in mcgrew's official version, it has been improved such that input from two seeds differing only by that flag will sync, and will not affect checksum anymore. It now mutes more things as well, so for some players' convenience, a new flag allows to not mute the Silver Harp, Fairy Flute and Gwaelin's music jingles.


#### Don't let Lorik or the Dragonlord end your game ####

Unfortunate accidents have happened before! Courtesy of mcgrew, the king doesn't ask the player if they want to continue playing when they talk to him. Also, accepting the Dragonlord offer is not possible anymore unless `Three's Company` is on and you're carrying the princess.


#### Speed Hacks & jingles ####

The Silver Harp and Gwaelin jingles are now sped up by the `Speed Hacks` flag.


#### NPC Guillotine ####

Talk to any mobile NPC to make them disappear! They will come back if you exit and reenter the area you found them in. If `Disguised Dragonlord` is on and the Dragonlord happens to be a mobile NPC:
* If you run from the fight during the Dragonlord's first form, he will disappear and you will have to leave and reenter the area he was in to initiate the fight again.
* If you run from the fight during the Dragonlord's second form, he (and all other NPCs from that area) will respawn in their original location immediately.


#### Masked Overworld Locations ####

Towns and caves share the same sprite, so you'll have to enter them to know which of the two they are.


#### Random Spell Costs ####

Spells cost a random amount of MP between 1 and 8.


#### Random Town Entrances ####

The player's location upon entering a town will be randomized. Note: Garinham has no entrance that is behind the door. All possible entrances are listed [here](https://juef.free.nf/dwr/random_town_entrances/).


#### Forfeit Trigger ####

When on your status screen, hold Start and Select to warp to the end of the game. Huge thanks to Crump's Brother for this one!


#### Build Options ####

Which stats the hero grows best in. Set on the Hero & Magic tab. Another huge piece of work from Crump's Brother!

* `Original`: the name impacts your stat build. All builds are -3/+10% for the selected stats.
* `Free Selection`: stat builds are freely chosen on a menu at the start and are not tied to name length. Text speed is set to `Fast` automatically.
* `Free Selection Plus`: two new stat builds are added to Free Selection (HP-Agility and Strength-MP).
* `Free Selection Unified`: all stat builds are merged into one that has the best of all of them and no Str/MP penalties.
* `Random`: one of the options above, chosen at random.


#### Insta-print Player Name ####

Text boxes will now display the player's name instantly, rather than character by character. Notes:
* Feature is ignored if the `Fast Text` flag is on (it shouldn't matter then anyway) or a `Build Option` is selected (because Crump's Brother's code does something similar then).
* Normal or Slow text speeds are converted to Fast. They could be handled but it would cost some bytes and pain, and nobody uses them anyway!
* While the name prints instantly regardless of its length, having a long name might require more lines for some texts to be displayed, and that does take a little more time. To some extent, that is even true with mcgrew's official DWR when the `Fast Text` flag is on. But there really isn't anything to be done about that.


## Command line options ##

Usage: `dwrandomizer [options] <rom_file> [flags [seed [sprite]]] <output_dir>`. Options come first, each as `--name value`.

| Option | Values |
|---|---|
| `--music` | Path to a JSON5 pack |
| `--bank-cap` | 1 to 99 |
| `--bank-level` | `normal`, `easy`, `strict`, `verystrict` |
| `--encounters` | `default`, `low`, `medium`, `high` |
| `--quick-spell` | `on` |
| `--key-herb` | `on`: adds a keys and herbs row to the stats window |
| `--auto-stairs` | `on`: stepping onto stairs takes them |
| `--spoiler` | `on`: writes a `.spoiler.txt` next to the ROM. It changes the checksum, so a spoiler seed never matches one without |

Most of these also exist as options on the website, including the spoiler log.


## Known issues ##

- In mcgrew's official DWR version, when a vendor is missing in Rimuldar (e.g. key vendor), the very top left tile of the town is an invisible wall. It doesn't matter then because there is no point in going there if there is no vendor. But since vendors can now be shuffled, it can be a valid check, and notably, coupling this with bonk damage could ruin a player's day. The block has been replaced with a water tile to avoid player frustration. Something similar happens with Cantlin.
- `NPC Guillotine` can also trigger the above problem, but if that happens, the block won't be replaced with a water tile. That's the penalty for murder.
- With the `Disguised Dragonlord` flag, there will likely be some graphical glitches in both Dragonlord fights, depending on his location. They are purely cosmetic and do not affect the ability to run away or to complete the game.
- The village music plays after spending the night at the `Charlock Inn`, but it fixes itself as the music changes to something else (e.g. enemy encounter, death necklace use, …).
- Copying a save does not copy its `Warp Whistle` list. Continuing a game at 0 XP is treated as a new game and clears the list and the `Run Bank`.
- If too many flags that require ROM space are active at the same time, it might not be possible to generate a seed. Biggest offenders are `Build Options`, `Hurtmore Doors` and `Warp Whistle`, so turn one of those off if you run into that problem.
