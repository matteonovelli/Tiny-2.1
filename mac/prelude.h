/* prelude.h - read before TINY.C on macOS (build.sh passes -include).

   TINY.C stays exactly as written for Turbo C 2.0. This file makes clang
   see the DOS screen and sound parts too: it defines __TURBOC__, and the
   conio.h, dos.h and io.h next to it stand in for Turbo C's. */

/* The C library first, so it is read without __TURBOC__ defined. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

/* In DOS clock() counts real time. On macOS it counts processor time, which
   hardly moves while TINY waits for its sounds, so the SPEED display and the
   "seconds" after each answer would show 0. */
clock_t tc_clock(void);
#define clock() tc_clock()

#define __TURBOC__ 0x0200
