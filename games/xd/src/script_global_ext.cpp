#include <dolphin/types.h>
#include <game/pcret.h>
#include <game/gcdata.h>
#include <game/tigadata.h>

namespace GSscript {
struct tigaVariant {
    gc_bes16 type;
    gc_be16 flags;
    union { gc_bes32 integer; gc_bef32 real; gc_ptr32(void) pointer; } value;
    s32 getCnvInteger();
    char* getCnvString(char*);
    inline s32 getInteger() {
        if (type == 1) return value.integer;
        if (type == 2) return (s32)value.real;
        return getCnvInteger();
    }
    inline char* getString(char* buf) {
        if (type == 3) return (char*)value.pointer;
        return getCnvString(buf);
    }
    inline void setInteger(s32 n) { type = 1; value.integer = n; }
};
}

extern "C" s32 PortXD_ScriptLuaCall(const char* name, s32 a, s32 b, s32 c, s32* out);
extern "C" void PortXD_ScriptRunPending(void);
extern "C" u32 PortXD_TakePendingBattle(void);
extern "C" s32 PortXD_BattleWon(void);
extern "C" s32 PortXD_GiftShadow(s32 shadowId, s32 level);
extern "C" u32 PortXD_WildRowOfSpot(s32 spot);
extern "C" int PortXD_WildArm(s32 species, s32 level);
extern "C" u32 scriptEncount(u16 id, u8 fade, s32 noFade);

extern "C" s32 PortXD_ScriptGlobalExt(int function, GSscript::tigaVariant* args, GSscript::tigaVariant* result)
{
    if (function == 0x100) {
        char buf[0x100];
        const char* name = args[0].getString(buf);
        s32 out = 0;
        if (name != 0 && PortXD_ScriptLuaCall(name, args[1].getInteger(), args[2].getInteger(), args[3].getInteger(), &out)) {
            result->setInteger(out);
        }
        PortXD_ScriptRunPending();
        if (const u32 battle = PortXD_TakePendingBattle()) {
            scriptEncount(static_cast<u16>(battle), 1, 0);
        }
    } else if (function == 0x103) {
        result->setInteger(PortXD_GiftShadow(args[0].getInteger(), args[1].getInteger()));
    } else if (function == 0x104) {
        u32 row = PortXD_WildRowOfSpot(args[0].getInteger());
        if (row != 0 && PortXD_WildArm(args[1].getInteger(), args[2].getInteger())) {
            scriptEncount(static_cast<u16>(row), 1, 0);
        } else {
            row = 0;
        }
        result->setInteger(static_cast<s32>(row));
    } else if (function == 0x102) {
        result->setInteger(PortXD_BattleWon());
    }
    return 0;
}
