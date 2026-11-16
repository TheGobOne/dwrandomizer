# Dragon Warrior Randomizer, TheGobOne Remix 1.2.1

Build your own ROM at https://thegobone.github.io/dwrandomizer/ (you supply your own vanilla Dragon Warrior ROM).

This is a hobby remix of [juef17's fork](https://github.com/juef17/dwrandomizer) of [mcgrew's randomizer](https://dwrandomizer.com/). Seeds and flags are not compatible with either.

## Random maps

- Wasteland and Riverlands grow their land out of open water the way Continent does. Coasts are irregular, with lakes and islands. Each keeps its own terrain mix.
- At Normal size, Wasteland and Riverlands fill the whole map, as Continent does.
- Caldera builds at Very Small size. Its land limit now fits its round coast.
- Riverlands has about twice as many rivers.

## Changed for these flags and seeds

- Wasteland, Riverlands and Caldera maps differ from 1.2.0 for the same flags and seed. All racers need the same version. The checksum on the title screen shows a mismatch.

## Known limits

- Earth and Westeros always use the full map size.
- Caldera is round, so it does not fill every row.
- Very large random flag sets can still fail with "not enough space", as before.
- Tested in FCEUX and Mesen, not on hardware.
