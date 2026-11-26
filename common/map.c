
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "mt64.h"
#include "map.h"
#include "dungeon_maps.h"
#include "biome_maps.h"

#define MIN_LM_SIZE     250   /* minimum size for a land_mass to use */
#define RIVER_COUNT     25
#define MIN_WALKABLE    5000
#define MAP_ENCODED_SIZE 2294

#define VAN_SPOT_COUNT 13

/** Original locations for each warp */
static const uint8_t vanilla_spots[VAN_SPOT_COUNT][2] = {
    /* vanilla place locations */
    { 43, 43 }, /* Tantegel */
    { 48, 41 }, /* Brecconary */
    {  2,  2 }, /* Garinham */
    {104, 10 }, /* Kol */
    { 81,  1 }, /* Northern Shrine */
    { 28, 12 }, /* Tablet Cave */
    { 25, 89 }, /* Hauksness */
    { 73,102 }, /* Cantlin */
    {102, 72 }, /* Rimuldar */
    {108,109 }, /* Southern Shrine */
    {104, 44 }, /* Swamp North */
    { 29, 57 }, /* Mountain Cave */
    {104, 49 }, /* Swamp South */
    /* extra locations 
    { 83,113 },  * Token Spot *
    { 40, 82 },
    { 13,107 },
    { 67, 86 },
    { 68, 82 },
    { 75, 82 },
    { 67, 77 },
    {112, 26 },
    {114, 37 }, */
};

/**
 * Indicates whether a coordinate is past the edge of the map
 *
 * @param The x or y coordinate
 * 
 * @return A bool with TRUE indicating that this coordinate is out of bounds.
 */
static inline BOOL map_ob(int x) {
    return (x < 0 || x > 119);
}

/**
 * Shifts bytes in memory 1 to the left. Shifts [start+1, end) to [start, end-1)
 * This is used for map encoding optimization.
 *
 * @param start A pointer to the location of the new location of the bytes
 * @param end A pointer to the end of the old bytes
 */
static void shift_bytes(uint8_t *start, uint8_t *end)
{
    uint8_t *p;
    --end;

    for (p = start; p < end; p++) {
        *p = *(p + 1);
    }
}

/**
 * Optimizes the map encoding to remove unneeded bytes.
 *
 * @param map The map struct
 * @param pointers A pointer to the in-game overworld map pointers.
 * @return The number of bytes saved
 */
static int optimize_map(uint8_t *encoded_map, uint16_t *pointers)
{
    int i, j, saved = 0;
    uint8_t *start, *end;
    uint16_t at;

    end = &encoded_map[MAP_DIM * MAP_DIM];

    for (i=1; i < MAP_DIM; i++) {
        /* a row that shares an earlier row's bytes has none of its own */
        for (j=0; j < i; j++) {
            if (pointers[j] == pointers[i])
                break;
        }
        if (j < i)
            continue;
        start = &encoded_map[pointers[i] - 0x9d5d - 1];
        if (*start >> 4 == *(start+1) >> 4 && *start <= *(start+1)) {
            ++saved;
            at = pointers[i];
            shift_bytes(start, end);
            for (j=0; j < MAP_DIM; j++) {
                if (pointers[j] >= at)
                    --pointers[j];
            }
        }
    }
    return saved;
}

/**
 * Encodes the newly generated map into one that can be used in-game.
 *
 * @param map The map struct
 * @return A boolean indicating success or failure of map generation
 */
/* Tile $D, never used on the overworld before: a bridge drawn running north to south. The game treats it
 * as a normal bridge (see vertical_bridges in dwr.c). */
#define TILE_BRIDGE_V 0xd

/* Set while the Zelda II map is encoded: every bridge is stored as an across bridge (see map_z2). */
static BOOL flat_bridges = FALSE;

static BOOL bridge_tile_is(dw_map *map, int x, int y, dw_tile tile)
{
    return x >= 0 && x < 120 && y >= 0 && y < 120 && map->tiles[x][y] == tile;
}

/* How many bridge tiles in a straight line run through (x, y), going by (dx, dy) and back. */
static int bridge_run(dw_map *map, int x, int y, int dx, int dy)
{
    int n = 1, i;

    for (i = 1; bridge_tile_is(map, x + i * dx, y + i * dy, TILE_BRIDGE); i++)
        n++;
    for (i = 1; bridge_tile_is(map, x - i * dx, y - i * dy, TILE_BRIDGE); i++)
        n++;
    return n;
}

/* The tile to store for this spot: a bridge is drawn along its longer straight run, so the corner of an L and
 * two north-south bridges side by side are drawn the right way. */
static uint8_t map_encoded_tile(dw_map *map, int x, int y)
{
    int across, down;

    if (map->tiles[x][y] != TILE_BRIDGE || flat_bridges)
        return map->tiles[x][y];
    across = bridge_run(map, x, y, 1, 0);
    down = bridge_run(map, x, y, 0, 1);
    if (down > across)
        return TILE_BRIDGE_V;
    if (across > 1)
        return TILE_BRIDGE;
    /* A single bridge: vertical when it has water on both sides and something else above or below. */
    if ((x == 0 || map->tiles[x - 1][y] == TILE_WATER) && (x == 119 || map->tiles[x + 1][y] == TILE_WATER) &&
            ((y > 0 && map->tiles[x][y - 1] != TILE_WATER) || (y < 119 && map->tiles[x][y + 1] != TILE_WATER)))
        return TILE_BRIDGE_V;
    return TILE_BRIDGE;
}

static BOOL map_encode(dw_map *map)
{
    int x, y;
    int k, len, row_len[MAP_DIM];
    uint8_t t, *e, *row, encoded[MAP_DIM * MAP_DIM];
    uint16_t pointers[MAP_DIM];
    BOOL shared[MAP_DIM];

    memset(encoded, 0xff, MAP_DIM * MAP_DIM);
    e = encoded;
    map->vertical_bridges = FALSE;

    for (y=0; y < 120; y++) {
        row = e;
        shared[y] = FALSE;
        pointers[y] = e - encoded + 0x9d5d;
        for (x=0; x < 120; x++) {
            t = map_encoded_tile(map, x, y);
            if (t == TILE_BRIDGE_V)
                map->vertical_bridges = TRUE;
            if (*e == 0xff) {
                *e = t << 4;
            } else if (*e >> 4 == t) {
                (*e)++;
            } else {
                e++;
                *e = t << 4;
            }
            if ((*e & 0xf) == 0xf) {
                e++;
            }
        }
        if (*e != 0xff) {
            e++;
        }
        /* A row that repeats an earlier row points at that row's bytes instead of storing its own. */
        len = (int)(e - row);
        row_len[y] = len;
        for (k=0; k < y; k++) {
            if (!shared[k] && row_len[k] == len && !memcmp(&encoded[pointers[k] - 0x9d5d], row, len))
                break;
        }
        if (k < y) {
            memset(row, 0xff, len);
            e = row;
            pointers[y] = pointers[k];
            shared[y] = TRUE;
        }
    }

    e -= optimize_map(encoded, pointers);
    if (e - encoded > MAP_ENCODED_SIZE) {
        printf("Compressed map is too large (%d)\n", (int)(e - encoded));
        return FALSE;
    } else {
        printf("Compressed map size: %d\n", (int)(e - encoded));
    }
    memcpy(map->encoded, encoded, MAP_ENCODED_SIZE);
    memcpy(map->pointers, pointers, 240);
    return TRUE;
}

/**
 * Decodes the in-game map into a 2 dimentional array of tiles
 *
 * @param map The map struct
 */
void map_decode(dw_map *map)
{
    uint8_t x, y, *e, tile, count;

    for (y=0; y < 120; y++) {
        /* get a pointer to the beginning of the row */
        e = &map->encoded[map->pointers[y] - 0x9d5d];
        /* assign the pointer value to current */
        tile = *e >> 4;
        count = *e & 0xf;
        for (x=0; x < 120; x++) {
            /* fill in each column in the row */
            map->tiles[x][y] = tile;
            if (!count) {
                ++e;
                tile = *e >> 4;
                count = *e & 0xf;
            } else {
                count--;
            }
        }
    }
}

/**
 * Smooths the map to clean it up and ensure better compression.
 *
 * @param map The map struct
 */
static void map_smooth(dw_map *map)
{
    uint8_t x, y;

    /* If a single different tile is between 2 of the same tile, convert it.
       This will help the map compress (and look) better. */
    for (x=1; x < 119; x++) {
        for (y=0; y < 120; y++) {
            if (map->tiles[x-1][y] == map->tiles[x+1][y]) {
                map->tiles[x][y] = map->tiles[x-1][y];
            }
        }
    }
}

/**
 * Determines whether or not an overworld tile can be walked on.
 *
 * @param tile The tile type to check
 * @return A boolean indicating whether the tile can be walked on
 */
static BOOL tile_is_walkable(dw_tile tile)
{
    switch(tile) {
        case TILE_WATER:
        case TILE_MOUNTAIN:
        case TILE_BLOCK:
            return FALSE;
        default:
            return TRUE;
    }
}

/**
 * Finds the size of a land_mass given a point on it. Also fills in the 
 * walkable area array. This is a less recursive version so we don't overflow
 * the wasm stack.
 *
 * @param rom The rom pointer.
 * @param x The x coordinate of the tile to start mapping.
 * @param y The y coordinate of the tile to start mapping.
 * @param lm_index The index for this land_mass.
 */
static int map_land_mass_2(dw_map *map, uint8_t x, uint8_t y,
        uint8_t lm_index)
{
    /* A span of land at a time, with the rows to look at later kept in an explicit stack instead of
     * recursion, so a long twisting corridor of land cannot overflow the web build's stack. Each span
     * pushes at most 2 entries per tile, so 2 x 120 x 120 is enough. */
    static uint8_t stack_x[2 * 120 * 120], stack_y[2 * 120 * 120];
    int size = 0, top = 1, first = 1;

    stack_x[0] = x;
    stack_y[0] = y;
    while (top) {
        int left, right, sx, sy;

        top--;
        sx = stack_x[top];
        sy = stack_y[top];
        /* A tile pushed earlier may have been filled by a span since. */
        if (!first && map->walkable[sx][sy])
            continue;
        first = 0;
        x = (uint8_t)sx;
        y = (uint8_t)sy;
        /* Move all the way to the left */
        while(!map_ob(x-1) && tile_is_walkable(map->tiles[x-1][y]))
            x--;
        left = x;
        right = 119;

        while(!map_ob(x) && tile_is_walkable(map->tiles[x][y])) {
            map->walkable[x][y] = lm_index;
            right = x++;
            size++;
        }
        /* go back to the left side and look up & down */
        for (x = (uint8_t)left; x <= right; x++) {
            if (!map_ob(y-1) && tile_is_walkable(map->tiles[x][y-1]) &&
                    !map->walkable[x][y-1]) {
                stack_x[top] = x;
                stack_y[top++] = (uint8_t)(y - 1);
            }
            if (!map_ob(y+1) && tile_is_walkable(map->tiles[x][y+1]) &&
                    !map->walkable[x][y+1]) {
                stack_x[top] = x;
                stack_y[top++] = (uint8_t)(y + 1);
            }
        }
    }
    return size;
}

/**
 * Determines whether a bridge needs to be placed at this location in order to
 * connect the land on each side.
 *
 * @param left The land mass on the left
 * @param right The land mass on the right
 * @param lm_sizes The sizes of all land masses
 * @return A boolean indicating if a bridge is needed
 */
static inline BOOL needs_bridge(uint8_t left, uint8_t right)
{
    if (!left || !right || left == right)
        return FALSE;
    return TRUE;
}

/**
 * Adds bridges where necessary to connect continents
 * @param map The map struct
 * @return A boolean indicating if a bridge was placed.
 */
static BOOL add_bridges(dw_map *map)
{
    /* Every one-tile gap between two land masses, across or up and down, has the same chance (the same
     * pick as biome_add_bridge). The gap is filled with the land beside it when there is some, so it looks
     * like a narrow neck of land, and with a bridge otherwise. */
    int x, y, count = 0, bx = 0, by = 0, vertical = 0, across, down;

    for (y = 1; y < 119; y++) {
        for (x = 1; x < 119; x++) {
            if (map->tiles[x][y] != TILE_WATER)
                continue;
            across = needs_bridge(map->walkable[x-1][y], map->walkable[x+1][y]);
            down = needs_bridge(map->walkable[x][y-1], map->walkable[x][y+1]);
            if (!across && !down)
                continue;
            count++;
            if (mt_rand(0, count - 1) == 0) {
                bx = x;
                by = y;
                vertical = !across;
            }
        }
    }
    if (!count)
        return FALSE;
    if (!vertical) {
        if (tile_is_walkable(map->tiles[bx][by-1]))
            map->tiles[bx][by] = map->tiles[bx][by-1];
        else if (tile_is_walkable(map->tiles[bx][by+1]))
            map->tiles[bx][by] = map->tiles[bx][by+1];
        else
            map->tiles[bx][by] = TILE_BRIDGE;
    } else {
        if (tile_is_walkable(map->tiles[bx-1][by]))
            map->tiles[bx][by] = map->tiles[bx-1][by];
        else if (tile_is_walkable(map->tiles[bx+1][by]))
            map->tiles[bx][by] = map->tiles[bx+1][by];
        else
            map->tiles[bx][by] = TILE_BRIDGE;
    }
    return TRUE;
}

/* Lake sizes, shared by the terrain, the Tantegel check and the area check. */
#define LAKE_RADIUS(m) ((m)->size * 22 / 100)
#define LAKE_ISLAND 7

/* Set when a land mass has no free spot left, so the map is thrown away and drawn again. */
static BOOL land_missing;
/* Set once Tantegel and its Return point are placed on this try. */
static BOOL tantegel_placed;

/* Earth and Westeros are drawn from an outline. This holds the outline letter under each tile, '.' for sea.
 * The letters are listed in biome_maps.h. */
static char map_region[120][120];
#define TEMPLATE_BIOME(m) (RANDOM_MAP(m) && (BIOME(m) == BIOME_EARTH || BIOME(m) == BIOME_WESTEROS))
#define EARTH_MAIN(c)     ((c) == 'E' || (c) == 'A' || (c) == 'F')
#define EARTH_AMERICAS(c) ((c) == 'N' || (c) == 'S')

/* Whether Charlock's castle, walls and moat (x-7..x+1, y-4..y+4, for a drop spot at x, y) fit on the outline:
 * on Earth not over the sea (bridges are there), on Westeros wholly beyond the Wall. */
static BOOL charlock_fits(dw_map *map, int x, int y)
{
    int i, j;
    char c;

    for (i = x - 7; i <= x + 1; i++) {
        for (j = y - 4; j <= y + 4; j++) {
            if (map_ob(i) || map_ob(j))
                return FALSE;
            c = map_region[i][j];
            if (BIOME(map) == BIOME_WESTEROS ? c != 'B' : c == '.')
                return FALSE;
        }
    }
    return TRUE;
}

/**
 * Finds a land area where something can be placed and will be accessible.
 *
 * @param map The map struct
 * @param one The first land mass that is acceptable.
 * @param two The second land mass that is acceptable. May be the same as 'one',
 *      or may be zero to indicate no mass.
 * @param x A pointer to a uint8_t which will be filled with the x coordinate
 * @param y A pointer to a uint8_t which will be filled with the y coordinate
 * @param ignore_vanilla A boolean indicating this function should assume the
 *  RANDOM_MAP flag is on.
 * @return TRUE if a spot was found. FALSE sets land_missing.
 */
static BOOL map_find_land(dw_map *map, int one, int two, uint8_t *x, uint8_t *y,
        BOOL ignore_vanilla)
{
    uint8_t cont;
    int spot_index, loop = 0, tries = 0, start, i;

    for (;;) {
        if (!ignore_vanilla && !RANDOM_MAP(map) && loop < 200) {
            spot_index = mt_rand(0, VAN_SPOT_COUNT-1);
            *x = vanilla_spots[spot_index][0];
            *y = vanilla_spots[spot_index][1];
            loop++;
        } else {
            *x = mt_rand(0, 119);
            *y = mt_rand(0, 119);
        }
        cont = map->walkable[*x][*y];
        if (cont && (cont == one || cont == two))
            return TRUE;
        /* A land mass that is gone or full would loop forever, so look through the whole map. */
        if (++tries >= 20000) {
            start = (int)mt_rand(0, 120 * 120 - 1);
            for (i = 0; i < 120 * 120; i++) {
                int at = (start + i) % (120 * 120);
                *x = (uint8_t)(at % 120);
                *y = (uint8_t)(at / 120);
                cont = map->walkable[*x][*y];
                if (cont && (cont == one || cont == two))
                    return TRUE;
            }
            land_missing = TRUE;
            *x = *y = 0;
            return FALSE;
        }
    }
}

/**
 * Translates an overworld map tile into a border tile for a town or dungeon.
 *
 * @param tile The overworld tile index
 * @return The border tile index
 */
static dw_border_tile border_for(dw_tile tile)
{
    switch(tile) {
        case TILE_DESERT: return BORDER_DESERT;
        case TILE_HILL:   return BORDER_HILL;
        case TILE_SWAMP:  return BORDER_SWAMP;
        case TILE_TREES:  return BORDER_TREES;
        default:          return BORDER_GRASS;
    }
}

/**
 * Places a town or cave on the map.
 *
 * @param map The map struct
 * @param warp_idx The index of the warp for this town or dungeon
 * @param tile The tile type to place (town, castle, or cave)
 * @param lm_one The first acceptable land mass for the location
 * @param lm_two The second acceptable land mass for the location
 * @return The tile type on the overworld which was replaced by the new tile
 */
/* Extra rules for where a town or cave may go on a random map. */
static BOOL new_biome_spot(dw_map *map, int x, int y)
{
    const dw_warp *charlock = &map->warps_from[WARP_CHARLOCK];
    int dx, dy;

    if (!RANDOM_MAP(map))
        return TRUE;
    /* Not where Return and Wings land, outside Tantegel. */
    if (tantegel_placed && x == map->return_point->x && y == map->return_point->y)
        return FALSE;
    /* On a bridge it would cut the land beyond it off. */
    if (map->tiles[x][y] == TILE_BRIDGE)
        return FALSE;
    /* Keep the Rainbow Drop spot and the tiles beside it clear, or the only way to it can be a town. */
    dx = x - (charlock->x + 3);
    dy = y - charlock->y;
    if (abs(dx) + abs(dy) <= 1)
        return FALSE;
    /* The Lake island is Charlock's alone. */
    if (BIOME(map) == BIOME_LAKE) {
        dx = x - 60;
        dy = y - 60;
        if (dx * dx + dy * dy <= (LAKE_ISLAND + 1) * (LAKE_ISLAND + 1))
            return FALSE;
    }
    return TRUE;
}

static dw_border_tile place(dw_map *map, dw_warp_index warp_idx, dw_tile tile,
        int lm_one, int lm_two)
{
    uint8_t x, y;
    dw_warp *warp;
    dw_tile old_tile;

    warp = &map->warps_from[warp_idx];

    int spots = 0;

    do {
        if (!map_find_land(map, lm_one, lm_two, &x, &y, FALSE))
            return BORDER_GRASS;
        old_tile = map->tiles[x][y];
        /* Every spot taken by another town or cave: the land is too crowded. */
        if (++spots > 50000) {
            land_missing = TRUE;
            return BORDER_GRASS;
        }
    } while (old_tile == TILE_CASTLE || old_tile == TILE_TOWN ||
             old_tile == TILE_CAVE || !new_biome_spot(map, x, y));

    warp->map = 1;
    warp->x = x;
    warp->y = y;
    map->tiles[x][y] = tile;
    return border_for(old_tile);
}

/**
 * Places Charlock castle on the map and updates the rainbow drop code
 *
 * @param map The map struct
 * @param largest The first acceptable land mass
 * @param next The second acceptable land mass
 */
static void place_charlock(dw_map *map, int largest, int next)
{

    uint8_t x = 0, y = 0;
    int  i, j;
    dw_warp *warp;

    if (!RANDOM_MAP(map)) {
        map->rainbow_drop->x = x = 65;
        map->rainbow_bridge->x = 64;
        map->rainbow_bridge->y = map->rainbow_drop->y = y = 49;

    } else {
        warp = &map->warps_from[WARP_CHARLOCK];

        if (BIOME(map) == BIOME_CALDERA || BIOME(map) == BIOME_LAKE) {
            /* The castle sits in the middle of the basin, or of the lake's island. */
            x = 63;
            y = 60;
        } else if (TEMPLATE_BIOME(map)) {
            int spots = 0;
            do {
                if (!map_find_land(map, largest, next, &x, &y, FALSE) || ++spots > 50000) {
                    land_missing = TRUE;
                    return;
                }
            } while (x < 7 || x > 118 || y < 3 || y > 116 || !charlock_fits(map, x, y));
        } else {
            int spots = 0;
            while(x < 7 || x > 118 || y < 3 || y > 116) {
                if (!map_find_land(map, largest, next, &x, &y, FALSE) || ++spots > 50000) {
                    land_missing = TRUE;
                    return;
                }
            }
        }
        warp->x = x-3;
        warp->y = y;
        for (i = -3; i <= 3; i++) {
            for (j = -3; j <= 3; j++) {
                if (i < 0 || j != 0)
                    map->tiles[x - 3 + i][y + j] = TILE_BLOCK;
            }
        }
        for (i = -2; i <= 2; i++) {
            for (j = -2; j <= 2; j++) {
                map->tiles[x - 3 + i][y + j] = TILE_WATER;
            }
        }
        for (i=-1; i <= 1; i++) {
            for (j=-1; j <= 1; j++) {
                map->tiles[x-3+i][y+j] = TILE_SWAMP;
            }
        }
        map->tiles[x-3][y] = TILE_CASTLE;
        map->rainbow_drop->x = warp->x + 3;
        map->rainbow_bridge->x = warp->x + 2;
        map->rainbow_bridge->y = map->rainbow_drop->y = warp->y;

    }

    if (OPEN_CHARLOCK(map)) {
        printf("Leaving Charlock open...\n");
        map->tiles[x-1][y] = TILE_BRIDGE;
    }

}

/**
 * Places tantegel castle on the map and updates the appropriate code to match
 * (return, wings, Gwaelin's Love, etc)
 * @param map The map struct
 * @param largest The largest land mass
 * @param next The next largest land mass
 * @return A uint8_t indicating which land mass the castle was placed on.
 */
static uint8_t place_tantegel(dw_map *map, int largest, int next)
{
    int x, y;
    dw_warp *warp;
    dw_border_tile old_tile;

    old_tile = place(map, WARP_TANTEGEL, TILE_CASTLE, largest, next);
    warp = &map->warps_from[WARP_TANTEGEL];
    x = warp->x;
    y = warp->y;

    map->love_calc->x = x;
    map->love_calc->y = y;
    /* Return and Wings land beside the castle, south first. Not on a town, cave or castle placed earlier:
     * the hero would land in its doorway. */
    {
        static const int side[4][2] = {{0, 1}, {0, -1}, {-1, 0}, {1, 0}};
        int i, nx, ny;
        BOOL found = FALSE;

        for (i = 0; i < 4 && !found; i++) {
            nx = x + side[i][0];
            ny = y + side[i][1];
            if (map_ob(nx) || map_ob(ny) || !tile_is_walkable(map->tiles[nx][ny]))
                continue;
            if (RANDOM_MAP(map) && (map->tiles[nx][ny] == TILE_TOWN || map->tiles[nx][ny] == TILE_CAVE ||
                    map->tiles[nx][ny] == TILE_CASTLE))
                continue;
            map->return_point->x = nx;
            map->return_point->y = ny;
            found = TRUE;
        }
        if (!found && RANDOM_MAP(map))
            land_missing = TRUE;
    }
    tantegel_placed = TRUE;
    /* do some stuff */
    map->meta[TANTEGEL].border = old_tile;
    return map->walkable[x][y];
}

/**
 * Randomly fills a point of the map with a particular tile.
 *
 * @param map The map struct.
 * @param points An array for points that have been filled.
 * @param x The x coordinate to place the tile
 * @param y The y coordinate to place the tile
 * @param tile The type of tile to place.
 * @return An integer indicating how many tiles to remove from remaining tiles
 *      to place.
 */
static int map_fill_point(dw_map *map, uint8_t **points,
        uint8_t x, uint8_t y, dw_tile tile)
{
    if (x >= 120 || y >= 120)
        return 0;

    for (;;) {
        if ((*points)[0] > 120) { break; }
//        if (p[0] == x && p[1] == y) return 0;
        (*points) += 2;
    }
    (*points)[0] = x;
    (*points)[1] = y;
    /* [y][x] where the rest of the file uses [x][y]: the blob comes out mirrored, which is harmless, and
     * swapping it would change every random map. */
    map->tiles[y][x] = (uint8_t)tile;
    if (tile == TILE_MOUNTAIN)
        return 2; /* not so many mountain tiles */
    return 1;
}

/**
 * Fills an area of the map with a particular tile
 *
 * @param map The map struct
 * @param tile The tile to fill the area with
 */
static void map_fill(dw_map *map, dw_tile tile, int max_fill)
{
    uint8_t x, y, *points, *p, *points_orig;
    uint64_t directions, limit;
    int size;
    int min, max;
    const uint64_t WEST=8, EAST=4, NORTH=2, SOUTH=1;

    min = (120 - map->size) / 2;
    max = 120 - min;
    /* push mountain ranges to be more diagonal */
    if (tile == TILE_MOUNTAIN) {
        limit = 1 << mt_rand(0, 1);
        limit |= 4 << mt_rand(0, 1);
    }

    points_orig = p = points = malloc(max_fill * 4 + 1);
    memset(p, 0xff, max_fill * 4 + 1);
    p[0] = mt_rand(min, max-1);
    p[1] = mt_rand(min, max-1);
    size = mt_rand(max_fill/4, max_fill);

    while (size > 0) {
        x = p[0]; y=p[1];
        directions = mt_rand(0, 15);
        if (tile == TILE_MOUNTAIN) {
            directions &= limit;
        }
        if (directions & WEST) {
            size -= map_fill_point(map, &points, x-1, y, tile);
        }
        if (directions & EAST) {
            size -= map_fill_point(map, &points, x+1, y, tile);
        }
        if (directions & NORTH) {
            size -= map_fill_point(map, &points, x, y-1, tile);
        }
        if (directions & SOUTH) {
            size -= map_fill_point(map, &points, x, y+1, tile);
        }
        if (p[2] < 120) {
            p += 2;
        } else break; 
    }
    free(points_orig);

}

/**
 * Finds the largest 2 land masses on the map.
 *
 * @param map The map struct
 * @param lm_sizes An array containing the sizes of each land mass
 * @param lm_count The number of land masses
 * @param largest A pointer to fill with the largest land mass
 * @param next A pointer to fill with the next largest land mass
 */
static inline void find_largest_lm(int *lm_sizes, int lm_count,
                            int *largest, int *next)
{
    int i;

    *largest = *next = 0;
    if (!lm_count)
        return;
    for(i=0; i < lm_count; i++) {
        if (!(*largest) || lm_sizes[i] > lm_sizes[(*largest)-1]) {
            *next = *largest;
            *largest = i + 1;
        } else if (!(*next) || lm_sizes[i] > lm_sizes[(*next)-1]) {
            *next = i + 1;
        }
    }

    if (!*next || lm_sizes[(*next)-1] < MIN_LM_SIZE) {
        *next = *largest;
    }
}

/**
 * Finds all land masses on the map along with their sizes.
 *
 * @param map The map struct
 * @param lm_sizes An array to fill with the land mass sizes
 * @param largest The index of the largest land mass
 * @param next The index of the next larges land mass
 * @return The total number of land masses found
 */
static int find_walkable_area(dw_map *map, int *lm_sizes, int *largest,
                                     int *next)
{
    int x, y;
    uint8_t land_mass = 0;

    memset(map->walkable, 0, 120 * 120);
    for (y=0; y < 120; y++) {
        for (x=0; x < 120; x++) {
            if (!map->walkable[x][y] && tile_is_walkable(map->tiles[x][y])) {
                /* Index 0 means "no land mass", so the count must not wrap. */
                if (land_mass == 255)
                    goto all_found;
                lm_sizes[land_mass] = 
                        map_land_mass_2(map, x, y, land_mass+1);
                land_mass++;
            }
        }
    }
all_found:
    find_largest_lm(lm_sizes, land_mass, largest, next);
    return (int)land_mass;
}

/**
 * Checks to see if swamp cave is contained in the specified town map
 *
 * @param map The map struct
 * @param warp_index The warp index of the town map to check.
 * @return A boolean indicating if swamp cave entrance is in this map.
 */
static BOOL swamp_cave_in(dw_map *map, uint8_t warp_index)
{
    int warp_to;
    uint8_t swamp_north, swamp_south;

    swamp_north = map->warps_from[WARP_SWAMP_NORTH].map;
    swamp_south = map->warps_from[WARP_SWAMP_SOUTH].map;
    warp_to = map->warps_to[warp_index].map;

    return (swamp_north == warp_to || swamp_south == warp_to);
}

/**
 * Checks multiple conditions to determine if rimuldar needs to be accessible
 * for obtaining keys
 *
 * @param map The map struct
 * @return A boolean indicating if Rimuldar is needed
 */
static BOOL need_rimuldar(dw_map *map)
{
    uint8_t lm, tantegel_lm, garinham_lm;
    dw_warp warp;

    warp = map->warps_from[WARP_TANTEGEL];
    tantegel_lm = map->walkable[warp.x][warp.y];
    warp = map->warps_from[WARP_GARINHAM];
    garinham_lm = map->walkable[warp.x][warp.y];

    /* Is swap cave in tantegel? */
    if (!swamp_cave_in(map, WARP_TANTEGEL)) {
        /* Is Garinhamp on the same mass? If so, does it have swamp cave? */
        if (garinham_lm != tantegel_lm || !swamp_cave_in(map, WARP_GARINHAM)) {
            return FALSE;
        }
    }

    warp = map->warps_from[WARP_TANTEGEL_BASEMENT];
    if (warp.map == 1) {
        lm = map->walkable[warp.x][warp.y];
        if (lm == tantegel_lm && map->key_access & KEY_IN_BASEMENT) {
            return FALSE;
        }
    }

    warp = map->warps_from[WARP_ERDRICKS_CAVE];
    if (warp.map == 1) {
        lm = map->walkable[warp.x][warp.y];
        if (lm == tantegel_lm && map->key_access & KEY_IN_TABLET) {
            return FALSE;
        }
    }

    warp = map->warps_from[WARP_MOUNTAIN_CAVE];
    if (warp.map == 1) {
        lm = map->walkable[warp.x][warp.y];
        if (lm == tantegel_lm && map->key_access & KEY_IN_MOUNTAIN) {
            return FALSE;
        }
    }

    warp = map->warps_from[WARP_GARINS_GRAVE];
    if (warp.map == 1) {
        lm = map->walkable[warp.x][warp.y];
        if (lm == tantegel_lm && map->key_access & KEY_IN_GRAVE) {
            return FALSE;
        }
    }

    return TRUE;
}

/**
 * Places landmarks on the map (towns, castles, caves)
 *
 * @param map The map struct
 * @return A boolean indicating success or failure
 */
static BOOL place_landmarks(dw_map *map)
{
    int i, tmp, largest = 0, next = 0, lm_sizes[256],
            rimuldar_lm1, rimuldar_lm2, brecc_lm1, brecc_lm2;
    uint8_t tantegel_lm, charlock_lm;
    BOOL swamp_placed = FALSE;
    dw_warp *warp;
    dw_warp_index caves[8] = {
            WARP_SWAMP_NORTH,
            WARP_SWAMP_SOUTH,
            WARP_STAFF_SHRINE,
            WARP_MOUNTAIN_CAVE,
            WARP_JERK_CAVE,
            WARP_ERDRICKS_CAVE,
            WARP_TANTEGEL_BASEMENT,
            WARP_GARINS_GRAVE
    };


    /* find the 2 largest land masses */
    find_walkable_area(map, lm_sizes, &largest, &next);

    land_missing = FALSE;
    tantegel_placed = FALSE;
    warp = &map->warps_from[WARP_CHARLOCK];
    place_charlock(map, largest, next);
    if (land_missing) {
        printf("No room for Charlock, retrying...\n");
        return FALSE;
    }
    find_walkable_area(map, lm_sizes, &largest, &next);
    charlock_lm = map->walkable[warp->x+3][warp->y];

    if (RANDOM_MAP(map)) {
        if (charlock_lm != largest && charlock_lm != next) {
            printf("Charlock is obstructed, retrying...\n");
            return FALSE;
        }
    }

    mt_shuffle(caves, 8, sizeof(dw_warp_index));

    /* place the first two caves in tantegel & garinham */
    warp = &map->warps_from[caves[0]];
    warp->map = TANTEGEL;
    warp->x = 29;
    warp->y = 29;
    warp = &map->warps_from[caves[1]];
    warp->map = GARINHAM;
    warp->x = 19;
    warp->y = 0;

    // Make sure swamp caves are placed ASAP so there's room left for them
    for(i=3; i<8; i++)
    {
        if(caves[i] == WARP_SWAMP_NORTH || caves[i] == WARP_SWAMP_SOUTH)
        {
            tmp = caves[i-1];
            caves[i-1] = caves[i];
            caves[i] = tmp;
        }
    }

    /* place the remaining caves */
    for (i=2; i < 8; i++) {
        /* make sure the swamp cave entrances are on different land masses */
        if (caves[i] == WARP_SWAMP_NORTH || caves[i] == WARP_SWAMP_SOUTH) {
            if (!swamp_placed) {
                place(map, caves[i], TILE_CAVE, next, 0);
                swamp_placed = TRUE;
            } else {
                place(map, caves[i], TILE_CAVE, largest, 0);
            }
        } else {
            place(map, caves[i], TILE_CAVE, largest, next);
        }
    }
    rimuldar_lm1 = brecc_lm1 = largest;
    rimuldar_lm2 = brecc_lm2 = next;

    /* check for swamp cave to be in garinham */
    if (swamp_cave_in(map, WARP_GARINHAM)) {
        if (swamp_placed) {
            map->meta[GARINHAM].border =
                    place(map, WARP_GARINHAM, TILE_TOWN, largest, 0);
        } else {
            map->meta[GARINHAM].border =
                    place(map, WARP_GARINHAM, TILE_TOWN, next, 0);
            swamp_placed = TRUE;
        }
    } else {
        map->meta[GARINHAM].border =
                place(map, WARP_GARINHAM, TILE_TOWN, largest, next);
    }
    /* check for swamp cave to be in tantegel */
    if (swamp_cave_in(map, WARP_TANTEGEL)) {
        if (swamp_placed) {
            tantegel_lm = place_tantegel(map, largest, 0);
        } else {
            tantegel_lm = place_tantegel(map, next, 0);
            swamp_placed = TRUE;
        }
        /* brecconary on the same land mass as tantegel (for uncursing) */
        brecc_lm1 = brecc_lm2 = (int)tantegel_lm;
    } else {
        tantegel_lm = place_tantegel(map, largest, next);
    }

    if (RANDOM_MAP(map) && BIOME(map) == BIOME_CALDERA) {
        /* Tantegel inside the basin would skip the whole journey to Charlock. */
        const dw_warp *t = &map->warps_from[WARP_TANTEGEL];
        const int dx = (int)t->x - 60, dy = (int)t->y - 60, r = map->size * 27 / 100 + 3;

        if (dx * dx + dy * dy < r * r) {
            printf("Tantegel is inside the caldera, retrying...\n");
            return FALSE;
        }
    }
    if (RANDOM_MAP(map) && BIOME(map) == BIOME_LAKE) {
        /* Tantegel on Charlock's island would skip the whole journey. new_biome_spot already keeps every
         * place outside radius 8 of the island, so this can't fire today; it is a guard in case that changes. */
        const dw_warp *t = &map->warps_from[WARP_TANTEGEL];
        const int dx = (int)t->x - 60, dy = (int)t->y - 60, r = LAKE_ISLAND + 2;

        if (dx * dx + dy * dy < r * r) {
            printf("Tantegel is on the lake island, retrying...\n");
            return FALSE;
        }
    }
    if (RANDOM_MAP(map) && BIOME(map) == BIOME_WESTEROS) {
        /* Tantegel stays south of the Wall, so the journey always ends beyond it. */
        const dw_warp *t = &map->warps_from[WARP_TANTEGEL];
        const char c = map_region[t->x][t->y];

        if (c == 'B' || c == 'W') {
            printf("Tantegel is beyond the Wall, retrying...\n");
            return FALSE;
        }
    }

    if (need_rimuldar(map)) {
        /* rimuldar needs to be on the same land mass as tantegel (for keys) */
        rimuldar_lm1 = rimuldar_lm2 = tantegel_lm;
    }

    map->meta[RIMULDAR].border =
            place(map, WARP_RIMULDAR, TILE_TOWN, rimuldar_lm1, rimuldar_lm2);
    map->meta[BRECCONARY].border =
            place(map, WARP_BRECCONARY, TILE_TOWN, brecc_lm1, brecc_lm2);

    map->meta[KOL].border =
            place(map, WARP_KOL, TILE_TOWN, largest, next);
    map->meta[HAUKSNESS].border =
            place(map, WARP_HAUKSNESS, TILE_TOWN, largest, next);
    map->meta[CANTLIN].border =
            place(map, WARP_CANTLIN, TILE_TOWN, largest, next);

    if (land_missing) {
        printf("No room for every town and cave, retrying...\n");
        return FALSE;
    }
    if (RANDOM_MAP(map) && BIOME(map) == BIOME_WESTEROS) {
        /* The swamp cave must not be a second way past the Wall. An entrance inside Tantegel or Garinham
         * counts as where that town is. */
        int side[2];

        for (i = 0; i < 2; i++) {
            const dw_warp *w = &map->warps_from[i ? WARP_SWAMP_SOUTH : WARP_SWAMP_NORTH];
            if (w->map == TANTEGEL)
                w = &map->warps_from[WARP_TANTEGEL];
            else if (w->map == GARINHAM)
                w = &map->warps_from[WARP_GARINHAM];
            side[i] = map_region[w->x][w->y] == 'B';
        }
        if (side[0] != side[1]) {
            printf("The swamp cave goes under the Wall, retrying...\n");
            return FALSE;
        }
    }
    return TRUE;
}

/**
 * Randomly adds a river to the overworld map
 *
 * @param map The map struct.
 */
static void map_add_river(dw_map *map) {
    int x, y, maindir, dir;

    do {
        x = mt_rand(0, 119);
        y = mt_rand(0, 119);
    } while (map->tiles[x][y] != TILE_WATER);

    maindir = mt_rand(0, 3);
    while (map->tiles[x][y] == TILE_WATER) {

        if (maindir == 0) /* north */
            --y;
        else if (maindir == 1) /* south */
            ++y;
        else if (maindir == 2) /* west */
            --x;
        else if (maindir == 3) /* east */
            ++x;
        if (map_ob(x) || map_ob(y)) {
            return;
        }
    }
    while (map->walkable[x][y] || map->tiles[x][y] != TILE_WATER) {
        map->tiles[x][y] = TILE_WATER;
            dir = mt_rand(0, 3);
         /* keep the general direction */
        if (dir == (maindir ^ 1)) {
            dir ^= 1;
        }
        if (dir == 0) /* north */
            --y;
        else if (dir == 1) /* south */
            ++y;
        else if (dir == 2) /* west */
            --x;
        else if (dir == 3) /* east */
            ++x;

        if (map_ob(x) || map_ob(y)) {
            break;
        }
    }
}

/**
 * Checks where keys are available for later placement of rimuldar
 *
 * @param rom The rom struct
 */
static void check_keys(dw_rom *rom)
{
    dw_chest *chest, *chest_end;

    printf("Checking for key availability...\n");
    chest_end = rom->chests + CHEST_COUNT;

    for ( chest = rom->chests; chest < chest_end; chest++) {
        if (chest->item == KEY) {
            switch(chest->map) {
                case TANTEGEL_BASEMENT:
                    rom->map.key_access |= KEY_IN_BASEMENT;
                    break;
            }
        }
    }
}

/* Biomes use whole numbers only, so the CLI and the site always build the same map. */
static int biome_isqrt(int n)
{
    int r = 0;

    while ((r + 1) * (r + 1) <= n)
        r++;
    return r;
}

/* One step of a slow drift, used to roughen edges without hurting compression. */
static int biome_drift(int value, int limit)
{
    value += (int)mt_rand(0, 2) - 1;
    if (value > limit) value = limit;
    if (value < -limit) value = -limit;
    return value;
}

/* Fills the map with layered blobs, largest first, over a base tile. */
static void biome_fill(dw_rom *rom, dw_tile *tiles, dw_tile base, int layers)
{
    int i, j;

    memset(rom->map.tiles, base, 120 * 120);
    for (i = 0; i < layers; i++) {
        mt_shuffle(tiles, 16, sizeof(dw_tile));
        for (j = 0; j < 16; j++)
            map_fill(&rom->map, tiles[j], rom->map.size * (1 << (10 - i/2)));
    }
    map_smooth(&rom->map);
}

/* Unit directions around the coast: 64 steps of 1000 * (cos, sin), in order. Integers only, so every
 * build draws the same coast for a seed. */
static const int coast_dir[64][2] = {
    {1000, 0}, {995, 98}, {981, 195}, {957, 290}, {924, 383}, {882, 471}, {831, 556}, {773, 634},
    {707, 707}, {634, 773}, {556, 831}, {471, 882}, {383, 924}, {290, 957}, {195, 981}, {98, 995},
    {0, 1000}, {-98, 995}, {-195, 981}, {-290, 957}, {-383, 924}, {-471, 882}, {-556, 831}, {-634, 773},
    {-707, 707}, {-773, 634}, {-831, 556}, {-882, 471}, {-924, 383}, {-957, 290}, {-981, 195}, {-995, 98},
    {-1000, 0}, {-995, -98}, {-981, -195}, {-957, -290}, {-924, -383}, {-882, -471}, {-831, -556}, {-773, -634},
    {-707, -707}, {-634, -773}, {-556, -831}, {-471, -882}, {-383, -924}, {-290, -957}, {-195, -981}, {-98, -995},
    {0, -1000}, {98, -995}, {195, -981}, {290, -957}, {383, -924}, {471, -882}, {556, -831}, {634, -773},
    {707, -707}, {773, -634}, {831, -556}, {882, -471}, {924, -383}, {957, -290}, {981, -195}, {995, -98}
};

/* Grows land out of open water the way a Continent map does: blobs of the biome's tiles, largest first, so the
 * coast is irregular and there can be lakes and islands. */
static void biome_landmass(dw_rom *rom, dw_tile *tiles)
{
    int i, j;

    memset(rom->map.tiles, TILE_WATER, 120 * 120);
    for (i = 0; i < rom->map.size / 10; i++) {
        mt_shuffle(tiles, 16, sizeof(dw_tile));
        for (j = 0; j < 16; j++)
            map_fill(&rom->map, tiles[j], rom->map.size * (1 << (10 - i/2)));
    }
    map_smooth(&rom->map);
}

/* Cuts the land down to a wobbly circle around the middle. The radius changes gently from one direction to the
 * next, so the coast has bays and points on every side. The radius starts at 46% of the map size and stays
 * between 36% and size / 2 - 2, so a ring or lake drawn in the middle always fits. Radii are in tiles; the
 * distance test uses doubled coordinates because the centre is between tiles 59 and 60. */
static void biome_coast(dw_map *map)
{
    int r[64], s[64], k, pass, x, y, px, py, d2, sector, lo, hi, min, max, step;

    min = map->size * 36 / 100;
    max = map->size / 2 - 2;
    r[0] = map->size * 46 / 100;
    for (k = 1; k < 64; k++) {
        step = (int)mt_rand(0, 2) - 1;
        r[k] = r[k - 1] + step;
        if (r[k] < min) r[k] = min;
        if (r[k] > max) r[k] = max;
    }
    /* The walk does not come back to where it started: tilt it so the last direction meets the first. */
    step = r[63] - r[0];
    for (k = 1; k < 64; k++) {
        r[k] -= step * k / 63;
        if (r[k] < min) r[k] = min;
        if (r[k] > max) r[k] = max;
    }
    /* Smooth round the circle, twice, so no direction is a sharp spike. */
    for (pass = 0; pass < 2; pass++) {
        for (k = 0; k < 64; k++)
            s[k] = (r[(k + 63) % 64] + 2 * r[k] + r[(k + 1) % 64] + 2) / 4;
        for (k = 0; k < 64; k++)
            r[k] = s[k];
    }

    for (y = 0; y < 120; y++) {
        for (x = 0; x < 120; x++) {
            /* Doubled coordinates, so the centre (59.5, 59.5) is the origin. */
            px = 2 * x - 119;
            py = 2 * y - 119;
            d2 = px * px + py * py;
            if (d2 == 0) continue;
            /* The sector whose two edge directions bracket this tile's direction. */
            sector = -1;
            for (k = 0; k < 64; k++) {
                lo = coast_dir[k][0] * py - coast_dir[k][1] * px;
                hi = coast_dir[(k + 1) % 64][0] * py - coast_dir[(k + 1) % 64][1] * px;
                if (lo >= 0 && hi < 0) {
                    sector = k;
                    break;
                }
            }
            if (sector < 0 || d2 > 4 * r[sector] * r[sector])
                map->tiles[x][y] = TILE_WATER;
        }
    }
}

/* A mountain ring around a basin with two passes. Charlock goes in the middle. */
static void biome_caldera(dw_map *map)
{
    int x, y, dy, t, dist, outer, inner, first, second, side;
    int left = 0, right = 0;
    int r_out = map->size * 27 / 100;
    int r_in = r_out - 5;

    /* The coast is already cut, so only water inside the land is changed here. */
    for (y = 60 - r_out - 8; y <= 60 + r_out + 8; y++) {
        for (x = 60 - r_out - 8; x <= 60 + r_out + 8; x++) {
            dist = (x - 60) * (x - 60) + (y - 60) * (y - 60);
            if (dist >= (r_out + 8) * (r_out + 8))
                continue;
            if (map->tiles[x][y] == TILE_WATER) {
                map->tiles[x][y] = dist < r_in * r_in ? TILE_DESERT : TILE_GRASS;
            } else if (dist < r_in * r_in && map->tiles[x][y] == TILE_MOUNTAIN) {
                map->tiles[x][y] = TILE_HILL;
            }
        }
    }
    for (y = 60 - r_out; y <= 60 + r_out; y++) {
        dy = y < 60 ? 60 - y : y - 60;
        left = biome_drift(left, 2);
        right = biome_drift(right, 2);
        outer = biome_isqrt(r_out * r_out - dy * dy);
        inner = dy < r_in ? biome_isqrt(r_in * r_in - dy * dy) : -1;
        for (x = 60 - outer + left; x <= 60 + outer + right; x++) {
            if (inner < 0 || x < 60 - inner + left || x > 60 + inner + right)
                map->tiles[x][y] = TILE_MOUNTAIN;
        }
    }
    /* Passes on two different sides. */
    first = (int)mt_rand(0, 3);
    second = (first + 1 + (int)mt_rand(0, 2)) % 4;
    for (t = r_in - 4; t <= r_out + 4; t++) {
        for (dist = 0; dist < 2; dist++) {
            for (side = 0; side < 2; side++) {
                int dir = side ? second : first;
                if (dir == 0) map->tiles[60 + dist][60 - t] = TILE_GRASS;
                if (dir == 1) map->tiles[60 + dist][60 + t] = TILE_GRASS;
                if (dir == 2) map->tiles[60 - t][60 + dist] = TILE_GRASS;
                if (dir == 3) map->tiles[60 + t][60 + dist] = TILE_GRASS;
            }
        }
    }
    /* Past the ring, keep cutting each pass through any mountains the land fill left in its way, so a
     * pass always opens onto land that can be walked. */
    for (side = 0; side < 2; side++) {
        int dir = side ? second : first;
        for (t = r_out + 5; t <= 59; t++) {
            BOOL cut = FALSE;
            for (dist = 0; dist < 2; dist++) {
                uint8_t *tile = dir == 0 ? &map->tiles[60 + dist][60 - t] :
                                dir == 1 ? &map->tiles[60 + dist][60 + t] :
                                dir == 2 ? &map->tiles[60 - t][60 + dist] : &map->tiles[60 + t][60 + dist];
                if (*tile == TILE_MOUNTAIN) {
                    *tile = TILE_GRASS;
                    cut = TRUE;
                }
            }
            if (!cut)
                break;
        }
    }
}

/* Runs a one tile wide river across the land from one coast to the other. */
static void biome_river(dw_map *map, BOOL vertical)
{
    int a, b, side, on_land = 0;
    int half = map->size / 2 - 4;

    a = 60 + (int)mt_rand(0, half) - half / 2;
    b = 60 - map->size / 2;
    if (b < 0) b = 0;
    side = mt_rand(0, 1) ? 1 : -1;
    while (b < 120 && a > 0 && a < 119) {
        uint8_t *tile = vertical ? &map->tiles[a][b] : &map->tiles[b][a];
        if (*tile == TILE_WATER) {
            if (on_land > 8) return; /* reached the far coast */
        } else {
            on_land++;
            *tile = TILE_WATER;
        }
        if (mt_rand(0, 9) < 3) {
            if (mt_rand(0, 4) == 0) side = -side;
            a += side;
            /* stay connected when turning */
            tile = vertical ? &map->tiles[a][b] : &map->tiles[b][a];
            *tile = TILE_WATER;
        }
        b++;
    }
}

/* Lays a straight bridge from the island out to the shore. dx, dy is the direction.
 * The ring may be mountains or hills there, so the tile past the bridge is made grass. */
static BOOL lake_bridge(dw_map *map, int dx, int dy, int offset)
{
    int x = 60 + (dy ? offset : 0), y = 60 + (dx ? offset : 0);

    /* Off the island, then over the water. */
    while (map->tiles[x][y] != TILE_WATER) {
        x += dx; y += dy;
        if (x < 1 || x > 118 || y < 1 || y > 118)
            return FALSE;
    }
    while (map->tiles[x][y] == TILE_WATER) {
        map->tiles[x][y] = TILE_BRIDGE;
        x += dx; y += dy;
        if (x < 1 || x > 118 || y < 1 || y > 118)
            return FALSE;
    }
    map->tiles[x][y] = TILE_GRASS;
    map->tiles[x + dx][y + dy] = TILE_GRASS;
    return TRUE;
}

/* A ring of land around a lake. Charlock is on an island in the middle, reached by 2 to 4 long bridges. */
static BOOL biome_lake(dw_map *map)
{
    int x, y, dy, inner, i, count, dist;
    int left = 0, right = 0;
    int r_lake = LAKE_RADIUS(map);
    /* N, S, W, E as dx, dy. */
    int dirs[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};

    for (y = 60 - r_lake; y <= 60 + r_lake; y++) {
        dy = y < 60 ? 60 - y : y - 60;
        left = biome_drift(left, 2);
        right = biome_drift(right, 2);
        inner = biome_isqrt(r_lake * r_lake - dy * dy);
        for (x = 60 - inner + left; x <= 60 + inner + right; x++)
            map->tiles[x][y] = TILE_WATER;
    }
    /* The island. place_charlock cuts the moat and castle out of its middle. */
    for (y = 60 - LAKE_ISLAND; y <= 60 + LAKE_ISLAND; y++) {
        for (x = 60 - LAKE_ISLAND; x <= 60 + LAKE_ISLAND; x++) {
            dist = (x - 60) * (x - 60) + (y - 60) * (y - 60);
            if (dist <= LAKE_ISLAND * LAKE_ISLAND)
                map->tiles[x][y] = dist <= 9 ? TILE_GRASS : (mt_rand(0, 3) ? TILE_GRASS : TILE_TREES);
        }
    }
    mt_shuffle(dirs, 4, sizeof(dirs[0]));
    count = (int)mt_rand(2, 4);
    for (i = 0; i < count; i++) {
        if (!lake_bridge(map, dirs[i][0], dirs[i][1], (int)mt_rand(0, 4) - 2))
            return FALSE;
    }
    printf("Lake with %d bridges\n", count);
    return TRUE;
}

/* Draws an outline over the filled land: sea outside it. Each row is nudged left or right and each column
 * up or down, a little at a time, so coasts are not 2x2 stair steps. A whole row moves together (and a whole
 * column), so the sea between two landmasses keeps its width. */
static void biome_template(dw_map *map, const char *const *tpl)
{
    int x, y, tx, ty, s, shift_x[120], shift_y[120];
    char c;

    for (s = 0, y = 0; y < 120; y++) {
        s = biome_drift(s, 2);
        shift_x[y] = s;
    }
    for (s = 0, x = 0; x < 120; x++) {
        s = biome_drift(s, 2);
        shift_y[x] = s;
    }
    for (y = 0; y < 120; y++) {
        for (x = 0; x < 120; x++) {
            tx = x + shift_x[y];
            ty = y + shift_y[x];
            c = '.';
            if (x > 0 && x < 119 && y > 0 && y < 119 && tx >= 0 && tx < 120 && ty >= 0 && ty < 120)
                c = tpl[ty / 2][tx / 2];
            map_region[x][y] = c;
            if (c == '.')
                map->tiles[x][y] = TILE_WATER;
        }
    }
}

static const int dir4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

/* The region a tile belongs to for walking: Europe, Asia and Africa are one continent, and so are North and
 * South America. */
static char earth_group(char c)
{
    return EARTH_MAIN(c) ? 'E' : EARTH_AMERICAS(c) ? 'N' : c;
}

/* Labels the walkable pieces of one region (1, 2, ...) in label, with their sizes in size, and returns how
 * many there are. */
static int earth_label(dw_map *map, char key, int label[120][120], int *size)
{
    static uint8_t qx[120 * 120], qy[120 * 120];
    int cx, cy, i, n = 0, head, tail;

    memset(label, 0, sizeof(int) * 120 * 120);
    for (cx = 0; cx < 120; cx++) {
        for (cy = 0; cy < 120; cy++) {
            if (label[cx][cy] || earth_group(map_region[cx][cy]) != key || !tile_is_walkable(map->tiles[cx][cy]))
                continue;
            n++;
            size[n] = 0;
            head = tail = 0;
            label[cx][cy] = n;
            qx[tail] = (uint8_t)cx;
            qy[tail++] = (uint8_t)cy;
            while (head < tail) {
                const int px = qx[head], py = qy[head++];
                size[n]++;
                for (i = 0; i < 4; i++) {
                    const int nx = px + dir4[i][0], ny = py + dir4[i][1];
                    if (map_ob(nx) || map_ob(ny) || label[nx][ny] || earth_group(map_region[nx][ny]) != key ||
                            !tile_is_walkable(map->tiles[nx][ny]))
                        continue;
                    label[nx][ny] = n;
                    qx[tail] = (uint8_t)nx;
                    qy[tail++] = (uint8_t)ny;
                }
            }
        }
    }
    return n;
}

/* 1 on every tile of the biggest walkable piece of its region. A bridge lands only on these, so it never ends
 * on a lone tile or a pocket of grass cut off by mountains or water. */
static uint8_t earth_big[120][120];

/* The label of the biggest piece, from the sizes earth_label returns. */
static int earth_biggest(const int *size, int n)
{
    int i, big = 1;

    for (i = 2; i <= n; i++)
        if (size[i] > size[big])
            big = i;
    return big;
}

static void earth_mark_big(dw_map *map)
{
    static int label[120][120];
    static int size[120 * 120 + 1];
    static char done[256];
    int x, y, i, n, big;
    char key;

    memset(earth_big, 0, sizeof(earth_big));
    memset(done, 0, sizeof(done));
    for (x = 1; x < 119; x++) {
        for (y = 1; y < 119; y++) {
            key = earth_group(map_region[x][y]);
            if (key == '.' || done[(uint8_t)key])
                continue;
            done[(uint8_t)key] = 1;
            n = earth_label(map, key, label, size);
            if (!n)
                continue;
            big = earth_biggest(size, n);
            for (i = 0; i < 120 * 120; i++)
                if (label[i / 120][i % 120] == big)
                    earth_big[i / 120][i % 120] = 1;
        }
    }
}

/* Whether a walkable tile of the given region touches this tile. Region 0 means the main continent
 * (Europe, Asia or Africa). */
static BOOL earth_touches(dw_map *map, int x, int y, char region)
{
    int i, nx, ny;
    char c;

    for (i = 0; i < 4; i++) {
        nx = x + dir4[i][0];
        ny = y + dir4[i][1];
        if (map_ob(nx) || map_ob(ny) || !tile_is_walkable(map->tiles[nx][ny]) || !earth_big[nx][ny])
            continue;
        c = map_region[nx][ny];
        if (region ? c == region : EARTH_MAIN(c))
            return TRUE;
    }
    return FALSE;
}

/* A bridge may go on open sea that does not touch the Americas, which are reached only by raft. */
static BOOL earth_sea(dw_map *map, int x, int y)
{
    int i, nx, ny;

    if (x < 1 || x > 118 || y < 1 || y > 118)
        return FALSE;
    if (map->tiles[x][y] != TILE_WATER || map_region[x][y] != '.')
        return FALSE;
    for (i = 0; i < 4; i++) {
        nx = x + dir4[i][0];
        ny = y + dir4[i][1];
        if (EARTH_AMERICAS(map_region[nx][ny]))
            return FALSE;
    }
    return TRUE;
}

/* Walks a straight or L-shaped bridge from (sx, sy) to (tx, ty). bend 0 goes across first, bend 1 goes
 * down or up first. With draw FALSE it only checks the way is open sea. */
static BOOL earth_path(dw_map *map, int sx, int sy, int tx, int ty, int bend, BOOL draw)
{
    int x = sx, y = sy;

    for (;;) {
        if (draw)
            map->tiles[x][y] = TILE_BRIDGE;
        else if (!earth_sea(map, x, y))
            return FALSE;
        if (x == tx && y == ty)
            return TRUE;
        if (bend == 0 ? x != tx : y == ty)
            x += tx > x ? 1 : -1;
        else
            y += ty > y ? 1 : -1;
    }
}

/* Joins an island (Australia, Japan, the Arctic or Antarctica) to the main continent with a bridge that is
 * straight or bends once. It picks among the shortest ways, up to 2 tiles longer than the shortest. */
static BOOL earth_bridge(dw_map *map, char target)
{
    static uint8_t from[120 * 120][2], to[120 * 120][2];
    int nf = 0, nt = 0, i, j, bend, len, best = 1000, count = 0, pass;
    int pick_f = 0, pick_t = 0, pick_bend = 0;
    int x, y;

    for (x = 1; x < 119; x++) {
        for (y = 1; y < 119; y++) {
            if (!earth_sea(map, x, y))
                continue;
            if (earth_touches(map, x, y, 0)) {
                from[nf][0] = x; from[nf][1] = y; nf++;
            }
            if (earth_touches(map, x, y, target)) {
                to[nt][0] = x; to[nt][1] = y; nt++;
            }
        }
    }
    /* The first pass finds the shortest way, the second picks one of the ways close to it. */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < nf; i++) {
            for (j = 0; j < nt; j++) {
                len = abs(from[i][0] - to[j][0]) + abs(from[i][1] - to[j][1]);
                if (len > (pass ? best + 2 : best))
                    continue;
                for (bend = 0; bend < 2; bend++) {
                    if (!earth_path(map, from[i][0], from[i][1], to[j][0], to[j][1], bend, FALSE))
                        continue;
                    if (!pass) {
                        best = len;
                    } else if (mt_rand(0, count++) == 0) {
                        pick_f = i; pick_t = j; pick_bend = bend;
                    }
                    break;
                }
            }
        }
        if (best == 1000)
            return FALSE;
    }
    earth_path(map, from[pick_f][0], from[pick_f][1], to[pick_t][0], to[pick_t][1], pick_bend, TRUE);
    return TRUE;
}

/* The mountains drawn with the land can cut it in pieces: a pocket of grass inside a ring of mountains, or a
 * bridge that lands in a pocket, leaves part of an island with no way in. This opens a path through the
 * mountains from the biggest piece of each region to every other piece, turning the mountains on it to hills.
 * It draws no random numbers. */
static void earth_connect(dw_map *map)
{
    static int label[120][120], parent[120][120];
    static uint8_t qx[120 * 120], qy[120 * 120];
    static int size[120 * 120 + 1];
    static char done[256];
    int x, y, i, n, head, tail, big, cx, cy, round;
    char key;

    memset(done, 0, sizeof(done));
    for (x = 1; x < 119; x++) {
        for (y = 1; y < 119; y++) {
            key = earth_group(map_region[x][y]);
            if (key == '.' || done[(uint8_t)key])
                continue;
            done[(uint8_t)key] = 1;
            for (round = 0; round < 200; round++) {
                n = earth_label(map, key, label, size);
                if (n <= 1)
                    break;
                big = earth_biggest(size, n);
                /* Search outward from the biggest piece through this region's land (walkable or mountain) until
                 * another piece is touched, then open the mountains on the way back. */
                memset(parent, 0xff, sizeof(parent));
                head = tail = 0;
                for (cx = 0; cx < 120; cx++)
                    for (cy = 0; cy < 120; cy++)
                        if (label[cx][cy] == big) {
                            parent[cx][cy] = -2;
                            qx[tail] = (uint8_t)cx;
                            qy[tail++] = (uint8_t)cy;
                        }
                cx = -1;
                cy = -1;
                while (head < tail && cx < 0) {
                    const int px = qx[head], py = qy[head++];
                    for (i = 0; i < 4; i++) {
                        const int nx = px + dir4[i][0], ny = py + dir4[i][1];
                        if (map_ob(nx) || map_ob(ny) || parent[nx][ny] != -1 || earth_group(map_region[nx][ny]) != key)
                            continue;
                        if (map->tiles[nx][ny] != TILE_MOUNTAIN && !tile_is_walkable(map->tiles[nx][ny]))
                            continue;
                        parent[nx][ny] = px * 120 + py;
                        if (label[nx][ny] && label[nx][ny] != big) {
                            cx = nx;
                            cy = ny;
                            break;
                        }
                        qx[tail] = (uint8_t)nx;
                        qy[tail++] = (uint8_t)ny;
                    }
                }
                if (cx < 0)
                    break;   /* no way through the region's land */
                while (parent[cx][cy] >= 0) {
                    if (map->tiles[cx][cy] == TILE_MOUNTAIN)
                        map->tiles[cx][cy] = TILE_HILL;
                    i = parent[cx][cy];
                    cx = i / 120;
                    cy = i % 120;
                }
            }
        }
    }
}

/* The world: the Americas in the west, Europe, Asia and Africa as one continent in the east, and Australia,
 * Japan, the Arctic and Antarctica joined to it by bridges. The rafts to the Americas come later, once the
 * towns are placed. */
static BOOL biome_earth(dw_map *map)
{
    static const char islands[] = "UJRT";
    int i;

    biome_template(map, earth_template);
    earth_mark_big(map);
    for (i = 0; islands[i]; i++) {
        if (!earth_bridge(map, islands[i])) {
            printf("No way to bridge to an island, retrying...\n");
            return FALSE;
        }
    }
    earth_connect(map);
    /* earth_connect turns mountains into hills to join cut-off pieces, so the biggest pieces are judged again
     * here before the rafts pick their docks. */
    earth_mark_big(map);
    return TRUE;
}

/* A plain land tile a dock or a raft arrival can use. */
static BOOL dock_land(dw_tile tile)
{
    return tile == TILE_GRASS || tile == TILE_DESERT || tile == TILE_HILL || tile == TILE_TREES;
}

/* Picks a dock for a region: a bridge tile on open water, like a pier, with exactly one land tile beside it,
 * to the east or west so the planks run the right way. That land tile is a plain tile of the region, not
 * beside a town, and it is where a raft from the other dock arrives. A bridge tile never starts a warp
 * on its own, so an Earth build also patches the step code (see raft_piers in dwr.c). */
static BOOL earth_dock(dw_map *map, char region, uint8_t *dock_x, uint8_t *dock_y,
        uint8_t *land_x, uint8_t *land_y)
{
    const dw_warp *charlock = &map->warps_from[WARP_CHARLOCK];
    int x, y, i, j, nx, ny, mx, my, lands, side, count = 0;
    dw_tile t;

    for (x = 2; x < 118; x++) {
        for (y = 2; y < 118; y++) {
            if (map->tiles[x][y] != TILE_WATER || map_region[x][y] != '.')
                continue;
            if (abs(x - (int)charlock->x) < 8 && abs(y - (int)charlock->y) < 8)
                continue;
            lands = 0;
            side = -1;
            for (i = 0; i < 4; i++) {
                nx = x + dir4[i][0];
                ny = y + dir4[i][1];
                t = map->tiles[nx][ny];
                if (t == TILE_WATER)
                    continue;
                lands++;
                if (i < 2 && dock_land(t) && map_region[nx][ny] == region)
                    side = i;
            }
            if (lands != 1 || side < 0)
                continue;
            nx = x + dir4[side][0];
            ny = y + dir4[side][1];
            /* Not beside a town, cave or castle, and the land is a real stretch, not a single tile. It must also
             * be on the biggest piece of its land, so a raft never lands in a pocket that is cut off. */
            for (j = 0; j < 4; j++) {
                mx = nx + dir4[j][0];
                my = ny + dir4[j][1];
                t = map->tiles[mx][my];
                if (t == TILE_TOWN || t == TILE_CAVE || t == TILE_CASTLE)
                    break;
            }
            if (j < 4 || map->walkable[nx][ny] == 0 || !earth_big[nx][ny])
                continue;
            if (mt_rand(0, count++) == 0) {
                *dock_x = x;
                *dock_y = y;
                *land_x = nx;
                *land_y = ny;
            }
        }
    }
    if (!count)
        return FALSE;
    map->tiles[*dock_x][*dock_y] = TILE_BRIDGE;
    /* Keep later docks off this one and off its landing tile. */
    map_region[*dock_x][*dock_y] = '*';
    map_region[*land_x][*land_y] = '*';
    return TRUE;
}

/* Two rafts: one from North America and one from South America, each to a different one of Europe, Asia
 * and Africa. A raft is 2 warps, one each way, and each lands beside the other dock. */
/* The way to face when landing on (lx, ly) beside the pier at (px, py): away from the pier. */
static uint8_t raft_facing(int px, int py, int lx, int ly)
{
    if (lx > px) return DIR_RIGHT;
    if (lx < px) return DIR_LEFT;
    return ly > py ? DIR_DOWN : DIR_UP;
}

static BOOL earth_rafts(dw_map *map)
{
    static const char *const names = "NSEAF";
    static const char *const long_names[] = {"North America", "South America", "Europe", "Asia", "Africa"};
    static const dw_warp_index out[2] = {WARP_RAFT_1_OUT, WARP_RAFT_2_OUT};
    static const dw_warp_index back[2] = {WARP_RAFT_1_BACK, WARP_RAFT_2_BACK};
    char dest[3] = {'E', 'A', 'F'};
    uint8_t ax, ay, bx, by, alx, aly, blx, bly;
    int r;

    mt_shuffle(dest, 3, sizeof(char));
    for (r = 0; r < 2; r++) {
        if (!earth_dock(map, r ? 'S' : 'N', &ax, &ay, &alx, &aly) ||
                !earth_dock(map, dest[r], &bx, &by, &blx, &bly))
            return FALSE;
        map->warps_from[out[r]] = (dw_warp){ 1, ax, ay };
        map->warps_to[out[r]] = (dw_warp){ 1, blx, bly };
        map->warps_from[back[r]] = (dw_warp){ 1, bx, by };
        map->warps_to[back[r]] = (dw_warp){ 1, alx, aly };
        /* Land facing away from the pier, onto the land, not toward the water. */
        map->warp_facing[out[r]] = raft_facing(bx, by, blx, bly);
        map->warp_facing[back[r]] = raft_facing(ax, ay, alx, aly);
        printf("Raft from %s (%d, %d) to %s (%d, %d)\n", long_names[r], ax, ay,
                long_names[strchr(names, dest[r]) - names], bx, by);
    }
    return TRUE;
}

/* The stairs the rafts take over lead nowhere now, so they become floor. Run once, before the rafts
 * overwrite the warps. */
static void free_raft_stairs(dw_rom *rom)
{
    static const dw_warp_index slots[4] = {WARP_RAFT_1_OUT, WARP_RAFT_1_BACK, WARP_RAFT_2_OUT, WARP_RAFT_2_BACK};
    const dw_warp *w;
    dw_dungeon_tile tile;
    int i, end;

    for (i = 0; i < 4; i++) {
        for (end = 0; end < 2; end++) {
            w = end ? &rom->map.warps_to[slots[i]] : &rom->map.warps_from[slots[i]];
            if (w->map == 1)
                continue;
            tile = get_dungeon_tile(rom, w->map, w->x, w->y);
            if (tile == DUNGEON_TILE_STAIRS_UP || tile == DUNGEON_TILE_STAIRS_DOWN)
                set_dungeon_tile(rom, w->map, w->x, w->y, DUNGEON_TILE_BRICK);
        }
    }
}

/* Westeros: the Wall across the North with one pass, the swampy Neck, and Charlock beyond the Wall. */
static BOOL biome_westeros(dw_map *map)
{
    int x, y, i, count = 0, gap = -1, top, bottom, lm_sizes[256], largest, next;

    biome_template(map, westeros_template);
    for (x = 0; x < 120; x++) {
        for (y = 0; y < 120; y++) {
            if (map_region[x][y] == 'W')
                map->tiles[x][y] = TILE_MOUNTAIN;
            else if (map_region[x][y] == 'K')
                map->tiles[x][y] = TILE_SWAMP;
        }
    }
    /* The pass: 2 columns of the Wall, at least 4 tiles of Wall to each side so it is not at the coast. */
    for (x = 5; x < 114; x++) {
        for (i = -4; i <= 5; i++) {
            for (y = 1; y < 119 && map_region[x + i][y] != 'W'; y++)
                ;
            if (y == 119)
                break;
        }
        if (i <= 5)
            continue;
        if (mt_rand(0, count++) == 0)
            gap = x;
    }
    if (gap < 0)
        return FALSE;
    for (x = gap; x <= gap + 1; x++) {
        for (top = 1; map_region[x][top] != 'W'; top++)
            ;
        for (bottom = top; map_region[x][bottom + 1] == 'W'; bottom++)
            ;
        for (y = top; y <= bottom; y++)
            map->tiles[x][y] = TILE_HILL;
        /* Clear mountains on the way to the pass from both sides. */
        for (i = -1; i <= 1; i++) {
            /* Clamped so a Wall near the map edge can't index row -3 or past the last row. */
            for (y = top - 4 < 1 ? 1 : top - 4; y <= (bottom + 4 > 118 ? 118 : bottom + 4); y++) {
                if (map_region[x + i][y] != 'W' && map->tiles[x + i][y] == TILE_MOUNTAIN)
                    map->tiles[x + i][y] = TILE_GRASS;
            }
        }
    }
    /* North and south have to be one land through the pass. */
    for (top = 1; map_region[gap][top] != 'W'; top++)
        ;
    for (bottom = top; map_region[gap][bottom + 1] == 'W'; bottom++)
        ;
    find_walkable_area(map, lm_sizes, &largest, &next);
    if (!map->walkable[gap][top - 1] || map->walkable[gap][top - 1] != map->walkable[gap][bottom + 1] ||
            map->walkable[gap][top - 1] != largest) {
        printf("The pass through the Wall is blocked, retrying...\n");
        return FALSE;
    }
    printf("The pass through the Wall is at x %d\n", gap);
    return TRUE;
}

/* Connects land masses across a one tile river in either direction. */
static BOOL biome_add_bridge(dw_map *map)
{
    int x, y, count = 0, bx = 0, by = 0;

    for (y = 1; y < 119; y++) {
        for (x = 1; x < 119; x++) {
            if (map->tiles[x][y] != TILE_WATER)
                continue;
            if (needs_bridge(map->walkable[x-1][y], map->walkable[x+1][y]) ||
                    needs_bridge(map->walkable[x][y-1], map->walkable[x][y+1])) {
                count++;
                if (mt_rand(0, count - 1) == 0) {
                    bx = x;
                    by = y;
                }
            }
        }
    }
    if (!count)
        return FALSE;
    map->tiles[bx][by] = TILE_BRIDGE;
    return TRUE;
}

/**
 * Draws the land for a biome, ready for the same checks as a normal map.
 *
 * @param rom The rom struct
 */
static BOOL biome_terrain(dw_rom *rom)
{
    int i, lm_sizes[256], largest, next, layers;
    dw_map *map = &rom->map;
    dw_tile tiles[16] = {
        TILE_GRASS, TILE_GRASS, TILE_GRASS, TILE_GRASS,
        TILE_GRASS, TILE_TREES, TILE_TREES, TILE_TREES,
        TILE_TREES, TILE_DESERT, TILE_DESERT, TILE_HILL,
        TILE_HILL, TILE_MOUNTAIN, TILE_SWAMP, TILE_SWAMP
    };

    if (BIOME(rom) == BIOME_WASTELAND) {
        static const dw_tile waste[16] = {
            TILE_DESERT, TILE_DESERT, TILE_DESERT, TILE_DESERT,
            TILE_DESERT, TILE_DESERT, TILE_DESERT, TILE_SWAMP,
            TILE_SWAMP, TILE_SWAMP, TILE_HILL, TILE_HILL,
            TILE_MOUNTAIN, TILE_MOUNTAIN, TILE_TREES, TILE_WATER
        };
        memcpy(tiles, waste, sizeof(tiles));
    }
    if (BIG_SWAMP(rom)) {
        printf("Adding more swamp...\n");
        tiles[0] = tiles[1] = tiles[4] = tiles[8] = tiles[9] = TILE_SWAMP;
    }

    layers = map->size / 15;
    switch (BIOME(rom)) {
        case BIOME_CALDERA:
            printf("Drawing a caldera\n");
            biome_fill(rom, tiles, TILE_GRASS, layers);
            biome_coast(map);
            biome_caldera(map);
            break;
        case BIOME_EARTH:
            printf("Drawing the Earth\n");
            biome_fill(rom, tiles, TILE_GRASS, layers);
            return biome_earth(map);
        case BIOME_WESTEROS:
            printf("Drawing Westeros\n");
            biome_fill(rom, tiles, TILE_GRASS, layers);
            return biome_westeros(map);
        case BIOME_LAKE:
            printf("Drawing a lake\n");
            biome_fill(rom, tiles, TILE_GRASS, layers);
            biome_coast(map);
            return biome_lake(map);
        case BIOME_WASTELAND:
            printf("Drawing a wasteland\n");
            biome_landmass(rom, tiles);
            find_walkable_area(map, lm_sizes, &largest, &next);
            for (i = 0; i < map->size / 20; i++)
                map_add_river(map);
            break;
        default:
            printf("Drawing riverlands\n");
            biome_landmass(rom, tiles);
            for (i = 0; i < map->size / 20; i++)
                biome_river(map, TRUE);
            for (i = 0; i < map->size / 15; i++)
                biome_river(map, FALSE);
            break;
    }
    return TRUE;
}

#define SAFE_START_RADIUS 3

/**
 * Turns swamp around Tantegel into grass, so the first steps are safe.
 * Charlock keeps the swamp inside its moat.
 *
 * @param map The map struct
 */
/* Where an entrance is on the overworld: its own tile, or for one inside Tantegel or Garinham, that town's
 * tile. FALSE for anything else. */
static BOOL overworld_spot(dw_map *map, const dw_warp *w, int *x, int *y)
{
    if (w->map == TANTEGEL)
        w = &map->warps_from[WARP_TANTEGEL];
    else if (w->map == GARINHAM)
        w = &map->warps_from[WARP_GARINHAM];
    if (w->map != 1)
        return FALSE;
    *x = w->x;
    *y = w->y;
    return TRUE;
}

/* Walks the finished map from Tantegel and checks every place the hero needs can be reached. Towns, caves
 * and castles can be walked through, since leaving one puts the hero back on its tile. A raft dock (a warp
 * from the overworld to the overworld) is not: stepping on it moves the hero to its landing tile. The swamp
 * cave joins its two entrances. Dungeon insides are left to stair_shuffle's own checks. */
static BOOL places_reachable(dw_rom *rom)
{
    static uint8_t seen[120][120];
    static uint8_t qx[120 * 120], qy[120 * 120];
    dw_map *map = &rom->map;
    int head = 0, tail = 0, i, x, y, nx, ny, swamp[2][2], swamps = 0;
    BOOL dock;

    memset(seen, 0, sizeof(seen));
    for (i = 0; i < 2; i++) {
        if (overworld_spot(map, &map->warps_from[i ? WARP_SWAMP_SOUTH : WARP_SWAMP_NORTH], &x, &y)) {
            swamp[swamps][0] = x;
            swamp[swamps][1] = y;
            swamps++;
        }
    }
#define VISIT(vx, vy) do { if (!map_ob(vx) && !map_ob(vy) && !seen[vx][vy] && \
        tile_is_walkable(map->tiles[vx][vy])) { seen[vx][vy] = 1; qx[tail] = (vx); qy[tail++] = (vy); } } while (0)
    VISIT(map->warps_from[WARP_TANTEGEL].x, map->warps_from[WARP_TANTEGEL].y);
    while (head < tail) {
        x = qx[head];
        y = qy[head++];
        dock = FALSE;
        for (i = 0; i < 51; i++) {
            if (map->warps_from[i].map == 1 && map->warps_to[i].map == 1 &&
                    map->warps_from[i].x == x && map->warps_from[i].y == y) {
                VISIT(map->warps_to[i].x, map->warps_to[i].y);
                dock = TRUE;
            }
        }
        if (dock)
            continue;
        if (swamps == 2) {
            for (i = 0; i < 2; i++) {
                if (swamp[i][0] == x && swamp[i][1] == y)
                    VISIT(swamp[!i][0], swamp[!i][1]);
            }
        }
        for (i = 0; i < 4; i++) {
            nx = x + dir4[i][0];
            ny = y + dir4[i][1];
            VISIT(nx, ny);
        }
    }
#undef VISIT

    /* Every entrance on the overworld (Charlock's castle is behind its moat), the docks, the Rainbow Drop
     * spot, where Return and Wings land, and the token. */
    for (i = 0; i < 51; i++) {
        const dw_warp *w = &map->warps_from[i];
        if (w->map == 1 && i != WARP_CHARLOCK && !seen[w->x][w->y])
            return FALSE;
    }
    x = map->rainbow_drop->x;
    y = map->rainbow_drop->y;
    if (map_ob(x) || !seen[x][y])
        return FALSE;
    if (!seen[map->return_point->x][map->return_point->y])
        return FALSE;
    if (!seen[rom->search_table->x[0]][rom->search_table->y[0]])
        return FALSE;
    return TRUE;
}

static void safe_start(dw_map *map)
{
    const dw_warp *t = &map->warps_from[WARP_TANTEGEL];
    const dw_warp *c = &map->warps_from[WARP_CHARLOCK];
    static const struct { dw_warp_index warp; int town; } towns[] = {
        {WARP_BRECCONARY, BRECCONARY}, {WARP_GARINHAM, GARINHAM}, {WARP_KOL, KOL},
        {WARP_RIMULDAR, RIMULDAR}, {WARP_CANTLIN, CANTLIN}, {WARP_HAUKSNESS, HAUKSNESS}};
    size_t i;
    int x, y;

    for (i = 0; i < sizeof(towns) / sizeof(towns[0]); i++) {
        const dw_warp *w = &map->warps_from[towns[i].warp];
        if (abs(w->x - t->x) <= SAFE_START_RADIUS && abs(w->y - t->y) <= SAFE_START_RADIUS
                && map->meta[towns[i].town].border == BORDER_SWAMP)
            map->meta[towns[i].town].border = BORDER_GRASS;
    }
    for (y = t->y - SAFE_START_RADIUS; y <= t->y + SAFE_START_RADIUS; y++) {
        for (x = t->x - SAFE_START_RADIUS; x <= t->x + SAFE_START_RADIUS; x++) {
            if (map_ob(x) || map_ob(y))
                continue;
            if (abs(x - c->x) <= 3 && abs(y - c->y) <= 3)
                continue;
            if (map->tiles[x][y] == TILE_SWAMP)
                map->tiles[x][y] = TILE_GRASS;
        }
    }
    if (map->meta[TANTEGEL].border == BORDER_SWAMP)
        map->meta[TANTEGEL].border = BORDER_GRASS;
}

/**
 * Generates a new in-game map.
 *
 * @param rom The rom struct
 * @return A boolean indicating whether terrain generation was successful or not
 */
void map_generate_terrain(dw_rom *rom)
{
    int i, j, x, y, lm_sizes[256], max_fill;
    int largest, next, total_area, map_tries = 0;

    check_keys(rom);

    if (SMALL_MAP(rom)) {
        printf("Generating a small map\n");
        rom->map.size = 90;
    } else if (VERY_SMALL_MAP(rom)) {
        printf("Generating a very small map\n");
        rom->map.size = 60;
    } else {
        printf("Generating a normal size map\n");
        rom->map.size = 120;
    }
    if (TEMPLATE_BIOME(rom) && rom->map.size != 120) {
        /* The outline needs the whole map. */
        printf("This biome is always normal size\n");
        rom->map.size = 120;
    }
    if (RAFT_BIOME(rom))
        free_raft_stairs(rom);
    //rom->map.size /= 3;

retry_map:
    if (++map_tries > 2000)
        dwr_give_up("Could not draw a map that works with these flags.");
    if (!RANDOM_MAP(rom)) {

        map_decode(&rom->map);
        for (i=0; i < VAN_SPOT_COUNT; ++i) {
            /* clear out the towns & caves so they can be placed again */
            x = vanilla_spots[i][0];
            y = vanilla_spots[i][1];
            rom->map.tiles[x][y] = rom->map.tiles[x-1][y];
            if (!tile_is_walkable(rom->map.tiles[x][y])) {
                rom->map.tiles[x][y] = rom->map.tiles[x+1][y];
            }
        }

    } else {

        if (BIOME(rom) != BIOME_CONTINENT) {
            if (!biome_terrain(rom))
                goto retry_map;
        } else {
            dw_tile tiles[16] = {
                    TILE_GRASS, TILE_GRASS, TILE_GRASS, TILE_SWAMP,
                    TILE_DESERT, TILE_DESERT, TILE_HILL, TILE_MOUNTAIN,
                    TILE_TREES, TILE_TREES, TILE_TREES, TILE_MOUNTAIN,
                    TILE_WATER, TILE_WATER, TILE_WATER, TILE_SWAMP
            };

            if (BIG_SWAMP(rom)) {
                printf("Adding more swamp...\n");
                tiles[0] = tiles[1] = tiles[4] = tiles[8] = tiles[9] = TILE_SWAMP;
            }

            /* first fill the map with water */
            memset(rom->map.tiles, TILE_WATER, 120 * 120);

            /* now fill it in with some other tiles */
            for (i = 0; i < rom->map.size / 10; i++) {
                mt_shuffle(tiles, sizeof(tiles) / sizeof(dw_tile), sizeof(dw_tile));
                /* size of the fill blob should decrease each iteration */
                max_fill = rom->map.size * (1 << (10 - i/2));
                for (j = 0; j < 16; j++) {
                    map_fill(&rom->map, tiles[j], max_fill);
                }
            }

            map_smooth(&rom->map);

            find_walkable_area(&rom->map, lm_sizes, &largest, &next);

            for (i=0; i < RIVER_COUNT; i++) {
                map_add_river(&rom->map);
            }
        }

        if (BIOME(rom) == BIOME_RIVERLANDS) {
            /* Rivers cut the land, so bridge it first and then judge the size. */
            find_walkable_area(&rom->map, lm_sizes, &largest, &next);
            while (biome_add_bridge(&rom->map))
                find_walkable_area(&rom->map, lm_sizes, &largest, &next);
        }
        find_walkable_area(&rom->map, lm_sizes, &largest, &next);
        if (!largest)
            goto retry_map;
        /* make sure the walkable area is large enough */
        total_area = lm_sizes[largest - 1];
        if (largest != next)
            total_area += lm_sizes[next - 1];
        /* A lake takes out about 3 * r * r tiles of land, so it needs that much less. An outline sets its
         * own amount of land, so it only has to keep most of it. A caldera sits inside the round coast, which
         * holds about 40% of the square at the smallest, so its limit follows the area, not the width. */
        if (total_area < (TEMPLATE_BIOME(rom) ? rom->map.size * 30 :
                BIOME(rom) == BIOME_CALDERA ? rom->map.size * rom->map.size * 3 / 10 :
                rom->map.size * 42 - (BIOME(rom) == BIOME_LAKE ?
                3 * LAKE_RADIUS(&rom->map) * LAKE_RADIUS(&rom->map) : 0))) {
            printf("Total map area (%d) is too small, retrying...\n",
                    total_area);
            goto retry_map;
        }

        /* Earth keeps its seas: the Americas are reached only by raft. */
        if (BIOME(rom) != BIOME_RIVERLANDS && !RAFT_BIOME(rom)) {
            while (add_bridges(&rom->map)) {
                find_walkable_area(&rom->map, lm_sizes, &largest, &next);
            }
        }
    }

    find_walkable_area(&rom->map, lm_sizes, &largest, &next);
    if (!place_landmarks(&rom->map)) {
        goto retry_map;
    }
    if (RAFT_BIOME(rom)) {
        find_walkable_area(&rom->map, lm_sizes, &largest, &next);
        if (!earth_rafts(&rom->map)) {
            printf("No room for the rafts, retrying...\n");
            goto retry_map;
        }
    }

    /* place the token */
    find_walkable_area(&rom->map, lm_sizes, &largest, &next);
    land_missing = FALSE;
    if (!map_find_land(&rom->map, largest, next, &rom->search_table->x[0],
            &rom->search_table->y[0], TRUE))
        goto retry_map;
    {
        /* The token can't be under a pier or any other entrance, where the hero never stands. */
        for (i = 0; rom->map.tiles[rom->search_table->x[0]][rom->search_table->y[0]] == TILE_CAVE ||
                rom->map.tiles[rom->search_table->x[0]][rom->search_table->y[0]] == TILE_BRIDGE ||
                rom->map.tiles[rom->search_table->x[0]][rom->search_table->y[0]] == TILE_TOWN ||
                rom->map.tiles[rom->search_table->x[0]][rom->search_table->y[0]] == TILE_CASTLE; i++) {
            if (i > 1000 || !map_find_land(&rom->map, largest, next, &rom->search_table->x[0],
                    &rom->search_table->y[0], TRUE))
                goto retry_map;
        }
    }

    if (SAFE_START(rom))
        safe_start(&rom->map);

    if (RANDOM_MAP(rom) && !places_reachable(rom)) {
        printf("A place cannot be reached, retrying...\n");
        goto retry_map;
    }

    if (!map_encode(&rom->map))
        goto retry_map;
}


/* Zelda II map data: the encoded rows (2294 bytes, $9D5D-$A652), the row pointers and the enemy zones. Rows 113
 * to 119 share the same water row, so the map encodes into the same room as any other. */
static const uint8_t z2_map_data[MAP_ENCODED_SIZE] = {
    0x3B, 0x4D, 0x17, 0x4F, 0x4B, 0x11, 0x4F, 0x4F, 0x4F, 0x4F, 0x3C, 0x4B, 0x12, 0x32, 0x14, 0x4F,
    0x47, 0x13, 0x4F, 0x4F, 0x4F, 0x4F, 0x3F, 0x46, 0x14, 0x30, 0x90, 0x30, 0x15, 0x4F, 0x44, 0x13,
    0xA0, 0x10, 0x4F, 0x4F, 0x4F, 0x4F, 0x3F, 0x32, 0x41, 0x1F, 0x10, 0x4F, 0x43, 0x15, 0x4F, 0x4F,
    0x41, 0x3E, 0x4F, 0x3F, 0x32, 0x1F, 0x12, 0x4F, 0x42, 0x16, 0x4F, 0x4F, 0x32, 0x1C, 0x30, 0x4F,
    0x05, 0x3C, 0x1F, 0x13, 0x4F, 0x40, 0x17, 0x4F, 0x4F, 0x40, 0x31, 0x10, 0x38, 0x10, 0x33, 0x4F,
    0x01, 0x80, 0x02, 0x3B, 0x1F, 0x14, 0x4B, 0x1C, 0x4F, 0x4F, 0x32, 0x10, 0x78, 0x12, 0x32, 0x4F,
    0x08, 0x37, 0x1F, 0x16, 0x47, 0x1E, 0x4F, 0x4F, 0x33, 0x10, 0x36, 0x70, 0x32, 0x10, 0x33, 0x4F,
    0x09, 0x35, 0x1F, 0x17, 0x46, 0x1F, 0x4F, 0x4F, 0x33, 0x10, 0x30, 0x14, 0x30, 0x72, 0x30, 0x10,
    0x33, 0x4F, 0x05, 0x31, 0x0D, 0x1F, 0x12, 0x44, 0x1F, 0x4F, 0x4F, 0x42, 0x31, 0x10, 0x30, 0x10,
    0x32, 0x10, 0x30, 0x70, 0x30, 0x70, 0x30, 0x10, 0x34, 0x4F, 0x00, 0x37, 0x6B, 0x05, 0x15, 0x05,
    0x11, 0x43, 0x1F, 0x11, 0x4F, 0x4F, 0x41, 0x31, 0x10, 0x30, 0x12, 0x30, 0x10, 0x71, 0x30, 0x70,
    0x30, 0x10, 0x35, 0x4F, 0x00, 0x37, 0x6F, 0x02, 0x12, 0x09, 0x41, 0x34, 0x20, 0x31, 0x1B, 0x4F,
    0x4F, 0x41, 0x31, 0x10, 0x32, 0x10, 0x30, 0x10, 0x32, 0x70, 0x30, 0x10, 0x35, 0x4F, 0x00, 0x37,
    0x6F, 0x03, 0x10, 0x06, 0x60, 0x02, 0x36, 0x21, 0x33, 0x17, 0x4F, 0x4F, 0x42, 0x31, 0x14, 0x30,
    0x12, 0x30, 0x70, 0x30, 0x10, 0x36, 0x4F, 0x00, 0x37, 0x6F, 0x0F, 0x37, 0x22, 0x34, 0x14, 0x4F,
    0x4F, 0x42, 0x39, 0x10, 0x30, 0x70, 0x30, 0x10, 0x37, 0x4F, 0x00, 0x38, 0x6F, 0x0F, 0x38, 0x22,
    0x34, 0x12, 0x4F, 0x4F, 0x42, 0x31, 0x18, 0x30, 0x70, 0x30, 0x10, 0x37, 0x4F, 0x00, 0x39, 0x6E,
    0x0F, 0x03, 0x63, 0x32, 0x22, 0x33, 0x11, 0x4F, 0x4F, 0x42, 0x31, 0x10, 0x36, 0x70, 0x30, 0x70,
    0x30, 0x10, 0x37, 0x4F, 0x00, 0x3A, 0x6B, 0x0F, 0x07, 0x63, 0x32, 0x20, 0x33, 0x10, 0x4F, 0x4F,
    0x43, 0x31, 0x16, 0x30, 0x70, 0x30, 0x71, 0x14, 0x32, 0x4F, 0x00, 0x3A, 0x03, 0x66, 0x0F, 0x09,
    0x66, 0x33, 0x10, 0x4F, 0x4F, 0x43, 0x35, 0x10, 0x32, 0x70, 0x36, 0x10, 0x31, 0x4F, 0x00, 0x3A,
    0x05, 0x63, 0x42, 0x0F, 0x07, 0x65, 0x34, 0x10, 0x4F, 0x4F, 0x44, 0x30, 0x12, 0x30, 0x10, 0x30,
    0x74, 0x30, 0x72, 0x30, 0x10, 0x32, 0x4F, 0x00, 0x3A, 0x07, 0x45, 0x0F, 0x63, 0x02, 0x65, 0x34,
    0x10, 0x4F, 0x4F, 0x44, 0x30, 0x10, 0x30, 0x10, 0x30, 0x10, 0x34, 0x70, 0x32, 0x70, 0x30, 0x10,
    0x33, 0x4F, 0x01, 0x38, 0x07, 0x48, 0x0B, 0x65, 0x02, 0x64, 0x35, 0x10, 0x4F, 0x4F, 0x35, 0x10,
    0x30, 0x10, 0x30, 0x12, 0x71, 0x30, 0x74, 0x30, 0x10, 0x34, 0x30, 0x01, 0x35, 0x08, 0x43, 0x12,
    0x43, 0x09, 0x65, 0x03, 0x63, 0x35, 0x11, 0x4F, 0x4F, 0x35, 0x10, 0x30, 0x10, 0x32, 0x10, 0x36,
    0x70, 0x30, 0x10, 0x30, 0x12, 0x30, 0x30, 0x02, 0x35, 0x07, 0x43, 0x10, 0xA0, 0x10, 0x0C, 0x66,
    0x02, 0x63, 0x36, 0x11, 0x4F, 0x4F, 0x35, 0x10, 0x30, 0x14, 0x75, 0x30, 0x70, 0x30, 0x12, 0xA0,
    0x10, 0x30, 0x03, 0x37, 0x05, 0x43, 0x12, 0x43, 0x08, 0x65, 0x03, 0x62, 0x38, 0x20, 0x4F, 0x4F,
    0x35, 0x10, 0x38, 0x70, 0x46, 0x12, 0x30, 0x03, 0x38, 0x04, 0x4A, 0x07, 0x66, 0x01, 0x80, 0x00,
    0x61, 0x39, 0x20, 0x4F, 0x4F, 0x35, 0x1A, 0xB0, 0x10, 0x30, 0x72, 0x34, 0x03, 0x38, 0x05, 0x48,
    0x07, 0x67, 0x03, 0x3B, 0x20, 0x4F, 0x4F, 0x43, 0x3A, 0x42, 0x10, 0x30, 0x70, 0x35, 0x4F, 0x03,
    0x39, 0x05, 0x43, 0x0A, 0x64, 0x35, 0x01, 0x3B, 0x21, 0x4F, 0x4F, 0x36, 0x76, 0x40, 0x12, 0x30,
    0x72, 0x33, 0x4F, 0x03, 0x39, 0x0A, 0x33, 0x05, 0x61, 0x39, 0x20, 0x3C, 0x20, 0x4F, 0x4F, 0x36,
    0x70, 0x34, 0x70, 0x40, 0x10, 0x34, 0x70, 0x33, 0x4F, 0x30, 0x20, 0x3C, 0x06, 0x38, 0x02, 0x3C,
    0x02, 0x3A, 0x21, 0x4F, 0x4F, 0x35, 0x74, 0x30, 0x70, 0xB0, 0x10, 0x30, 0x12, 0x30, 0x70, 0x33,
    0x4F, 0x30, 0x20, 0x3D, 0x03, 0x3F, 0x38, 0x07, 0x38, 0x20, 0x4F, 0x4F, 0x49, 0xB0, 0x42, 0x10,
    0x30, 0x10, 0x30, 0x10, 0x30, 0x70, 0x33, 0x4F, 0x30, 0x22, 0x3B, 0x90, 0x3F, 0x39, 0x0A, 0x37,
    0x21, 0x4F, 0x4F, 0x34, 0x70, 0x30, 0x72, 0x30, 0x71, 0x10, 0x30, 0x10, 0x30, 0x10, 0x30, 0x70,
    0x33, 0x4F, 0x32, 0x20, 0x3F, 0x3F, 0x34, 0x0C, 0x38, 0x20, 0x4F, 0x4F, 0x75, 0x30, 0x70, 0x32,
    0x70, 0x30, 0x10, 0x30, 0x10, 0x30, 0x10, 0x30, 0x70, 0x33, 0x4F, 0x32, 0x20, 0x3C, 0x72, 0x3F,
    0x31, 0x90, 0x31, 0x0C, 0x90, 0x37, 0x20, 0x4F, 0x4F, 0x34, 0x70, 0x30, 0x74, 0x30, 0x12, 0x30,
    0x10, 0x30, 0x70, 0x32, 0x4F, 0x31, 0x03, 0x33, 0x7B, 0x3F, 0x0F, 0x36, 0x62, 0x4F, 0x4F, 0x42,
    0x31, 0x70, 0x32, 0x70, 0x36, 0x10, 0x30, 0x70, 0x31, 0x4F, 0x30, 0x05, 0x60, 0x31, 0x70, 0xA0,
    0x76, 0x11, 0x71, 0x3C, 0x0F, 0x01, 0x36, 0x60, 0x00, 0x60, 0x4F, 0x4F, 0x42, 0x31, 0x70, 0x1B,
    0x71, 0x32, 0x4F, 0x03, 0x74, 0x31, 0x73, 0x10, 0x79, 0x37, 0x0F, 0x02, 0x37, 0x62, 0x4F, 0x4F,
    0x35, 0x10, 0x3E, 0x4F, 0x01, 0x76, 0x34, 0x7C, 0x12, 0x7D, 0x08, 0x3A, 0x4F, 0x4F, 0x44, 0x30,
    0x10, 0x3A, 0x4F, 0x01, 0x75, 0x38, 0x76, 0x35, 0x7F, 0x05, 0x3B, 0x4F, 0x4F, 0x44, 0x30, 0x10,
    0x38, 0x4F, 0x00, 0x76, 0x3F, 0x37, 0x73, 0x11, 0x77, 0x04, 0x3A, 0x4F, 0x4F, 0x47, 0x10, 0x42,
    0x34, 0x4F, 0x00, 0x76, 0x30, 0x90, 0x3F, 0x36, 0x7C, 0x02, 0x3C, 0x41, 0x12, 0x37, 0x14, 0x4F,
    0x40, 0x18, 0x4F, 0x00, 0x79, 0x3F, 0x34, 0x7D, 0x00, 0x3D, 0x41, 0x13, 0x37, 0x12, 0x62, 0x4F,
    0x1A, 0x4F, 0x00, 0x7B, 0x3F, 0x31, 0x79, 0x34, 0x20, 0x3C, 0x10, 0x40, 0x12, 0x80, 0x11, 0x35,
    0x11, 0x66, 0x4C, 0x1D, 0x4F, 0x01, 0x7C, 0x3D, 0x79, 0x36, 0x20, 0x3A, 0x12, 0x40, 0x18, 0x32,
    0x11, 0x69, 0x48, 0x66, 0x18, 0x4F, 0x02, 0x7B, 0x01, 0x3A, 0x7A, 0x36, 0x20, 0x38, 0x14, 0x40,
    0x14, 0x32, 0x11, 0x32, 0x11, 0x69, 0x45, 0x6C, 0x15, 0x4F, 0x03, 0x7A, 0x04, 0x36, 0x61, 0x78,
    0x36, 0x21, 0x36, 0x16, 0x40, 0x13, 0x34, 0x10, 0x32, 0x11, 0x6F, 0x6E, 0x14, 0x4F, 0x02, 0x7A,
    0x03, 0x6A, 0x01, 0x76, 0x36, 0x20, 0x34, 0x18, 0x80, 0x40, 0x38, 0x11, 0x31, 0x11, 0x30, 0x6F,
    0x6D, 0x15, 0x4F, 0x01, 0x77, 0x05, 0x6B, 0x03, 0x75, 0x35, 0x21, 0x32, 0x1B, 0x40, 0x39, 0x10,
    0x30, 0x12, 0x30, 0x68, 0x03, 0x6F, 0x16, 0x4F, 0x01, 0x73, 0x08, 0x6B, 0x05, 0x74, 0x32, 0x26,
    0x1C, 0x40, 0x39, 0x14, 0x31, 0x63, 0x08, 0x6F, 0x61, 0x13, 0x4F, 0x42, 0x71, 0x09, 0x68, 0x0E,
    0x22, 0x35, 0x1C, 0x41, 0x39, 0x13, 0x31, 0x62, 0x0D, 0x6F, 0x11, 0x4F, 0x31, 0x41, 0x0B, 0x61,
    0x06, 0x62, 0x0A, 0x38, 0x1D, 0x40, 0x3A, 0x11, 0x34, 0x0F, 0x6E, 0x11, 0x4F, 0x32, 0x41, 0x0D,
    0x6E, 0x04, 0x37, 0x1E, 0x41, 0x3F, 0x32, 0x0A, 0x6F, 0x61, 0x11, 0x4F, 0x33, 0x42, 0x0F, 0x6E,
    0x37, 0x1F, 0x42, 0x3F, 0x33, 0x04, 0x32, 0x6F, 0x61, 0x11, 0x4F, 0x35, 0x49, 0xB0, 0x40, 0x06,
    0x6C, 0x37, 0x1F, 0x42, 0x3F, 0x34, 0x02, 0x38, 0x65, 0x35, 0x12, 0x4F, 0x36, 0x09, 0x43, 0x04,
    0x6A, 0x37, 0x11, 0x7A, 0x13, 0x43, 0x3F, 0x35, 0x20, 0x3F, 0x33, 0x4F, 0x36, 0x0C, 0x43, 0x01,
    0x6A, 0x36, 0x11, 0x7B, 0x13, 0x44, 0x3F, 0x34, 0x21, 0x3F, 0x30, 0x4F, 0x37, 0x0E, 0x42, 0x69,
    0x36, 0x11, 0x7C, 0x13, 0x46, 0x04, 0x3E, 0x20, 0x3F, 0x4F, 0x38, 0x0A, 0x60, 0x03, 0x41, 0x63,
    0x14, 0x34, 0x12, 0x7D, 0x13, 0xB6, 0x06, 0x3C, 0x20, 0x3D, 0x4F, 0x37, 0x0F, 0x01, 0xB0, 0x15,
    0x60, 0x10, 0x34, 0x11, 0x76, 0x12, 0x75, 0x12, 0x48, 0x08, 0x38, 0x21, 0x3B, 0x4F, 0x4F, 0x37,
    0x0F, 0x47, 0x12, 0x33, 0x11, 0x77, 0x10, 0x70, 0x10, 0x75, 0x12, 0x48, 0x0A, 0x34, 0x22, 0x3A,
    0x4F, 0x4F, 0x47, 0x09, 0x4F, 0x40, 0x32, 0x12, 0x77, 0x12, 0x75, 0x11, 0x49, 0x0B, 0x33, 0x20,
    0x32, 0x02, 0x36, 0x4F, 0x4F, 0x36, 0x40, 0x80, 0x4F, 0x49, 0x15, 0x7F, 0x70, 0x10, 0x4A, 0x0B,
    0x33, 0x20, 0x31, 0x03, 0x36, 0x4F, 0x4F, 0x37, 0x14, 0x43, 0x32, 0x4F, 0x14, 0x7F, 0x70, 0x4B,
    0x0D, 0x31, 0x07, 0x34, 0x4F, 0x4F, 0x37, 0x17, 0x33, 0x13, 0x4B, 0x14, 0x7F, 0x4B, 0x0F, 0x07,
    0x12, 0x32, 0x4F, 0x4F, 0x37, 0x17, 0x32, 0x15, 0x4A, 0x14, 0x7E, 0x4B, 0x0F, 0x07, 0x16, 0x4F,
    0x4F, 0x38, 0x15, 0x34, 0x14, 0x4A, 0x14, 0x7D, 0x4A, 0x0F, 0x09, 0x17, 0x4F, 0x4F, 0x39, 0x20,
    0x3A, 0x13, 0x49, 0x14, 0x7D, 0x42, 0x32, 0x0F, 0x0D, 0x18, 0x4F, 0x4F, 0x39, 0x20, 0x39, 0x20,
    0x13, 0x48, 0x15, 0x7C, 0x42, 0x34, 0x0F, 0x0C, 0x18, 0x4F, 0x4F, 0x39, 0x20, 0x39, 0x20, 0x30,
    0x12, 0xB8, 0x15, 0x7B, 0x42, 0x36, 0x0F, 0x0A, 0x18, 0x4F, 0x12, 0x4F, 0x39, 0x20, 0x39, 0x20,
    0x33, 0x48, 0x15, 0x7A, 0x43, 0x37, 0x0F, 0x04, 0x80, 0x03, 0x17, 0xBF, 0xB0, 0x10, 0xA0, 0x10,
    0x4F, 0x39, 0x20, 0x39, 0x20, 0x33, 0x48, 0x15, 0x78, 0x45, 0x38, 0x0F, 0x08, 0x17, 0x4E, 0x41,
    0x12, 0x4F, 0x39, 0x20, 0x39, 0x20, 0x33, 0x49, 0x14, 0x77, 0x46, 0x38, 0x01, 0x63, 0x0F, 0x02,
    0x16, 0x4F, 0x4F, 0x38, 0x21, 0x39, 0x20, 0x33, 0x49, 0x14, 0x76, 0x47, 0x39, 0x65, 0x0B, 0x16,
    0x4F, 0x15, 0x4F, 0x38, 0x20, 0x3A, 0x20, 0x32, 0x4B, 0x13, 0x74, 0x49, 0x39, 0x66, 0x0A, 0x15,
    0x4E, 0x19, 0x4F, 0x38, 0x20, 0x3A, 0x20, 0x32, 0x4C, 0x12, 0x73, 0x4A, 0x3A, 0x67, 0x08, 0x13,
    0x4E, 0x12, 0x33, 0x15, 0x4F, 0x38, 0x20, 0x3A, 0x20, 0x31, 0x4E, 0x11, 0x71, 0x4C, 0x3A, 0x61,
    0x41, 0x64, 0x06, 0x13, 0x4E, 0x12, 0x35, 0x14, 0x4F, 0x38, 0x20, 0x3A, 0x20, 0x33, 0x4F, 0x4D,
    0x3A, 0x61, 0x43, 0x62, 0x06, 0x12, 0x4E, 0x11, 0x3B, 0x4F, 0x38, 0x20, 0x3A, 0x24, 0x30, 0x4F,
    0x4C, 0x3B, 0x61, 0x42, 0x62, 0x06, 0x12, 0x4D, 0x11, 0x3B, 0x4F, 0x38, 0x20, 0x3E, 0x20, 0x31,
    0x4F, 0x4B, 0x3C, 0x61, 0x40, 0x62, 0x07, 0x13, 0x4B, 0x11, 0x3B, 0x4F, 0x38, 0x20, 0x3E, 0x20,
    0x31, 0x4F, 0x4B, 0x3D, 0x60, 0x40, 0x61, 0x08, 0x32, 0x11, 0x4A, 0x10, 0x3B, 0x4F, 0x37, 0x11,
    0x36, 0x12, 0x32, 0x12, 0x31, 0x4F, 0x4B, 0x3E, 0x40, 0x05, 0x32, 0x90, 0x34, 0x11, 0x48, 0x11,
    0x3A, 0x4F, 0x36, 0x13, 0x34, 0x20, 0x12, 0x31, 0x14, 0x4A, 0x12, 0x4E, 0x31, 0x10, 0x3B, 0x40,
    0x03, 0x3B, 0x11, 0x46, 0x11, 0x3B, 0x4F, 0x36, 0x13, 0x21, 0x32, 0x20, 0x31, 0x10, 0x20, 0x30,
    0x11, 0x30, 0x11, 0x49, 0x14, 0x4D, 0x30, 0x10, 0xA0, 0x10, 0x3A, 0x41, 0x02, 0x3C, 0x11, 0x44,
    0x11, 0x3D, 0x4F, 0x31, 0x24, 0x13, 0x30, 0x20, 0x32, 0x22, 0x30, 0x20, 0x30, 0x14, 0x48, 0x11,
    0x32, 0x11, 0x4C, 0x31, 0x10, 0x3C, 0x42, 0x10, 0x47, 0x35, 0x12, 0x41, 0x11, 0x3E, 0x4F, 0x31,
    0x20, 0x34, 0x42, 0x30, 0x21, 0x11, 0x31, 0x20, 0x30, 0x20, 0x31, 0x13, 0x48, 0x10, 0xA0, 0x32,
    0x12, 0x4B, 0x31, 0x70, 0x40, 0x70, 0x3D, 0x12, 0x3D, 0x13, 0x3F, 0x4F, 0x31, 0x11, 0x39, 0x13,
    0x20, 0x30, 0x20, 0x34, 0x10, 0x48, 0x10, 0x34, 0x11, 0x4B, 0x33, 0x70, 0x3F, 0x11, 0x3F, 0x3F,
    0x33, 0x4F, 0x31, 0x12, 0x30, 0x23, 0x33, 0x13, 0x31, 0x20, 0x34, 0x10, 0xB8, 0x10, 0x34, 0x11,
    0x4B, 0x32, 0x71, 0x30, 0x74, 0x3A, 0x10, 0x3E, 0x3A, 0x25, 0x32, 0x4F, 0x31, 0x12, 0x21, 0x31,
    0x20, 0x39, 0x61, 0x32, 0x11, 0x48, 0x35, 0x11, 0x4B, 0x32, 0x70, 0x31, 0x70, 0x32, 0x70, 0x38,
    0x12, 0x37, 0x90, 0x3F, 0x30, 0x20, 0x33, 0x23, 0x12, 0x4F, 0x37, 0x11, 0x39, 0x61, 0x31, 0x11,
    0x4A, 0x31, 0x90, 0x12, 0x4C, 0x32, 0x70, 0x31, 0x70, 0x32, 0x70, 0x37, 0x11, 0x38, 0x73, 0x3D,
    0x63, 0x36, 0x13, 0x4F, 0x34, 0x22, 0x12, 0x21, 0x32, 0x22, 0x30, 0x11, 0x31, 0x10, 0x4B, 0x40,
    0x34, 0x4C, 0x32, 0x70, 0x31, 0x70, 0x32, 0x71, 0x36, 0x10, 0x37, 0x77, 0x35, 0x62, 0x31, 0x65,
    0x35, 0x11, 0x61, 0x10, 0x4F, 0x34, 0x20, 0x31, 0x12, 0x30, 0x21, 0x12, 0x30, 0x20, 0x12, 0x20,
    0x30, 0x11, 0x4B, 0x40, 0x32, 0x4D, 0x32, 0x70, 0x30, 0x71, 0x31, 0x71, 0x37, 0x10, 0x35, 0x7A,
    0x31, 0x6E, 0x33, 0x11, 0x60, 0x80, 0x61, 0x4F, 0x33, 0x11, 0x37, 0x12, 0x31, 0x11, 0x30, 0x20,
    0x31, 0x11, 0x4F, 0x4D, 0x31, 0x70, 0x30, 0x70, 0x32, 0x70, 0x38, 0x10, 0x35, 0x7A, 0x69, 0x42,
    0x63, 0x33, 0x10, 0x64, 0x4F, 0x32, 0x12, 0x36, 0x12, 0x35, 0x20, 0x32, 0x11, 0x4F, 0x4C, 0x31,
    0x70, 0x30, 0x70, 0x32, 0x70, 0x38, 0x11, 0x33, 0x7A, 0x69, 0x44, 0x63, 0x33, 0x64, 0x4F, 0x31,
    0x13, 0x20, 0x32, 0x22, 0x12, 0x35, 0x11, 0x32, 0x10, 0x4F, 0x4C, 0x31, 0x72, 0x31, 0x71, 0x39,
    0x10, 0x33, 0x7A, 0x68, 0x46, 0x62, 0x33, 0x64, 0x4F, 0x31, 0x12, 0x30, 0x20, 0x32, 0x20, 0x3A,
    0x11, 0x20, 0x31, 0x11, 0x4F, 0x4C, 0x31, 0x71, 0x31, 0x70, 0x3A, 0x10, 0x33, 0x7A, 0x68, 0x47,
    0x62, 0x33, 0x63, 0x4F, 0x31, 0x12, 0x30, 0x20, 0x30, 0x13, 0x35, 0x11, 0x33, 0x20, 0x32, 0x11,
    0x4F, 0x4B, 0x32, 0x70, 0x31, 0x70, 0x3A, 0x11, 0x33, 0x77, 0x66, 0x4B, 0x62, 0x34, 0x62, 0x4F,
    0x32, 0x11, 0x30, 0x21, 0x11, 0x30, 0x11, 0x23, 0x12, 0x31, 0x22, 0x33, 0x10, 0x4F, 0x4C, 0x34,
    0x70, 0x3B, 0x12, 0x31, 0x75, 0x67, 0x41, 0x62, 0x47, 0x62, 0x37, 0x4F, 0x37, 0x14, 0x34, 0x60,
    0x31, 0x11, 0x35, 0x10, 0x4F, 0x4C, 0x34, 0x70, 0x3B, 0x21, 0x11, 0x75, 0x67, 0x41, 0x66, 0x43,
    0x63, 0x37, 0x4F, 0x38, 0x12, 0x37, 0x13, 0x31, 0x13, 0x4F, 0x4B, 0x35, 0x71, 0x39, 0x23, 0x12,
    0x72, 0x65, 0x43, 0x68, 0x41, 0x65, 0x36, 0x4F, 0x38, 0x20, 0x33, 0x11, 0x33, 0x20, 0x33, 0x13,
    0x4F, 0x4A, 0x34, 0x70, 0x31, 0x72, 0x37, 0x26, 0x11, 0x67, 0x40, 0x6F, 0x64, 0x35, 0x4F, 0x31,
    0x11, 0x34, 0x20, 0x33, 0x12, 0x23, 0x33, 0x13, 0x4F, 0x4A, 0x33, 0x72, 0x31, 0x71, 0x37, 0x27,
    0x11, 0x65, 0x41, 0x66, 0x13, 0x6A, 0x33, 0x4F, 0x31, 0x12, 0x24, 0x32, 0x13, 0x37, 0x12, 0x4F,
    0x4B, 0x35, 0x74, 0x38, 0x27, 0x12, 0x63, 0x40, 0x65, 0x16, 0x69, 0x13, 0x4F, 0x32, 0x11, 0x39,
    0x20, 0x30, 0x22, 0x35, 0x20, 0x30, 0x4F, 0x4B, 0x36, 0x72, 0x3B, 0x27, 0x14, 0xB0, 0x64, 0x16,
    0x68, 0x14, 0x4F, 0x3E, 0x22, 0x30, 0x11, 0x32, 0x22, 0x30, 0x4F, 0x4B, 0x37, 0x71, 0x3C, 0x26,
    0x34, 0x41, 0x63, 0x1A, 0x63, 0x15, 0x4F, 0x3F, 0x31, 0x12, 0x32, 0x20, 0x31, 0x4F, 0x4C, 0x36,
    0x71, 0x3E, 0x20, 0x3A, 0x40, 0x62, 0x1C, 0x61, 0x15, 0x4F, 0x38, 0x14, 0x35, 0x20, 0x32, 0x20,
    0x31, 0x4F, 0x4C, 0x35, 0x71, 0x3F, 0x20, 0x3A, 0x41, 0x1F, 0x16, 0x4F, 0x38, 0x10, 0x30, 0x10,
    0x90, 0x12, 0x31, 0x22, 0x30, 0x12, 0x4F, 0x4F, 0x33, 0x71, 0x38, 0x73, 0x33, 0x70, 0x3B, 0x40,
    0x1F, 0x15, 0x4F, 0x38, 0x14, 0x30, 0x10, 0x31, 0x20, 0x32, 0x11, 0x4F, 0x4F, 0x33, 0x72, 0x31,
    0x7F, 0x35, 0x15, 0x40, 0x16, 0x30, 0x1C, 0x4F, 0x3E, 0x17, 0x4F, 0x4F, 0x36, 0x7F, 0x75, 0x32,
    0x12, 0x80, 0x11, 0x40, 0x1F, 0x13, 0x4F, 0x33, 0x73, 0x36, 0x17, 0x4F, 0x4F, 0x3C, 0x77, 0x43,
    0x72, 0x32, 0x16, 0xB0, 0x14, 0x30, 0x12, 0x30, 0x18, 0x4F, 0x33, 0x73, 0x33, 0x1B, 0x4F, 0x4F,
    0x3C, 0x74, 0x47, 0x70, 0x32, 0x16, 0x40, 0x1F, 0x11, 0x4F, 0x33, 0x73, 0x1F, 0x4F, 0x4F, 0x43,
    0x39, 0x72, 0x4A, 0x31, 0x15, 0x42, 0x15, 0xA0, 0x17, 0x4F, 0x33, 0x73, 0x19, 0x42, 0x14, 0x4F,
    0x4F, 0x42, 0x39, 0x4D, 0x14, 0x44, 0x1B, 0x4F, 0x37, 0x65, 0x11, 0x44, 0x10, 0x62, 0x10, 0x4F,
    0x4F, 0x45, 0x35, 0x4F, 0x41, 0x11, 0x47, 0x17, 0x4F, 0x4F, 0x4F, 0x4F, 0x4F, 0x4F, 0x4F, 0x4F,
    0x4F, 0x4F, 0x4F, 0x4F, 0x4F, 0x4F,
};

/* Where each of the 120 rows starts, as ROM bytes (low, high). */
static const uint8_t z2_pointer_data[2 * MAP_DIM] = {
    0x5D, 0x9D, 0x67, 0x9D, 0x73, 0x9D, 0x83, 0x9D, 0x90, 0x9D, 0x9D, 0x9D, 0xAD, 0x9D, 0xBD, 0x9D,
    0xCD, 0x9D, 0xDF, 0x9D, 0xF7, 0x9D, 0x11, 0x9E, 0x2B, 0x9E, 0x44, 0x9E, 0x57, 0x9E, 0x6A, 0x9E,
    0x81, 0x9E, 0x97, 0x9E, 0xAB, 0x9E, 0xC4, 0x9E, 0xDF, 0x9E, 0xF7, 0x9E, 0x13, 0x9F, 0x2F, 0x9F,
    0x44, 0x9F, 0x59, 0x9F, 0x6C, 0x9F, 0x80, 0x9F, 0x96, 0x9F, 0xAE, 0x9F, 0xC5, 0x9F, 0xDF, 0x9F,
    0xF8, 0x9F, 0x12, 0xA0, 0x27, 0xA0, 0x40, 0xA0, 0x51, 0xA0, 0x60, 0xA0, 0x6F, 0xA0, 0x7F, 0xA0,
    0x90, 0xA0, 0x9F, 0xA0, 0xB2, 0xA0, 0xC3, 0xA0, 0xD7, 0xA0, 0xEB, 0xA0, 0x00, 0xA1, 0x15, 0xA1,
    0x28, 0xA1, 0x39, 0xA1, 0x4A, 0xA1, 0x59, 0xA1, 0x68, 0xA1, 0x79, 0xA1, 0x89, 0xA1, 0x99, 0xA1,
    0xA7, 0xA1, 0xB8, 0xA1, 0xCC, 0xA1, 0xDF, 0xA1, 0xF2, 0xA1, 0x04, 0xA2, 0x13, 0xA2, 0x21, 0xA2,
    0x2E, 0xA2, 0x3B, 0xA2, 0x49, 0xA2, 0x58, 0xA2, 0x69, 0xA2, 0x7E, 0xA2, 0x8F, 0xA2, 0xA0, 0xA2,
    0xB0, 0xA2, 0xC0, 0xA2, 0xD2, 0xA2, 0xE6, 0xA2, 0xF7, 0xA2, 0x08, 0xA3, 0x19, 0xA3, 0x2B, 0xA3,
    0x3F, 0xA3, 0x54, 0xA3, 0x70, 0xA3, 0x8C, 0xA3, 0xA9, 0xA3, 0xBF, 0xA3, 0xD9, 0xA3, 0xF7, 0xA3,
    0x11, 0xA4, 0x32, 0xA4, 0x55, 0xA4, 0x72, 0xA4, 0x8C, 0xA4, 0xA6, 0xA4, 0xC1, 0xA4, 0xDD, 0xA4,
    0xF9, 0xA4, 0x10, 0xA5, 0x25, 0xA5, 0x3C, 0xA5, 0x55, 0xA5, 0x6A, 0xA5, 0x80, 0xA5, 0x94, 0xA5,
    0xA7, 0xA5, 0xB9, 0xA5, 0xD0, 0xA5, 0xE5, 0xA5, 0xF4, 0xA5, 0x07, 0xA6, 0x17, 0xA6, 0x27, 0xA6,
    0x35, 0xA6, 0x46, 0xA6, 0x46, 0xA6, 0x46, 0xA6, 0x46, 0xA6, 0x46, 0xA6, 0x46, 0xA6, 0x46, 0xA6,
};

/* The enemy zone layout for this map (8 x 4 bytes, from $F522). */
static const uint8_t z2_zones[32] = {
    51, 34, 53, 205, 48, 18, 51, 205,
    68, 68, 220, 189, 70, 102, 154, 170,
    85, 72, 103, 118, 103, 141, 199, 135,
    103, 221, 199, 152, 119, 205, 220, 153,
};

/* ---------------------------------------------------------------------------------------------------------
 * Zelda II map (Z2 Map flag)
 *
 * The map is drawn from the tables above, then goes through the same steps as any other map: its tiles are
 * decoded, towns and caves are placed on them, the result is checked and encoded again. So the rest of the
 * build (Zoom, border tiles, Safe Start, vertical bridges) sees the real map.
 *
 * Plain: every place in its Zelda II spot. Lite: towns trade places with towns and caves with caves; Tantegel
 * stays in the starting palace. Deep: all of them mix, Tantegel included. Lock Tantegel keeps Tantegel in the
 * starting palace in Deep. Entrances without a place become mountain, so nothing says "Thou cannot enter here".
 * Bridges are all stored as across bridges, as in the base game: the 2 single bridges at (16,52) and (105,29)
 * would otherwise need the 80-byte vertical bridge code, which a full ROM may have no room for.
 * --------------------------------------------------------------------------------------------------------- */

typedef struct {
    uint8_t map;
    uint8_t x;
    uint8_t y;
} z2_spot;

/* The bridge between the 2 continents: row 56, x 62 to 68. It is cut when the swamp cave is the way across. */
#define Z2_BRIDGE_Y   56
#define Z2_BRIDGE_X0  62
#define Z2_BRIDGE_X1  68
/* Town map bytes of the stairs in Tantegel and Garinham, and what they become. */
#define Z2_TANTEGEL_STAIRS_ADDR  0x0401
#define Z2_GARINHAM_STAIRS_ADDR  0x0aa3

/* Plain tiles where the token can be, in the order they were chosen. The first is used with no shuffle. */
static const uint8_t z2_search_spots[25][2] = {
    { 62,  34}, { 37,  12}, {  7,  34}, { 21,  46}, { 33,  57}, { 37,  38}, { 17,  37}, {  8,  41}, { 18,  96},
    {  9, 105}, { 38,  72}, { 50,  66}, { 48,  62}, { 50,  58}, { 66,  67}, { 69,  76}, { 96,  64}, {110,  52},
    {115,  24}, {109,  72}, { 82,  89}, {110,  99}, {113, 107}, { 83, 107}, { 60,  99}
};

/* Where Tantegel, a town or a cave can be. The first palace is where Tantegel is in Zelda II. */
static const z2_spot z2_palaces[7] = {
    {OVERWORLD,  23,  22}, /* start */
    {OVERWORLD,  62,   2}, /* parapa */
    {OVERWORLD,  11,  34}, /* midoro */
    {OVERWORLD, 118,  22}, /* maze */
    {OVERWORLD, 116,  68}, /* sea */
    {OVERWORLD, 101, 110}, /* 3-eye rock */
    {OVERWORLD,  37,  83}  /* island */
};
static const z2_spot z2_towns[8] = {
    {OVERWORLD,  46,  24}, /* rauru */
    {OVERWORLD,   2,   6}, /* ruto */
    {OVERWORLD,   8,  60}, /* saria */
    {OVERWORLD,  60,  45}, /* mido */
    {OVERWORLD,  85,  68}, /* nabooru */
    {OVERWORLD,  65,  41}, /* darunia */
    {OVERWORLD, 117,  89}, /* new kasuto */
    {OVERWORLD,  90, 107}  /* old kasuto */
};
static const z2_spot z2_caves[9] = {
    {OVERWORLD,  16,  30}, {OVERWORLD,  29,   2}, {OVERWORLD,   9,  39}, {OVERWORLD,  38,  32},
    {OVERWORLD,  54,  32}, {OVERWORLD,  12, 105}, {OVERWORLD,  81,  79}, {OVERWORLD,  87,  86},
    {OVERWORLD,  39,  87}  /* the island cave */
};

/* Which town and cave warps take the slots above, in order (Lite). */
static const dw_warp_index z2_town_warps[6] = {
    WARP_GARINHAM, WARP_KOL, WARP_BRECCONARY, WARP_RIMULDAR, WARP_HAUKSNESS, WARP_CANTLIN
};
static const dw_warp_index z2_cave_warps[8] = {
    WARP_SWAMP_NORTH, WARP_SWAMP_SOUTH, WARP_STAFF_SHRINE, WARP_MOUNTAIN_CAVE, WARP_JERK_CAVE,
    WARP_ERDRICKS_CAVE, WARP_TANTEGEL_BASEMENT, WARP_GARINS_GRAVE
};

/* The towns that have a border tile on the town map. */
static const struct { dw_warp_index warp; int town; } z2_border_towns[] = {
    {WARP_TANTEGEL, TANTEGEL}, {WARP_GARINHAM, GARINHAM}, {WARP_RIMULDAR, RIMULDAR},
    {WARP_BRECCONARY, BRECCONARY}, {WARP_KOL, KOL}, {WARP_HAUKSNESS, HAUKSNESS}, {WARP_CANTLIN, CANTLIN}
};

static BOOL z2_is_entrance(dw_tile tile)
{
    return tile == TILE_TOWN || tile == TILE_CAVE || tile == TILE_CASTLE || tile == TILE_BRIDGE;
}

static BOOL z2_plain_land(dw_tile tile)
{
    return tile == TILE_GRASS || tile == TILE_DESERT || tile == TILE_HILL || tile == TILE_TREES;
}

/* How picky z2_beside is: plain land, then any ground but an entrance, then any walkable tile. */
enum { Z2_PLAIN, Z2_GROUND, Z2_ANY };

/* The first walkable tile beside (x, y): west, east, north, south, at the given pickiness. It is never
 * (skip_x, skip_y). */
static BOOL z2_beside(dw_map *map, int x, int y, int pick, int skip_x, int skip_y, uint8_t *ox, uint8_t *oy)
{
    static const int dir[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    int i, nx, ny;

    for (i = 0; i < 4; i++) {
        nx = x + dir[i][0];
        ny = y + dir[i][1];
        if (map_ob(nx) || map_ob(ny) || !tile_is_walkable(map->tiles[nx][ny]))
            continue;
        if (nx == skip_x && ny == skip_y)
            continue;
        if (pick == Z2_PLAIN && !z2_plain_land(map->tiles[nx][ny]))
            continue;
        if (pick == Z2_GROUND && z2_is_entrance(map->tiles[nx][ny]))
            continue;
        *ox = (uint8_t)nx;
        *oy = (uint8_t)ny;
        return TRUE;
    }
    return FALSE;
}

/* Where Return and Wings land: plain land beside the place if there is any, so not swamp. */
static BOOL z2_land_beside(dw_map *map, int x, int y, uint8_t *ox, uint8_t *oy)
{
    int pick;

    for (pick = Z2_PLAIN; pick <= Z2_ANY; pick++) {
        if (z2_beside(map, x, y, pick, -1, -1, ox, oy))
            return TRUE;
    }
    return FALSE;
}

static void z2_set_warp(dw_rom *rom, dw_warp_index idx, const z2_spot *s)
{
    dw_warp *warp = &rom->map.warps_from[idx];

    warp->map = s->map;
    warp->x = s->x;
    warp->y = s->y;
}

/* Draws a tile at a spot. A spot inside Tantegel or Garinham is the stairs on that town's map. */
static void z2_put(dw_rom *rom, const z2_spot *s, dw_tile tile)
{
    if (s->map == OVERWORLD)
        rom->map.tiles[s->x][s->y] = (uint8_t)tile;
}

/* Town maps keep 2 tiles in a byte, left tile in the high nibble. Only the right nibble is the stairs spot:
 * 7 is stairs down, and z2_default_places sets 2 (water) or 4 (block) there, the tile beside it. */
static void z2_put_cave(dw_rom *rom, const z2_spot *s)
{
    if (s->map == OVERWORLD)
        rom->map.tiles[s->x][s->y] = TILE_CAVE;
    else if (s->map == TANTEGEL)
        rom->content[Z2_TANTEGEL_STAIRS_ADDR] = 0x27;   /* water, then stairs down */
    else if (s->map == GARINHAM)
        rom->content[Z2_GARINHAM_STAIRS_ADDR] = 0x47;   /* block, then stairs down */
}

static void z2_tantegel_at(dw_rom *rom, const z2_spot *s)
{
    z2_set_warp(rom, WARP_TANTEGEL, s);
    rom->map.love_calc->x = s->x;
    rom->map.love_calc->y = s->y;
}

/* Where each place is in Zelda II, and the other spots that are tied to the map. */
static void z2_default_places(dw_rom *rom)
{
    static const struct { dw_warp_index warp; uint8_t x, y; } places[] = {
        {WARP_GARINHAM, 2, 6}, {WARP_STAFF_SHRINE, 54, 32}, {WARP_KOL, 117, 89}, {WARP_BRECCONARY, 46, 24},
        {WARP_TANTEGEL, 23, 22}, {WARP_SWAMP_NORTH, 16, 30}, {WARP_CHARLOCK, 58, 81}, {WARP_SWAMP_SOUTH, 87, 86},
        {WARP_MOUNTAIN_CAVE, 9, 39}, {WARP_RIMULDAR, 8, 60}, {WARP_HAUKSNESS, 90, 107}, {WARP_CANTLIN, 65, 41},
        {WARP_JERK_CAVE, 81, 79}, {WARP_ERDRICKS_CAVE, 29, 2}, {WARP_TANTEGEL_BASEMENT, 38, 32},
        {WARP_GARINS_GRAVE, 12, 105}
    };
    size_t i;

    for (i = 0; i < sizeof(places) / sizeof(places[0]); i++) {
        dw_warp *warp = &rom->map.warps_from[places[i].warp];

        warp->map = OVERWORLD;
        warp->x = places[i].x;
        warp->y = places[i].y;
    }
    rom->map.love_calc->x = 23;
    rom->map.love_calc->y = 22;
    rom->map.rainbow_bridge->x = 59;
    rom->map.rainbow_bridge->y = 83;
    rom->map.rainbow_drop->x = 60;
    rom->map.rainbow_drop->y = 83;
    /* The stairs inside Tantegel and Garinham are gone until a cave is placed there. */
    rom->content[Z2_TANTEGEL_STAIRS_ADDR] = 0x22;
    rom->content[Z2_GARINHAM_STAIRS_ADDR] = 0x44;
}

/* No shuffle: the 9 entrances that have no place become mountain. */
static void z2_plain(dw_rom *rom)
{
    int i;

    for (i = 1; i < 7; i++)
        z2_put(rom, &z2_palaces[i], TILE_MOUNTAIN);
    z2_put(rom, &z2_towns[3], TILE_MOUNTAIN);
    z2_put(rom, &z2_towns[4], TILE_MOUNTAIN);
    z2_put(rom, &z2_caves[8], TILE_MOUNTAIN);
    rom->search_table->x[0] = z2_search_spots[0][0];
    rom->search_table->y[0] = z2_search_spots[0][1];
}

static void z2_shuffle_token(dw_rom *rom)
{
    uint8_t spots[25][2];

    memcpy(spots, z2_search_spots, sizeof(spots));
    mt_shuffle(spots, 25, 2);
    rom->search_table->x[0] = spots[0][0];
    rom->search_table->y[0] = spots[0][1];
}

/* Lite: towns among the town slots, caves among the cave slots. Tantegel stays in the starting palace.
 * Unused places are mountain. */
static void z2_lite(dw_rom *rom)
{
    z2_spot towns[8], caves[9];
    int i;

    memcpy(towns, z2_towns, sizeof(towns));
    memcpy(caves, z2_caves, sizeof(caves));
    z2_shuffle_token(rom);

    z2_tantegel_at(rom, &z2_palaces[0]);
    z2_put(rom, &z2_palaces[0], TILE_CASTLE);
    for (i = 1; i < 7; i++)
        z2_put(rom, &z2_palaces[i], TILE_MOUNTAIN);

    mt_shuffle(towns, 8, sizeof(z2_spot));
    for (i = 0; i < 6; i++)
        z2_set_warp(rom, z2_town_warps[i], &towns[i]);
    for (i = 6; i < 8; i++)
        z2_put(rom, &towns[i], TILE_MOUNTAIN);

    mt_shuffle(caves, 9, sizeof(z2_spot));
    for (i = 0; i < 8; i++)
        z2_set_warp(rom, z2_cave_warps[i], &caves[i]);
    z2_put(rom, &caves[8], TILE_MOUNTAIN);
}

/* Deep: every place in the pool of slots. */
static void z2_deep(dw_rom *rom)
{
    static const z2_spot slots[26] = {
        {OVERWORLD,  62,   2}, {OVERWORLD,  11,  34}, {OVERWORLD, 118,  22}, {OVERWORLD, 116,  68},
        {OVERWORLD, 101, 110}, {OVERWORLD,  37,  83}, {OVERWORLD,  46,  24}, {OVERWORLD,   2,   6},
        {OVERWORLD,   8,  60}, {OVERWORLD,  60,  45}, {OVERWORLD,  85,  68}, {OVERWORLD,  65,  41},
        {OVERWORLD, 117,  89}, {OVERWORLD,  90, 107}, {OVERWORLD,  16,  30}, {OVERWORLD,  29,   2},
        {OVERWORLD,   9,  39}, {OVERWORLD,  38,  32}, {OVERWORLD,  54,  32}, {OVERWORLD,  12, 105},
        {OVERWORLD,  81,  79}, {OVERWORLD,  87,  86}, {OVERWORLD,  39,  87},
        {GARINHAM,   19,   0}, {TANTEGEL,   29,  29},
        {OVERWORLD,  23,  22}  /* the starting palace, last so Lock Tantegel can leave it out of the shuffle */
    };
    static const dw_warp_index warps[15] = {
        WARP_TANTEGEL, WARP_GARINHAM, WARP_KOL, WARP_BRECCONARY, WARP_RIMULDAR, WARP_HAUKSNESS, WARP_CANTLIN,
        WARP_SWAMP_NORTH, WARP_SWAMP_SOUTH, WARP_STAFF_SHRINE, WARP_MOUNTAIN_CAVE, WARP_JERK_CAVE,
        WARP_ERDRICKS_CAVE, WARP_TANTEGEL_BASEMENT, WARP_GARINS_GRAVE
    };
    z2_spot everything[26];
    const int lock = LOCK_TANTEGEL(rom) ? 1 : 0;
    const int tant = lock ? 25 : 0;
    int i;

    memcpy(everything, slots, sizeof(everything));
    /* Tantegel and the 6 towns must be on the overworld: the stairs inside Tantegel and Garinham are not
     * places a town can stand. Shuffle until they are. With Lock Tantegel, slot 0 is a leftover and Tantegel
     * is the last slot, which is not shuffled. */
    do {
        mt_shuffle(everything, 26 - lock, sizeof(z2_spot));
        for (i = lock; i < 7; i++) {
            if (everything[i].map != OVERWORLD)
                break;
        }
    } while (i < 7);

    if (lock)
        z2_put(rom, &everything[0], TILE_MOUNTAIN);
    z2_tantegel_at(rom, &everything[tant]);
    z2_put(rom, &everything[tant], TILE_CASTLE);
    for (i = 1; i < 7; i++) {
        z2_set_warp(rom, warps[i], &everything[i]);
        z2_put(rom, &everything[i], TILE_TOWN);
    }
    for (i = 7; i < 15; i++) {
        z2_set_warp(rom, warps[i], &everything[i]);
        z2_put_cave(rom, &everything[i]);
    }
    /* The slots left over have no place. */
    for (i = 15; i < 26 - lock; i++)
        z2_put(rom, &everything[i], TILE_MOUNTAIN);
    z2_shuffle_token(rom);
}

/* Can the hero get a key on Tantegel's side? Rimuldar sells them, or a key chest is within reach. */
static BOOL z2_key_reachable(dw_map *map, uint8_t lm)
{
    static const struct { dw_warp_index warp; uint8_t access; } sources[] = {
        {WARP_TANTEGEL_BASEMENT, KEY_IN_BASEMENT}, {WARP_ERDRICKS_CAVE, KEY_IN_TABLET},
        {WARP_MOUNTAIN_CAVE, KEY_IN_MOUNTAIN}, {WARP_GARINS_GRAVE, KEY_IN_GRAVE}
    };
    const dw_warp *w = &map->warps_from[WARP_RIMULDAR];
    size_t i;

    if (w->map == OVERWORLD && map->walkable[w->x][w->y] == lm)
        return TRUE;
    for (i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        int x, y;

        /* A key cave inside Tantegel or Garinham counts where that town is. */
        if (overworld_spot(map, &map->warps_from[sources[i].warp], &x, &y) && map->walkable[x][y] == lm &&
                (map->key_access & sources[i].access))
            return TRUE;
    }
    return FALSE;
}

/* With the bridge cut: can every place still be reached and the game finished? Needs find_walkable_area to
 * have run on the cut map. */
static BOOL z2_crossing_works(dw_rom *rom)
{
    dw_map *map = &rom->map;
    const dw_warp *t = &map->warps_from[WARP_TANTEGEL];
    const dw_warp *sn = &map->warps_from[WARP_SWAMP_NORTH];
    const dw_warp *ss = &map->warps_from[WARP_SWAMP_SOUTH];
    const dw_warp *b = &map->warps_from[WARP_BRECCONARY];
    const uint8_t t_lm = map->walkable[t->x][t->y];
    BOOL keyed = sn->map != OVERWORLD || ss->map != OVERWORLD;
    BOOL in_tantegel = sn->map == TANTEGEL || ss->map == TANTEGEL;

    /* Every entrance, the Rainbow Drop spot, the Return point and the token, walking from Tantegel and
     * crossing by the swamp cave. */
    if (!places_reachable(rom))
        return FALSE;
    /* A swamp entrance inside Tantegel or Garinham is behind a locked door: a key must be on this side. */
    if (keyed && !NO_KEYS(rom) && !z2_key_reachable(map, t_lm))
        return FALSE;
    /* A cursed hero who dies in the swamp cave has to be able to walk to Brecconary. */
    if (in_tantegel && map->walkable[b->x][b->y] != t_lm)
        return FALSE;
    return TRUE;
}

/* Cuts the bridge between the continents when the swamp cave joins them and the game can still be finished.
 * Otherwise the bridge stays and the whole map is one land. */
static void z2_cut_bridge(dw_rom *rom)
{
    dw_map *map = &rom->map;
    int lm_sizes[256], largest, next, x, sx[2], sy[2], i;   /* largest and next are not used here */
    static const dw_warp_index swamp[2] = {WARP_SWAMP_NORTH, WARP_SWAMP_SOUTH};

    for (x = Z2_BRIDGE_X0; x <= Z2_BRIDGE_X1; x++)
        map->tiles[x][Z2_BRIDGE_Y] = TILE_WATER;
    find_walkable_area(map, lm_sizes, &largest, &next);
    for (i = 0; i < 2; i++) {
        BOOL found = overworld_spot(map, &map->warps_from[swamp[i]], &sx[i], &sy[i]);

        if (!found)
            dwr_give_up("The Zelda II map has a swamp cave that is not on the overworld.");
    }
    if (map->walkable[sx[0]][sy[0]] != map->walkable[sx[1]][sy[1]] && z2_crossing_works(rom))
        return;
    for (x = Z2_BRIDGE_X0; x <= Z2_BRIDGE_X1; x++)
        map->tiles[x][Z2_BRIDGE_Y] = TILE_BRIDGE;
    find_walkable_area(map, lm_sizes, &largest, &next);
}

/* A shuffle must leave a town near Tantegel, on its side of the water: within this many steps (x plus y). */
#define Z2_TOWN_NEAR 30

static BOOL z2_town_near(dw_map *map)
{
    static const dw_warp_index towns[6] = {
        WARP_BRECCONARY, WARP_GARINHAM, WARP_KOL, WARP_RIMULDAR, WARP_HAUKSNESS, WARP_CANTLIN
    };
    const dw_warp *t = &map->warps_from[WARP_TANTEGEL];
    size_t i;

    for (i = 0; i < 6; i++) {
        const dw_warp *w = &map->warps_from[towns[i]];

        if (w->map == OVERWORLD && map->walkable[w->x][w->y] == map->walkable[t->x][t->y] &&
                abs(w->x - t->x) + abs(w->y - t->y) <= Z2_TOWN_NEAR)
            return TRUE;
    }
    return FALSE;
}

/* The border tile of a town is the kind of ground it stands on, taken from the ground beside it. */
static dw_border_tile z2_border(dw_map *map, int x, int y)
{
    static const int dir[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    int i, nx, ny;

    for (i = 0; i < 4; i++) {
        nx = x + dir[i][0];
        ny = y + dir[i][1];
        if (!map_ob(nx) && !map_ob(ny) && tile_is_walkable(map->tiles[nx][ny]) && !z2_is_entrance(map->tiles[nx][ny]))
            return border_for(map->tiles[nx][ny]);
    }
    return BORDER_GRASS;
}

/**
 * Builds the Zelda II map in place of a generated one. Used by apply_stuff_to_rom when the Z2 Map flag is on,
 * after the quest items are settled and before the spike table is written.
 *
 * @param rom The rom struct
 */
/* Can every place but Cantlin still be reached with (x, y) blocked? And with Cantlin counted too? */
static void z2_reach_without(dw_rom *rom, int x, int y, BOOL *others, BOOL *all)
{
    dw_map *map = &rom->map;
    dw_warp *cantlin = &map->warps_from[WARP_CANTLIN];
    const uint8_t tile = map->tiles[x][y];
    const uint8_t cantlin_map = cantlin->map;

    map->tiles[x][y] = TILE_MOUNTAIN;
    *all = places_reachable(rom);
    cantlin->map = NO_MAP;   /* places_reachable only checks entrances on the overworld */
    *others = places_reachable(rom);
    cantlin->map = cantlin_map;
    map->tiles[x][y] = tile;
}

/* Where the Golem stands in a shuffle: beside Cantlin, on a tile that blocks nothing else, so every other
 * place, the token and the Return point stay reachable while it is unbeaten. A tile that also shuts off
 * Cantlin is picked first. FALSE when no tile is safe. */
static BOOL z2_golem_spot(dw_rom *rom, uint8_t *x, uint8_t *y)
{
    static const int dir[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    dw_map *map = &rom->map;
    const dw_warp *cantlin = &map->warps_from[WARP_CANTLIN];
    int i, nx, ny, found = 0;   /* 1: a safe tile, 2: a safe tile that is also the only way in */

    for (i = 0; i < 4 && found < 2; i++) {
        BOOL others, all;

        nx = cantlin->x + dir[i][0];
        ny = cantlin->y + dir[i][1];
        if (map_ob(nx) || map_ob(ny) || !tile_is_walkable(map->tiles[nx][ny]) ||
                z2_is_entrance(map->tiles[nx][ny]))
            continue;
        /* Not the Return point, and not the token: a treasure guard there (added later, in a higher entry)
         * would be hidden behind the Golem, since the first spike entry on a tile wins. */
        if ((nx == map->return_point->x && ny == map->return_point->y) ||
                (nx == rom->search_table->x[0] && ny == rom->search_table->y[0]))
            continue;
        z2_reach_without(rom, nx, ny, &others, &all);
        if (!others)
            continue;
        if (!found || !all) {
            *x = (uint8_t)nx;
            *y = (uint8_t)ny;
            found = all ? 1 : 2;
        }
    }
    return found > 0;
}

/* One try at the Zelda II map. FALSE when a shuffle left something out of reach, so map_z2 tries again. */
static BOOL z2_try(dw_rom *rom)
{
    dw_map *map = &rom->map;
    const BOOL shuffled = LITE_SHUFFLE(rom) || DEEP_SHUFFLE(rom);
    uint8_t rx, ry;
    size_t i;

    memcpy(map->encoded, z2_map_data, MAP_ENCODED_SIZE);
    memcpy(map->pointers, z2_pointer_data, sizeof(z2_pointer_data));
    map_decode(map);
    memcpy(rom->zone_layout, z2_zones, sizeof(z2_zones));

    z2_default_places(rom);
    if (LITE_SHUFFLE(rom)) {
        z2_lite(rom);
    } else if (DEEP_SHUFFLE(rom)) {
        z2_deep(rom);
    } else {
        z2_plain(rom);
    }

    /* Return and Wings land beside Tantegel. */
    if (!z2_land_beside(map, map->warps_from[WARP_TANTEGEL].x, map->warps_from[WARP_TANTEGEL].y, &rx, &ry))
        return FALSE;
    map->return_point->x = rx;
    map->return_point->y = ry;

    if (shuffled) {
        z2_cut_bridge(rom);   /* also fills map->walkable for the check below */
        if (!z2_town_near(map))
            return FALSE;
    }

    for (i = 0; i < sizeof(z2_border_towns) / sizeof(z2_border_towns[0]); i++) {
        const dw_warp *w = &map->warps_from[z2_border_towns[i].warp];

        map->meta[z2_border_towns[i].town].border = z2_border(map, w->x, w->y);
    }
    if (SAFE_START(rom))
        safe_start(map);
    if (!places_reachable(rom))
        return FALSE;
    return !shuffled || z2_golem_spot(rom, &rx, &ry);
}

#define Z2_TRIES 500

void map_z2(dw_rom *rom)
{
    dw_map *map = &rom->map;
    const int tries = LITE_SHUFFLE(rom) || DEEP_SHUFFLE(rom) ? Z2_TRIES : 1;
    BOOL encoded;
    int i;

    printf("Drawing the Zelda II map...\n");
    check_keys(rom);
    map->size = MAP_DIM;
    for (i = 0; i < tries; i++) {
        if (z2_try(rom))
            break;
    }
    if (i == tries)
        dwr_give_up("No Zelda II map layout could be finished.");
    flat_bridges = TRUE;
    encoded = map_encode(map);
    flat_bridges = FALSE;
    if (!encoded)
        dwr_give_up("The Zelda II map does not fit in the ROM.");
}

/**
 * Puts the Golem on the road to Cantlin: spike entry 3, which the base leaves free for treasure guards.
 * Runs after spike_rewrite, which writes the whole spike table. Treasure guards come later, in entry 4 on.
 * In a shuffle the spot comes from z2_golem_spot; map_z2 only keeps layouts that have one.
 *
 * @param rom The rom struct
 */
void map_z2_guard(dw_rom *rom)
{
    dwr_spike_table *spike = rom->spike_table;
    uint8_t x = 71, y = 44;   /* where it stands in Zelda II */

    if ((LITE_SHUFFLE(rom) || DEEP_SHUFFLE(rom)) && !z2_golem_spot(rom, &x, &y)) {
        printf("No safe spot for the Golem beside Cantlin, so it is left out.\n");
        return;
    }
    spike->map[3] = OVERWORLD;
    spike->x[3] = x;
    spike->y[3] = y;
    spike->monster[3] = GOLEM;
}

void hints(dw_rom *rom)
{

    if(!HINTS(rom))
        return;
    else if(HINTS(rom) == 1)
        fix_geography_talk(rom);
    // else if()
}

void fix_geography_talk(dw_rom *rom)
{
    enum Direction dir;

    // NPC in Tantegel: "East of this castle is a town"
    dir = find_direction(rom, WARP_TANTEGEL, WARP_BRECCONARY);
    set_text(rom, 0xB08F, "XXXXX of this castle's a town");
    set_direction(rom, 0xB08F, dir, TRUE);
    remove_useless_spaces(rom, 0xB08F);

    // NPC in Brecconary: "Within sight of Tantegel Castle to the south is Charlock"
    dir = find_direction(rom, WARP_TANTEGEL, WARP_CHARLOCK);
    set_direction(rom, 0x9096, dir, FALSE);
    remove_useless_spaces(rom, 0x9096);

    // NPC in Brecconary: "Go north to the seashore, then follow the coastline west until thou hath reached Garinham."
    dir = find_direction(rom, WARP_BRECCONARY, WARP_GARINHAM);
    set_text(rom, 0x8F4E, "XXXXX doth lie Garinham; venture onward, and continue thy quest until thou dost arrive.   ");
    set_direction(rom, 0x8F4E, dir, TRUE);
    remove_useless_spaces(rom, 0x8F4E);

    // NPC in Kol: "Dreadful is the South island."
    set_text(rom, 0x91B8, "entire world");

    // NPC in Kol: "Hast thou been to the southern island?"
    set_text(rom, 0x9BE6, "outside of this hamlet");
    // "To the south, I believe, there is a town called Rimuldar."
    dir = find_direction(rom, WARP_KOL, WARP_RIMULDAR);
    set_direction(rom, 0x9DC5, dir, FALSE);
    remove_useless_spaces(rom, 0x9DC5);

    // NPC in Kol: "East of Hauksness there is a town, ’tis said, where one may purchase weapons of extraordinary quality."
    dir = find_direction(rom, WARP_HAUKSNESS, WARP_CANTLIN);
    set_text(rom, 0x90EA, "XXXXX of Hauksness, 'tis said there is a town");
    set_direction(rom, 0x90EA, dir, TRUE);
    remove_useless_spaces(rom, 0x90EA);

    // NPC in Garinham: "Once there was a town called Hauksness far to the south"
    dir = find_direction(rom, WARP_GARINHAM, WARP_HAUKSNESS);
    set_direction(rom, 0x9385, dir, FALSE);
    remove_useless_spaces(rom, 0x9385);

    // NPC in Garinham: "It is said that the Princess was kidnapped and taken eastward."
    if(rom->princess_map == SWAMP_CAVE)
        set_text(rom, 0x9509, "'tis said that the Princess was taken to a dark and damp cave.");
    else if(rom->princess_map == ERDRICKS_CAVE || rom->princess_map == ERDRICKS_CAVE_2)
        set_text(rom, 0x9509, "It is said that the Princess was taken to a monsterless cave. ");
    else if(rom->princess_map == MOUNTAIN_CAVE || rom->princess_map == MOUNTAIN_CAVE_2)
        set_text(rom, 0x9509, "It is said that the cave where the Princess is held is rocky. ");
    else
        set_text(rom, 0x9509, "It is said that the Princess is being held captive in a grave.");

    // NPC in Rimuldar: "Heed my warning! Travel not to the south"
    set_text(rom, 0x9AA5, "inside caves");

    // NPC in Rimuldar: "Hast thou found a magic temple? (No:) Go to the south."
    dir = find_direction(rom, WARP_RIMULDAR, WARP_JERK_CAVE);
    set_direction(rom, 0x9E34, dir, FALSE);
    remove_useless_spaces(rom, 0x9E34);

    // NPC in Rimuldar: "Over the western part of this island Erdrick created a rainbow."
    set_text(rom, 0x9886, "Erdrick once created a rainbow bridge to reach Charlock castle.");
}

enum Direction find_direction(dw_rom *rom, dw_warp_index w1, dw_warp_index w2)
{
    uint8_t x1;
    uint8_t y1;
    uint8_t x2;
    uint8_t y2;
    int16_t dx, dy;
    dw_warp *warp;

    if(w1 == WARP_JERK_CAVE)
    {
        warp = &rom->map.warps_from[WARP_JERK_CAVE];
        if(warp->map == TANTEGEL)
            w1 = WARP_TANTEGEL;
        else if(warp->map == GARINHAM)
            w1 = WARP_GARINHAM;
    }
    if(w2 == WARP_JERK_CAVE)
    {
        warp = &rom->map.warps_from[WARP_JERK_CAVE];
        if(warp->map == TANTEGEL)
            w2 = WARP_TANTEGEL;
        else if(warp->map == GARINHAM)
            w2 = WARP_GARINHAM;
    }

    x1 = rom->map.warps_from[w1].x;
    y1 = rom->map.warps_from[w1].y;
    x2 = rom->map.warps_from[w2].x;
    y2 = rom->map.warps_from[w2].y;
    dx = x2-x1;
    dy = y2-y1;

    if(ABS(dx) > ABS(dy))
        return dx > 0 ? DIR_EAST  : DIR_WEST;
    else
        return dy > 0 ? DIR_SOUTH : DIR_NORTH;
}

void set_direction(dw_rom *rom, const size_t address, enum Direction dir, BOOL cap)
{
    char direction[6];
    if(dir == DIR_SOUTH)
        strcpy(direction, "south");
    else if(dir == DIR_NORTH)
        strcpy(direction, "north");
    else if(dir == DIR_WEST)
        strcpy(direction, "west ");
    else
        strcpy(direction, "east ");

    if(cap)
        direction[0] = toupper(direction[0]);

    set_text(rom, address, direction);
}

void remove_useless_spaces(dw_rom *rom, size_t address)
{
    char next;
    int i;
    while(rom->content[address] != 0xfc)
    {
        next = rom->content[address+1];
        if(rom->content[address] == 0x5f && (next == 0x5f || next == 0x52 || next == 0x48 || next == 0x4c || next == 0x47)) // __ _.' _, _! _.
        {
            for(i = 0; rom->content[address+i+1] != 0xfc; i++)
                rom->content[address+i] = rom->content[address+i+1];
            rom->content[address+i] = 0x5f; // Move space to character just before end of dialog
        }
        address++;
    }
}