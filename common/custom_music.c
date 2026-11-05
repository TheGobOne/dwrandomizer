/**
 * Custom music: plays FamiTracker modules from JSON5 packs with the
 * Dn-FamiTracker driver (see tools/custom-music).
 *
 * Banks 3 and 6 each hold a copy of the driver plus some songs. The game's own
 * sound code keeps running so sound effects, jingles and jingle timing still
 * work: its music tracks write to unmapped registers unless a jingle plays.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "custom_music.h"
#include "dwr.h"
#include "ft_driver.h"
#include "mt64.h"
#include "patch.h"

#define MAX_SONGS 2048
#define MAX_USES 16
#define MIN_SONG_SIZE 256
#define LO8(x) ((uint8_t)((x) & 0xff))
#define HI8(x) ((uint8_t)(((x) >> 8) & 0xff))

#define FIRST_JINGLE 0x10 /* requests from here on are events */
#define REQUEST_BATTLE_RESUME 0x18
#define REQUEST_CREDITS 0x1f
#define REQUEST_HAUKSNESS 0x1c
#define MAP_HAUKSNESS 3
#define REQUEST_OLD_MAN_CAVE 0x1d
#define MAX_PLACED 24
#define RAM_JINGLE 0x6bfa
#define RAM_WANT 0x6bfc
#define RAM_BANKSEL 0x6bf9
#define RAM_DELAY 0x6bf8

typedef struct {
    char title[80];
    char uses[MAX_USES][24];
    int use_count;
    BOOL enabled;
    int square;
    uint8_t *data;
    size_t size;
    int bank;   /* -1 until placed */
    uint16_t address;
} song_t;

static song_t songs[MAX_SONGS];
static int song_count = 0;

/* ------------------------------------------------------------ JSON5 reader */

typedef struct {
    const char *p;
    BOOL error;
} reader;

static void skip_space(reader *r)
{
    for (;;) {
        while (*r->p && (isspace((unsigned char)*r->p) || *r->p == ','))
            r->p++;
        if (r->p[0] == '/' && r->p[1] == '/') {
            while (*r->p && *r->p != '\n')
                r->p++;
        } else if (r->p[0] == '/' && r->p[1] == '*') {
            r->p += 2;
            while (*r->p && !(r->p[0] == '*' && r->p[1] == '/'))
                r->p++;
            if (*r->p)
                r->p += 2;
        } else {
            return;
        }
    }
}

/* Reads a quoted string; the result is malloc'd. Only simple escapes matter here. */
static char *read_string(reader *r)
{
    char quote = *r->p++;
    const char *start = r->p;
    char *out, *o;

    while (*r->p && *r->p != quote) {
        if (*r->p == '\\' && r->p[1])
            r->p++;
        r->p++;
    }
    if (!*r->p) {
        r->error = TRUE;
        return NULL;
    }
    out = o = malloc(r->p - start + 1);
    while (start < r->p) {
        if (*start == '\\') {
            start++;
            *o++ = (*start == 'n') ? '\n' : *start;
        } else {
            *o++ = *start;
        }
        start++;
    }
    *o = '\0';
    r->p++;
    return out;
}

static void skip_value(reader *r);

/* Reads a key, quoted or not, into buf. */
static void read_key(reader *r, char *buf, size_t n)
{
    size_t i = 0;

    skip_space(r);
    if (*r->p == '"' || *r->p == '\'') {
        char *s = read_string(r);
        if (s) {
            snprintf(buf, n, "%s", s);
            free(s);
        }
    } else {
        while (*r->p && *r->p != ':' && !isspace((unsigned char)*r->p)) {
            if (i + 1 < n)
                buf[i++] = *r->p;
            r->p++;
        }
        buf[i] = '\0';
    }
    skip_space(r);
    if (*r->p == ':')
        r->p++;
    else
        r->error = TRUE;
    skip_space(r);
}

static void skip_value(reader *r)
{
    skip_space(r);
    if (*r->p == '"' || *r->p == '\'') {
        free(read_string(r));
    } else if (*r->p == '{' || *r->p == '[') {
        char close = (*r->p == '{') ? '}' : ']';
        char key[32];
        BOOL object = (*r->p == '{');
        r->p++;
        for (;;) {
            skip_space(r);
            if (r->error || !*r->p) {
                r->error = TRUE;
                return;
            }
            if (*r->p == close) {
                r->p++;
                return;
            }
            if (object)
                read_key(r, key, sizeof(key));
            skip_value(r);
        }
    } else {
        const char *start = r->p;
        while (*r->p && *r->p != ',' && *r->p != '}' && *r->p != ']'
                && !isspace((unsigned char)*r->p))
            r->p++;
        if (r->p == start)
            r->error = TRUE;
    }
}

/* ------------------------------------------------------------ module data */

static int base64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static uint8_t *decode_module(const char *text, size_t *size)
{
    size_t len = strlen(text), n = 0;
    uint8_t *out;

    if (!strncmp(text, "deflate:", 8))
        return NULL; /* not supported yet */
    out = malloc(len + 1);
    if (!strncmp(text, "hex:", 4)) {
        const char *p = text + 4;
        int high = -1;
        for (; *p; p++) {
            int v;
            if (isspace((unsigned char)*p))
                continue;
            if (!isxdigit((unsigned char)*p)) {
                free(out);
                return NULL;
            }
            v = isdigit((unsigned char)*p) ? *p - '0' : tolower(*p) - 'a' + 10;
            if (high < 0) {
                high = v;
            } else {
                out[n++] = (uint8_t)(high << 4 | v);
                high = -1;
            }
        }
    } else {
        unsigned int bits = 0;
        int count = 0;
        const char *p;
        for (p = text; *p; p++) {
            int v = base64_value(*p);
            if (v < 0)
                continue;
            bits = bits << 6 | (unsigned int)v;
            count += 6;
            if (count >= 8) {
                count -= 8;
                out[n++] = (uint8_t)(bits >> count);
            }
        }
    }
    *size = n;
    return out;
}

/* Swaps square 1 and 2 in every frame so the melody avoids sound effects, which
 * take over square 2. Returns FALSE if the module's tables are out of bounds. */
static BOOL swap_squares(uint8_t *d, size_t size)
{
#define W16(o) ((size_t)d[o] | (size_t)d[(o) + 1] << 8)
    size_t list, header, frames;
    int count, i;
    size_t seen[256];

    if (size < 15)
        return FALSE;
    list = W16(0);
    if (list + 2 > size)
        return FALSE;
    header = W16(list);
    if (header + 4 > size)
        return FALSE;
    frames = W16(header);
    count = d[header + 2];
    if (frames + 2 * (size_t)count > size)
        return FALSE;
    for (i = 0; i < count; i++) {
        size_t f = W16(frames + 2 * (size_t)i);
        int j;
        BOOL dup = FALSE;
        uint8_t t;
        if (f + 4 > size)
            return FALSE;
        for (j = 0; j < i; j++)
            dup = dup || seen[j] == f;
        seen[i] = f;
        if (dup)
            continue;
        t = d[f]; d[f] = d[f + 2]; d[f + 2] = t;
        t = d[f + 1]; d[f + 1] = d[f + 3]; d[f + 3] = t;
    }
    return TRUE;
#undef W16
}

/* ------------------------------------------------------------ pack loading */

/* Songs with these tags only play for that event. */
static const char *const reserved_tags[] = {
    "SilverHarp", "FairyFlute", "RainbowBridge", "Death", "Dying", "PlayerDeath", "Inn",
    "Gwaelin", "Cursed", "Victory", "LevelUp", "Ending", "Credits",
    "DragonlordRoom", "Hauksness", "OldManCave", "Floor1", "Floor2", "Floor3", "Floor4", "Floor5",
    "Floor6", "Floor7", NULL
};

static BOOL same_tag(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a++) != tolower((unsigned char)*b++))
            return FALSE;
    }
    return *a == *b;
}

static BOOL is_reserved(const song_t *s)
{
    int i, j;

    for (i = 0; i < s->use_count; i++)
        for (j = 0; reserved_tags[j]; j++)
            if (same_tag(s->uses[i], reserved_tags[j]))
                return TRUE;
    return FALSE;
}

static void read_item(reader *r, int group_square, const char *group_title)
{
    song_t s;
    char key[32];
    char *data = NULL;
    BOOL has_songs = FALSE;

    memset(&s, 0, sizeof(s));
    s.enabled = TRUE;
    s.square = group_square;
    s.bank = -1;
    r->p++; /* { */
    for (;;) {
        skip_space(r);
        if (r->error || !*r->p) {
            r->error = TRUE;
            break;
        }
        if (*r->p == '}') {
            r->p++;
            break;
        }
        read_key(r, key, sizeof(key));
        if (!strcmp(key, "title") && (*r->p == '"' || *r->p == '\'')) {
            char *t = read_string(r);
            if (t) {
                snprintf(s.title, sizeof(s.title), "%s: %s", group_title, t);
                free(t);
            }
        } else if (!strcmp(key, "data") && (*r->p == '"' || *r->p == '\'')) {
            free(data);
            data = read_string(r);
        } else if (!strcmp(key, "enabled")) {
            s.enabled = strncmp(r->p, "false", 5) != 0;
            skip_value(r);
        } else if (!strcmp(key, "primary_square_chan")) {
            s.square = atoi(r->p);
            skip_value(r);
        } else if (!strcmp(key, "uses") && *r->p == '[') {
            r->p++;
            for (;;) {
                skip_space(r);
                if (r->error || !*r->p) {
                    r->error = TRUE;
                    break;
                }
                if (*r->p == ']') {
                    r->p++;
                    break;
                }
                if (*r->p == '"' || *r->p == '\'') {
                    char *u = read_string(r);
                    if (u && s.use_count < MAX_USES)
                        snprintf(s.uses[s.use_count++], 24, "%s", u);
                    free(u);
                } else {
                    skip_value(r);
                }
            }
        } else {
            if (!strcmp(key, "songs"))
                has_songs = TRUE;
            skip_value(r);
        }
    }
    /* Modules holding several songs need per-song handling, not supported yet. */
    if (!r->error && data && !has_songs && song_count < MAX_SONGS) {
        s.data = decode_module(data, &s.size);
        /* Rips include sound effects and very short stingers, which make poor loops. */
        if (s.data && (s.size >= MIN_SONG_SIZE || is_reserved(&s)) && s.size <= 0xc000 - FT_SONGS
                && !strstr(s.title, "SFX")
                && (s.square != 1 || swap_squares(s.data, s.size)))
            songs[song_count++] = s;
        else
            free(s.data);
    }
    free(data);
}

static void read_items(reader *r, int group_square, const char *group_title)
{
    r->p++; /* [ */
    for (;;) {
        skip_space(r);
        if (r->error || !*r->p) {
            r->error = TRUE;
            return;
        }
        if (*r->p == ']') {
            r->p++;
            return;
        }
        if (*r->p == '{')
            read_item(r, group_square, group_title);
        else
            skip_value(r);
    }
}

static void read_group(reader *r)
{
    /* Group fields come before items in every pack written so far, but look
     * ahead so that the order doesn't matter. */
    reader scan = *r;
    char key[32], title[48] = "";
    int square = 0;
    const char *items = NULL;

    scan.p++;
    for (;;) {
        skip_space(&scan);
        if (scan.error || !*scan.p) {
            r->error = TRUE;
            return;
        }
        if (*scan.p == '}') {
            scan.p++;
            break;
        }
        read_key(&scan, key, sizeof(key));
        if (!strcmp(key, "items") && *scan.p == '[') {
            items = scan.p;
        } else if (!strcmp(key, "title") && (*scan.p == '"' || *scan.p == '\'')) {
            char *t = read_string(&scan);
            if (t) {
                snprintf(title, sizeof(title), "%s", t);
                free(t);
            }
            continue;
        } else if (!strcmp(key, "primary_square_chan")) {
            square = atoi(scan.p);
        }
        skip_value(&scan);
    }
    if (items) {
        reader it = *r;
        it.p = items;
        read_items(&it, square, title);
        r->error = it.error;
    }
    r->p = scan.p;
}

BOOL custom_music_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *text;
    reader r;
    char key[32];

    if (!f) {
        fprintf(stderr, "Unable to open music pack '%s'\n", path);
        return FALSE;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = malloc((size_t)size + 1);
    size = (long)fread(text, 1, (size_t)size, f);
    fclose(f);
    text[size] = '\0';

    r.p = text;
    r.error = FALSE;
    if (!strncmp(r.p, "\xef\xbb\xbf", 3))
        r.p += 3;
    skip_space(&r);
    if (*r.p != '{')
        r.error = TRUE;
    else
        r.p++;
    while (!r.error) {
        skip_space(&r);
        if (!*r.p) {
            r.error = TRUE;
            break;
        }
        if (*r.p == '}')
            break;
        read_key(&r, key, sizeof(key));
        if (!strcmp(key, "single") && *r.p == '[') {
            read_items(&r, 0, "");
        } else if (!strcmp(key, "groups") && *r.p == '[') {
            r.p++;
            for (;;) {
                skip_space(&r);
                if (r.error || !*r.p) {
                    r.error = TRUE;
                    break;
                }
                if (*r.p == ']') {
                    r.p++;
                    break;
                }
                if (*r.p == '{')
                    read_group(&r);
                else
                    skip_value(&r);
            }
        } else {
            skip_value(&r);
        }
    }
    free(text);
    if (r.error) {
        fprintf(stderr, "Music pack '%s' could not be parsed\n", path);
        return FALSE;
    }
    if (!song_count) {
        fprintf(stderr, "Music pack '%s' has no usable songs\n", path);
        return FALSE;
    }
    printf("Loaded %d songs from music pack\n", song_count);
    return TRUE;
}

/* ------------------------------------------------------------ song choice */

/* Scene tags. Without a tagged song, a scene keeps the game's own music. */
typedef struct {
    const char *name;
    const char *primary[4];
} scene_t;

enum { SC_OVERWORLD, SC_BATTLE, SC_DUNGEON, SC_TOWN, SC_CASTLE, SC_LAST_BATTLE, SC_TITLE, SCENE_COUNT };

static const scene_t scenes[SCENE_COUNT] = {
    {"Overworld",   {"Overworld"}},
    {"Battle",      {"Battle"}},
    {"Dungeon",     {"Dungeon", "Cave"}},
    {"Town",        {"Town"}},
    {"Castle",      {"Castle"}},
    {"Last battle", {"LastBattle"}},
    {"Title",       {"Title"}},
};

/* Scene for each of the game's music requests. Below $11; the rest are jingles. */
static int scene_for_request(int id)
{
    if (id == 1) return SC_TITLE;
    if (id == 2 || id == 3) return SC_CASTLE;
    if (id == 4) return SC_TOWN;
    if (id == 5) return SC_OVERWORLD;
    if (id >= 6 && id <= 0x0d) return SC_DUNGEON;
    if (id == 0x0e) return SC_BATTLE;
    return SC_LAST_BATTLE;
}

static BOOL has_tag(const song_t *s, const char *const tags[4])
{
    int i, j;

    for (i = 0; i < s->use_count; i++)
        for (j = 0; j < 4 && tags[j]; j++) {
            if (same_tag(s->uses[i], tags[j]))
                return TRUE;
        }
    return FALSE;
}

/* Puts a song in the first bank with room. */
static BOOL place(song_t *s, uint16_t next[2], int order[MAX_PLACED], int *placed, int index)
{
    int b;

    if (*placed >= MAX_PLACED)
        return FALSE;
    for (b = 0; b < 2; b++) {
        if (next[b] + s->size <= 0xc000) {
            s->bank = b;
            s->address = next[b];
            next[b] = (uint16_t)(next[b] + s->size);
            order[(*placed)++] = index;
            return TRUE;
        }
    }
    return FALSE;
}

/* Random unplaced song from those matching: tags (NULL = any), enabled only or not. */
static int pick(const char *const *tags, BOOL enabled_only, const uint16_t next[2], BOOL event)
{
    int candidates[MAX_SONGS];
    int n = 0, i;

    for (i = 0; i < song_count; i++) {
        const song_t *s = &songs[i];
        if (s->bank >= 0 || (enabled_only && !s->enabled))
            continue;
        if (tags && !has_tag(s, tags))
            continue;
        if (!event && is_reserved(s))
            continue;
        if (next[0] + s->size > 0xc000 && next[1] + s->size > 0xc000)
            continue;
        candidates[n++] = i;
    }
    return n ? candidates[mt_rand(0, n - 1)] : -1;
}

/* Events with their own song only if a pack tags one; otherwise the game's music plays. */
typedef struct {
    const char *name;
    int request;
    const char *tags[4];
} event_t;

#define EVENT_COUNT 21
static const event_t events[EVENT_COUNT] = {
    {"Credits",     REQUEST_CREDITS, {"Credits", "Ending"}},
    {"Ending",      0x10, {"Ending"}},
    {"Dragonlord",  0x0d, {"DragonlordRoom"}},
    {"Hauksness",   REQUEST_HAUKSNESS, {"Hauksness"}},
    {"Old man cave", REQUEST_OLD_MAN_CAVE, {"OldManCave"}},
    {"Silver harp", 0x11, {"SilverHarp"}},
    {"Fairy flute", 0x12, {"FairyFlute"}},
    {"Bridge",      0x13, {"RainbowBridge"}},
    {"Death",       0x14, {"Death", "Dying", "PlayerDeath"}},
    {"Inn",         0x15, {"Inn"}},
    {"Gwaelin",     0x16, {"Gwaelin"}},
    {"Cursed",      0x17, {"Cursed"}},
    {"Victory",     0x19, {"Victory"}},
    {"Level up",    0x1a, {"LevelUp"}},
    /* Last, so floors only use space nothing else needed. */
    {"Floor 1",     0x06, {"Floor1"}},
    {"Floor 2",     0x07, {"Floor2"}},
    {"Floor 3",     0x08, {"Floor3"}},
    {"Floor 4",     0x09, {"Floor4"}},
    {"Floor 5",     0x0a, {"Floor5"}},
    {"Floor 6",     0x0b, {"Floor6"}},
    {"Floor 7",     0x0c, {"Floor7"}},
};

void custom_music_apply(dw_rom *rom)
{
    static const int banks[2] = {6, 3};
    uint16_t next[2] = {FT_SONGS, FT_SONGS};
    int order[MAX_PLACED], placed = 0;
    int event_song[EVENT_COUNT];
    int chosen[SCENE_COUNT];
    int i, b, id;
    uint16_t ys, rq, frame;
    uint8_t map[32], lo[MAX_PLACED], hi[MAX_PLACED], bank_of[MAX_PLACED];

    if (!song_count)
        return;
    printf("Adding custom music...\n");

    mt_init(rom->seed ^ 0x6d75736963ULL);
    /* The credits run in 32 KB banking mode, where only bank 6 of the music banks
     * can be mapped, so their song is placed first. */
    {
        int found = pick(events[0].tags, TRUE, next, TRUE);
        if (found < 0) found = pick(events[0].tags, FALSE, next, TRUE);
        event_song[0] = -1;
        if (found >= 0 && place(&songs[found], next, order, &placed, found)) {
            event_song[0] = placed - 1;
            printf("  %-12s %s (%u bytes, bank %d)\n", events[0].name, songs[found].title,
                    (unsigned int)songs[found].size, banks[songs[found].bank]);
        }
    }
    for (i = 0; i < SCENE_COUNT; i++) {
        int found = -1;
        found = pick(scenes[i].primary, TRUE, next, FALSE);
        if (found < 0) found = pick(scenes[i].primary, FALSE, next, FALSE);
        if (found >= 0 && place(&songs[found], next, order, &placed, found)) {
            chosen[i] = placed - 1;
            printf("  %-12s %s (%u bytes, bank %d)\n", scenes[i].name, songs[found].title,
                    (unsigned int)songs[found].size, banks[songs[found].bank]);
        } else {
            chosen[i] = -1; /* the game's own music plays */
        }
    }
    for (i = 1; i < EVENT_COUNT; i++) {
        int found = pick(events[i].tags, TRUE, next, TRUE);
        if (found < 0) found = pick(events[i].tags, FALSE, next, TRUE);
        event_song[i] = -1;
        if (found >= 0 && place(&songs[found], next, order, &placed, found)) {
            event_song[i] = placed - 1;
            printf("  %-12s %s (%u bytes, bank %d)\n", events[i].name, songs[found].title,
                    (unsigned int)songs[found].size, banks[songs[found].bank]);
        }
    }
    mt_init(time(NULL));
    if (!placed) {
        printf("No custom music fits, keeping the original music.\n");
        return;
    }

    memset(map, 0xff, sizeof(map));
    for (id = 1; id < FIRST_JINGLE; id++)
        map[id] = (uint8_t)chosen[scene_for_request(id)]; /* -1 becomes $ff: silence */
    map[REQUEST_BATTLE_RESUME] = map[0x0e];
    map[REQUEST_HAUKSNESS] = map[0x09];
    for (i = 0; i < EVENT_COUNT; i++)
        if (event_song[i] >= 0)
            map[events[i].request] = (uint8_t)event_song[i];
    memset(lo, 0, sizeof(lo));
    memset(hi, 0, sizeof(hi));
    memset(bank_of, 0, sizeof(bank_of));
    for (i = 0; i < placed; i++) {
        lo[i] = LO8(songs[order[i]].address);
        hi[i] = HI8(songs[order[i]].address);
        bank_of[i] = (uint8_t)songs[order[i]].bank;
    }

    for (b = 0; b < 2; b++) {
        uint8_t *base = &rom->expansion[(banks[b] - 3) * 0x4000];
        size_t offset;
        for (offset = 0x100; offset < 0x4000; offset++) {
            if (base[offset] != 0xff) {
                printf("Music bank %d is in use, keeping the original music.\n", banks[b]);
                return;
            }
        }
        memcpy(base + (FT_BASE - 0x8000), ft_image, sizeof(ft_image));
        memcpy(base + (FT_MAP - 0x8000), map, sizeof(map));
        memcpy(base + (FT_LO - 0x8000), lo, sizeof(lo));
        memcpy(base + (FT_HI - 0x8000), hi, sizeof(hi));
        memcpy(base + (FT_BANKS - 0x8000), bank_of, sizeof(bank_of));
    }
    for (i = 0; i < placed; i++) {
        song_t *s = &songs[order[i]];
        uint8_t *base = &rom->expansion[(banks[s->bank] - 3) * 0x4000];
        memcpy(base + (s->address - 0x8000), s->data, s->size);
    }

    /* Game music tracks write to unmapped $402x unless a jingle is playing. */
    ys = find_free_space(rom->content, 0xc422, 10);
    vpatch(rom, ys, 10, 0xad, LO8(RAM_JINGLE), HI8(RAM_JINGLE), 0xd0, 0x02, 0xa0, 0x20,
            0x4c, 0xcb, 0x80);
    vpatch(rom, 0x4071, 2, LO8(ys), HI8(ys));
    vpatch(rom, 0x407e, 2, LO8(ys), HI8(ys));
    vpatch(rom, 0x4085, 2, LO8(ys), HI8(ys));

    /* Music requests: remember the id and quiet the channels for the new song. */
    /* The battle sting is part of the game's battle music, so let it play first. */
    rq = find_free_space(rom->content, 0xc422, 71);
    vpatch(rom, rq, 71,
            0xa2, 0xff, 0x8e, 0x05, 0x60,         /* ldx #$ff, stx $6005 (replaced code) */
            0xc9, 0x80, 0xb0, 0x3d,               /* cmp #$80, bcs done: sound effect */
            0x8d, LO8(RAM_WANT), HI8(RAM_WANT),
            0xa2, 0x00, 0x8e, LO8(RAM_DELAY), HI8(RAM_DELAY),
            /* The game's music plays unless the driver starts a song for this request. */
            0xa2, 0x01, 0x8e, LO8(RAM_JINGLE), HI8(RAM_JINGLE),
            0xa2, 0x30, 0x8e, 0x00, 0x40, 0x8e, 0x04, 0x40, 0x8e, 0x0c, 0x40,
            0xa2, 0x80, 0x8e, 0x08, 0x40,
            /* Map music requests shared between places: tell them apart by map. */
            0xc9, 0x0e, 0xb0, 0x1c, 0xc9, 0x01, 0x90, 0x18,
            0xa6, 0x45,
            0xe0, MAP_HAUKSNESS, 0xd0, 0x05,
            0xa2, REQUEST_HAUKSNESS, 0x8e, LO8(RAM_WANT), HI8(RAM_WANT),
            0xe0, TANTEGEL_BASEMENT, 0x90, 0x09, 0xe0, SOUTHERN_SHRINE + 1, 0xb0, 0x05,
            0xa2, REQUEST_OLD_MAN_CAVE, 0x8e, LO8(RAM_WANT), HI8(RAM_WANT),
            0x60);
    vpatch(rom, 0x41a0, 5, 0x20, LO8(rq), HI8(rq), 0xea, 0xea);

    /* Credits: ask for the tagged song and update the driver from their NMI. */
    if (event_song[0] >= 0) {
        static const uint8_t start[14] = {0xa9, 0x01, 0xa2, 0x00, 0xa0, 0x80, 0x20, 0x75, 0xaa,
                0xa9, 0x00, 0x20, 0x38, 0xab};
        static const uint8_t update[3] = {0x20, 0x11, 0xac};
        uint8_t *credits = &rom->expansion[0x4000];
        if (!memcmp(credits + 0x204b, start, 14) && !memcmp(credits + 0x2a63, update, 3)) {
            /* Same bytes at the same address in banks 5 and 7, because the switch to
             * banks 6 and 7 happens while this code is running. */
            static const uint8_t stub[51] = {
                0xa9, 0x00, 0x8d, LO8(RAM_BANKSEL), HI8(RAM_BANKSEL),
                0xa9, 0x06,
                0x8d, 0xff, 0xff, 0x4a, 0x8d, 0xff, 0xff, 0x4a, 0x8d, 0xff, 0xff, 0x4a,
                0x8d, 0xff, 0xff, 0x4a, 0x8d, 0xff, 0xff,
                0x20, LO8(FT_WRAP), HI8(FT_WRAP),
                0xa9, 0x04,
                0x8d, 0xff, 0xff, 0x4a, 0x8d, 0xff, 0xff, 0x4a, 0x8d, 0xff, 0xff, 0x4a,
                0x8d, 0xff, 0xff, 0x4a, 0x8d, 0xff, 0xff,
                0x60
            };
            uint32_t u;
            uint8_t *bank5 = &rom->expansion[0x8000];
            for (u = 0xc422; u < 0xff00 - sizeof(stub); u++) {
                size_t n;
                for (n = 0; n < sizeof(stub); n++)
                    if (rom->content[u + n] != 0xff || bank5[u + n - 0xc000] != 0xff)
                        break;
                if (n == sizeof(stub))
                    break;
            }
            patch(rom, u, sizeof(stub), stub);
            memcpy(bank5 + (u - 0xc000), stub, sizeof(stub));
            memset(credits + 0x204b, 0xea, 14);
            credits[0x204b] = 0xa9;
            credits[0x204c] = REQUEST_CREDITS;
            credits[0x204d] = 0x8d;
            credits[0x204e] = LO8(RAM_WANT);
            credits[0x204f] = HI8(RAM_WANT);
            credits[0x2a64] = LO8(u);
            credits[0x2a65] = HI8(u);
        } else {
            printf("Credits code not recognized, keeping the original credits music.\n");
        }
    }

    /* Each frame: the game's sound code, then the driver in its current bank. */
    frame = find_free_space(rom->content, 0xc422, 27);
    vpatch(rom, frame, 27,
            0xa9, 0x01, 0x20, 0x96, 0xff, 0x20, 0x28, 0x80,
            0xad, LO8(RAM_BANKSEL), HI8(RAM_BANKSEL), 0x29, 0x01, 0xf0, 0x04,
            0xa9, 0x03, 0xd0, 0x02, 0xa9, 0x06,
            0x20, 0x96, 0xff,
            0x4c, LO8(FT_WRAP), HI8(FT_WRAP));
    vpatch(rom, 0xfee8, 8, 0x20, LO8(frame), HI8(frame), 0xea, 0xea, 0xea, 0xea, 0xea);
}
