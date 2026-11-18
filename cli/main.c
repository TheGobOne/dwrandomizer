
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "archipelago.h"
#include "base32.h"
#include "custom_music.h"
#include "dwr.h"
#include "dwr_types.h"
#include "map.h"
#include "sprites.h"


/* Reads on or off, and stops on anything else. */
/* Whether two words match, ignoring case. */
static int same_word(const char *a, const char *b)
{
    while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return !*a && !*b;
}

/* Reads off, on or random (0, 1, 2), and stops on anything else. */
static int off_on_random(const char *option, const char *value)
{
    if (!strcmp(value, "random"))
        return 2;
    if (strcmp(value, "on") && strcmp(value, "off")) {
        fprintf(stderr, "%s must be off, on or random\n", option);
        exit(1);
    }
    return !strcmp(value, "on");
}

static int on_off(const char *option, const char *value)
{
    if (strcmp(value, "on") && strcmp(value, "off")) {
        fprintf(stderr, "%s must be on or off\n", option);
        exit(1);
    }
    return !strcmp(value, "on");
}

static void print_usage(const char *command, char *default_flags)
{
    size_t i;

    printf("Usage %s [options] <rom_file> [flags [seed [sprite]]] <output_dir>\n", command);
    printf("\n");
    printf("A randomizer for Dragon Warrior for NES\n");
    printf("\n");
    printf("\n");
    printf("If flags are not specified, the flags \"%s\" will be used\n",
            default_flags);
    printf("If the seed is not specified, a random seed will be chosen\n");
    printf("\n");
    printf("Options, before the rom file:\n");
    printf("  --music <pack.json5>\n");
    printf("  --music-exact <pack.json5>   (testing: songs go in as tagged, no site picks)\n");
    printf("  --encounters default|low|medium|high\n");
    printf("  --biome continent|caldera|wasteland|riverlands|lake|earth|westeros|random\n");
    printf("  --free-charlock on|off\n");
    printf("  --auto-return on|off\n");
    printf("  --responsive-controls on|off\n");
    printf("  --auto-stairs on|off\n");
    printf("  --spoiler on|off\n");
    printf("  --ap-slot <name>   (Archipelago mode, for this slot name)\n");
    printf("  --ap-patch <file>  (Archipelago mode from an .apdwr file, which also sets the flags and seed if they are left out)\n");
    printf("  --key-herb on|off\n");
    printf("  --quick-spell on|off\n");
    printf("  --quick-spell-swap on|off   (SPELL top right, quick spell under it)\n");
    printf("  --cursor-blink on|off   (off: Disable Cursor Blink)\n");
    printf("  --fast-hills on|off\n");
    printf("  --bank-cap 1-99\n");
    printf("  --bank-level normal|easy|strict|verystrict\n");
    printf("  --run-bank off|on|random\n");
    printf("  --safe-start on|off\n");
    printf("  --fast-flute on|off\n");
    printf("  --discard-flute off|on|random\n");
    printf("  --screen-shake on|off   (off: Disable Screen Shake)\n");
    printf("  --spell-flashing on|off   (off: Disable Spell Flashing)\n");
    printf("  --red-flashes on|off   (off: Disable Red Flashes)\n");
    printf("  --disable-music on|off   (on: Disable Music; a music pack is left out too)\n");
    printf("  --mute-jingles on|off   (on: Don't Mute Jingles, which keeps jingles when Disable Music is on)\n");
    printf("  --original-credits on|off   (off: Skip Original Credits)\n");
    printf("  --menu-wrap on|off\n");
    printf("\n");
    printf("Sprite should be one of: ");
    for(i=0; i < sprite_count(); i++) {
        if (i) {
            printf(", %s", dwr_sprite_names[i]);
        } else {
            printf("%s", dwr_sprite_names[i]);
        }
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    uint64_t seed;
    const char *sprite;
    char *flags;
    char default_flags[] = DEFAULT_FLAGS;
    char *input_file, *output_dir;
    static const char *const bank_levels[4] = {"normal", "easy", "strict", "verystrict"};
    /* -1 and 0 for the cap: not given, so the flags decide. */
    int responsive = -1, bank_cap = 0, bank_level = -1, encounters = -1, quick_spell = -1, quick_swap = -1, no_blink = -1, fast_hills = -1, key_herb = -1, free_charlock = -1, auto_return = -1, auto_stairs = -1, biome = -1;
    int run_bank = -1, safe_start = -1, fast_flute = -1, discard_flute = -1, no_shake = -1;
    int no_spell_flash = -1, no_red_flash = -1, no_music = -1, dont_mute_jingles = -1, skip_credits = -1, menu_wrap = -1;
    static const char *const encounter_rates[4] = {"default", "low", "medium", "high"};
    char encoded[64] = {0};

    printf("DWR version %s\n", VERSION);
    while (argc >= 3 && !strncmp(argv[1], "--", 2)) {
        if (!strcmp(argv[1], "--music")) {
            if (!custom_music_load(argv[2]))
                exit(1);
            custom_music_choose_like_site();
        } else if (!strcmp(argv[1], "--music-exact")) {
            /* Testing: every song goes in as tagged, without the site's picks. */
            if (!custom_music_load(argv[2]))
                exit(1);
        } else if (!strcmp(argv[1], "--encounters")) {
            for (encounters = 0; encounters < 4; encounters++)
                if (!strcmp(argv[2], encounter_rates[encounters]))
                    break;
            if (encounters == 4) {
                fprintf(stderr, "--encounters must be default, low, medium or high\n");
                exit(1);
            }
        } else if (!strcmp(argv[1], "--biome")) {
            /* The names are dwr_biome_names in lower case, then "random" (BIOME_COUNT). */
            for (biome = 0; biome < BIOME_COUNT; biome++)
                if (same_word(argv[2], dwr_biome_names[biome]))
                    break;
            if (biome == BIOME_COUNT && strcmp(argv[2], "random")) {
                fprintf(stderr, "--biome must be");
                for (biome = 0; biome < BIOME_COUNT; biome++)
                    fprintf(stderr, " %c%s,", tolower((unsigned char)dwr_biome_names[biome][0]), dwr_biome_names[biome] + 1);
                fprintf(stderr, " or random\n");
                exit(1);
            }
        } else if (!strcmp(argv[1], "--ap-slot")) {
            dwr_set_ap(TRUE, argv[2]);
        } else if (!strcmp(argv[1], "--ap-patch")) {
            if (!dwr_load_ap_patch(argv[2])) {
                printf("Could not read the AP patch file %s\n", argv[2]);
                return 1;
            }
        } else if (!strcmp(argv[1], "--spoiler")) {
            dwr_set_spoiler(on_off(argv[1], argv[2]));
        } else if (!strcmp(argv[1], "--key-herb")) {
            key_herb = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--auto-return")) {
            auto_return = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--auto-stairs")) {
            auto_stairs = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--responsive-controls")) {
            responsive = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--free-charlock")) {
            free_charlock = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--quick-spell")) {
            quick_spell = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--quick-spell-swap")) {
            quick_swap = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--cursor-blink")) {
            no_blink = !on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--fast-hills")) {
            fast_hills = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--bank-cap")) {
            bank_cap = atoi(argv[2]);
            if (bank_cap < 1 || bank_cap > 99) {
                fprintf(stderr, "--bank-cap must be from 1 to 99\n");
                exit(1);
            }
        } else if (!strcmp(argv[1], "--run-bank")) {
            run_bank = off_on_random(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--safe-start")) {
            safe_start = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--fast-flute")) {
            fast_flute = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--discard-flute")) {
            discard_flute = off_on_random(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--screen-shake")) {
            no_shake = !on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--spell-flashing")) {
            no_spell_flash = !on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--red-flashes")) {
            no_red_flash = !on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--disable-music")) {
            no_music = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--mute-jingles")) {
            dont_mute_jingles = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--original-credits")) {
            skip_credits = !on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--menu-wrap")) {
            menu_wrap = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--bank-level")) {
            for (bank_level = 0; bank_level < 4; bank_level++)
                if (!strcmp(argv[2], bank_levels[bank_level]))
                    break;
            if (bank_level == 4) {
                fprintf(stderr, "--bank-level must be normal, easy, strict or verystrict\n");
                exit(1);
            }
        } else {
            fprintf(stderr, "Unknown option %s\n", argv[1]);
            exit(1);
        }
        argv[2] = argv[0];
        argv += 2;
        argc -= 2;
    }
    if (argc < 3) {
        print_usage(argv[0], default_flags);
        exit(0);
    }

    input_file = argv[1];
    output_dir = argv[argc-1];
    if (argc >= 4) {
        flags = argv[2];
    } else {
        flags = default_flags;
    }
    if (argc < 5 || sscanf(argv[3], "%"PRIu64, &seed) != 1) {
        srand((unsigned)time(NULL) ^ (unsigned)clock());
        /* RAND_MAX is only 32767 on Windows, so take 16 bits at a time. */
        seed = ((uint64_t)(rand() & 0xffff) << 48) | ((uint64_t)(rand() & 0xffff) << 32) |
                ((uint64_t)(rand() & 0xffff) << 16) | (uint64_t)(rand() & 0xffff);
    }
    /* An AP patch file brings its own flags, seed and Free Charlock setting,
     * unless the command line gives them. */
    if (argc < 4 && dwr_ap_patch_flags())
        flags = (char *)dwr_ap_patch_flags();
    if (argc < 5)
        dwr_ap_patch_seed(&seed);
    if (free_charlock < 0)
        free_charlock = dwr_ap_patch_free_charlock();
    if (argc > 5) {
        sprite = argv[4];
    } else {
        sprite = "Random";
    }

    if (bank_cap || bank_level >= 0 || encounters >= 0 || quick_spell >= 0 || quick_swap >= 0 || no_blink >= 0 || fast_hills >= 0 || key_herb >= 0 || free_charlock >= 0 || auto_return >= 0 || responsive >= 0 || auto_stairs >= 0 || biome >= 0 ||
            run_bank >= 0 || safe_start >= 0 || fast_flute >= 0 || discard_flute >= 0 || no_shake >= 0 ||
            no_spell_flash >= 0 || no_red_flash >= 0 || no_music >= 0 || dont_mute_jingles >= 0 || skip_credits >= 0 || menu_wrap >= 0) {
        /* 27 bytes plus room for the 4 spare bits a 44 character code decodes into. */
        uint8_t raw[28] = {0};
        int length;
        strncpy(encoded, flags, 44);
        base32_decode((uint8_t *)encoded, raw);
        if (bank_cap)
            raw[12] = (uint8_t)((raw[12] & 0x80) | (bank_cap == 9 ? 0 : bank_cap));   /* 9 is the default, written as 0 like the site */
        if (bank_level >= 0) {
            raw[12] = (uint8_t)((raw[12] & 0x7f) | ((bank_level & 2) << 6));
            raw[14] = (uint8_t)((raw[14] & 0xfe) | (bank_level & 1));
        }
        if (encounters >= 0) {
            raw[3] = (uint8_t)((raw[3] & 0xfe) | ((encounters >> 1) & 1));
            raw[6] = (uint8_t)((raw[6] & 0xfe) | (encounters & 1));
        }
        if (key_herb >= 0)
            raw[8] = (uint8_t)((raw[8] & 0xef) | (key_herb ? 0x10 : 0));
        if (responsive >= 0)
            raw[6] = (uint8_t)((raw[6] & 0xfd) | (responsive ? 0x02 : 0));
        if (auto_return >= 0)
            raw[3] = (uint8_t)((raw[3] & 0xfd) | (auto_return ? 0x02 : 0));
        if (auto_stairs >= 0)
            raw[22] = (uint8_t)((raw[22] & 0xef) | (auto_stairs ? 0x10 : 0));
        if (free_charlock >= 0)
            raw[11] = (uint8_t)((raw[11] & 0xfd) | (free_charlock ? 0x02 : 0));
        /* The bits are those in common/dwr.h. For pairs, the low bit is on and the high bit is random. */
        if (run_bank >= 0)
            raw[11] = (uint8_t)((raw[11] & 0xf3) | (run_bank << 2));
        if (!(raw[11] & 0x0c)) {   /* no cap or level without Run Bank, as on the site */
            raw[12] = 0;
            raw[14] &= 0xfe;
        }
        if (safe_start >= 0)
            raw[8] = (uint8_t)((raw[8] & 0xdf) | (safe_start ? 0x20 : 0));
        if (fast_flute >= 0)   /* stored inverted: 0 is on */
            raw[22] = (uint8_t)((raw[22] & 0xdf) | (fast_flute ? 0 : 0x20));
        if (discard_flute >= 0)
            raw[18] = (uint8_t)((raw[18] & 0xe7) | (discard_flute << 3));
        if (no_shake >= 0)
            raw[14] = (uint8_t)((raw[14] & 0xfd) | (no_shake ? 0x02 : 0));
        if (no_spell_flash >= 0)
            raw[14] = (uint8_t)((raw[14] & 0xdf) | (no_spell_flash ? 0x20 : 0));
        if (no_music >= 0)
            raw[14] = (uint8_t)((raw[14] & 0xbf) | (no_music ? 0x40 : 0));
        if (skip_credits >= 0)
            raw[14] = (uint8_t)((raw[14] & 0xfb) | (skip_credits ? 0x04 : 0));
        if (dont_mute_jingles >= 0)
            raw[16] = (uint8_t)((raw[16] & 0xef) | (dont_mute_jingles ? 0x10 : 0));
        if (no_red_flash >= 0)
            raw[17] = (uint8_t)((raw[17] & 0x7f) | (no_red_flash ? 0x80 : 0));
        if (menu_wrap >= 0)
            raw[3] = (uint8_t)((raw[3] & 0x3f) | (menu_wrap ? 0x40 : 0));
        if (quick_spell >= 0)
            raw[11] = (uint8_t)((raw[11] & 0xfe) | (quick_spell ? 0x01 : 0));
        if (quick_swap >= 0)
            raw[26] = (uint8_t)((raw[26] & 0x7f) | (quick_swap ? 0x80 : 0));
        /* Swap Quick Spell needs Quick Spell, as on the site, so the code and file name match it. */
        if (!(raw[11] & 0x01))
            raw[26] &= 0x7f;
        if (no_blink >= 0)
            raw[26] = (uint8_t)((raw[26] & 0xbf) | (no_blink ? 0x40 : 0));
        if (fast_hills >= 0)
            raw[26] = (uint8_t)((raw[26] & 0xdf) | (fast_hills ? 0x20 : 0));
        if (biome >= 0) {
            raw[2] &= (uint8_t)~0x33;
            raw[25] = 0;
            if (biome == BIOME_COUNT)
                raw[25] = BIOME_RANDOM;
            else if (biome >= 4)
                raw[25] = (uint8_t)biome;
            else
                raw[2] |= (uint8_t)(((biome & 2) << 3) | (biome & 1));
            if (!(raw[0] & 0x03))
                fprintf(stderr, "--biome needs Random Map on in the flags; without it the map is the original one.\n");
        }
        /* Byte 25 (the biome number) and byte 26 only go in the code when set, so old codes stay 40
         * characters. The top 2 bits of byte 26 fit in the 42nd character; the others need 44. */
        raw[27] = 0;
        length = FLAG_CODE_LENGTH(raw);
        base32_encode(raw, 27, (uint8_t *)encoded);   /* then cut to the length */
        encoded[length] = '\0';
        flags = encoded;
    }

    printf("Randomizing using seed: %"PRIu64" with flags %s\n", seed, flags);

    if (!dwr_randomize(input_file, seed, flags, sprite, output_dir))
        return 1;

    return 0;
}

