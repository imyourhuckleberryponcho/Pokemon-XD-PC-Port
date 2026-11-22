#ifndef XD_SOUND_BANKS_H
#define XD_SOUND_BANKS_H

namespace xd::sound {

struct Bank {
  const char* name;
  const char* proj;
  const char* pool;
  const char* sdir;
  const char* samp;
  const char* what;
};

inline constexpr Bank kBanks[] = {
    {"menu", "snd_se_proj", "snd_se_pool", "snd_se_sdir", "sound/snd_se.samp", "menus, the field, doors, steps, most sounds outside battle (effects 0-200)"},
    {"battle", "snd_se_battle_proj", "snd_se_battle_pool", "snd_se_battle_sdir", "sound/snd_se_battle.samp", "battle sounds: hits, moves, balls (effects 201 and up)"},
    {"atmos", "snd_music_atmos_proj", "snd_music_atmos_pool", "snd_music_atmos_sdir", "sound/snd_music_atmos.samp", "ambient loops"},
};
inline constexpr const char* kBanksArchive = "common.fsys";

}

#endif
