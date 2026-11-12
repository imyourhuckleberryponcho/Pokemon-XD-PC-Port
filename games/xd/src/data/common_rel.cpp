#include "common_rel.h"

#include <data_table.h>
#include <gs_formats.h>
#include <port_disc.h>

#include <game/pxdvs/app/fight/fightData.h>
#include <game/pxdvs/app/item/itemData.h>
#include <game/pxdvs/app/pokemon/pokemonDB.hpp>
#include <game/pxdvs/app/pokemon/pokemonSeikaku.h>
#include <game/pxdvs/app/waza/wazaData.h>
#include <game/pxdvs/app/zokusei/zokuseiData.h>
#include <game/menuTables.h>
#include <game/miscTables.h>
#include <game/pxdvs/GSAPI/GSflag/flagData.h>
#include <game/pxdvs/app/floor/floorData.h>
#include <game/pxdvs/app/deck/deckDataVirtual.h>
#include <game/pxdvs/app/esaba/esabaData.h>
#include <game/faceData.h>
#include <game/pxdvs/app/mail/mailData.h>
#include <game/shopData.h>
#include <game/bingoData.h>
#include <game/pxdvs/app/people/peopleInfo.h>
#include <game/pxdvs/GSAPI/GSsnd/sndData.h>
#include <game/worldMapData.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <signal.h>
#include <sys/mman.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct RelGlobal {
  uint32_t address;
  const char* name;
};
const RelGlobal kRelGlobals[] = {
#define RELGLOBAL(addr, name) {addr, #name},
#include "relglobal_vars.inc"
#undef RELGLOBAL
};

const char* relglobal_name(uint32_t address) {
  for (const auto& g : kRelGlobals) {
    if (g.address == address) {
      return g.name;
    }
  }
  return nullptr;
}

bool ends_with(const char* s, const char* suffix) {
  const size_t n = std::strlen(s), m = std::strlen(suffix);
  return n >= m && std::strcmp(s + n - m, suffix) == 0;
}

#define PD(member) offsetof(PokemonData, member)
const FieldDesc kPokemonDataFields[] = {
    {"growDataId", FIELD_U8, 1, 0x00, PD(growDataId)},
    {"get", FIELD_U8, 1, 0x01, PD(get)},
    {"sexRatio", FIELD_U8, 1, 0x02, PD(sexRatio)},
    {"unk0", FIELD_U8, 1, 0x03, PD(unk0)},
    {"giveExp", FIELD_U16, 1, 0x04, PD(giveExp)},
    {"initFriend", FIELD_U16, 1, 0x06, PD(initFriend)},
    {"height", FIELD_U16, 1, 0x08, PD(height)},
    {"weight", FIELD_U16, 1, 0x0A, PD(weight)},
    {"voice", FIELD_U16, 1, 0x0C, PD(voice)},
    {"unk1", FIELD_U8, 2, 0x0E, PD(unk1)},
    {"shout", FIELD_U16, 1, 0x10, PD(shout)},
    {"numPokemon", FIELD_U16, 1, 0x12, PD(numPokemon)},
    {"numZukan", FIELD_U16, 1, 0x14, PD(numZukan)},
    {"unk2", FIELD_U8, 2, 0x16, PD(unk2)},
    {"name", sizeof(PokemonData::name) == 4 ? FIELD_U32 : FIELD_U32_IN_PTR, 1, 0x18, PD(name)},
    {"typeName", FIELD_U32, 1, 0x1C, PD(typeName)},
    {"doc", FIELD_U32, 1, 0x20, PD(doc)},
    {"mitaFlag", FIELD_U32, 1, 0x24, PD(mitaFlag)},
    {"tukamaetaFlag", FIELD_U32, 1, 0x28, PD(tukamaetaFlag)},
    {"pkxDataId", FIELD_U32, 1, 0x2C, PD(pkxDataId)},
    {"zokuseiDataId", FIELD_U8, 2, 0x30, PD(zokuseiDataId)},
    {"tokuseiDataId", FIELD_U8, 2, 0x32, PD(tokuseiDataId)},
    {"wazaMcn", FIELD_U8, 58, 0x34, PD(wazaMcn)},
    {"teachWaza", FIELD_U8, 12, 0x6E, PD(teachWaza)},
    {"itemDataId", FIELD_U16, 2, 0x7A, PD(itemDataId)},
    {"kowaza", FIELD_U16, 8, 0x7E, PD(kowaza)},
    {"basisStatus", FIELD_U16, 12, 0x8E, PD(basisStatus)},
    {"sinka.kind", FIELD_U8, 5, 0xA6, PD(sinka) + offsetof(Sinka, kind), 6, sizeof(Sinka)},
    {"sinka.pad", FIELD_U8, 5, 0xA7, PD(sinka) + offsetof(Sinka, kind) + 1, 6, sizeof(Sinka)},
    {"sinka.buff", FIELD_U16, 5, 0xA8, PD(sinka) + offsetof(Sinka, buff), 6, sizeof(Sinka)},
    {"sinka.pokemonDataId", FIELD_U16, 5, 0xAA, PD(sinka) + offsetof(Sinka, pokemonDataId), 6, sizeof(Sinka)},
    {"waza.level", FIELD_U8, 20, 0xC4, PD(waza) + offsetof(PokemonDataWaza, level), 4, sizeof(PokemonDataWaza)},
    {"waza.pad", FIELD_U8, 20, 0xC5, PD(waza) + offsetof(PokemonDataWaza, level) + 1, 4, sizeof(PokemonDataWaza)},
    {"waza.dataId", FIELD_U16, 20, 0xC6, PD(waza) + offsetof(PokemonDataWaza, dataId), 4, sizeof(PokemonDataWaza)},
    {"cgData.color", FIELD_U8, 2, 0x114, PD(cgData) + offsetof(PokemonDataCgData, color), 8, sizeof(PokemonDataCgData)},
    {"cgData.pad", FIELD_U8, 2, 0x115, PD(cgData) + offsetof(PokemonDataCgData, color) + 1, 8, sizeof(PokemonDataCgData)},
    {"cgData.spriteId", FIELD_U16, 2, 0x116, PD(cgData) + offsetof(PokemonDataCgData, spriteId), 8, sizeof(PokemonDataCgData)},
    {"cgData.pokebodyId", FIELD_U32, 2, 0x118, PD(cgData) + offsetof(PokemonDataCgData, pokebodyId), 8, sizeof(PokemonDataCgData)},
};
#undef PD
const TableDesc kPokemonData = {"pokemon_data", 0x124, sizeof(PokemonData), kPokemonDataFields,
                                sizeof(kPokemonDataFields) / sizeof(kPokemonDataFields[0])};

const FieldDesc kU16Value[] = {{"value", FIELD_U16, 1, 0, 0}};
const FieldDesc kU8Value[] = {{"value", FIELD_U8, 1, 0, 0}};
#define U16_TABLE(name) {#name, 2, 2, kU16Value, 1}
const TableDesc kU16Tables[] = {
    U16_TABLE(item_data_index), U16_TABLE(legendPokemonData), U16_TABLE(mball_data),
    U16_TABLE(pdapokemonsortdata), U16_TABLE(shopGoods), U16_TABLE(snd_test_bgm),
    U16_TABLE(snd_test_env), U16_TABLE(snd_test_nakigoe), U16_TABLE(snd_test_se),
    U16_TABLE(tutorialdata),
};
#undef U16_TABLE
const FieldDesc kRgbaValue[] = {{"rgba", FIELD_U8, 4, 0, 0, 0, 0}};
const TableDesc kMsgPalette = {"msg_palette", 4, 4, kRgbaValue, 1};

#define ID(member) offsetof(ItemDataBiosRef, member)
const FieldDesc kItemDataFields[] = {
    {"kind", FIELD_U8, 1, 0x00, ID(kind)},
    {"important", FIELD_U8, 1, 0x01, ID(important)},
    {"useful", FIELD_U8, 1, 0x02, ID(useful)},
    {"field_03", FIELD_U8, 1, 0x03, ID(field_03)},
    {"itemEffectParam", FIELD_U8, 1, 0x04, ID(itemEffectParam)},
    {"padding_05", FIELD_U8, 1, 0x05, ID(padding_05)},
    {"price", FIELD_U16, 1, 0x06, ID(price)},
    {"coupon", FIELD_U16, 1, 0x08, ID(coupon)},
    {"itemSoubiDataId", FIELD_U16, 1, 0x0A, ID(itemSoubiDataId)},
    {"fightUseKoukaDataId", FIELD_U16, 1, 0x0C, ID(fightUseKoukaDataId)},
    {"padding_0E", FIELD_U16, 1, 0x0E, ID(padding_0E)},
    {"name", FIELD_U32, 1, 0x10, ID(name)},
    {"doc", FIELD_U32, 1, 0x14, ID(doc)},
    {"buff", FIELD_U32, 1, 0x18, ID(buff)},
    {"fieldUseFunc", FIELD_ADDR32, 1, 0x1C, ID(fieldUseFunc)},
    {"battleUseFunc", FIELD_ADDR32, 1, 0x20, ID(battleUseFunc)},
    {"useFriend", FIELD_U8, 3, 0x24, ID(useFriend)},
    {"padding_27", FIELD_U8, 1, 0x27, ID(padding_27)},
};
#undef ID
const TableDesc kItemData = {"item_data_prime", 0x28, sizeof(ItemDataBiosRef), kItemDataFields,
                             sizeof(kItemDataFields) / sizeof(kItemDataFields[0])};

#define WD(member) offsetof(WazaData, member)
const FieldDesc kWazaDataFields[] = {
    {"pri", FIELD_U8, 1, 0x00, WD(pri)},
    {"pp", FIELD_U8, 1, 0x01, WD(pp)},
    {"zokuseiDataId", FIELD_U8, 1, 0x02, WD(zokuseiDataId)},
    {"rangeId", FIELD_U8, 1, 0x03, WD(rangeId)},
    {"avg", FIELD_U8, 1, 0x04, WD(avg)},
    {"addFightKoukaAvg", FIELD_U8, 1, 0x05, WD(addFightKoukaAvg)},
    {"dageki", FIELD_U8, 1, 0x06, WD(dageki)},
    {"mamoru", FIELD_U8, 1, 0x07, WD(mamoru)},
    {"majikku", FIELD_U8, 1, 0x08, WD(majikku)},
    {"yokodori", FIELD_U8, 1, 0x09, WD(yokodori)},
    {"oumugaesi", FIELD_U8, 1, 0x0A, WD(oumugaesi)},
    {"oujanosirusi", FIELD_U8, 1, 0x0B, WD(oujanosirusi)},
    {"yubiwohuru", FIELD_U8, 1, 0x0C, WD(yubiwohuru)},
    {"monomane", FIELD_U8, 1, 0x0D, WD(monomane)},
    {"nekonote", FIELD_U8, 1, 0x0E, WD(nekonote)},
    {"negoto", FIELD_U8, 1, 0x0F, WD(negoto)},
    {"bouon", FIELD_U8, 1, 0x10, WD(bouon)},
    {"pressure", FIELD_U8, 1, 0x11, WD(pressure)},
    {"hidenFlag", FIELD_U8, 1, 0x12, WD(hidenFlag)},
    {"darkWazaKind", FIELD_U8, 1, 0x13, WD(darkWazaKind)},
    {"riskFlag", FIELD_U8, 1, 0x14, WD(riskFlag)},
    {"compatible", FIELD_U8, 1, 0x15, WD(compatible)},
    {"unk16", FIELD_U8, 2, 0x16, WD(unk16)},
    {"iryoku", FIELD_U16, 1, 0x18, WD(iryoku)},
    {"fightKoukaDataId", FIELD_U16, 1, 0x1A, WD(fightKoukaDataId)},
    {"seqId", FIELD_U16, 1, 0x1C, WD(seqId)},
    {"wazaFuncDataId", FIELD_U16, 1, 0x1E, WD(wazaFuncDataId)},
    {"name", FIELD_U32, 1, 0x20, WD(name)},
    {"fightAttackTunagiMsgId", FIELD_U32, 1, 0x24, WD(fightAttackTunagiMsgId)},
    {"fightAttackMsgId", FIELD_U32, 1, 0x28, WD(fightAttackMsgId)},
    {"doc", FIELD_U32, 1, 0x2C, WD(doc)},
    {"wazawzxdataId", FIELD_U32, 1, 0x30, WD(wazawzxdataId)},
    {"typeId", FIELD_U8, 3, 0x34, WD(typeId)},
    {"pad37", FIELD_U8, 1, 0x37, WD(pad37)},
};
#undef WD
const TableDesc kWazaData = {"waza_data", 0x38, sizeof(WazaData), kWazaDataFields,
                             sizeof(kWazaDataFields) / sizeof(kWazaDataFields[0])};

#define SD(member) offsetof(PokemonSeikakuDataBios, member)
const FieldDesc kSeikakuFields[] = {
    {"reliveFightout", FIELD_U8, 1, 0x00, SD(reliveFightout)},
    {"reliveWalk", FIELD_U8, 1, 0x01, SD(reliveWalk)},
    {"reliveCall", FIELD_U8, 1, 0x02, SD(reliveCall)},
    {"reliveSodateya", FIELD_U8, 1, 0x03, SD(reliveSodateya)},
    {"reliveNadenade", FIELD_U8, 1, 0x04, SD(reliveNadenade)},
    {"phyAtkRateDataId", FIELD_U8, 1, 0x05, SD(phyAtkRateDataId)},
    {"phyDefRateDataId", FIELD_U8, 1, 0x06, SD(phyDefRateDataId)},
    {"speAtkRateDataId", FIELD_U8, 1, 0x07, SD(speAtkRateDataId)},
    {"speDefRateDataId", FIELD_U8, 1, 0x08, SD(speDefRateDataId)},
    {"nimblenessRateDataId", FIELD_U8, 1, 0x09, SD(nimblenessRateDataId)},
    {"unk0A", FIELD_U8, 0x14 - 0x0A, 0x0A, SD(unk0A)},
    {"name", FIELD_U32, 1, 0x14, SD(name)},
    {"unk18", FIELD_U8, 0x28 - 0x18, 0x18, SD(unk18)},
};
#undef SD
const TableDesc kSeikakuData = {"pokemon_seikaku_data", 0x28, sizeof(PokemonSeikakuDataBios), kSeikakuFields,
                                sizeof(kSeikakuFields) / sizeof(kSeikakuFields[0])};
const FieldDesc kSeikakuRateFields[] = {
    {"kake", FIELD_U8, 1, 0x00, offsetof(PokemonSeikakuRateBios, kake)},
    {"waru", FIELD_U8, 1, 0x01, offsetof(PokemonSeikakuRateBios, waru)},
};
const TableDesc kSeikakuRate = {"pokemon_seikaku_rate_data", 2, sizeof(PokemonSeikakuRateBios), kSeikakuRateFields, 2};

#define FE(member) offsetof(FightEncountData, member)
const FieldDesc kFightEncountFields[] = {
    {"fightKind", FIELD_U8, 1, 0x00, FE(fightKind)},
    {"trainer", FIELD_U8, 1, 0x01, FE(trainer)},
    {"pokemon", FIELD_U8, 1, 0x02, FE(pokemon)},
    {"entry", FIELD_U8, 1, 0x03, FE(entry)},
    {"zenmetuFlag", FIELD_U8, 1, 0x04, FE(zenmetuFlag)},
    {"unk5", FIELD_U8, 1, 0x05, FE(unk5)},
    {"fightFloorDataId", FIELD_U16, 1, 0x06, FE(fightFloorDataId)},
    {"diskId", FIELD_U16, 1, 0x08, FE(diskId)},
    {"unkA", FIELD_U8, 2, 0x0A, FE(unkA)},
    {"fightName", FIELD_U32, 1, 0x0C, FE(fightName)},
    {"bgmSndId", FIELD_U32, 1, 0x10, FE(bgmSndId)},
    {"wipeId", FIELD_U32, 1, 0x14, FE(wipeId)},
    {"syoukaiWzxDataId", FIELD_U32, 1, 0x18, FE(syoukaiWzxDataId)},
    {"trainerInfo.ids", FIELD_U32, 4, 0x1C, FE(trainerInfo) + offsetof(FightTrainerInfo, ids), 8,
     sizeof(FightTrainerInfo)},
    {"trainerInfo.gsInputDevice", FIELD_U32, 4, 0x20, FE(trainerInfo) + offsetof(FightTrainerInfo, gsInputDevice), 8,
     sizeof(FightTrainerInfo)},
};
#undef FE
const TableDesc kFightEncount = {"fight_encount_data", 0x3C, sizeof(FightEncountData), kFightEncountFields,
                                 sizeof(kFightEncountFields) / sizeof(kFightEncountFields[0])};

#define FF(member) offsetof(FightFloorData, member)
const FieldDesc kFightFloorFields[] = {
    {"tikeiDataId", FIELD_U8, 1, 0x00, FF(tikeiDataId)},
    {"unk1", FIELD_U8, 1, 0x01, FF(unk1)},
    {"floorDataId", FIELD_U16, 1, 0x02, FF(floorDataId)},
    {"name", FIELD_U32, 1, 0x04, FF(name)},
    {"syoukaiWzxDataId", FIELD_U32, 1, 0x08, FF(syoukaiWzxDataId)},
    {"bgmSndId", FIELD_U32, 1, 0x0C, FF(bgmSndId)},
    {"envSndId", FIELD_U32, 1, 0x10, FF(envSndId)},
    {"fightSideDataId", FIELD_U16, 2, 0x14, FF(fightSideDataId)},
};
#undef FF
const TableDesc kFightFloor = {"fight_floor_data", 0x18, sizeof(FightFloorData), kFightFloorFields,
                               sizeof(kFightFloorFields) / sizeof(kFightFloorFields[0])};

const FieldDesc kFightKindFields[] = {
    {"flags", FIELD_U8, 0x1C, 0x00, offsetof(FightKindData, backSaveDataFlag)},
    {"name", FIELD_U32, 1, 0x1C, offsetof(FightKindData, name)},
};
static_assert(offsetof(FightKindData, unk1A) + 2 == 0x1C && offsetof(FightKindData, backSaveDataFlag) == 0,
              "fight_kind_data's flag bytes are one run");
const TableDesc kFightKind = {"fight_kind_data", 0x20, sizeof(FightKindData), kFightKindFields, 2};

#define FS(member) offsetof(FightSideData, member)
const FieldDesc kFightSideFields[] = {
    {"yrot", FIELD_U16, 1, 0x00, FS(yrot)},
    {"unk2", FIELD_U8, 2, 0x02, FS(unk2)},
    {"fightTrainerStatusMenuDataId", FIELD_U32, 2, 0x04, FS(fightTrainerStatusMenuDataId)},
    {"fightoutPokemonStatusMenuDataId", FIELD_U32, 2, 0x0C, FS(fightoutPokemonStatusMenuDataId)},
};
#undef FS
const TableDesc kFightSide = {"fight_side_data", 0x14, sizeof(FightSideData), kFightSideFields,
                              sizeof(kFightSideFields) / sizeof(kFightSideFields[0])};

#define FK(member) offsetof(FightTrainerKindData, member)
const FieldDesc kFightTrainerKindFields[] = {
    {"syoukinBairitu", FIELD_U16, 1, 0x00, FK(syoukinBairitu)},
    {"unk2", FIELD_U8, 2, 0x02, FK(unk2)},
    {"prefixName", FIELD_U32, 1, 0x04, FK(prefixName)},
    {"bgmSndId", FIELD_U32, 1, 0x08, FK(bgmSndId)},
};
#undef FK
const TableDesc kFightTrainerKind = {"fight_trainer_kind_data", 0x0C, sizeof(FightTrainerKindData),
                                     kFightTrainerKindFields,
                                     sizeof(kFightTrainerKindFields) / sizeof(kFightTrainerKindFields[0])};

#define FT(member) offsetof(FightTypeData, member)
const FieldDesc kFightTypeFields[] = {
    {"type", FIELD_U8, 1, 0x00, FT(type)},
    {"entryPokemonNum", FIELD_U8, 1, 0x01, FT(entryPokemonNum)},
    {"fightoutPokemonNum", FIELD_U8, 1, 0x02, FT(fightoutPokemonNum)},
    {"unk3", FIELD_U8, 1, 0x03, FT(unk3)},
    {"name", FIELD_U32, 1, 0x04, FT(name)},
};
#undef FT
const TableDesc kFightType = {"fight_type_data", 8, sizeof(FightTypeData), kFightTypeFields,
                              sizeof(kFightTypeFields) / sizeof(kFightTypeFields[0])};

#define ZD(member) offsetof(ZokuseiData, member)
const FieldDesc kZokuseiFields[] = {
    {"wazaKind", FIELD_U8, 1, 0x00, ZD(wazaKind)},
    {"unk1", FIELD_U8, 1, 0x01, ZD(unk1)},
    {"spriteID", FIELD_U16, 1, 0x02, ZD(spriteID)},
    {"smallSpriteID", FIELD_U16, 1, 0x04, ZD(smallSpriteID)},
    {"unk6", FIELD_U8, 2, 0x06, ZD(unk6)},
    {"name", FIELD_U32, 1, 0x08, ZD(name)},
    {"wazaJoutai", FIELD_U16, 18, 0x0C, ZD(wazaJoutai)},
};
#undef ZD
const TableDesc kZokusei = {"zokusei_data", 0x30, sizeof(ZokuseiData), kZokuseiFields,
                            sizeof(kZokuseiFields) / sizeof(kZokuseiFields[0])};

#define WM(member) offsetof(WorldMapData, member)
const FieldDesc kWorldMapFields[] = {
    {"camera", FIELD_U16, 1, 0x00, WM(camera)},
    {"npc", FIELD_U16, 1, 0x02, WM(npc)},
    {"flag", FIELD_U32, 1, 0x04, WM(flag)},
    {"selectIndex", FIELD_U32, 1, 0x08, WM(selectIndex)},
    {"model", FIELD_U32, 1, 0x0C, WM(model)},
    {"snap", FIELD_U32, 1, 0x10, WM(snap)},
    {"motion", FIELD_U32, 1, 0x14, WM(motion)},
    {"pad18", FIELD_U32, 1, 0x18, WM(pad18)},
    {"floorID", FIELD_U32, 1, 0x1C, WM(floorID)},
    {"msgID", FIELD_U32, 1, 0x20, WM(msgID)},
};
#undef WM
const TableDesc kWorldMap = {"worldmapXD_data", 0x24, sizeof(WorldMapData), kWorldMapFields,
                             sizeof(kWorldMapFields) / sizeof(kWorldMapFields[0])};

#define WO(member) offsetof(WAZAOSHIE_DATA, member)
const FieldDesc kWazaoshieFields[] = {
    {"id", FIELD_U16, 1, 0x00, WO(id)},
    {"pad2", FIELD_U16, 1, 0x02, WO(pad2)},
    {"mission", FIELD_U32, 1, 0x04, WO(mission)},
    {"flag", FIELD_U32, 1, 0x08, WO(flag)},
};
#undef WO
const TableDesc kWazaoshie = {"wazaoshie_data", 0x0C, sizeof(WAZAOSHIE_DATA), kWazaoshieFields,
                              sizeof(kWazaoshieFields) / sizeof(kWazaoshieFields[0])};
const FieldDesc kMenuSeFields[] = {{"sndId", FIELD_U16, 6, 0x00, offsetof(MenuSe, sndId)}};
const TableDesc kMenuSe = {"menu_se", 0x0C, sizeof(MenuSe), kMenuSeFields, 1};

#define UT(member) offsetof(UnderTakerData, member)
const FieldDesc kUndertakerFields[] = {
    {"threshold..limit", FIELD_U16, 6, 0x00, UT(threshold)},
    {"flagStart..flagSide", FIELD_U32, 10, 0x0C, UT(flagStart)},
    {"places.weight", FIELD_U16, 7, 0x34, UT(places) + offsetof(UnderTakerPlace, weight), 8, sizeof(UnderTakerPlace)},
    {"places.id", FIELD_U16, 7, 0x36, UT(places) + offsetof(UnderTakerPlace, id), 8, sizeof(UnderTakerPlace)},
    {"places.msgID", FIELD_U32, 7, 0x38, UT(places) + offsetof(UnderTakerPlace, msgID), 8, sizeof(UnderTakerPlace)},
};
static_assert(UT(limit) == UT(threshold) + 10 && UT(flagSide) == UT(flagStart) + 36,
              "undertakerdata's u16 and u32 runs are contiguous");
#undef UT
const TableDesc kUndertaker = {"undertakerdata", 0x6C, sizeof(UnderTakerData), kUndertakerFields,
                               sizeof(kUndertakerFields) / sizeof(kUndertakerFields[0])};

static_assert(offsetof(ReliveHallData, parts) == 6, "part numbers follow the 6-byte header, as on the GameCube");
#define RH(member) offsetof(ReliveHallData, member)
const FieldDesc kReliveHallFields[] = {
    {"alpha", FIELD_U8, 1, 0x00, RH(alpha)},
    {"unk1", FIELD_U8, 1, 0x01, RH(unk1)},
    {"steps", FIELD_U16, 1, 0x02, RH(steps)},
    {"scale", FIELD_U16, 1, 0x04, RH(scale)},
    {"parts", FIELD_U8, 68, 0x06, RH(parts)},
};
#undef RH
const TableDesc kReliveHall = {"relivehall_data", 6 + 68, sizeof(ReliveHallData), kReliveHallFields, 5};

const FieldDesc kCharNameFields[] = {
    {"hearFlag", FIELD_U16, 1, 0x00, offsetof(CharName, hearFlag)},
    {"unk2", FIELD_U8, 2, 0x02, offsetof(CharName, unk2)},
    {"nameID", FIELD_U32, 1, 0x04, offsetof(CharName, nameID)},
};
const TableDesc kCharName = {"char_name", 8, sizeof(CharName), kCharNameFields, 3};

#define FV(member) offsetof(FloorEventEntry, member)
const FieldDesc kFloorEventFields[] = {
    {"type", FIELD_U8, 1, 0x00, FV(type)},
    {"pad1", FIELD_U8, 1, 0x01, FV(pad1)},
    {"floorID", FIELD_U16, 1, 0x02, FV(floorID)},
    {"arg", FIELD_U32, 1, 0x04, FV(arg)},
    {"script", FIELD_U32, 1, 0x08, FV(script)},
    {"args", FIELD_U32, 4, 0x0C, FV(args)},
};
#undef FV
const TableDesc kFloorEvent = {"floor_event_list", 0x1C, sizeof(FloorEventEntry), kFloorEventFields,
                               sizeof(kFloorEventFields) / sizeof(kFloorEventFields[0])};

const FieldDesc kMsgSeFields[] = {{"a", FIELD_U16, 1, 0x00, offsetof(MsgSE, a)}, {"b", FIELD_U16, 1, 0x02, offsetof(MsgSE, b)}};
const TableDesc kMsgSe = {"message_se", 4, sizeof(MsgSE), kMsgSeFields, 2};

const FieldDesc kMenuAnimFields[] = {
    {"id", FIELD_U8, 1, 0x00, offsetof(PokemonMenuAnim, id)},
    {"pad", FIELD_U8, 1, 0x01, offsetof(PokemonMenuAnim, pad)},
    {"index", FIELD_U16, 1, 0x02, offsetof(PokemonMenuAnim, index)},
};
const TableDesc kMenuAnim = {"pokemon_menu_animation", 4, sizeof(PokemonMenuAnim), kMenuAnimFields, 3};

const FieldDesc kRibbonFields[] = {
    {"unk0", FIELD_U32, 1, 0x00, offsetof(RibbonData, unk0)},
    {"msgID", FIELD_U32, 1, 0x04, offsetof(RibbonData, msgID)},
};
const TableDesc kRibbon = {"ribbon_data", 8, sizeof(RibbonData), kRibbonFields, 2};

const FieldDesc kAiValueAddsubFields[] = {
    {"value", FIELD_U32, 1, 0x00, 0x00},     {"koudouName", FIELD_U32, 1, 0x04, 0x04},
    {"prefixName", FIELD_U32, 1, 0x08, 0x08}, {"name", FIELD_U32, 1, 0x0C, 0x0C},
    {"unk10", FIELD_U32, 1, 0x10, 0x10},
};
const TableDesc kAiValueAddsub = {"fight_trainer_ai_value_addsub_data", 0x14, 0x14, kAiValueAddsubFields, 5};
const FieldDesc kTrainerPokemonPartFields[] = {
    {"name", FIELD_U32, 1, 0x00, 0x00},
    {"unk4", FIELD_U8, 4, 0x04, 0x04},
    {"wazaTypeRevise", FIELD_U8, 14, 0x08, 0x08},
    {"unk16", FIELD_U8, 2, 0x16, 0x16},
};
const TableDesc kTrainerPokemonPart = {"fight_trainer_pokemon_part_data", 0x18, 0x18, kTrainerPokemonPartFields, 4};

#define FS(member) offsetof(floorsound_dd_, member)
const FieldDesc kFloorSoundFields[] = {
    {"kind", FIELD_U8, 1, 0x00, FS(kind)},
    {"volume", FIELD_U8, 1, 0x01, FS(volume)},
    {"floorId..flagId", FIELD_U16, 5, 0x02, FS(floorId)},
};
static_assert(FS(flagId) == FS(floorId) + 8, "floorsoundTbl's u16 run is contiguous");
#undef FS
const TableDesc kFloorSound = {"floorsoundTbl", 0x0C, sizeof(floorsound_dd_), kFloorSoundFields, 3};

#define SN(member) offsetof(SndData, member)
const FieldDesc kSndDataFields[] = {
    {"group/reverb (bits)", FIELD_U8, 1, 0x00, SN(vol) - 1},
    {"vol", FIELD_U8, 1, 0x01, SN(vol)},
    {"env..f6 (bits)", FIELD_U8, 1, 0x02, SN(b3) - 1},
    {"b3", FIELD_U8, 1, 0x03, SN(b3)},
    {"fxid", FIELD_U16, 1, 0x04, SN(fxid)},
    {"file", FIELD_U16, 1, 0x06, SN(file)},
    {"work", FIELD_PTR, 1, 0x08, SN(work)},
};
static_assert(SN(vol) == 1 && SN(b3) == 3, "snd_data's bitfield bytes are 0 and 2");
#undef SN
const TableDesc kSndData = {"snd_data", 0x0C, sizeof(SndData), kSndDataFields,
                            sizeof(kSndDataFields) / sizeof(kSndDataFields[0])};
const FieldDesc kSndFileFields[] = {{"a,b,c", FIELD_U32, 3, 0x00, offsetof(SndFile, a)}};
static_assert(offsetof(SndFile, c) == 8, "snd_file is three u32");
const TableDesc kSndFile = {"snd_file", 0x0C, sizeof(SndFile), kSndFileFields, 1};
const FieldDesc kSndWaveFields[] = {
    {"b0", FIELD_U8, 1, 0x00, offsetof(SndWaveData, b0)},
    {"loaded", FIELD_U8, 1, 0x01, offsetof(SndWaveData, loaded)},
    {"gid", FIELD_U16, 1, 0x02, offsetof(SndWaveData, gid)},
    {"pad[0..3]", FIELD_U32, 4, 0x04, offsetof(SndWaveData, pad)},
    {"pad[4] (file name)", FIELD_ADDR32, 1, 0x14, offsetof(SndWaveData, pad) + 16},
};
const TableDesc kSndWave = {"snd_wave_data", 0x18, sizeof(SndWaveData), kSndWaveFields, 5};
const FieldDesc kSndRevFields[] = {{"coloration..crosstalk", FIELD_U32, 6, 0x00, offsetof(SndRevData, coloration)}};
static_assert(offsetof(SndRevData, crosstalk) == 0x14, "snd_rev_data is six f32");
const TableDesc kSndRev = {"snd_rev_data", 0x18, sizeof(SndRevData), kSndRevFields, 1};

#define FD(member) offsetof(FlagData, member)
const FieldDesc kFlagDataFields[] = {
    {"level/length (bits)", FIELD_U8, 1, 0x00, FD(unk1) - 1},
    {"unk1", FIELD_U8, 1, 0x01, FD(unk1)},
    {"pos", FIELD_U16, 1, 0x02, FD(pos)},
    {"next", FIELD_U16, 1, 0x04, FD(next)},
};
static_assert(FD(unk1) == 1, "flag_data's bitfield byte is 0");
#undef FD
const TableDesc kFlagData = {"flag_data", 6, sizeof(FlagData), kFlagDataFields, 4};
const FieldDesc kFlagLevelFields[] = {
    {"numWords", FIELD_U32, 1, 0x00, offsetof(FlagLevel, numWords)},
    {"bits", FIELD_PTR, 1, 0x04, offsetof(FlagLevel, bits)},
};
const TableDesc kFlagLevel = {"flag_level", 8, sizeof(FlagLevel), kFlagLevelFields, 2};
#define FX(member) offsetof(FlagEtc, member)
const FieldDesc kFlagEtcFields[] = {
    {"type", FIELD_U8, 1, 0x00, FX(type)},
    {"unk1", FIELD_U8, 1, 0x01, FX(unk1)},
    {"floor", FIELD_U16, 1, 0x02, FX(floor)},
    {"pokedoru", FIELD_U16, 1, 0x04, FX(pokedoru)},
    {"unk6", FIELD_U8, 2, 0x06, FX(unk6)},
    {"flagValue", FIELD_U32, 1, 0x08, FX(flagValue)},
    {"script", sizeof(FlagEtc::script) == 4 ? FIELD_U32 : FIELD_U32_IN_PTR, 1, 0x0C, FX(script)},
    {"x", FIELD_U32, 1, 0x10, FX(x)},
    {"y", FIELD_U32, 1, 0x14, FX(y)},
    {"z", FIELD_U32, 1, 0x18, FX(z)},
    {"msg", sizeof(FlagEtc::msg) == 4 ? FIELD_U32 : FIELD_U32_IN_PTR, 1, 0x1C, FX(msg)},
};
#undef FX
const TableDesc kFlagEtc = {"flag_etc", 0x20, sizeof(FlagEtc), kFlagEtcFields,
                            sizeof(kFlagEtcFields) / sizeof(kFlagEtcFields[0])};

#define FL(member) offsetof(FloorData, member)
const FieldDesc kFloorDataFields[] = {
    {"area", FIELD_U8, 1, 0x00, FL(area)},
    {"kind", FIELD_U8, 1, 0x01, FL(kind)},
    {"floorID", FIELD_U16, 1, 0x02, FL(floorID)},
    {"mapResID", FIELD_U32, 1, 0x04, FL(mapResID)},
    {"gfx", FIELD_PTR, 1, 0x08, FL(gfx)},
    {"chars", FIELD_PTR, 1, 0x0C, FL(chars)},
    {"positions", FIELD_PTR, 1, 0x10, FL(positions)},
    {"fieldCamera", FIELD_PTR, 1, 0x14, FL(fieldCamera)},
    {"placeName", FIELD_U32, 1, 0x18, FL(placeName)},
    {"preFunc", FIELD_ADDR32, 1, 0x1C, FL(preFunc)},
    {"mainFunc", FIELD_ADDR32, 1, 0x20, FL(mainFunc)},
    {"postFunc", FIELD_ADDR32, 1, 0x24, FL(postFunc)},
    {"offscreen", FIELD_PTR, 1, 0x28, FL(offscreen)},
    {"groupID", FIELD_U32, 5, 0x2C, FL(groupID)},
};
#undef FL
const TableDesc kFloorData = {"floordata", 0x40, sizeof(FloorData), kFloorDataFields,
                              sizeof(kFloorDataFields) / sizeof(kFloorDataFields[0])};
#define FT2(member) offsetof(FloorTresure, member)
const FieldDesc kFloorTresureFields[] = {
    {"kind/type (bits)", FIELD_U8, 1, 0x00, FT2(count) - 1},
    {"count", FIELD_U8, 1, 0x01, FT2(count)},
    {"angle..flagB", FIELD_U16, 4, 0x02, FT2(angle)},
    {"unkA", FIELD_U8, 2, 0x0A, FT2(unkA)},
    {"item", FIELD_U32, 1, 0x0C, FT2(item)},
    {"x,y,z", FIELD_U32, 3, 0x10, FT2(x)},
};
static_assert(FT2(count) == 1 && FT2(flagB) == FT2(angle) + 6 && FT2(z) == FT2(x) + 8,
              "floor_tresure_list's runs are contiguous");
#undef FT2
const TableDesc kFloorTresure = {"floor_tresure_list", 0x1C, sizeof(FloorTresure), kFloorTresureFields,
                                 sizeof(kFloorTresureFields) / sizeof(kFloorTresureFields[0])};
#define DR(member) offsetof(FloorDoor, member)
const FieldDesc kFloorDoorFields[] = {
    {"openAnim..pad9", FIELD_U8, 10, 0x00, DR(openAnim)},
    {"collisionId..closeFlag", FIELD_U16, 4, 0x0A, DR(collisionId)},
    {"pad12", FIELD_U8, 2, 0x12, DR(pad12)},
    {"resourceId", FIELD_U32, 1, 0x14, DR(resourceId)},
};
static_assert(DR(pad9) == 9 && DR(closeFlag) == DR(collisionId) + 6, "floor_door_list's runs are contiguous");
#undef DR
const TableDesc kFloorDoor = {"floor_door_list", 0x18, sizeof(FloorDoor), kFloorDoorFields, 4};

#define DV(member) offsetof(DeckDataVirtual, member)
const FieldDesc kDeckVirtualFields[] = {
    {"unk0..unk3", FIELD_U8, 4, 0x00, DV(unk0)},
    {"fightFloorDataId", FIELD_U16, 1, 0x04, DV(fightFloorDataId)},
    {"unk6", FIELD_U8, 2, 0x06, DV(unk6)},
    {"allyDeck..flag", FIELD_U32, 6, 0x08, DV(allyDeck)},
    {"allyJoutai, enemyJoutai", FIELD_U8, 12, 0x20, DV(allyJoutai)},
    {"items.num", FIELD_U8, 4, 0x2C, DV(items) + offsetof(DeckDataVirtualItem, num), 4, sizeof(DeckDataVirtualItem)},
    {"items.unk1", FIELD_U8, 4, 0x2D, DV(items) + offsetof(DeckDataVirtualItem, unk1), 4, sizeof(DeckDataVirtualItem)},
    {"items.id", FIELD_U16, 4, 0x2E, DV(items) + offsetof(DeckDataVirtualItem, id), 4, sizeof(DeckDataVirtualItem)},
};
static_assert(DV(unk3) == 3 && DV(fightFloorDataId) == 4 && DV(allyDeck) == 8 && DV(flag) == 0x1C && DV(enemyJoutai) == 0x26, "deckdata_virtual's runs are contiguous");
#undef DV
const TableDesc kDeckVirtual = {"deckdata_virtual", 0x3C, sizeof(DeckDataVirtual), kDeckVirtualFields,
                                sizeof(kDeckVirtualFields) / sizeof(kDeckVirtualFields[0])};

const FieldDesc kEsaDataFields[] = {
    {"flagNum..flag14", FIELD_U32, 6, 0x00, offsetof(EsaData, flagNum)},
    {"table", FIELD_PTR, 1, 0x18, offsetof(EsaData, table)},
};
static_assert(offsetof(EsaData, flag14) == 0x14, "esabadata's u32 run is contiguous");
const TableDesc kEsaData = {"esabadata", 0x1C, sizeof(EsaData), kEsaDataFields, 2};
const FieldDesc kEsaTableFields[] = {
    {"head", FIELD_PTR, 1, 0x00, offsetof(EsaTable, head)},
    {"entries", FIELD_PTR, 1, 0x04, offsetof(EsaTable, entries)},
};
#define ESA_TOP(n) {n, 8, sizeof(EsaTable), kEsaTableFields, 2}
const FieldDesc kEsaEntryFields[] = {
    {"minLevel", FIELD_U8, 1, 0x00, offsetof(EsaEntry, minLevel)},
    {"maxLevel", FIELD_U8, 1, 0x01, offsetof(EsaEntry, maxLevel)},
    {"id", FIELD_U16, 1, 0x02, offsetof(EsaEntry, id)},
    {"prob, rate", FIELD_U32, 2, 0x04, offsetof(EsaEntry, prob)},
};
#define ESA_LIST(n) {n, 0x0C, sizeof(EsaEntry), kEsaEntryFields, 4}
const TableDesc kEsaLists[] = {
    ESA_TOP("esabapokemondata_E1_top"), ESA_TOP("esabapokemondata_E2_top"), ESA_TOP("esabapokemondata_E3_top"),
    ESA_TOP("esabapokemondata_EX_top"), ESA_LIST("esabapokemondata_E1"), ESA_LIST("esabapokemondata_E2"),
    ESA_LIST("esabapokemondata_E3"), ESA_LIST("esabapokemondata_EX"),
};
#undef ESA_TOP
#undef ESA_LIST
const FieldDesc kEsaAppFields[] = {
    {"limitA..pad", FIELD_U16, 6, 0x00, offsetof(EsaAppData, limitA)},
    {"unkC, flagKind, flagCheck", FIELD_U32, 3, 0x0C, offsetof(EsaAppData, unkC)},
};
static_assert(offsetof(EsaAppData, unkC) == 0x0C, "esabaappdata's runs are contiguous");
const TableDesc kEsaApp = {"esabaappdata", 0x18, sizeof(EsaAppData), kEsaAppFields, 2};

const FieldDesc kWorldMapMsgFields[] = {
    {"etc", FIELD_U32, 1, 0x00, offsetof(WorldMapMsgData, etc)},
    {"msg", FIELD_U32, 18, 0x04, offsetof(WorldMapMsgData, msg)},
};
const TableDesc kWorldMapMsg = {"worldmapXD_msgdata", 0x4C, sizeof(WorldMapMsgData), kWorldMapMsgFields, 2};
const FieldDesc kWorldMapCameraFields[] = {
    {"pos, a, b, c, fov", FIELD_U32, 7, 0x00, offsetof(WorldMapCameraData, pos)},
};
static_assert(sizeof(WorldMapCameraData) == 0x1C, "worldmap_cameradata: 7 floats");
const TableDesc kWorldMapCamera = {"worldmap_cameradata", 0x1C, sizeof(WorldMapCameraData), kWorldMapCameraFields, 1};
const FieldDesc kShopDataFields[] = {
    {"unk0", FIELD_U8, 1, 0x00, offsetof(SHOP_DATA, unk0)},
    {"mode", FIELD_U8, 1, 0x01, offsetof(SHOP_DATA, mode)},
    {"goodsIdx", FIELD_U16, 1, 0x02, offsetof(SHOP_DATA, goodsIdx)},
};
const TableDesc kShopData = {"shopData", 4, sizeof(SHOP_DATA), kShopDataFields, 3};
const FieldDesc kShopExchangerFields[] = {
    {"se", FIELD_U8, 1, 0x00, offsetof(SHOP_EXCHANGER, se)},
    {"pad", FIELD_U8, 3, 0x01, offsetof(SHOP_EXCHANGER, pad)},
    {"msg", FIELD_U32, 18, 0x04, offsetof(SHOP_EXCHANGER, msg)},
};
const TableDesc kShopExchanger = {"shopExchanger", 0x4C, sizeof(SHOP_EXCHANGER), kShopExchangerFields, 3};
const FieldDesc kShopSalesmanFields[] = {
    {"se", FIELD_U8, 1, 0x00, offsetof(SHOP_SALESMAN, se)},
    {"pad", FIELD_U8, 3, 0x01, offsetof(SHOP_SALESMAN, pad)},
    {"msg", FIELD_U32, 14, 0x04, offsetof(SHOP_SALESMAN, msg)},
};
const TableDesc kShopSalesman = {"shopSalesman", 0x3C, sizeof(SHOP_SALESMAN), kShopSalesmanFields, 3};
const FieldDesc kMailFields[] = {
    {"senderName, subject, contents, attachFileGroup, openFlag", FIELD_U32, 5, 0x00, offsetof(MailData, senderName)},
};
static_assert(sizeof(MailData) == 0x14, "mailData: 5 numbers");
const TableDesc kMail = {"mailData", 0x14, sizeof(MailData), kMailFields, 1};
const FieldDesc kTrainerFaceFields[] = {
    {"id", FIELD_U16, 1, 0x00, offsetof(ToolentryFaceData, id)},
    {"unk2", FIELD_U8, 2, 0x02, offsetof(ToolentryFaceData, unk2)},
    {"bigFace", FIELD_U32, 2, 0x04, offsetof(ToolentryFaceData, bigFace)},
    {"smallFace", FIELD_U32, 2, 0x0C, offsetof(ToolentryFaceData, smallFace)},
};
const TableDesc kTrainerFace = {"trainer_face_data", 0x14, sizeof(ToolentryFaceData), kTrainerFaceFields, 4};

const FieldDesc kPokeFaceFields[] = {
    {"rareFlag (bits)", FIELD_U8, 1, 0x00, offsetof(MenuFaceData, unk1) - 1},
    {"unk1", FIELD_U8, 3, 0x01, offsetof(MenuFaceData, unk1)},
    {"texID", FIELD_U32, 1, 0x04, offsetof(MenuFaceData, texID)},
};
static_assert(offsetof(MenuFaceData, unk1) == 1, "poke_face's bitfield byte is 0");
const TableDesc kPokeFace = {"poke_face", 8, sizeof(MenuFaceData), kPokeFaceFields, 3};

#define BG(member) offsetof(BINGO_DATA_dd_, member)
#define BP(field, kind, gc) {"panel." #field, kind, 14, 0x24 + (gc), BG(panel) + offsetof(BINGO_PANEL_dd_, field), 10, \
                             sizeof(BINGO_PANEL_dd_)}
const FieldDesc kBingoFields[] = {
    {"no..unk6", FIELD_U8, 0x10, 0x00, BG(no)},
    {"score", FIELD_U16, 10, 0x10, BG(score)},
    BP(type, FIELD_U8, 0), BP(tokusei, FIELD_U8, 1), BP(seikaku, FIELD_U8, 2), BP(sex, FIELD_U8, 3),
    BP(no, FIELD_U16, 4), BP(waza, FIELD_U16, 6), BP(hosei, FIELD_U16, 8),
    {"grid", FIELD_U8, 6, 0xB0, BG(grid)},
    {"unkB6", FIELD_U8, 2, 0xB6, BG(unkB6)},
};
static_assert(BG(score) == 0x10 && sizeof(BINGO_PANEL_dd_) == 10 && sizeof(BINGO_GRID_dd_) == 2,
              "bingo_data's layout");
#undef BP
#undef BG
const TableDesc kBingo = {"bingo_data", 0xB8, sizeof(BINGO_DATA_dd_), kBingoFields,
                          sizeof(kBingoFields) / sizeof(kBingoFields[0])};

#define PI(member) offsetof(PeopleInfo, member)
const FieldDesc kPeopleFields[] = {
    {"talkSe.. (bits)", FIELD_U8, 1, 0x00, PI(neckIndex) - 1},
    {"neckIndex, chestIndex, unk3", FIELD_U8, 3, 0x01, PI(neckIndex)},
    {"modelResID", FIELD_U32, 1, 0x04, PI(modelResID)},
    {"walkLength..talkDistance", FIELD_U32, 8, 0x08, PI(walkLength)},
    {"speakParts, blinkParts, unk33", FIELD_U8, 12, 0x28, PI(speakParts)},
};
static_assert(PI(neckIndex) == 1 && PI(talkDistance) == 0x24 && PI(unk33) == 0x33, "peopleInfoData's runs are contiguous");
#undef PI
const TableDesc kPeople = {"peopleInfoData", 0x34, sizeof(PeopleInfo), kPeopleFields, 5};

const FieldDesc kTableResFields[] = {{"resource", FIELD_PTR, 1, 0, 0}};
const TableDesc kTableRes = {"table_res", 4, sizeof(void*), kTableResFields, 1};
const TableDesc kDpFilter = {"pokemon_dp_filter_data", 1, 1, kU8Value, 1};

const TableDesc* const kDescribed[] = {
    &kPokemonData, &kU16Tables[0], &kU16Tables[1], &kU16Tables[2], &kU16Tables[3], &kU16Tables[4],
    &kU16Tables[5], &kU16Tables[6], &kU16Tables[7], &kU16Tables[8], &kU16Tables[9], &kMsgPalette,
    &kItemData, &kWazaData, &kSeikakuData, &kSeikakuRate, &kFightEncount, &kFightFloor, &kFightKind,
    &kFightSide, &kFightTrainerKind, &kFightType, &kZokusei, &kWorldMap, &kWazaoshie, &kMenuSe,
    &kUndertaker, &kReliveHall, &kCharName, &kFloorEvent, &kMsgSe, &kMenuAnim, &kRibbon, &kFloorSound, &kSndData,
    &kSndFile, &kSndWave, &kSndRev, &kFlagData, &kFlagLevel, &kFlagEtc, &kFloorData, &kFloorTresure, &kFloorDoor,
    &kDeckVirtual, &kEsaData, &kEsaLists[0], &kEsaLists[1], &kEsaLists[2], &kEsaLists[3], &kEsaLists[4], &kEsaLists[5],
    &kEsaLists[6], &kEsaLists[7], &kEsaApp, &kPokeFace, &kBingo, &kPeople,
    &kTableRes, &kDpFilter, &kWorldMapMsg, &kWorldMapCamera, &kShopData, &kShopExchanger, &kShopSalesman,
    &kMail, &kTrainerFace, &kAiValueAddsub, &kTrainerPokemonPart,
};

const TableDesc* described(const char* name) {
  for (const TableDesc* d : kDescribed) {
    if (std::strcmp(d->name, name) == 0) {
      return d;
    }
  }
  return nullptr;
}

struct Guarded {
  const char* name;
  uint8_t* start;
  size_t size;
};
std::vector<Guarded> sGuarded;

#ifdef _WIN32
#define GUARD_NOT_MINE return EXCEPTION_CONTINUE_SEARCH
LONG CALLBACK guard_handler(EXCEPTION_POINTERS* info) {
  const EXCEPTION_RECORD* e = info->ExceptionRecord;
  if (e->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || e->NumberParameters < 2) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  const auto* addr = reinterpret_cast<const uint8_t*>(e->ExceptionInformation[1]);
#else
struct sigaction sOldSegv;
#ifdef __APPLE__
struct sigaction sOldBus;
#define GUARD_OLD (guardSignal == SIGBUS ? sOldBus : sOldSegv)
#define GUARD_SIGNAL guardSignal
#else
#define GUARD_OLD sOldSegv
#define GUARD_SIGNAL SIGSEGV
#endif
#define GUARD_NOT_MINE \
  do { \
    if ((GUARD_OLD.sa_flags & SA_SIGINFO) != 0 && GUARD_OLD.sa_sigaction != nullptr) { \
      GUARD_OLD.sa_sigaction(guardSignal, info, guardContext); \
    } else { \
      sigaction(GUARD_SIGNAL, &GUARD_OLD, nullptr); \
    } \
    return; \
  } while (0)
void guard_handler(int guardSignal, siginfo_t* info, void* guardContext) {
  const auto* addr = static_cast<const uint8_t*>(info->si_addr);
#endif
  for (const Guarded& g : sGuarded) {
    if (addr >= g.start && addr < g.start + g.size) {
      std::fprintf(stderr,
                   "[games/xd] The game used the data table '%s' (byte %zu), which the port doesn't convert yet.\n"
                   "           Describe it in games/xd/src/data/common_rel.cpp. Stopping here.\n",
                   g.name, static_cast<size_t>(addr - g.start));
      std::fflush(stderr);
      std::abort();
    }
  }
  GUARD_NOT_MINE;
}

uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

struct LowBytes {
  uint8_t* ptr = nullptr;
  size_t n = 0;
  uint8_t* data() const { return ptr; }
  size_t size() const { return n; }
};

struct Loaded {
  std::vector<uint8_t> relFile;
  RelModule rel{};
  std::vector<LowBytes> sectionCopies;
  uint8_t section = 0;
  uint32_t* counts = nullptr;
};

const XDMainProgramAddress* sMainMap = nullptr;
size_t sMainMapCount = 0;

int resolve_main(uint32_t gcAddress, void** out) {
  if (sMainMap == nullptr) {
    *out = reinterpret_cast<void*>(static_cast<uintptr_t>(gcAddress));
    return 1;
  }
  for (size_t i = 0; i < sMainMapCount; i++) {
    const XDMainProgramAddress& m = sMainMap[i];
    if (gcAddress >= m.gcAddress && gcAddress - m.gcAddress < m.size) {
      *out = static_cast<uint8_t*>(m.pc) + (gcAddress - m.gcAddress);
      return 1;
    }
  }
  return -1;
}

int resolve(void* user, uint32_t gcEntryOffset, uint32_t fieldGcOffset, void** out) {
  auto* l = static_cast<Loaded*>(user);
  const RelReloc* r = Rel_FindReloc(&l->rel, l->section, gcEntryOffset + fieldGcOffset);
  if (r == nullptr) {
    return 0;
  }
  if (r->type != REL_R_ADDR32) {
    return -1;
  }
  if (r->module == 0) {
    return resolve_main(r->addend, out);
  }
  if (r->module != l->rel.id || r->targetSection >= l->sectionCopies.size() ||
      r->addend >= l->sectionCopies[r->targetSection].size()) {
    return -1;
  }
  *out = l->sectionCopies[r->targetSection].data() + r->addend;
  return 1;
}

bool fail(char* error, size_t errorSize, const std::string& message) {
  std::snprintf(error, errorSize, "%s", message.c_str());
  return false;
}

void* convert_table(Loaded* l, const TableDesc* desc, const RelPrologStore& at, uint32_t count, std::string& problem) {
  const auto& section = l->sectionCopies[at.section];
  if (at.offset + (uint64_t)count * desc->gcSize > section.size()) {
    problem = std::string(desc->name) + " runs past the end of the module";
    return nullptr;
  }
  void* pc = PortMem_LowAlloc(static_cast<size_t>(count ? count : 1) * desc->pcSize, 16);
  l->section = at.section;
  const uint8_t* gc = section.data() + at.offset;
  const uint32_t unresolved = DataTable_Convert(desc, gc, at.offset, count, pc, resolve, l);
  if (unresolved != 0) {
    problem = std::string(desc->name) + ": " + std::to_string(unresolved) +
              " address fields hold values that aren't addresses (description doesn't match the data)";
    return nullptr;
  }
  const uint32_t differing = DataTable_VerifyRoundTrip(desc, gc, count, pc);
  if (differing != 0) {
    problem = std::string(desc->name) + ": " + std::to_string(differing) +
              " entries don't convert back to the original bytes";
    return nullptr;
  }
  return pc;
}

void put_be32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

void patch_section_addresses(Loaded* l) {
  for (uint32_t i = 0; i < l->rel.numRelocs; i++) {
    const RelReloc& r = l->rel.relocs[i];
    if (r.section >= l->sectionCopies.size() || l->rel.sections[r.section].code || r.type != REL_R_ADDR32) {
      continue;
    }
    LowBytes& copy = l->sectionCopies[r.section];
    if (copy.ptr == nullptr || r.offset + 4 > copy.size()) continue;
    void* target = nullptr;
    if (r.module == 0) {
      if (resolve_main(r.addend, &target) != 1) continue;
    } else if (r.module == l->rel.id && r.targetSection < l->sectionCopies.size() &&
               l->sectionCopies[r.targetSection].ptr != nullptr &&
               r.addend <= l->sectionCopies[r.targetSection].size()) {
      target = l->sectionCopies[r.targetSection].data() + r.addend;
    } else {
      continue;
    }
    put_be32(copy.ptr + r.offset, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)));
  }
}

}

extern "C" void PortXD_SetMainProgramMap(const XDMainProgramAddress* map, size_t count) {
  sMainMap = map;
  sMainMapCount = count;
}

extern "C" bool PortXD_ConvertCommonRel(const uint8_t* relFile, size_t relSize, XDCommonTables* out, char* error,
                                        size_t errorSize) {
  std::memset(out, 0, sizeof(*out));
  for (const TableDesc* d : kDescribed) {
    if (!DataTable_Validate(d, error, errorSize)) {
      return false;
    }
  }

  auto* l = new Loaded();
  l->relFile.assign(relFile, relFile + relSize);
  if (!Rel_Parse(l->relFile.data(), l->relFile.size(), &l->rel)) {
    delete l;
    return fail(error, errorSize, "common_rel is not a valid REL module");
  }
  for (uint32_t i = 0; i < l->rel.numSections; i++) {
    const RelSection& s = l->rel.sections[i];
    LowBytes copy;
    if (s.data != nullptr && s.size != 0) {
      copy.n = s.size;
      copy.ptr = static_cast<uint8_t*>(PortMem_LowAlloc(s.size, 32));
      std::memcpy(copy.ptr, s.data, s.size);
    }
    l->sectionCopies.push_back(copy);
  }

  RelPrologStore all[512];
  const int numAll = Rel_ReadPrologStores(&l->rel, all, 512);
  if (numAll < 0) {
    delete l;
    return fail(error, errorSize, "the module's start-up code isn't in the expected form");
  }
  RelPrologStore stores[XD_COMMON_MAX_GLOBALS];
  int n = 0;
  for (int i = 0; i < numAll; i++) {
    if (relglobal_name(all[i].dolAddress) == nullptr) continue;
    if (n == XD_COMMON_MAX_GLOBALS || !all[i].isRelAddress || all[i].section >= l->sectionCopies.size()) {
      delete l;
      return fail(error, errorSize, "the module stores into a table variable in a way the port doesn't expect");
    }
    stores[n++] = all[i];
  }

  l->counts = static_cast<uint32_t*>(PortMem_LowAlloc(sizeof(uint32_t) * (n > 0 ? n : 1), 4));
  auto count_of = [&](const char* tableName) -> const uint32_t* {
    const std::string want = std::string(tableName) + "_number";
    for (int i = 0; i < n; i++) {
      if (want == relglobal_name(stores[i].dolAddress)) {
        return &l->counts[i];
      }
    }
    return nullptr;
  };
  for (int i = 0; i < n; i++) {
    const char* name = relglobal_name(stores[i].dolAddress);
    if (ends_with(name, "_number")) {
      const auto& s = l->sectionCopies[stores[i].section];
      if (stores[i].offset + 4 > s.size()) {
        delete l;
        return fail(error, errorSize, std::string(name) + " points outside the module");
      }
      l->counts[i] = be32(s.data() + stores[i].offset);
    }
  }

  auto extent_of = [&](int i) -> uint32_t {
    uint32_t next = (uint32_t)l->sectionCopies[stores[i].section].size();
    for (int k = 0; k < n; k++) {
      if (stores[k].section == stores[i].section && stores[k].offset > stores[i].offset) {
        next = std::min(next, stores[k].offset);
      }
    }
    return next - stores[i].offset;
  };

  std::vector<int> undescribed;
  for (int i = 0; i < n; i++) {
    const char* name = relglobal_name(stores[i].dolAddress);
    XDRelGlobalValue& v = out->globals[out->numGlobals++];
    v.name = name;
    if (ends_with(name, "_number")) {
      v.value = &l->counts[i];
      v.converted = true;
    } else if (const TableDesc* desc = described(name)) {
      const uint32_t* count = count_of(name);
      std::string problem;
      v.value = convert_table(l, desc, stores[i], count ? *count : extent_of(i) / desc->gcSize, problem);
      if (v.value == nullptr) {
        delete l;
        return fail(error, errorSize, problem);
      }
      v.converted = true;
    } else {
      undescribed.push_back(i);
    }
  }
  if (!undescribed.empty()) {
    std::vector<size_t> sizes;
    size_t total = 0;
    for (int i : undescribed) {
      const size_t size = std::max<size_t>(16, extent_of(i));
      sizes.push_back(size);
      total += size;
    }
    total = (total + 0xFFFF) & ~size_t{0xFFFF};
    auto* block = static_cast<uint8_t*>(PortMem_LowAlloc(total, 0x10000));
#ifdef _WIN32
    DWORD oldProtect;
    if (!VirtualProtect(block, total, PAGE_NOACCESS, &oldProtect)) {
      delete l;
      return fail(error, errorSize, "Could not protect memory for unconverted tables");
    }
    static bool handlerInstalled = false;
    if (!handlerInstalled) {
      AddVectoredExceptionHandler(1, guard_handler);
      handlerInstalled = true;
    }
#else
    if (mprotect(block, total, PROT_NONE) != 0) {
      delete l;
      return fail(error, errorSize, "Could not protect memory for unconverted tables");
    }
    static bool handlerInstalled = false;
    if (!handlerInstalled) {
      struct sigaction action {};
      action.sa_sigaction = guard_handler;
      action.sa_flags = SA_SIGINFO | SA_ONSTACK;
      sigemptyset(&action.sa_mask);
      sigaction(SIGSEGV, &action, &sOldSegv);
#ifdef __APPLE__
      sigaction(SIGBUS, &action, &sOldBus);
#endif
      handlerInstalled = true;
    }
#endif
    size_t at = 0;
    for (size_t k = 0; k < undescribed.size(); k++) {
      const char* name = relglobal_name(stores[undescribed[k]].dolAddress);
      sGuarded.push_back({name, block + at, sizes[k]});
      for (uint32_t g = 0; g < out->numGlobals; g++) {
        if (out->globals[g].name == name) {
          out->globals[g].value = block + at;
        }
      }
      at += sizes[k];
    }
  }

  for (uint32_t g = 0; g < out->numGlobals; g++) {
    if (std::strcmp(out->globals[g].name, "pokemon_data") == 0) {
      out->pokemon_data = static_cast<PokemonData*>(out->globals[g].value);
    } else if (std::strcmp(out->globals[g].name, "pokemon_data_number") == 0) {
      out->pokemon_data_number = *static_cast<uint32_t*>(out->globals[g].value);
    }
  }
  out->tablesFound = (uint32_t)n;
  patch_section_addresses(l);
  (void)l;
  return true;
}

extern "C" bool PortXD_LoadCommonRel(XDCommonTables* out, char* error, size_t errorSize) {
  size_t size = 0;
  uint8_t* file = PortDisc_ReadFile("common.fsys", &size);
  if (file == nullptr) {
    return fail(error, errorSize, "common.fsys not found on the disc");
  }
  std::vector<uint8_t> fsysFile(file, file + size);
  PortDisc_Free(file);

  FsysArchive fsys;
  int index = -1;
  if (!Fsys_Open(fsysFile.data(), fsysFile.size(), &fsys) || (index = Fsys_Find(&fsys, "common_rel")) < 0) {
    return fail(error, errorSize, "common.fsys has no common_rel entry");
  }
  uint32_t relSize = 0;
  uint8_t* relData = Fsys_Extract(&fsys, (uint32_t)index, &relSize);
  if (relData == nullptr) {
    return fail(error, errorSize, "Could not unpack common_rel");
  }
  const bool ok = PortXD_ConvertCommonRel(relData, relSize, out, error, errorSize);
  std::free(relData);
  return ok;
}

extern "C" size_t PortXD_DescribedTables(const TableDesc* const** out) {
  *out = kDescribed;
  return sizeof(kDescribed) / sizeof(kDescribed[0]);
}
