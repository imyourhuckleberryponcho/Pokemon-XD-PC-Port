#include <dolphin/types.h>
#include <game/pcret.h>
#include <game/gcdata.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

struct HeroPokemonGetParam {
    u8 trainerSexID;
    u8 catchLevel;
    u16 pokemonDataID;
    u16 shadowPokemonDataID;
    u16 friendLevel;
    u16 move1;
    u16 move2;
    u16 move3;
    u16 move4;
    u32 trainerRnd;
    u16* trainerNamePtr;
    u16* nicknamePtr;
    u32 catchFloorID;
    u32 exp;
};
struct DarkPokemon {
public:
    void setCatchBallID(u16);
    void setCatchFloorId(u16);
    void setUpdate(bool);
    void setSnatchIndex(u8);
    void setRnd(u32);
};
struct Pokemon {
    DarkPokemon* getDarkPokemon();
    u32 getRnd() const;
};
struct Hero {
public:
    u32 getHeroRnd() const;
    s16 pokemonGet(HeroPokemonGetParam*, bool);
    Pokemon* getPokemon(s32, bool&);
    u8 getMeetDarkPokemonCount() const;
    void setMeetDarkPokemonCount(u8);
};

extern "C" {
Hero* savedataGetStatus(s32, s32);
u32 heroBiosGetRnd(Hero*);
u8 heroBiosGetSexDataId(Hero*);
u16* heroBiosGetNamePtr(Hero*);
s32 GSfloorGetCurrent(void);
u16 darkPokemonListBiosGetPokemonID(u16 index);
u8 darkPokemonBiosGetLevel(u16 id);
BOOL darkPokemonBiosGetFightFlag(u16 id);
BOOL darkPokemonBiosGetSnatchSuccessFlag(u16 id);
void darkPokemonBiosSetStatusRnd(u16 id, s32 index, s32 value);
void* pokemonGrowDataBiosGetPtr(u16);
u16 pokemonDB_GetGrowDataId(u16);
u32 pokemonGrowDataBiosGetExp(void*, u32);
uintptr_t pokemonGetStatus(Pokemon*, u32, u32, u32);
extern u32 darkPokemonList_number;
extern u32* pokemon_data_number;
u16 esabaGetPokemonID(s32 index);
u32 esabaGetPokemonRnd(s32 index);
void pokemonCreate(void* pokemon, u16 id, u8 level, u32 trainer);
extern u32* fight_encount_data_number;
void* fightEncountDataBiosGetPtr(u16 id);
u32 fightEncountDataBiosGetFightKind(void* data);
u16 fightEncountDataBiosGetFightFloorDataId(void* data);
}

namespace {
struct Wild {
    bool armed = false;
    u16 species = 0;
    u8 level = 5;
} sWild;
}

extern "C" {

s32 PortXD_GiftShadow(s32 shadowId, s32 level) {
    Hero* hero = savedataGetStatus(0, 2);
    static u16 rnd_status_id[6] = {0x93, 0x94, 0x95, 0x96, 0x97, 0x98};
    if (hero == 0 || shadowId < 1 || static_cast<u32>(shadowId) >= darkPokemonList_number) return -3;
    const u16 id = static_cast<u16>(shadowId);
    const u16 species = darkPokemonListBiosGetPokemonID(id);
    if (species == 0 || species >= *pokemon_data_number) return -3;
    if (darkPokemonBiosGetSnatchSuccessFlag(id)) return -4;
    if (level <= 0) level = darkPokemonBiosGetLevel(id);
    if (level < 1 || level > 100) return -3;
    const bool met = darkPokemonBiosGetFightFlag(id) != 0;
    HeroPokemonGetParam param;
    memset(&param, 0, sizeof(param));
    param.pokemonDataID = species;
    param.shadowPokemonDataID = id;
    param.catchLevel = static_cast<u8>(level);
    param.friendLevel = 0;
    param.trainerSexID = heroBiosGetSexDataId(hero);
    param.trainerNamePtr = heroBiosGetNamePtr(hero);
    param.catchFloorID = GSfloorGetCurrent();
    param.trainerRnd = heroBiosGetRnd(hero);
    void* grow = pokemonGrowDataBiosGetPtr(pokemonDB_GetGrowDataId(species));
    if (grow == 0) return -3;
    param.exp = pokemonGrowDataBiosGetExp(grow, param.catchLevel);
    const s16 slot = hero->pokemonGet(&param, false);
    if (slot < 0) return -2;
    bool found = false;
    Pokemon* pokemon = hero->getPokemon(slot, found);
    DarkPokemon* dark = pokemon != 0 ? pokemon->getDarkPokemon() : 0;
    if (dark != 0) {
        dark->setCatchBallID(4);
        dark->setCatchFloorId(static_cast<u16>(param.catchFloorID));
        dark->setUpdate(true);
        if (!met) {
            const u8 count = hero->getMeetDarkPokemonCount();
            hero->setMeetDarkPokemonCount(count + 1);
            dark->setSnatchIndex(count);
        }
        dark->setRnd(pokemon->getRnd());
        for (int i = 0; i < 6; i++) darkPokemonBiosSetStatusRnd(id, i, static_cast<s32>(pokemonGetStatus(pokemon, 0, rnd_status_id[i], 0)));
    }
    return slot;
}

s32 PortXD_ShadowSpecies(s32 shadowId) {
    if (shadowId < 1 || static_cast<u32>(shadowId) >= darkPokemonList_number) return 0;
    return darkPokemonListBiosGetPokemonID(static_cast<u16>(shadowId));
}

u32 PortXD_WildRowOfSpot(s32 spot) {
    if (spot < 1 || spot > 3 || fight_encount_data_number == 0) return 0;
    for (u32 id = 1; id < *fight_encount_data_number; id++) {
        void* row = fightEncountDataBiosGetPtr(static_cast<u16>(id));
        if (row == 0) continue;
        if (fightEncountDataBiosGetFightKind(row) == 0xC && fightEncountDataBiosGetFightFloorDataId(row) == 0x38 + spot) return id;
    }
    return 0;
}

int PortXD_WildArm(s32 species, s32 level) {
    if (species < 1 || static_cast<u32>(species) >= *pokemon_data_number || level < 1 || level > 100) return 0;
    sWild.armed = true;
    sWild.species = static_cast<u16>(species);
    sWild.level = static_cast<u8>(level);
    return 1;
}
void PortXD_WildDisarm(void) { sWild.armed = false; }

u16 PortXD_WildEsabaSpecies(s32 index) {
    if (sWild.armed) return sWild.species;
    return esabaGetPokemonID(index);
}
u32 PortXD_WildEsabaRnd(s32 index) {
    if (sWild.armed) return 0;
    return esabaGetPokemonRnd(index);
}
void PortXD_WildPokemonCreate(void* pokemon, u16 id, u8 level, u32 trainer) {
    if (sWild.armed && id == sWild.species) {
        level = sWild.level;
        sWild.armed = false;
    }
    pokemonCreate(pokemon, id, level, trainer);
}

}
