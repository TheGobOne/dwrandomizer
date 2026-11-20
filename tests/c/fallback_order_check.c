/* Runs dwr_randomize_tries (common/dwr.c) with a stand-in for one build attempt that runs out of room until a
 * chosen call, and prints what each call was given. Built and run by tests/archipelago/test_drop_personal.py.
 *   usage: fallback_order_check <flag code> <fail stage 1|2> <succeed at call, 0 = never>
 * Output: one "call <fit_game> <fit_extras> <flags>" line per attempt, then "result <crc> <repacked> <flags>". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dwr.c"

static int calls, stage, succeed_at;

static uint64_t fake_once(const char *input_file, uint64_t seed, char *flags, const char *sprite_name,
        const char *output_dir, BOOL fit_game, BOOL fit_extras)
{
    calls++;
    printf("call %d %d %s\n", fit_game ? 1 : 0, fit_extras ? 1 : 0, flags);
    if (calls == succeed_at)
        return 1;
    ran_out_of_room = TRUE;
    fail_stage = stage;
    return 0;
}

int main(int argc, char **argv)
{
    char buf[64] = {0};
    uint64_t crc;

    if (argc < 4 || strlen(argv[1]) > 44)
        return 2;
    strcpy(buf, argv[1]);
    stage = atoi(argv[2]);
    succeed_at = atoi(argv[3]);
    build_once = fake_once;
    crc = dwr_randomize_tries("none", 1, buf, "", "");
    printf("result %d %d %s\n", (int)crc, dwr_last_repacked() ? 1 : 0, buf);
    return 0;
}
