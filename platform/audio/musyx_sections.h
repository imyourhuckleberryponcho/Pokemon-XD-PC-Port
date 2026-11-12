#ifndef PORT_MUSYX_SECTIONS_H
#define PORT_MUSYX_SECTIONS_H

#ifdef _MSC_VER
#define PORT_MUSYX_STATE_BEGIN __pragma(data_seg(".mxstate$m")) __pragma(bss_seg(".mxstate$m"))
#define PORT_MUSYX_STATE_END __pragma(data_seg()) __pragma(bss_seg())
#elif defined(__APPLE__)
#define PORT_MUSYX_STATE_BEGIN _Pragma("clang section data=\"__DATA,mxstate\"")
#define PORT_MUSYX_STATE_END _Pragma("clang section data=\"\"")
#elif defined(__clang__)
#define PORT_MUSYX_STATE_BEGIN _Pragma("clang section data=\"mxstate\"")
#define PORT_MUSYX_STATE_END _Pragma("clang section data=\"\"")
#else
#define PORT_MUSYX_STATE_BEGIN
#define PORT_MUSYX_STATE_END
#endif

PORT_MUSYX_STATE_BEGIN

#endif
