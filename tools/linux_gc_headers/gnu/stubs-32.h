/* Stand-in for glibc's gnu/stubs-32.h (the 32-bit C library's list of functions it lacks), which a 64-bit-only
 * Linux machine does not have. tools/gen_disc_tables.py reads the game's files once for a 32-bit target to get the
 * GameCube's layouts; nothing is compiled or linked for it, so an empty list is enough. */
