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

#include "credit_music.h"
#include "custom_music.h"
#include "dwr.h"
#include "expansion.h"
#include "ft_driver.h"
#include "mt64.h"
#include "patch.h"

#define MAX_SONGS 2048
#define MAX_USES 16
#define MIN_SONG_SIZE 256
#define LO8(x) ((uint8_t)((x) & 0xff))
#define HI8(x) ((uint8_t)(((x) >> 8) & 0xff))

#define FIRST_JINGLE 0x10 /* requests from here on are events */
/* Sizes of the three fixed-bank hooks. */
#define HOOK_YS_SIZE 10
#define HOOK_RQ_SIZE 111
#define HOOK_FRAME_SIZE 29
#define REQUEST_BATTLE_RESUME 0x18
#define REQUEST_CREDITS 0x1f
#define REQUEST_HAUKSNESS 0x1c
#define MAP_HAUKSNESS 3
#define REQUEST_CAVE 0x1d /* dungeon floor music in a cave, not Charlock */
#define MAX_PLACED 24
#define RAM_JINGLE 0x6bfa
#define RAM_SFX 0x6bf6 /* last sound effect request ($80 + n) */
#define RAM_WANT 0x6bfc
#define RAM_CUR 0x6bfd
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
/* The loaded pack, kept whole when a site-style pick replaces songs[] with per-slot copies. */
static song_t pack[MAX_SONGS];
static int pack_count = 0;

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

    buf[0] = '\0';
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

#define MAX_NESTING 64

static void skip_value_at(reader *r, int depth)
{
    if (depth > MAX_NESTING) {
        r->error = TRUE;
        return;
    }
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
            skip_value_at(r, depth + 1);
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

#define W16(o) ((size_t)d[o] | (size_t)d[(o) + 1] << 8)

/* FALSE if the module's song list, 8-byte song header, frame list, a frame's 5 channel pointers
 * or a pattern pointer is out of bounds, or the song has no frames. */
static BOOL module_ok(const uint8_t *d, size_t size)
{
    size_t list, header, frames;
    int count, i, c;

    if (size < 15)
        return FALSE;
    list = W16(0);
    if (list + 2 > size)
        return FALSE;
    header = W16(list);
    if (header + 8 > size)
        return FALSE;
    frames = W16(header);
    count = d[header + 2];
    if (!count || frames + 2 * (size_t)count > size)
        return FALSE;
    for (i = 0; i < count; i++) {
        size_t frame = W16(frames + 2 * (size_t)i);
        if (frame + 10 > size)
            return FALSE;
        for (c = 0; c < 5; c++) {
            if (W16(frame + 2 * (size_t)c) >= size)
                return FALSE;
        }
    }
    return TRUE;
}

/* Swaps square 1 and 2 in every frame so the melody avoids sound effects, which
 * take over square 2. The module must have passed module_ok. */
static void swap_squares(uint8_t *d)
{
    size_t header = W16(W16(0));
    size_t frames = W16(header);
    int count = d[header + 2], i;
    size_t seen[256];

    for (i = 0; i < count; i++) {
        size_t f = W16(frames + 2 * (size_t)i);
        int j;
        BOOL dup = FALSE;
        uint8_t t;
        for (j = 0; j < i; j++)
            dup = dup || seen[j] == f;
        seen[i] = f;
        if (dup)
            continue;
        t = d[f]; d[f] = d[f + 2]; d[f + 2] = t;
        t = d[f + 1]; d[f + 1] = d[f + 3]; d[f + 3] = t;
    }
}
#undef W16

/* ------------------------------------------------------------ pack loading */

/* Songs with these tags only play for that event. */
static const char *const reserved_tags[] = {
    "OriginalCredits", "StatScroll",
    "CharlockFinalFloor", "DragonlordRoom", "Hauksness", NULL
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
                && !strstr(s.title, "SFX") && module_ok(s.data, s.size)) {
            if (s.square == 1)
                swap_squares(s.data);
            songs[song_count++] = s;
        } else {
            free(s.data);
        }
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

static void skip_value(reader *r)
{
    skip_value_at(r, 0);
}

void custom_music_clear(void)
{
    int i;
    /* After a site-style pick, songs[] holds copies that share data with pack[] (and with each other),
     * so free the data once, from whichever list owns it. */
    if (pack_count) {
        for (i = 0; i < pack_count; i++)
            free(pack[i].data);
        pack_count = 0;
    } else {
        for (i = 0; i < song_count; i++)
            free(songs[i].data);
    }
    song_count = 0;
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
    text = (size < 0) ? NULL : malloc((size_t)size + 1);
    if (!text) {
        fprintf(stderr, "Unable to read music pack '%s'\n", path);
        fclose(f);
        return FALSE;
    }
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

enum { SC_OVERWORLD, SC_BATTLE, SC_CHARLOCK, SC_CAVE, SC_TOWN, SC_CASTLE, SC_LAST_BATTLE, SC_TITLE, SCENE_COUNT };

static const scene_t scenes[SCENE_COUNT] = {
    {"Overworld",   {"Overworld"}},
    {"Encounter",      {"Encounter"}},
    {"Charlock",    {"Charlock", "Dungeon"}},
    {"Cave",        {"Cave"}},
    {"Town",        {"Town"}},
    {"Tantegel",      {"Tantegel"}},
    {"Dragonlord", {"Dragonlord"}},
    {"Title",       {"Title"}},
};

/* Scene for each of the game's music requests. Below FIRST_JINGLE; the rest are events. */
static int scene_for_request(int id)
{
    if (id == 1) return SC_TITLE;
    if (id == 2 || id == 3) return SC_CASTLE;
    if (id == 4) return SC_TOWN;
    if (id == 5) return SC_OVERWORLD;
    if (id >= 6 && id <= 0x0d) return SC_CHARLOCK;
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

/* Music banks in placement order: 6, 3, and 5 when it is free. Bank 5 ends below the reset vectors,
 * The credits hook is at $8000 in banks 4 and 6, below the driver. */
#define MUSIC_BANKS 3
static int music_bank_count = 2;
static uint16_t music_bank_end[MUSIC_BANKS] = {0xc000, 0xc000, 0xbffa};

static BOOL fits_a_bank(const uint16_t next[MUSIC_BANKS], unsigned size)
{
    int b;

    for (b = 0; b < music_bank_count; b++)
        if (next[b] + size <= music_bank_end[b])
            return TRUE;
    return FALSE;
}

/* Puts a song in the first bank with room. */
static BOOL place(song_t *s, uint16_t next[MUSIC_BANKS], int order[MAX_PLACED], int *placed, int index)
{
    int b;

    if (*placed >= MAX_PLACED)
        return FALSE;
    for (b = 0; b < music_bank_count; b++) {
        if (next[b] + s->size <= music_bank_end[b]) {
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
static int pick(const char *const *tags, BOOL enabled_only, const uint16_t next[MUSIC_BANKS], BOOL event)
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
        if (!fits_a_bank(next, s->size))
            continue;
        candidates[n++] = i;
    }
    return n ? candidates[mt_rand(0, n - 1)] : -1;
}

static int count_tagged(const char *const *tags)
{
    int n = 0, i;

    for (i = 0; i < song_count; i++)
        if (has_tag(&songs[i], tags))
            n++;
    return n;
}

/* Events with their own song only if a pack tags one; otherwise the game's music plays. */
typedef struct {
    const char *name;
    int request;
    const char *tags[4];
} event_t;

#define EVENT_COUNT 4
static const event_t events[EVENT_COUNT] = {
    {"Credits",     REQUEST_CREDITS, {"StatScroll", "OriginalCredits"}},
    {"Ending",      0x10, {"OriginalCredits"}},
    {"Final floor", 0x0d, {"CharlockFinalFloor", "DragonlordRoom"}},
    {"Hauksness",   REQUEST_HAUKSNESS, {"Hauksness"}},
};

/* ------------------------------------------------------------ choosing like the site */

/* The command line picks songs exactly the way the site does with every slot on Random and one track for
 * all dungeon floors, so the same seed and pack give the same music. This mirrors chosenPack in
 * site/index.html: change the two together. */
#define PICK_ROOM (0xc000 - FT_SONGS)
/* Bank 5 ends below the reset vectors. */
#define PICK_ROOM5 (0xbffa - FT_SONGS)

static BOOL choose_like_site = FALSE;

void custom_music_choose_like_site(void)
{
    choose_like_site = TRUE;
}

typedef struct {
    const char *tags[4];
    BOOL location;
} pick_slot_t;

static const pick_slot_t pick_slots[] = {
    {{"Title"}, TRUE}, {{"Overworld"}, TRUE}, {{"Town"}, TRUE}, {{"Tantegel"}, TRUE},
    {{"Hauksness"}, TRUE},
    {{"Charlock", "Dungeon"}, TRUE}, {{"Cave"}, TRUE},
    {{"CharlockFinalFloor", "DragonlordRoom"}, TRUE}, {{"Encounter"}, TRUE}, {{"Dragonlord"}, TRUE},
    {{"OriginalCredits"}, FALSE}, {{"StatScroll", "OriginalCredits"}, FALSE},
};
#define PICK_SLOTS ((int)(sizeof(pick_slots) / sizeof(pick_slots[0])))
#define PICK_ORDER_COUNT 12

static const char *const pick_place_order[] = {"StatScroll", "Overworld", "Encounter", "Charlock", "Cave", "Town",
    "Tantegel", "Dragonlord", "Title", "OriginalCredits", "CharlockFinalFloor", "Hauksness"};
static const char *const pick_fill_order[] = {"Title", "Overworld", "Town", "Charlock", "Cave", "Encounter",
    "Tantegel", "Dragonlord", "StatScroll", "OriginalCredits", "CharlockFinalFloor", "Hauksness"};

static int order_of(const char *const *list, int n, const char *tag)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(list[i], tag))
            return i;
    return -1;
}

static BOOL pack_has(const song_t *s, const char *const *tags)
{
    int i, j;
    for (i = 0; i < s->use_count; i++)
        for (j = 0; j < 4 && tags[j]; j++)
            if (same_tag(s->uses[i], tags[j]))
                return TRUE;
    return FALSE;
}

/* TRUE if a module has a halt command, so it stops instead of looping. */
static BOOL song_stops(const song_t *s)
{
    const uint8_t *d = s->data;
    size_t head = (size_t)d[0] | (size_t)d[1] << 8, frames, i;
    int count, rows, f, c;

    head = (size_t)d[head] | (size_t)d[head + 1] << 8;
    frames = (size_t)d[head] | (size_t)d[head + 1] << 8;
    count = d[head + 2];
    rows = d[head + 3];
    for (f = 0; f < count; f++) {
        size_t fp = (size_t)d[frames + 2 * (size_t)f] | (size_t)d[frames + 2 * (size_t)f + 1] << 8;
        for (c = 0; c < 5; c++) {
            int row = 0, dur = -1;
            i = (size_t)d[fp + 2 * (size_t)c] | (size_t)d[fp + 2 * (size_t)c + 1] << 8;
            if (!i)
                continue;
            while (row < rows) {
                uint8_t b;
                if (i >= s->size)
                    return TRUE;
                b = d[i++];
                if (b >= 0xe0)
                    continue;
                if (b >= 0x80) {
                    if (b == 0x88 || b > 0xa6)
                        return TRUE;
                    if (b == 0x82)
                        dur = i < s->size ? d[i] : 0;
                    if (b == 0x83)
                        dur = -1;
                    if (b != 0x81 && b != 0x83 && b != 0x8a && b != 0x92)
                        i++;
                    continue;
                }
                if (dur < 0) {
                    if (i >= s->size)
                        break;
                    row += d[i++] + 1;
                } else {
                    row += dur + 1;
                }
            }
        }
    }
    return FALSE;
}

typedef struct {
    const char *tag;
    size_t size;
} pick_entry_t;

static uint32_t pick_rng;
static pick_entry_t pick_placed[MAX_PLACED + 1];
static int pick_placed_count;
static char *pick_used, *pick_stops;

static int pick_rank(const char *tag)
{
    return order_of(pick_place_order, PICK_ORDER_COUNT, tag);
}

/* Stat scroll first, then biggest first (the order the site uses). */
static BOOL pick_after(const pick_entry_t *a, const pick_entry_t *b)
{
    int ca = pick_rank(a->tag) == 0 ? 0 : 1, cb = pick_rank(b->tag) == 0 ? 0 : 1;
    if (ca != cb)
        return ca > cb;
    return a->size < b->size;
}

static BOOL pick_fits(const char *tag, size_t size)
{
    pick_entry_t a[MAX_PLACED + 1];
    int n = pick_placed_count, i, j;
    size_t b[3] = {0, 0, 0};

    if (n + 1 > MAX_PLACED)
        return FALSE;
    memcpy(a, pick_placed, (size_t)n * sizeof(a[0]));
    a[n].tag = tag;
    a[n++].size = size;
    for (i = 1; i < n; i++) { /* stable, like the site's sort */
        pick_entry_t e = a[i];
        for (j = i; j > 0 && pick_after(&a[j - 1], &e); j--)
            a[j] = a[j - 1];
        a[j] = e;
    }
    for (i = 0; i < n; i++) {
        if (b[0] + a[i].size <= PICK_ROOM)
            b[0] += a[i].size;
        else if (b[1] + a[i].size <= PICK_ROOM)
            b[1] += a[i].size;
        else if (b[2] + a[i].size <= PICK_ROOM5)
            b[2] += a[i].size;
        else
            return FALSE;
    }
    return TRUE;
}

static BOOL pick_loops(int i)
{
    if (pick_stops[i] < 0)
        pick_stops[i] = song_stops(&pack[i]) ? 1 : 0;
    return !pick_stops[i];
}

/* list is NULL for every song. */
static int pick_choose(const char *const *list, const char *tag, int left, BOOL location)
{
    static int fit[MAX_SONGS], base[MAX_SONGS], on[MAX_SONGS], fresh[MAX_SONGS];
    int nfit = 0, nbase = 0, non = 0, nfresh = 0, i, n, c;
    int *a, *p;
    size_t used = 0;
    double share;

    for (i = 0; i < pack_count; i++) {
        if (list && !pack_has(&pack[i], list))
            continue;
        if (pack[i].size >= 256 && pick_fits(tag, pack[i].size) && (!location || pick_loops(i)))
            fit[nfit++] = i;
    }
    if (!nfit)
        return -1;
    for (i = 0; i < pick_placed_count; i++)
        used += pick_placed[i].size;
    share = 1.5 * (double)(2 * PICK_ROOM + PICK_ROOM5 - (long)used) / left;
    for (i = 0; i < nfit; i++)
        if ((double)pack[fit[i]].size <= share)
            base[nbase++] = fit[i];
    if (!nbase) {
        memcpy(base, fit, (size_t)nfit * sizeof(int));
        nbase = nfit;
    }
    for (i = 0; i < nbase; i++)
        if (pack[base[i]].enabled)
            on[non++] = base[i];
    a = non ? on : base;
    n = non ? non : nbase;
    for (i = 0; i < n; i++)
        if (!pick_used[a[i]])
            fresh[nfresh++] = a[i];
    p = nfresh ? fresh : a;
    n = nfresh ? nfresh : n;
    pick_rng = pick_rng * 1664525u + 1013904223u;
    c = p[(int)(((uint64_t)pick_rng * (uint64_t)n) >> 32)];
    pick_used[c] = 1;
    pick_placed[pick_placed_count].tag = tag;
    pick_placed[pick_placed_count++].size = pack[c].size;
    return c;
}

/* Fills songs with one copy per slot, tagged for that slot only. */
/* With Skip Original Credits the ending never plays, so its slot is left out, and the stat scroll
 * slot takes only StatScroll songs (the same as the site). */
static const char *const stat_only_tags[4] = {"StatScroll", NULL, NULL, NULL};

static BOOL slot_skipped(const pick_slot_t *slot, BOOL skip)
{
    return skip && strcmp(slot->tags[0], "OriginalCredits") == 0 && slot->tags[1] == NULL;
}

static const char *const *slot_tags(const pick_slot_t *slot, BOOL skip)
{
    return skip && strcmp(slot->tags[0], "StatScroll") == 0 ? stat_only_tags : slot->tags;
}

static void pick_like_site(dw_rom *rom)
{
    int chosen[PICK_SLOTS], order[PICK_SLOTS], random_slots = 0, i, j, k, pass, left;
    BOOL skip = SKIP_VANILLA_CREDITS(rom) != 0;

    if (!pack_count) {
        pack_count = song_count;
        memcpy(pack, songs, (size_t)song_count * sizeof(song_t));
        /* The site lists loose songs first, then groups. */
        k = 0;
        for (pass = 0; pass < 2; pass++)
            for (i = 0; i < song_count; i++)
                if ((songs[i].title[0] == ':') == (pass == 0))
                    pack[k++] = songs[i];
    }
    pick_used = calloc((size_t)pack_count, 1);
    pick_stops = malloc((size_t)pack_count);
    memset(pick_stops, -1, (size_t)pack_count);
    pick_rng = (uint32_t)(rom->seed & 0xffffffffu) ^ 0x6d757369u;
    pick_placed_count = 0;

    for (i = 0; i < PICK_SLOTS; i++) {
        chosen[i] = -1;
        if (slot_skipped(&pick_slots[i], skip))
            continue;
        for (j = 0; j < pack_count; j++) {
            if (pack_has(&pack[j], slot_tags(&pick_slots[i], skip))) {
                order[random_slots++] = i;
                break;
            }
        }
    }
    for (i = 1; i < random_slots; i++) {
        int s = order[i];
        int r = order_of(pick_fill_order, PICK_ORDER_COUNT, pick_slots[s].tags[0]);
        for (j = i; j > 0 && order_of(pick_fill_order, PICK_ORDER_COUNT, pick_slots[order[j - 1]].tags[0]) > r; j--)
            order[j] = order[j - 1];
        order[j] = s;
    }
    left = random_slots;
    for (i = 0; i < random_slots; i++) {
        int s = order[i], c = -1;
        const pick_slot_t *slot = &pick_slots[s];
        BOOL own = FALSE;
        for (j = 0; j < pack_count; j++)
            own = own || pack_has(&pack[j], slot_tags(slot, skip));
        if (own)
            c = pick_choose(slot_tags(slot, skip), slot->tags[0], left, slot->location);
        if (c < 0 && slot->location)
            c = pick_choose(NULL, slot->tags[0], left, TRUE);
        chosen[s] = c;
        left--;
    }
    free(pick_used);
    free(pick_stops);

    /* The copies: loose songs first, then groups, in slot order. */
    song_count = 0;
    {
        int slot_order[PICK_SLOTS];
        for (i = 0; i < PICK_SLOTS; i++)
            slot_order[i] = i;
        for (pass = 0; pass < 2; pass++) {
            for (k = 0; k < PICK_SLOTS; k++) {
                int s = slot_order[k];
                song_t *copy;
                if (chosen[s] < 0 || (pack[chosen[s]].title[0] == ':') != (pass == 0))
                    continue;
                copy = &songs[song_count++];
                *copy = pack[chosen[s]];
                snprintf(copy->uses[0], 24, "%s", pick_slots[s].tags[0]);
                copy->use_count = 1;
                copy->enabled = strcmp(copy->uses[0], "OriginalCredits") != 0;
                copy->bank = -1;
            }
        }
    }
}

/* Run the driver from the game's own wait loop (at $FF74) instead of only from the NMI.
 * The NMI counts a pending tick. The wait runs one tick and drops any others: a frame the game was too
 * busy to wait in skips its music step instead of making the next frame later still.
 * The tick runs right after the NMI, never just before it: the game's NMI resets the console if it lands
 * on a bank switch while the game waits, and the next NMI is a whole frame away.
 * Code that never calls the wait (three frames without it) gets its ticks from the NMI. */
/* After lda RAM_BANKSEL: A = the music bank. $00 is bank 6, $40 bank 3, $80 bank 5 (see gen_driver.py).
 *   beq six; bmi five; lda #3; BIT $05a9; five: lda #5; BIT $06a9; six: lda #6
 * Each BIT skips the next load, so bank 3 and bank 5 fall through to the jsr with their own number. */
#define BANK_SELECT 0xf0, 0x08, 0x30, 0x03, 0xa9, 0x03, 0x2c, 0xa9, 0x05, 0x2c, 0xa9, 0x06
#define MW_PEND 0x6bf2
#define MW_RUNNING 0x6bf3
#define MW_INWAIT 0x6bf4
#define MW_TH 3
#define MW_BANKSEL RAM_BANKSEL
static void music_wait_loop(dw_rom *rom, uint16_t frame)
{
    static const uint8_t wait_in[9] = {0xa9, 0x01, 0x85, 0x02, 0xa5, 0x02, 0xd0, 0xfc, 0x60};
    static const uint8_t overrun_in[3] = {0x20, 0x2d, 0xff};
    uint8_t *k = rom->content;
    uint8_t c[48], c2[14], code[128];
    uint16_t h2, chk, w;
    int n = 0, beq, spin;

    if (memcmp(k + 0xff74, wait_in, 9) || memcmp(k + 0xfe55, overrun_in, 3)) {
        printf("Music wait loop not recognized, keeping the frame music.\n");
        return;
    }

    /* 1. NMI side: count the tick, and run one here only when the game has not waited for a while and
     * the driver is not running. */
    c[n++] = 0xee; c[n++] = LO8(MW_PEND); c[n++] = HI8(MW_PEND);
    c[n++] = 0xad; c[n++] = LO8(MW_PEND); c[n++] = HI8(MW_PEND);
    c[n++] = 0xc9; c[n++] = MW_TH; c[n++] = 0xb0; c[n++] = 1; c[n++] = 0x60;
    c[n++] = 0xad; c[n++] = LO8(MW_RUNNING); c[n++] = HI8(MW_RUNNING); c[n++] = 0xf0; c[n++] = 1; c[n++] = 0x60;
    c[n++] = 0xce; c[n++] = LO8(MW_PEND); c[n++] = HI8(MW_PEND);
    /* The hook's tail starts with the 3-byte lda of the bank selector, which the jump to here replaces:
     * do that load here, then carry on at the rest of the tail. This saves copying the tail. */
    c[n++] = 0xad; c[n++] = LO8(RAM_BANKSEL); c[n++] = HI8(RAM_BANKSEL);
    c[n++] = 0x4c; c[n++] = LO8(frame + 11); c[n++] = HI8(frame + 11);
    h2 = find_free_space(k, 0xc422, n);
    patch(rom, h2, n, c);
    patch(rom, frame + 8, 3, (uint8_t[]){0x4c, LO8(h2), HI8(h2)});

    /* 2. Overrun path of the NMI: while the game waits for a frame, take the full path. */
    n = 0;
    c2[n++] = 0xad; c2[n++] = LO8(MW_INWAIT); c2[n++] = HI8(MW_INWAIT);
    c2[n++] = 0xf0; c2[n++] = 3;
    c2[n++] = 0x4c; c2[n++] = 0x7f; c2[n++] = 0xfe;
    c2[n++] = 0x20; c2[n++] = 0x2d; c2[n++] = 0xff;
    c2[n++] = 0x4c; c2[n++] = 0x58; c2[n++] = 0xfe;
    chk = find_free_space(k, 0xc422, n);
    patch(rom, chk, n, c2);
    patch(rom, 0xfe55, 3, (uint8_t[]){0x4c, LO8(chk), HI8(chk)});

    /* 3. The wait itself: wait for the frame, then run one pending tick and drop any others. */
    n = 0;
#define EMIT(...) do { const uint8_t e_[] = {__VA_ARGS__}; memcpy(code + n, e_, sizeof(e_)); n += sizeof(e_); } while (0)
    EMIT(0xa9, 0x01, 0x85, 0x02, 0x8d, LO8(MW_INWAIT), HI8(MW_INWAIT));  /* lda #1; sta $02; sta INWAIT */
    EMIT(0xa5, 0x02, 0xd0, 0xfc);                                       /* spin: lda $02; bne spin */
    EMIT(0xa9, 0x00, 0x8d, LO8(MW_INWAIT), HI8(MW_INWAIT));             /* lda #0; sta INWAIT */
    EMIT(0xad, LO8(MW_PEND), HI8(MW_PEND));                             /* lda PEND */
    beq = n; EMIT(0xf0, 0);                                             /* beq done */
    EMIT(0x08, 0x8a, 0x48, 0x98, 0x48);                                 /* php txa pha tya pha */
    EMIT(0xad, 0x04, 0x60, 0x48);                                       /* lda $6004; pha */
    EMIT(0xa9, 0x01, 0x8d, LO8(MW_RUNNING), HI8(MW_RUNNING));           /* lda #1; sta RUNNING */
    EMIT(0xa9, 0x00, 0x8d, LO8(MW_PEND), HI8(MW_PEND));                 /* lda #0; sta PEND: one tick, drop the rest */
    EMIT(0xad, LO8(MW_BANKSEL), HI8(MW_BANKSEL), BANK_SELECT);
    EMIT(0x20, 0x91, 0xff);                                             /* jsr $ff91 (sets $6004) */
    EMIT(0x20, LO8(FT_WRAP), HI8(FT_WRAP));                             /* jsr wrap */
    EMIT(0xa9, 0x00, 0x8d, LO8(MW_RUNNING), HI8(MW_RUNNING));           /* lda #0; sta RUNNING */
    EMIT(0x68, 0x20, 0x91, 0xff);                                       /* pla; jsr $ff91 */
    EMIT(0x68, 0xa8, 0x68, 0xaa, 0x28);                                 /* pla tay pla tax plp */
    spin = n;
    code[beq + 1] = (uint8_t)(spin - (beq + 2));
    EMIT(0xa9, 0x00, 0x60);                                             /* done: lda #0; rts (A=0, Z set, as before) */
#undef EMIT
    w = find_free_space(k, 0xc422, n);
    patch(rom, w, n, code);
    patch(rom, 0xff74, 3, (uint8_t[]){0x4c, LO8(w), HI8(w)});
}

static BOOL apply_music(dw_rom *rom)
{
    static const int banks[MUSIC_BANKS] = {6, 3, 5};
    /* Bits 7-6 of a song's high address byte name its bank (gen_driver.py). */
    static const uint8_t bank_code[MUSIC_BANKS] = {0x80, 0xc0, 0x00};
    uint16_t next[MUSIC_BANKS] = {FT_SONGS, FT_SONGS, FT_SONGS};
    int order[MAX_PLACED], placed = 0;
    int event_song[EVENT_COUNT];
    int chosen[SCENE_COUNT];
    int i, b, id;
    uint16_t ys, rq, frame;
    uint8_t map[32], lo[MAX_PLACED], hi[MAX_PLACED];

    if (!song_count && !pack_count)
        return FALSE;
    if (choose_like_site)
        pick_like_site(rom);
    for (i = 0; i < song_count; i++)
        songs[i].bank = -1;
    printf("Adding custom music...\n");

    /* Bank 5 holds songs, so it must be empty apart from the reset vectors. With a pack it always is: the
     * built-in credits song and its samples are only added when the pack's music does not go in. */
    music_bank_count = 3;
    music_bank_end[2] = 0xbffa;
    {
        const uint8_t *b5 = &rom->expansion[0x8000];
        size_t o;
        for (o = 0; o < 0x3ffa && b5[o] == 0xff; o++)
            ;
        if (o < 0x3ffa) {
            printf("Music bank 5 is in use, keeping the original music.\n");
            return FALSE;
        }
    }

    mt_init(rom->seed ^ 0x6d75736963ULL);
    /* The credits run in 32 KB banking mode, where only bank 6 of the music banks
     * can be mapped, so their song is placed first. */
    {
        /* A StatScroll song first. An OriginalCredits song only if the pack has another one for
         * the ending. Skip Original Credits changes nothing: the stat scroll plays its own pick. */
        static const char *const credits_only[4] = {"StatScroll"};
        int found = pick(credits_only, TRUE, next, TRUE);
        if (found < 0) found = pick(credits_only, FALSE, next, TRUE);
        if (found < 0 && !SKIP_VANILLA_CREDITS(rom) && count_tagged(events[1].tags) >= 2) {
            found = pick(events[1].tags, TRUE, next, TRUE);
            if (found < 0) found = pick(events[1].tags, FALSE, next, TRUE);
        }
        event_song[0] = -1;
        if (found >= 0 && place(&songs[found], next, order, &placed, found)) {
            event_song[0] = placed - 1;
            printf("  %-12s %s (%u bytes, bank %d)\n", events[0].name, songs[found].title,
                    (unsigned int)songs[found].size, banks[songs[found].bank]);
        }
    }
    printf("Music bank 5: %u bytes for songs.\n", (unsigned)(music_bank_end[2] - FT_SONGS));
    {
        /* Choose the scene and event songs first, then place them biggest first, so the big songs
         * find room. The stat scroll was already placed first. Songs picked here are held (bank 9)
         * so no slot picks the same one. */
        typedef struct { int slot, song, event; } want_t;
        want_t want[SCENE_COUNT + EVENT_COUNT];
        int nwant = 0, w, k;
        for (i = 0; i < SCENE_COUNT; i++) {
            int found = pick(scenes[i].primary, TRUE, next, FALSE);
            if (found < 0) found = pick(scenes[i].primary, FALSE, next, FALSE);
            chosen[i] = -1;
            if (found >= 0) {
                songs[found].bank = 9;
                want[nwant].slot = i; want[nwant].song = found; want[nwant].event = 0;
                nwant++;
            }
        }
        for (i = 1; i < EVENT_COUNT; i++) {
            int found;
            event_song[i] = -1;
            if (events[i].request == 0x10 && SKIP_VANILLA_CREDITS(rom)) /* the ending never plays */
                continue;
            found = pick(events[i].tags, TRUE, next, TRUE);
            if (found < 0) found = pick(events[i].tags, FALSE, next, TRUE);
            if (found >= 0) {
                songs[found].bank = 9;
                want[nwant].slot = i; want[nwant].song = found; want[nwant].event = 1;
                nwant++;
            }
        }
        /* Insertion sort, biggest first; ties keep the order they were picked in. */
        for (w = 1; w < nwant; w++) {
            want_t e = want[w];
            for (k = w; k > 0 && songs[want[k - 1].song].size < songs[e.song].size; k--)
                want[k] = want[k - 1];
            want[k] = e;
        }
        for (w = 0; w < nwant; w++) {
            int found = want[w].song;
            songs[found].bank = -1;
            if (!place(&songs[found], next, order, &placed, found)) {
                /* Bigger songs took the room it was picked against: pick again with what is left. */
                const char *const *tags = want[w].event ? events[want[w].slot].tags : scenes[want[w].slot].primary;
                found = pick(tags, TRUE, next, want[w].event);
                if (found < 0) found = pick(tags, FALSE, next, want[w].event);
                if (found >= 0 && !place(&songs[found], next, order, &placed, found))
                    found = -1;
            } else {
                found = want[w].song;
            }
            if (found >= 0) {
                if (want[w].event) event_song[want[w].slot] = placed - 1;
                else chosen[want[w].slot] = placed - 1;
                printf("  %-12s %s (%u bytes, bank %d)\n",
                        want[w].event ? events[want[w].slot].name : scenes[want[w].slot].name,
                        songs[found].title, (unsigned int)songs[found].size, banks[songs[found].bank]);
            } else {
                printf("  %-12s no room for its song, leaving it out.\n",
                        want[w].event ? events[want[w].slot].name : scenes[want[w].slot].name);
            }
        }
    }
    mt_init(time(NULL));
    if (!placed) {
        printf("No custom music fits, keeping the original music.\n");
        return FALSE;
    }

    memset(map, 0xff, sizeof(map));
    for (id = 1; id < FIRST_JINGLE; id++)
        map[id] = (uint8_t)chosen[scene_for_request(id)]; /* -1 becomes $ff: silence */
    map[REQUEST_BATTLE_RESUME] = map[0x0e];
    map[REQUEST_HAUKSNESS] = map[0x09];
    map[REQUEST_CAVE] = (uint8_t)chosen[SC_CAVE]; /* -1: the game's own floor music */
    for (i = 0; i < EVENT_COUNT; i++)
        if (event_song[i] >= 0)
            map[events[i].request] = (uint8_t)event_song[i];
    memset(lo, 0, sizeof(lo));
    memset(hi, 0, sizeof(hi));
    for (i = 0; i < placed; i++) {
        lo[i] = LO8(songs[order[i]].address);
        /* Songs sit at $80xx-$BFxx, so bits 7-6 are free to say which bank holds the song. */
        hi[i] = (uint8_t)((HI8(songs[order[i]].address) & 0x3f) | bank_code[songs[order[i]].bank]);
    }

    for (b = 0; b < music_bank_count; b++) {
        uint8_t *base = &rom->expansion[(banks[b] - 3) * 0x4000];
        size_t offset, end = banks[b] == 5 ? 0x3ffa : 0x4000; /* bank 5 ends with the reset vectors */
        for (offset = 0x100; offset < end; offset++) {
            if (base[offset] != 0xff) {
                printf("Music bank %d is in use, keeping the original music.\n", banks[b]);
                return FALSE;
            }
        }
    }
    /* If the three hooks do not all fit, the original music stays. Better than a build that fails. */
    {
        static const uint8_t sizes[3] = {HOOK_YS_SIZE, HOOK_RQ_SIZE, HOOK_FRAME_SIZE};
        int at[3], k, ok = TRUE;

        for (k = 0; k < 3; k++) {
            at[k] = dwr_try_free_space(rom->content, 0xc422, sizes[k]);
            if (at[k] < 0) {
                ok = FALSE;
                break;
            }
            memset(&rom->content[at[k]], 0x00, sizes[k]); /* held while the others are looked up */
        }
        while (--k >= 0)
            memset(&rom->content[at[k]], 0xff, sizes[k]);
        if (!ok) {
            printf("No room for the music hooks, keeping the original music.\n");
            return FALSE;
        }
    }

    for (b = 0; b < music_bank_count; b++) {
        uint8_t *base = &rom->expansion[(banks[b] - 3) * 0x4000];
        memcpy(base + (FT_BASE - 0x8000), ft_image, sizeof(ft_image));
        memcpy(base + (FT_MAP - 0x8000), map, sizeof(map));
        memcpy(base + (FT_LO - 0x8000), lo, sizeof(lo));
        memcpy(base + (FT_HI - 0x8000), hi, sizeof(hi));
    }
    for (i = 0; i < placed; i++) {
        song_t *s = &songs[order[i]];
        uint8_t *base = &rom->expansion[(banks[s->bank] - 3) * 0x4000];
        memcpy(base + (s->address - 0x8000), s->data, s->size);
    }

    /* Game music tracks write to unmapped $402x unless a jingle is playing. */
    ys = find_free_space(rom->content, 0xc422, HOOK_YS_SIZE);
    vpatch(rom, ys, HOOK_YS_SIZE, 0xad, LO8(RAM_JINGLE), HI8(RAM_JINGLE), 0xd0, 0x02, 0xa0, 0x20,
            0x4c, 0xcb, 0x80);
    vpatch(rom, 0x4071, 2, LO8(ys), HI8(ys));
    vpatch(rom, 0x407e, 2, LO8(ys), HI8(ys));
    vpatch(rom, 0x4085, 2, LO8(ys), HI8(ys));

    /* Music requests: remember the id and quiet the channels for the new song. */
    /* The battle sting is part of the game's battle music, so let it play first. */
    rq = find_free_space(rom->content, 0xc422, HOOK_RQ_SIZE);
    {
        static const uint8_t rq_code[] = {
            0xa2, 0xff, 0x8e, 0x05, 0x60,         /* ldx #$ff, stx $6005 (replaced code) */
            /* cmp #$80, bcc music. A sound effect: keep its number for the driver, which mutes only the
             * channels that effect uses (gen_driver.py, sfxmask). */
            /* Effect $82 is the town footstep, which the game plays in its request routine. Return from that
             * routine instead (pla pla rts: this hook's return address, then the routine's), so nothing plays. */
            0xc9, 0x80, 0x90, 0x10, 0xc9, 0x82, 0xd0, 0x08,
            0xa9, 0x00, 0x8d, 0x05, 0x60,                 /* lda #0, sta $6005: the routine's busy flag, as its own end does */
            0x68, 0x68, 0x60,
            0x8d, LO8(RAM_SFX), HI8(RAM_SFX), 0x60,
            0x8d, LO8(RAM_WANT), HI8(RAM_WANT),
            0xa2, 0x00, 0x8e, LO8(RAM_DELAY), HI8(RAM_DELAY),
            /* The game's music plays unless the driver starts a song for this request. */
            0xa2, 0x01, 0x8e, LO8(RAM_JINGLE), HI8(RAM_JINGLE),
            /* Quiet the channels only when a pack song was playing (its notes would hang). After the game's
             * own music, such as the level-up jingle, the last note rings out as in the original game:
             * cutting it dead clipped the jingle and popped. */
            0xae, 0xfb, 0x6b, 0x30, 0x10,         /* ldx SONG ($6BFB), bmi: no pack song playing */
            0xa2, 0x30, 0x8e, 0x00, 0x40, 0x8e, 0x04, 0x40, 0x8e, 0x0c, 0x40,
            0xa2, 0x80, 0x8e, 0x08, 0x40,
            /* Map music requests shared between places: tell them apart by map. */
            0xc9, 0x0e, 0xb0, 0x1d, 0xc9, 0x01, 0x90, 0x19,
            0xa6, 0x45,
            0xe0, MAP_HAUKSNESS, 0xd0, 0x05,
            0xa9, REQUEST_HAUKSNESS, 0x8d, LO8(RAM_WANT), HI8(RAM_WANT), /* A, so X keeps the map */
            /* Old man caves keep whatever was playing: the overworld song from an overworld tile,
             * or the town or cave song when the cave is joined to a town or another cave. */
            0xe0, TANTEGEL_BASEMENT, 0x90, 0x0b, 0xe0, SOUTHERN_SHRINE + 1, 0xb0, 0x07,
            0xad, LO8(RAM_CUR), HI8(RAM_CUR), 0x8d, LO8(RAM_WANT), HI8(RAM_WANT),
            0x60,
            /* Floor music in Erdrick's Cave, the mountain cave, Garin's Grave or the swamp cave is the
             * Cave song; Charlock keeps the floor requests. */
            0xc9, 0x06, 0x90, 0x0d,
            0xe0, SWAMP_CAVE, 0x90, 0x09, 0xe0, ERDRICKS_CAVE_2 + 1, 0xb0, 0x05,
            0xa2, REQUEST_CAVE, 0x8e, LO8(RAM_WANT), HI8(RAM_WANT),
            0x60};
        /* The free-space check above reserves HOOK_RQ_SIZE bytes: the two must agree. */
        _Static_assert(sizeof(rq_code) == HOOK_RQ_SIZE, "HOOK_RQ_SIZE must match the request hook");
        patch(rom, rq, sizeof(rq_code), rq_code);
    }
    vpatch(rom, 0x41a0, 5, 0x20, LO8(rq), HI8(rq), 0xea, 0xea);

    /* Each frame: the game's sound code, then the driver in its current bank. */
    frame = find_free_space(rom->content, 0xc422, HOOK_FRAME_SIZE);
    vpatch(rom, frame, HOOK_FRAME_SIZE,
            0xa9, 0x01, 0x20, 0x96, 0xff, 0x20, 0x28, 0x80,
            0xad, LO8(RAM_BANKSEL), HI8(RAM_BANKSEL), BANK_SELECT,
            0x20, 0x96, 0xff,
            0x4c, LO8(FT_WRAP), HI8(FT_WRAP));
    vpatch(rom, 0xfee8, 8, 0x20, LO8(frame), HI8(frame), 0xea, 0xea, 0xea, 0xea, 0xea);
    music_wait_loop(rom, frame);

    /* Credits: ask for the tagged song and update the driver from their NMI. */
    if (event_song[0] >= 0) {
        const uint8_t start[14] = {0xa9, 0x01, 0xa2, 0x00, 0xa0, 0x80,
                0x20, LO8(CREDITS_FAMISTUDIO_INIT), HI8(CREDITS_FAMISTUDIO_INIT),
                0xa9, 0x00, 0x20, LO8(CREDITS_FAMISTUDIO_PLAY), HI8(CREDITS_FAMISTUDIO_PLAY)};
        const uint8_t update[3] = {0x20, LO8(CREDITS_FAMISTUDIO_UPDATE), HI8(CREDITS_FAMISTUDIO_UPDATE)};
        uint8_t *credits = &rom->expansion[0x4000];
        uint8_t *start_at = credits + (CREDITS_MUSIC_START - 0x8000);
        uint8_t *update_at = credits + (CREDITS_MUSIC_UPDATE - 0x8000);
        if (!memcmp(start_at, start, 14) && !memcmp(update_at, update, 3)) {
            /* The credits run in 32 KB mode with banks 4 and 5, and this switches to banks 6 and 7
             * while it runs, so it sits at the same address in banks 4 and 6: $8000, free in both
             * (bank 4's $8000 only held the built-in credits song, which a pack leaves out, and
             * bank 6's $8000-$80FF is left free by the driver). The two 5-bit mapper writes share one
             * subroutine, at $8020. */
            uint8_t stub[52] = {
                /* Until the credits ask for their song the old one may be in the other
                 * bank, where the driver would read junk. Stay silent until then. */
                0xad, LO8(RAM_WANT), HI8(RAM_WANT), 0xc9, REQUEST_CREDITS, 0xf0, 0x06,
                0xa9, 0x00, 0x8d, 0x15, 0x40, 0x60,
                0xa9, 0x00, 0x8d, LO8(RAM_BANKSEL), HI8(RAM_BANKSEL),
                0xa9, 0x06,
                0x20, 0x00, 0x00,           /* JSR sw (filled in) */
                0x20, LO8(FT_WRAP), HI8(FT_WRAP),
                0xa9, 0x04,
                0x20, 0x00, 0x00,           /* JSR sw (filled in) */
                0x60,
                /* sw: write A's low 5 bits to the mapper, one bit per write, lowest first */
                0x8d, 0xff, 0xff, 0x4a, 0x8d, 0xff, 0xff, 0x4a, 0x8d, 0xff, 0xff, 0x4a,
                0x8d, 0xff, 0xff, 0x4a, 0x8d, 0xff, 0xff,
                0x60
            };
            uint8_t *bank4 = &rom->expansion[0x4000], *bank6 = &rom->expansion[0xc000];
            const uint16_t u = 0x8000, sw = 0x8020;
            size_t n;
            for (n = 0; n < sizeof(stub) && bank4[n] == 0xff && bank6[n] == 0xff; n++)
                ;
            if (n < sizeof(stub)) {
                printf("No room for the credits song, so the stat scroll is silent.\n");
            } else {
                stub[21] = LO8(sw);
                stub[22] = HI8(sw);
                stub[29] = LO8(sw);
                stub[30] = HI8(sw);
                memcpy(bank4, stub, sizeof(stub));
                memcpy(bank6, stub, sizeof(stub));
                memset(start_at, 0xea, 14);
                start_at[0] = 0xa9;
                start_at[1] = REQUEST_CREDITS;
                start_at[2] = 0x8d;
                start_at[3] = LO8(RAM_WANT);
                start_at[4] = HI8(RAM_WANT);
                update_at[1] = LO8(u);
                update_at[2] = HI8(u);
            }
        } else {
            printf("Credits code not recognized, keeping the original credits music.\n");
        }
    }

    return TRUE;
}

/* The built-in credits song drawn by dwr.c, added only if the pack's music does not go in. */
static int fallback_credits_track = -1;

void custom_music_set_credits_fallback(int track)
{
    fallback_credits_track = track;
}

/* With a pack, the built-in credits song is left out (dwr.c) so bank 5 can hold songs.
 * - The pack's music did not go in: the credits get the built-in song after all.
 * - It went in without a credits song: the credits player would start on empty song data, so it is
 *   skipped, and the sound is switched off so no note from the ending keeps sounding. */
void custom_music_apply(dw_rom *rom)
{
    static const uint8_t start[14] = {0xa9, 0x01, 0xa2, 0x00, 0xa0, 0x80,
            0x20, LO8(CREDITS_FAMISTUDIO_INIT), HI8(CREDITS_FAMISTUDIO_INIT),
            0xa9, 0x00, 0x20, LO8(CREDITS_FAMISTUDIO_PLAY), HI8(CREDITS_FAMISTUDIO_PLAY)};
    static const uint8_t update[3] = {0x20, LO8(CREDITS_FAMISTUDIO_UPDATE), HI8(CREDITS_FAMISTUDIO_UPDATE)};
    uint8_t *credits = &rom->expansion[0x4000];
    uint8_t *start_at = credits + (CREDITS_MUSIC_START - 0x8000);
    uint8_t *update_at = credits + (CREDITS_MUSIC_UPDATE - 0x8000);

    if (!apply_music(rom)) {
        if (custom_music_has_pack() && fallback_credits_track >= 0) {
            add_music(rom, fallback_credits_track);
            printf("The stat scroll plays the built-in credits song.\n");
        }
        return;
    }
    if (custom_music_has_pack() && !memcmp(start_at, start, sizeof(start)) && !memcmp(update_at, update, sizeof(update))) {
        memset(start_at, 0xea, sizeof(start));
        start_at[0] = 0xa9; start_at[1] = 0x00;                 /* lda #0 */
        start_at[2] = 0x8d; start_at[3] = 0x15; start_at[4] = 0x40; /* sta $4015: sound off */
        memset(update_at, 0xea, sizeof(update));
        printf("No credits song: the stat scroll is silent.\n");
    }
}

BOOL custom_music_has_pack(void)
{
    return song_count > 0 || pack_count > 0;
}

