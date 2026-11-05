#!/bin/bash -e 

echo -n "Compiling... "

"${ASM6F:-asm6f}" -q -m -f -dDEBUG credits.asm credits.nes

echo "Done."

