#ifdef __ANDROID__
#include <SDL3/SDL_system.h>

#include <jni.h>

#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" int PortAndroid_RestartApp(void) {
  static const char* const kCarry[] = {"PORT_RESET_CODE", "XD_NEWGAME_SETUP", "XD_RESTART_INTENT", "XD_SPEEDRUN_NEWGAME_AT", "XD_SPEEDRUN_CARRY",
                                       "XD_RANDOMIZER", "XD_SETTINGS", "XD_PRESET", "XD_SETTINGS_ENTRY", "XD_BATTLE_REPLAY_TURN", "XD_BATTLE_REPLAY_AGAIN",
                                       "XD_CARD_DIR", "XD_SETTINGS_DIR", "XD_CACHE_DIR"};
  std::string text = "XD_NO_LAUNCHER=1\nXD_ANDROID_RESTART=1\n";
  for (const char* name : kCarry) {
    const char* value = std::getenv(name);
    if (value != nullptr && value[0] != 0) {
      text += std::string(name) + "=" + value + "\n";
    }
  }
  const char* file = "/data/data/net.pokemonxdpc.port/files/xd_restart_env.txt";
  std::FILE* f = std::fopen(file, "wb");
  if (f == nullptr) {
    std::fprintf(stderr, "[android restart] cannot write %s\n", file);
    return 0;
  }
  std::fwrite(text.data(), 1, text.size(), f);
  std::fclose(f);

  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    std::fprintf(stderr, "[android restart] no activity\n");
    return 0;
  }
  jclass activityClass = env->GetObjectClass(activity);
  jmethodID getLoader = env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
  jobject loader = env->CallObjectMethod(activity, getLoader);
  jclass loaderClass = env->GetObjectClass(loader);
  jmethodID loadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
  jstring className = env->NewStringUTF("net.pokemonxdpc.port.Restart");
  jclass restart = static_cast<jclass>(env->CallObjectMethod(loader, loadClass, className));
  if (env->ExceptionCheck() || restart == nullptr) {
    env->ExceptionClear();
    std::fprintf(stderr, "[android restart] the restart class is not in this app\n");
    return 0;
  }
  jmethodID begin = env->GetStaticMethodID(restart, "begin", "(Landroid/app/Activity;)Ljava/lang/String;");
  jstring result = static_cast<jstring>(env->CallStaticObjectMethod(restart, begin, activity));
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return 0;
  }
  if (result != nullptr) {
    const char* chars = env->GetStringUTFChars(result, nullptr);
    const bool ok = chars == nullptr || chars[0] == 0;
    if (!ok) std::fprintf(stderr, "[android restart] %s\n", chars);
    if (chars != nullptr) env->ReleaseStringUTFChars(result, chars);
    if (!ok) return 0;
  }
  std::fprintf(stderr, "[android restart] the app starts again\n");
  std::fflush(stderr);
  return 1;
}
#endif
