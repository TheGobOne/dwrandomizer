
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

    end = &encoded_map[120 * 120];

    for (i=1; i < 120; i++) {
        start = &encoded_map[pointers[i] - 0x9d5d - 1];
        if (*start >> 4 == *(start+1) >> 4 && *start <= *(start+1)) {
            ++saved;
            shift_bytes(start, end);
            for (j=i; j < 120; j++) {
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
static BOOL map_encode(dw_map *map)
{
    int x, y;
    uint8_t *e, encoded[120 * 120]; 
    uint16_t pointers[120];

    memset(encoded, 0xff, 120 * 120);
    e = encoded;

    for (y=0; y < 120; y++) {
        pointers[y] = e - encoded + 0x9d5d;
        for (x=0; x < 120; x++) {
            if (*e == 0xff) {
                *e = map->tiles[x][y] << 4;
            } else if (*e >> 4 == map->tiles[x][y]) {
                (*e)++;
            } else {
                e++;
                *e = map->tiles[x][y] << 4;
            }
            if ((*e & 0xf) == 0xf) {
                e++;
            }
        }
        if (*e != 0xff) {
            e++;
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
    int size = 0;
    int left = 0;
    int right = 119;

    /* Move all the way to the left */
    while(!map_ob(x-1) && tile_is_walkable(map->tiles[x-1][y]))
        x--;
    left = x;

    while(!map_ob(x) && tile_is_walkable(map->tiles[x][y])) {
        map->walkable[x][y] = lm_index;
        right = x++;
        size++;
    }
    /* go back to the left side and look up & down */
    for (x = left; x <= right; x++) {
        if (!map_ob(y-1) && tile_is_walkable(map->tiles[x][y-1]) &&
                !map->walkable[x][y-1])
            size += map_land_mass_2(map, x, y-1, lm_index);
        if (!map_ob(y+1) && tile_is_walkable(map->tiles[x][y+1]) &&
                !map->walkable[x][y+1])
            size += map_land_mass_2(map, x, y+1, lm_index);
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
    /* this could use more work */
    uint8_t x, y, left, right, x_candidate[120], y_candidate[120];
    uint64_t which;
    int count = 0;
    dw_tile tile;

    for (y=0; y < 120; y++) {
        for (x=2; x < 118; x++) {
            tile = map->tiles[x][y];
            if (tile == TILE_WATER) {
                left = map->walkable[x-1][y];
                right = map->walkable[x+1][y];
                if (needs_bridge(left, right)){
                    x_candidate[count]   = x;
                    y_candidate[count++] = y;
                }
            }
            if (count >= 120) {
                printf("Maximum bridge candidates reached\n");
                goto max_candidates;
            }
        }
    }
max_candidates:
    if (count) {
        which = mt_rand(0, count-1);
        x = x_candidate[which];
        y = y_candidate[which];
        if (y > 0 && tile_is_walkable(map->tiles[x][y-1])) {
            map->tiles[x][y] = map->tiles[x][y-1];

        } else if (y < 119 && tile_is_walkable(map->tiles[x][y+1])) {
            map->tiles[x][y] = map->tiles[x][y+1];
        } else {
            map->tiles[x][y] = TILE_BRIDGE;
        }
        return TRUE;
    }
    return FALSE;
}

/* Lake sizes, shared by the terrain, the Tantegel check and the area check. */
#define LAKE_RADIUS(m) ((m)->size * 22 / 100)
#define LAKE_ISLAND 7

/* Set when a land mass has no free spot left, so the map is thrown away and drawn again. */
static BOOL land_missing;

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
/* Extra rules for where a town or cave may go in the newer biomes (Lake and later). Older biomes keep
 * their old picks, so their flag codes still make the same maps. */
static BOOL new_biome_spot(dw_map *map, int x, int y)
{
    const dw_warp *charlock = &map->warps_from[WARP_CHARLOCK];
    int dx, dy;

    if (!RANDOM_MAP(map) || BIOME(map) < BIOME_LAKE)
        return TRUE;
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
    if (y < 119 && tile_is_walkable(map->tiles[x][y+1])) { 
        map->return_point->x = x;
        map->return_point->y = y+1;
    } else if (y > 0 && tile_is_walkable(map->tiles[x][y-1])) { 
        map->return_point->x = x;
        map->return_point->y = y-1;
    } else if (x > 0 && tile_is_walkable(map->tiles[x-1][y])) { 
        map->return_point->x = x-1;
        map->return_point->y = y;
    } else if (x < 119 && tile_is_walkable(map->tiles[x+1][y])) { 
        map->return_point->x = x+1;
        map->return_point->y = y;
    }
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
        /* Tantegel on Charlock's island would skip the whole journey. */
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

/* Cuts the land down to a rough rounded square with open water around it. */
static void biome_coast(dw_map *map)
{
    int x, y, dy, a, left = 0, right = 0;
    int half = map->size / 2 - 2;
    int radius = map->size * 62 / 100;

    for (y = 0; y < 120; y++) {
        dy = y < 60 ? 60 - y : y - 60;
        left = biome_drift(left, 4);
        right = biome_drift(right, 4);
        a = -1;
        if (dy <= half && dy < radius) {
            a = biome_isqrt(radius * radius - dy * dy);
            if (a > half) a = half;
        }
        for (x = 0; x < 120; x++) {
            if (a < 0 || x < 60 - a + left || x > 60 + a + right)
                map->tiles[x][y] = TILE_WATER;
        }
    }
}

/* A mountain ring around a basin with two passes. Charlock goes in the middle. */
static void biome_caldera(dw_map *map)
{
    int x, y, dy, t, dist, outer, inner, first, second;
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
            int side;
            for (side = 0; side < 2; side++) {
                int dir = side ? second : first;
                if (dir == 0) map->tiles[60 + dist][60 - t] = TILE_GRASS;
                if (dir == 1) map->tiles[60 + dist][60 + t] = TILE_GRASS;
                if (dir == 2) map->tiles[60 - t][60 + dist] = TILE_GRASS;
                if (dir == 3) map->tiles[60 + t][60 + dist] = TILE_GRASS;
            }
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

/* Whether a walkable tile of the given region touches this tile. Region 0 means the main continent
 * (Europe, Asia or Africa). */
static BOOL earth_touches(dw_map *map, int x, int y, char region)
{
    int i, nx, ny;
    char c;

    for (i = 0; i < 4; i++) {
        nx = x + dir4[i][0];
        ny = y + dir4[i][1];
        if (map_ob(nx) || map_ob(ny) || !tile_is_walkable(map->tiles[nx][ny]))
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

/* The world: the Americas in the west, Europe, Asia and Africa as one continent in the east, and Australia,
 * Japan, the Arctic and Antarctica joined to it by bridges. The rafts to the Americas come later, once the
 * towns are placed. */
static BOOL biome_earth(dw_map *map)
{
    static const char islands[] = "UJRT";
    int i;

    biome_template(map, earth_template);
    for (i = 0; islands[i]; i++) {
        if (!earth_bridge(map, islands[i])) {
            printf("No way to bridge to an island, retrying...\n");
            return FALSE;
        }
    }
    return TRUE;
}

/* A plain land tile a dock or a raft arrival can use. */
static BOOL dock_land(dw_tile tile)
{
    return tile == TILE_GRASS || tile == TILE_DESERT || tile == TILE_HILL || tile == TILE_TREES;
}

/* Whether a cave at x, y would block a way through: its walkable neighbours must still reach each other
 * nearby without crossing it (or any other town, cave or castle). */
static BOOL dock_blocks(dw_map *map, int x, int y)
{
    static uint8_t seen[17][17];
    int qx[17 * 17], qy[17 * 17], head = 0, tail = 0, i, nx, ny, first = -1, cx, cy;
    dw_tile t;

    memset(seen, 0, sizeof(seen));
    seen[8][8] = 1;   /* the dock itself */
    for (i = 0; i < 4; i++) {
        nx = x + dir4[i][0];
        ny = y + dir4[i][1];
        t = map->tiles[nx][ny];
        if (tile_is_walkable(t) && t != TILE_TOWN && t != TILE_CAVE && t != TILE_CASTLE) {
            first = i;
            break;
        }
    }
    if (first < 0)
        return FALSE;
    qx[tail] = x + dir4[first][0];
    qy[tail++] = y + dir4[first][1];
    seen[8 + dir4[first][0]][8 + dir4[first][1]] = 1;
    while (head < tail) {
        cx = qx[head];
        cy = qy[head++];
        for (i = 0; i < 4; i++) {
            nx = cx + dir4[i][0];
            ny = cy + dir4[i][1];
            if (nx - x < -8 || nx - x > 8 || ny - y < -8 || ny - y > 8 || map_ob(nx) || map_ob(ny))
                continue;
            if (seen[8 + nx - x][8 + ny - y])
                continue;
            t = map->tiles[nx][ny];
            if (!tile_is_walkable(t) || t == TILE_TOWN || t == TILE_CAVE || t == TILE_CASTLE)
                continue;
            seen[8 + nx - x][8 + ny - y] = 1;
            qx[tail] = nx;
            qy[tail++] = ny;
        }
    }
    for (i = 0; i < 4; i++) {
        nx = x + dir4[i][0];
        ny = y + dir4[i][1];
        t = map->tiles[nx][ny];
        if (tile_is_walkable(t) && t != TILE_TOWN && t != TILE_CAVE && t != TILE_CASTLE &&
                !seen[8 + dir4[i][0]][8 + dir4[i][1]])
            return TRUE;
    }
    return FALSE;
}

/* Picks a dock on the coast of a region: a plain tile beside open sea, away from Charlock, with a plain
 * tile of the same land beside it for the raft to land on. The dock becomes a cave tile. */
static BOOL earth_dock(dw_map *map, char region, uint8_t *dock_x, uint8_t *dock_y,
        uint8_t *land_x, uint8_t *land_y)
{
    const dw_warp *charlock = &map->warps_from[WARP_CHARLOCK];
    int x, y, i, nx, ny, wet, land, beside, count = 0;

    for (x = 1; x < 119; x++) {
        for (y = 1; y < 119; y++) {
            if (map_region[x][y] != region || !dock_land(map->tiles[x][y]))
                continue;
            if (abs(x - (int)charlock->x) < 8 && abs(y - (int)charlock->y) < 8)
                continue;
            wet = FALSE;
            land = -1;
            beside = FALSE;
            for (i = 0; i < 4; i++) {
                nx = x + dir4[i][0];
                ny = y + dir4[i][1];
                if (map->tiles[nx][ny] == TILE_WATER && map_region[nx][ny] == '.')
                    wet = TRUE;
                else if (dock_land(map->tiles[nx][ny]) && map_region[nx][ny] == region &&
                         map->walkable[nx][ny] == map->walkable[x][y])
                    land = i;
                /* dock_blocks skips towns, caves and castles, so a town whose only way in is this
                 * tile would be cut off. Keep docks away from them. */
                if (map->tiles[nx][ny] == TILE_TOWN || map->tiles[nx][ny] == TILE_CAVE ||
                        map->tiles[nx][ny] == TILE_CASTLE)
                    beside = TRUE;
            }
            if (!wet || land < 0 || beside || dock_blocks(map, x, y))
                continue;
            if (mt_rand(0, count++) == 0) {
                *dock_x = x;
                *dock_y = y;
                *land_x = x + dir4[land][0];
                *land_y = y + dir4[land][1];
            }
        }
    }
    if (!count)
        return FALSE;
    map->tiles[*dock_x][*dock_y] = TILE_CAVE;
    /* Keep later docks off this one's landing tile. */
    map_region[*land_x][*land_y] = '*';
    return TRUE;
}

/* Two rafts: one from North America and one from South America, each to a different one of Europe, Asia
 * and Africa. A raft is 2 warps, one each way, and each lands beside the other dock. */
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
            for (y = top - 4; y <= bottom + 4; y++) {
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
            biome_fill(rom, tiles, TILE_DESERT, layers);
            biome_coast(map);
            find_walkable_area(map, lm_sizes, &largest, &next);
            for (i = 0; i < map->size / 20; i++)
                map_add_river(map);
            break;
        default:
            printf("Drawing riverlands\n");
            biome_fill(rom, tiles, TILE_GRASS, layers);
            biome_coast(map);
            for (i = 0; i < map->size / 40; i++)
                biome_river(map, TRUE);
            for (i = 0; i < map->size / 30; i++)
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
    x = map->warps_from[WARP_CHARLOCK].x + 3;
    y = map->warps_from[WARP_CHARLOCK].y;
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
         * own amount of land, so it only has to keep most of it. */
        if (total_area < (TEMPLATE_BIOME(rom) ? rom->map.size * 30 :
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
    if (RAFT_BIOME(rom)) {
        /* The token can't be under a dock, where the hero never stands. */
        for (i = 0; rom->map.tiles[rom->search_table->x[0]][rom->search_table->y[0]] == TILE_CAVE ||
                rom->map.tiles[rom->search_table->x[0]][rom->search_table->y[0]] == TILE_TOWN ||
                rom->map.tiles[rom->search_table->x[0]][rom->search_table->y[0]] == TILE_CASTLE; i++) {
            if (i > 1000 || !map_find_land(&rom->map, largest, next, &rom->search_table->x[0],
                    &rom->search_table->y[0], TRUE))
                goto retry_map;
        }
    }

    if (SAFE_START(rom))
        safe_start(&rom->map);

    /* Older biomes are left alone so their flag codes keep making the same maps. */
    if (RANDOM_MAP(rom) && BIOME(rom) >= BIOME_LAKE && !places_reachable(rom)) {
        printf("A place cannot be reached, retrying...\n");
        goto retry_map;
    }

    if (!map_encode(&rom->map))
        goto retry_map;
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