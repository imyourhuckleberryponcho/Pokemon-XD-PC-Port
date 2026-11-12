#ifdef __APPLE__
__asm__(".zerofill __XDLOW,__reserve,_xd_reserve_low,0x10000000,12\n"
        ".no_dead_strip _xd_reserve_low\n"
        ".zerofill __XDMEM1,__reserve,_xd_reserve_mem1,0x01800000,12\n"
        ".no_dead_strip _xd_reserve_mem1\n"
        ".zerofill __XDMEMU,__reserve,_xd_reserve_memu,0x01800000,12\n"
        ".no_dead_strip _xd_reserve_memu\n"
        ".zerofill __XDLC,__reserve,_xd_reserve_lc,0x00004000,12\n"
        ".no_dead_strip _xd_reserve_lc\n");
#else
typedef int mac_low_reserve_is_empty_elsewhere;
#endif
