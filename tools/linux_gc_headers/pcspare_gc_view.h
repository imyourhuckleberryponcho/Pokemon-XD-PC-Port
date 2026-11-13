/* tools/gen_disc_tables.py: forced into the GAMECUBE-layout reading of the decomp's files only (never compiled into
 * the game). The decomp's game/pcspare.h gives some main.dol tables PC-only spare rows (port request 2026-11-12:
 * mods add species / moves / items): on PC `table[ORIG + SPARE]`, on the GameCube (__MWERKS__) exactly the original
 * `table[]`. The generator reads each table's GameCube layout to find and convert its bytes in the player's main.dol,
 * so here the GameCube spelling is put back: the original row count and size. The PC copy keeps its spare rows, which
 * stay zero; the conversion fills the original rows (var_layout takes the smaller of the two row counts). */
#if defined(__has_include)
#if __has_include(<game/pcspare.h>)
#include <game/pcspare.h>
#undef PC_SPARE_ROWS
#undef PC_SPARE_COUNT
#undef PC_SPARE_CHECK
#define PC_SPARE_ROWS(orig, spare)
#define PC_SPARE_COUNT(table, orig) (sizeof(table) / sizeof((table)[0]))
#define PC_SPARE_CHECK(table, orig) \
    typedef char table##_orig_rows_check[(sizeof(table) / sizeof((table)[0]) == (orig)) ? 1 : -1]
#endif
#endif
