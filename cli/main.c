
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "base32.h"
#include "custom_music.h"
#include "dwr.h"
#include "dwr_types.h"
#include "map.h"
#include "sprites.h"


/* Reads on or off, and stops on anything else. */
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

    printf("Usage %s <rom_file> [flags [seed [sprite]]] <output_dir>\n", command);
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
    printf("  --encounters default|low|medium|high\n");
    printf("  --spoiler on|off\n");
    printf("  --key-herb on|off\n");
    printf("  --quick-spell on|off\n");
    printf("  --bank-cap 1-99\n");
    printf("  --bank-level normal|easy|strict|verystrict\n");
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
    int bank_cap = 0, bank_level = -1, encounters = -1, quick_spell = -1, key_herb = -1;
    static const char *const encounter_rates[4] = {"default", "low", "medium", "high"};
    char encoded[64] = {0};

    printf("DWRandomizer version %s\n", VERSION);
    while (argc >= 3 && !strncmp(argv[1], "--", 2)) {
        if (!strcmp(argv[1], "--music")) {
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
        } else if (!strcmp(argv[1], "--spoiler")) {
            dwr_set_spoiler(on_off(argv[1], argv[2]));
        } else if (!strcmp(argv[1], "--key-herb")) {
            key_herb = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--quick-spell")) {
            quick_spell = on_off(argv[1], argv[2]);
        } else if (!strcmp(argv[1], "--bank-cap")) {
            bank_cap = atoi(argv[2]);
            if (bank_cap < 1 || bank_cap > 99) {
                fprintf(stderr, "--bank-cap must be from 1 to 99\n");
                exit(1);
            }
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
        srand(time(NULL));
        seed = ((uint64_t)rand() << 32) | ((uint64_t)rand() & 0xffffffffL);
    }
    if (argc > 5) {
        sprite = argv[4];
    } else {
        sprite = "Random";
    }

    if (bank_cap || bank_level >= 0 || encounters >= 0 || quick_spell >= 0 || key_herb >= 0) {
        uint8_t raw[25] = {0};
        strncpy(encoded, flags, 40);
        base32_decode((uint8_t *)encoded, raw);
        if (bank_cap)
            raw[12] = (uint8_t)((raw[12] & 0x80) | bank_cap);
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
        if (quick_spell >= 0)
            raw[11] = (uint8_t)((raw[11] & 0xfe) | (quick_spell ? 0x01 : 0));
        base32_encode(raw, 25, (uint8_t *)encoded);
        encoded[40] = '\0';
        flags = encoded;
    }

    printf("Randomizing using seed: %"PRIu64" with flags %s\n", seed, flags);

    if (!dwr_randomize(input_file, seed, flags, sprite, output_dir))
        return 1;

    return 0;
}

