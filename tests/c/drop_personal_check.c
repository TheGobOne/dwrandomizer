/* Runs drop_personal (common/dwr.c) on one flag code and prints "<changed> <new code>".
 * Built and run by tests/archipelago/test_drop_personal.py. */
#include <stdio.h>
#include <string.h>
#include "dwr.c"

int main(int argc, char **argv)
{
    char buf[64] = {0};

    if (argc < 2 || strlen(argv[1]) > 44)
        return 2;
    strcpy(buf, argv[1]);
    printf("%d %s\n", drop_personal(buf) ? 1 : 0, buf);
    return 0;
}
