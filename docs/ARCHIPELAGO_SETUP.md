# Playing DWR in Archipelago

This guide covers a solo or multiworld DWR seed on Windows. It needs Archipelago 0.6.8 or newer, the DWR randomizer, and your own Dragon Warrior (NES) ROM.

## 1. Install the pieces

1. Install Archipelago. Its Launcher and BizHawk Client are included.
2. Get the DWR randomizer (`dwrandomizer.exe`). Note its folder.
3. Copy `dwr.apworld` into the `custom_worlds` folder inside your Archipelago install.
4. Keep your vanilla Dragon Warrior ROM somewhere you can find it.

## 2. Point Archipelago at DWR

Open `host.yaml` in the Archipelago install folder and add this section. Use your own paths.

```yaml
dwr_options:
  rom_file: "C:/path/to/Dragon_warrior.nes"
  randomizer: "C:/path/to/dwrandomizer.exe"
```

`rom_file` is the vanilla ROM. `randomizer` is the DWR executable. Archipelago uses both to build your game ROM.

## 3. Set up your player file

Make a YAML file for yourself, for example `MyName.yaml`:

```yaml
name: MyName
game: DWR
DWR:
  dwr_flags: IVICEV2EKADEAMAAAADQAEBWAAAJACAAAAIBQAAA
```

Paste your own flag string from the DWR site in place of the example. Archipelago rejects these flags: Random Chest Locations, Stair Shuffle, Cursed Princess, Three's Company, Radish Finish and Disguised Dragonlord. Turn them off on the site before you copy the string.

## 4. Generate a seed

Put your YAML in Archipelago's `Players` folder and generate. The output is a `.zip` with a `.apdwr` file inside.

## 5. Build your ROM

1. Open the `.apdwr` file with the Archipelago Launcher or the BizHawk Client.
2. Archipelago runs the DWR randomizer on your vanilla ROM and writes a `DWR.<seed>.<flags>.nes` file next to the patch.

## 6. Play

1. Open the built ROM in BizHawk with the NES core.
2. In the BizHawk Client, connect to the server with your room address and slot name.
3. Chests and search spots send checks. Items you receive appear in your inventory, and the game shows "Thou hast found the <item>." when one arrives.
4. Defeating the Dragonlord sends your goal.

## Credits

DWR's Archipelago support builds on Serpikmin's Dragon Warrior APWorld ([Serpikmin/Archipelago-DragonWarrior](https://github.com/Serpikmin/Archipelago-DragonWarrior)) and ROM patcher ([Serpikmin/dwrandomizer](https://github.com/Serpikmin/dwrandomizer)). Both are MIT licensed.

## Troubleshooting

- **The server loads an old save:** delete the `.apsave` file next to the zip to start a fresh game.
- **"Could not read the AP patch file":** update to the latest `dwr.apworld`.
- **"DWR could not build the ROM":** check the paths in `host.yaml`.
- **"Invalid or missing manifest":** update to the latest `dwr.apworld`.
