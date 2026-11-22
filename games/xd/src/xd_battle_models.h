#ifndef XD_BATTLE_MODELS_H
#define XD_BATTLE_MODELS_H

namespace xd::battle_models {

inline constexpr const char* kLayerId = "pbr";
inline constexpr const char* kManifestId = "battle-revolution-models";
inline constexpr const char* kUsumLayerId = "usum";
inline constexpr const char* kRuleBr = "models_br";
inline constexpr const char* kRuleUsum = "models_usum";
inline constexpr const char* kSetting = "video.battle_revolution_models";

}

#ifdef __cplusplus
extern "C" {
#endif
bool PortXD_BattleModelsFound(void);
const char* PortXD_ExtraAssetsText(void);
#ifdef __cplusplus
}
#endif

#endif
