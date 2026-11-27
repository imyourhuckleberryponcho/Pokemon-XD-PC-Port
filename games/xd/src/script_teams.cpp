#include "script_teams.h"

#include <dolphin/types.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>

class DeckPokemon {
public:
    static DeckPokemon* getDeckPokemonObj(u32 index);
};
class DeckTrainer {
public:
    static DeckTrainer* getDeckTrainerObj(u32 index);
};
class DeckDarkPokemon {
public:
    static DeckDarkPokemon* getDeckDarkPokemonObj(u32 index);
};

extern "C" {
u32 deckGetTrainerSize(void);
u8 fightTrainerDB_GetKindDataId(u16 id);
void* fightEncountDataBiosGetPtr(u16 id);
u16 fightEncountDataBiosGetFightTrainerDataId(void* data, u8 index);
u16 fightGetFightEncountDataId(void);
u8 fightTrainerCreateFightTrainerPokemonDataIdToPokemon(u16 id, void* pokemon, u32 unused, u16 darkId, u16 encountId);
void deckCreate(u16 deckID, u16 pokemonID, void* pokemon);
void pokemon_SetFightTrainerPokemonDataId(void* pokemon, u16 id);
s16 heroCatchPokemon(void* hero, void* pokemon, u32 floorId, u32 ball, u8 allowPCBox);
u16 fightTrainerPokemonDB_GetItemBallId(u16 pokemonId);
void heroCreate(void* hero, u16* name, u32 sex);
u16* heroBiosGetNamePtr(void* hero);
void* savedataGetStatus(s32, s32);
void* itemDataBiosGetPtr(u16 index);
extern u32* pokemon_data_number;
extern u32* waza_data_number;
void PortXD_RealCreateTrainerHero(u16 id, s32 allowNoAi, void* hero, u16 encountId);
}

namespace {

constexpr size_t kPokemonRow = 32, kTrainerRow = 56;

uint16_t Be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
void PutBe16(uint8_t* p, uint16_t v) { p[0] = static_cast<uint8_t>(v >> 8), p[1] = static_cast<uint8_t>(v); }
uint32_t Be32(const uint8_t* p) { return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

uint8_t* PokemonRow(uint32_t id) { return reinterpret_cast<uint8_t*>(DeckPokemon::getDeckPokemonObj(id)); }
uint8_t* TrainerRow(uint32_t id) { return reinterpret_cast<uint8_t*>(DeckTrainer::getDeckTrainerObj(id)); }

struct Undo {
    uint8_t* where;
    std::vector<uint8_t> was;
};

struct State {
    bool foeArmed[4] = {false, false, false, false};
    xd::teams::Team foe[4];
    bool lendArmed = false;
    bool lendUsed = false;
    xd::teams::Team lend;
    std::vector<Undo> undo;
    std::set<uint32_t> claimed;
    uint8_t* rowsBase = nullptr;
    uint8_t* trainersBase = nullptr;
} sState;

std::vector<uint32_t> FreeRows() {
    std::set<uint32_t> used;
    for (uint32_t t = 0, n = deckGetTrainerSize(); t < n; t++) {
        const uint8_t* r = TrainerRow(t);
        if (r == nullptr) continue;
        for (int j = 0; j < 6; j++) {
            const uint16_t v = Be16(r + 0x1C + 2 * j);
            if (v != 0 && (r[4] & (1u << j)) == 0) used.insert(v);
        }
    }
    for (uint32_t i = 0; i < 0x200; i++) {
        const uint8_t* d = reinterpret_cast<const uint8_t*>(DeckDarkPokemon::getDeckDarkPokemonObj(i));
        if (d == nullptr) break;
        used.insert(Be32(d + 4));
    }
    uint32_t count = 0;
    while (PokemonRow(count) != nullptr && count < 0x4000) count++;
    std::vector<uint32_t> out;
    for (uint32_t i = count; i-- > 1;) {
        if (used.count(i) == 0 && sState.claimed.count(i) == 0) out.push_back(i);
    }
    return out;
}

void Remember(uint8_t* where, size_t size) {
    for (const Undo& u : sState.undo) {
        if (u.where == where) return;
    }
    sState.undo.push_back({where, std::vector<uint8_t>(where, where + size)});
}

void WriteRow(uint8_t* row, const xd::teams::Member& m, bool keepRole) {
    PutBe16(row + 0x00, m.species);
    row[0x02] = m.level;
    row[0x03] = m.friendship;
    PutBe16(row + 0x04, m.item);
    if (!keepRole) row[0x06] = 1, row[0x07] = 0;
    for (int i = 0; i < 6; i++) row[0x08 + i] = m.iv[i], row[0x0E + i] = m.ev[i];
    for (int i = 0; i < 4; i++) PutBe16(row + 0x14 + 2 * i, m.moves[i]);
    row[0x1C] = 0, row[0x1D] = 0;
    row[0x1E] = static_cast<uint8_t>((m.nature << 3) | (0 << 1) | (m.ability & 1));
    row[0x1F] = 0;
}

uint32_t TakeRow() {
    const std::vector<uint32_t> free = FreeRows();
    if (free.empty()) return 0;
    sState.claimed.insert(free[0]);
    return free[0];
}

void Restore() {
    uint8_t* rows = PokemonRow(0);
    uint8_t* trainers = TrainerRow(0);
    if (rows == sState.rowsBase && trainers == sState.trainersBase) {
        for (auto it = sState.undo.rbegin(); it != sState.undo.rend(); ++it) std::memcpy(it->where, it->was.data(), it->was.size());
        if (!sState.undo.empty()) std::fprintf(stderr, "[teams] %zu deck row(s) put back as they were\n", sState.undo.size());
    } else if (!sState.undo.empty()) {
        std::fprintf(stderr, "[teams] the deck was loaded again: nothing to put back\n");
    }
    sState.undo.clear();
    sState.claimed.clear();
    sState.rowsBase = sState.trainersBase = nullptr;
}

}

namespace xd::teams {

std::string Check(const Team& team) {
    if (team.members.empty() || team.members.size() > 6) return "a team has 1 to 6 members";
    for (size_t i = 0; i < team.members.size(); i++) {
        const Member& m = team.members[i];
        const std::string who = "member " + std::to_string(i + 1) + ": ";
        if (pokemon_data_number == nullptr || m.species < 1 || m.species >= *pokemon_data_number) return who + "not a real species";
        if (m.level < 1 || m.level > 100) return who + "the level is 1 to 100";
        if (m.item != 0 && itemDataBiosGetPtr(m.item) == nullptr) return who + "not a real item";
        for (uint16_t mv : m.moves) {
            if (mv != 0 && (waza_data_number == nullptr || mv >= *waza_data_number)) return who + "not a real move";
        }
        if (m.moves[0] == 0 && m.moves[1] == 0 && m.moves[2] == 0 && m.moves[3] == 0) return who + "needs at least one move";
        if (m.nature > 24) return who + "the nature is 0 to 24";
        for (int k = 0; k < 6; k++) {
            if (m.iv[k] > 31) return who + "an IV is 0 to 31";
        }
    }
    for (uint16_t it : team.aiItems) {
        if (it != 0 && itemDataBiosGetPtr(it) == nullptr) return "the AI items must be real items";
    }
    return "";
}

std::string SetFoeTeam(int slot, const Team& team) {
    if (slot < 1 || slot > 3) return "slot is 1 to 3 (1 = the first opponent; the player's own place is 0)";
    const std::string why = Check(team);
    if (!why.empty()) return why;
    sState.foe[slot] = team;
    sState.foeArmed[slot] = true;
    return "";
}

std::string LendTeam(const Team& team) {
    const std::string why = Check(team);
    if (!why.empty()) return why;
    sState.lend = team;
    sState.lendArmed = true;
    return "";
}

void Clear() {
    Restore();
    for (bool& b : sState.foeArmed) b = false;
    sState.lendArmed = false;
    sState.lendUsed = false;
}

bool LendInUse() { return sState.lendUsed; }

bool Armed() {
    return sState.lendArmed || sState.foeArmed[1] || sState.foeArmed[2] || sState.foeArmed[3];
}

}

extern "C" {
u8 darkPokemonBiosGetSnatchSuccessFlag(u16 id);
u8 fightEncountDataBiosGetFightKind(void* data);
}
static void ShadowRematch(u16 id, u16 encountId) {
    const u16 enc = encountId != 0 ? encountId : fightGetFightEncountDataId();
    void* data = fightEncountDataBiosGetPtr(enc);
    if (data == nullptr) return;
    const u8 fightKind = fightEncountDataBiosGetFightKind(data);
    if (fightKind != 8 && fightKind != 9) return;
    if (id >= deckGetTrainerSize()) return;
    uint8_t* row = TrainerRow(id);
    if (row == nullptr || row[4] == 0) return;
    for (int j = 0; j < 6; j++) {
        if ((row[4] & (1u << j)) == 0) continue;
        const uint16_t shadow = Be16(row + 0x1C + 2 * j);
        if (darkPokemonBiosGetSnatchSuccessFlag(shadow) == 0) continue;
        const uint8_t* deckRow = reinterpret_cast<const uint8_t*>(DeckDarkPokemon::getDeckDarkPokemonObj(shadow));
        if (deckRow == nullptr) continue;
        const uint32_t entry = Be32(deckRow + 4);
        if (entry == 0 || entry > 0xFFFF) continue;
        PutBe16(row + 0x1C + 2 * j, static_cast<uint16_t>(entry));
        row[4] = static_cast<uint8_t>(row[4] & ~(1u << j));
        std::fprintf(stderr, "[shadow rematch] Mt. Battle trainer %u slot %d: Shadow %u was snagged, the plain Pokemon (entry %u) is fielded\n", id, j + 1, shadow, entry);
    }
}

extern "C" void fightTrainerCreateFightTrainerDataIdToHero(u16 id, s32 allowNoAi, void* hero, u16 encountId) {
    ShadowRematch(id, encountId);
    if (!xd::teams::Armed()) {
        PortXD_RealCreateTrainerHero(id, allowNoAi, hero, encountId);
        return;
    }
    const u16 enc = encountId != 0 ? encountId : fightGetFightEncountDataId();
    const u8 kind = fightTrainerDB_GetKindDataId(id);
    if (kind == 1) {
        PortXD_RealCreateTrainerHero(id, allowNoAi, hero, encountId);
        if (!sState.lendArmed || hero == nullptr) return;
        void* player = savedataGetStatus(0, 2);
        if (player == nullptr) return;
        sState.rowsBase = PokemonRow(0), sState.trainersBase = TrainerRow(0);
        heroCreate(hero, heroBiosGetNamePtr(player), 0);
        sState.lendUsed = true;
        int made = 0;
        for (const xd::teams::Member& m : sState.lend.members) {
            const uint32_t row = TakeRow();
            uint8_t* r = PokemonRow(row);
            if (row == 0 || r == nullptr) {
                std::fprintf(stderr, "[teams] lend_team: no free deck row for a member; the team is shorter\n");
                break;
            }
            Remember(r, kPokemonRow);
            WriteRow(r, m, false);
            alignas(16) uint8_t pokemon[0x400];
            std::memset(pokemon, 0, sizeof(pokemon));
            if (!fightTrainerCreateFightTrainerPokemonDataIdToPokemon(static_cast<u16>(row), pokemon, 0, 0, enc)) continue;
            deckCreate(static_cast<u16>(row), Be16(r), pokemon);
            pokemon_SetFightTrainerPokemonDataId(pokemon, static_cast<u16>(row));
            if (heroCatchPokemon(hero, pokemon, 0, fightTrainerPokemonDB_GetItemBallId(static_cast<u16>(row)), 0) >= 0) made++;
        }
        std::fprintf(stderr, "[teams] lend_team: the player's side fights with %d lent Pokemon (the party is untouched)\n", made);
        return;
    }
    int slot = -1;
    if (void* row = fightEncountDataBiosGetPtr(enc)) {
        for (int k = 1; k < 4; k++) {
            if (fightEncountDataBiosGetFightTrainerDataId(row, static_cast<u8>(k)) == id && sState.foeArmed[k]) {
                slot = k;
                break;
            }
        }
    }
    uint8_t* tr = TrainerRow(id);
    if (slot < 0 || tr == nullptr) {
        PortXD_RealCreateTrainerHero(id, allowNoAi, hero, encountId);
        return;
    }
    const xd::teams::Team& team = sState.foe[slot];
    sState.rowsBase = PokemonRow(0), sState.trainersBase = TrainerRow(0);
    Remember(tr, kTrainerRow);
    uint32_t rows[6] = {0, 0, 0, 0, 0, 0};
    uint32_t firstOwn = 0;
    for (int j = 0; j < 6; j++) {
        const uint16_t v = Be16(tr + 0x1C + 2 * j);
        if (v != 0 && (tr[4] & (1u << j)) == 0 && firstOwn == 0) firstOwn = v;
    }
    for (size_t j = 0; j < team.members.size(); j++) {
        const uint16_t own = Be16(tr + 0x1C + 2 * j);
        uint32_t row = (own != 0 && (tr[4] & (1u << j)) == 0) ? own : 0;
        bool fresh = false;
        if (row == 0) row = TakeRow(), fresh = true;
        uint8_t* r = PokemonRow(row);
        if (row == 0 || r == nullptr) {
            std::fprintf(stderr, "[teams] set_trainer_team: no free deck row for member %zu; the team is shorter\n", j + 1);
            break;
        }
        Remember(r, kPokemonRow);
        if (fresh && firstOwn != 0 && PokemonRow(firstOwn) != nullptr) std::memcpy(r, PokemonRow(firstOwn), kPokemonRow);
        WriteRow(r, team.members[j], !fresh && true);
        rows[j] = row;
    }
    for (int j = 0; j < 6; j++) PutBe16(tr + 0x1C + 2 * j, static_cast<uint16_t>(rows[j]));
    tr[4] &= ~0x3F;
    if (team.hasAiItems) {
        for (int i = 0; i < 4; i++) PutBe16(tr + 0x08 + 2 * i, team.aiItems[i]);
    }
    std::fprintf(stderr, "[teams] set_trainer_team: trainer %u (place %d of battle %u) builds a team of %zu\n", id, slot, enc, team.members.size());
    PortXD_RealCreateTrainerHero(id, allowNoAi, hero, encountId);
}

extern "C" void heroBiosCopy(void* dst, void* src);
extern "C" void PortXD_FlowHeroCopy(void* dst, void* src) {
    if (xd::teams::LendInUse() && dst != nullptr && dst == savedataGetStatus(0, 2)) {
        std::fprintf(stderr, "[teams] lend_team: the lent team is not copied back into the save\n");
        return;
    }
    heroBiosCopy(dst, src);
}

extern "C" void PortXD_TeamsBattleEnded(void) { xd::teams::Clear(); }
