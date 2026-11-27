#ifdef _MSC_VER
#pragma code_seg(".text$xdg")
#pragma data_seg(".xdgd$m")
#pragma bss_seg(".xdgd$m")
#elif defined(__APPLE__)
#pragma clang section text = "__TEXT,xdg_text"
#pragma clang section rodata = "__DATA,xdg_const"
#elif defined(__clang__)
#pragma clang section text = "xdg_text"
#pragma clang section data = "xdg_data" bss = "xdg_bss"
#endif
