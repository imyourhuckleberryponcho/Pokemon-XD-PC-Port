#ifdef __ANDROID__
#include "port_android_install.h"

#include <SDL3/SDL_system.h>

#include <jni.h>

#include <cstdio>
#include <string>

static jclass AppClass(JNIEnv* env, jobject activity, const char* name) {
  jclass activityClass = env->GetObjectClass(activity);
  jmethodID getLoader = env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
  jobject loader = env->CallObjectMethod(activity, getLoader);
  jclass loaderClass = env->GetObjectClass(loader);
  jmethodID loadClass = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
  jstring className = env->NewStringUTF(name);
  jclass found = static_cast<jclass>(env->CallObjectMethod(loader, loadClass, className));
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return nullptr;
  }
  return found;
}
static jclass InstallerClass(JNIEnv* env, jobject activity) { return AppClass(env, activity, "net.pokemonxdpc.port.ApkInstaller"); }

static std::string TakeString(JNIEnv* env, jstring text) {
  std::string out;
  if (text != nullptr) {
    const char* chars = env->GetStringUTFChars(text, nullptr);
    out = chars != nullptr ? chars : "";
    if (chars != nullptr) env->ReleaseStringUTFChars(text, chars);
  }
  return out;
}

extern "C" bool PortAndroid_InstallApk(const char* apkPath, char* error, size_t errorSize) {
  const auto fail = [&](const std::string& why) {
    if (error != nullptr && errorSize > 0) {
      std::snprintf(error, errorSize, "%s", why.c_str());
    }
    return false;
  };
  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    return fail("no Android activity");
  }
  jclass installer = InstallerClass(env, activity);
  if (installer == nullptr) {
    return fail("the installer class is not in this app");
  }
  jmethodID install = env->GetStaticMethodID(installer, "install", "(Landroid/app/Activity;Ljava/lang/String;)Ljava/lang/String;");
  jstring path = env->NewStringUTF(apkPath);
  jstring result = static_cast<jstring>(env->CallStaticObjectMethod(installer, install, activity, path));
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return fail("the installer call failed");
  }
  const std::string text = TakeString(env, result);
  return text.empty() ? true : fail(text);
}

extern "C" bool PortAndroid_HttpFetch(const char* url, const char* file, unsigned long long maxBytes, int timeoutMs, bool httpsOnly, const char* userAgent,
                                      char* error, size_t errorSize) {
  const auto fail = [&](const std::string& why) {
    if (error != nullptr && errorSize > 0) {
      std::snprintf(error, errorSize, "%s", why.c_str());
    }
    return false;
  };
  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    return fail("no Android activity");
  }
  jclass fetcher = AppClass(env, activity, "net.pokemonxdpc.port.HttpFetch");
  if (fetcher == nullptr) {
    return fail("the download class is not in this app");
  }
  jmethodID fetch = env->GetStaticMethodID(fetcher, "fetch", "(Ljava/lang/String;Ljava/lang/String;JIZLjava/lang/String;)Ljava/lang/String;");
  jstring u = env->NewStringUTF(url);
  jstring f = env->NewStringUTF(file);
  jstring ua = env->NewStringUTF(userAgent != nullptr ? userAgent : "PortUpdater/1");
  jstring result = static_cast<jstring>(env->CallStaticObjectMethod(fetcher, fetch, u, f, static_cast<jlong>(maxBytes), static_cast<jint>(timeoutMs),
                                                                    static_cast<jboolean>(httpsOnly), ua));
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return fail("the download call failed");
  }
  const std::string text = TakeString(env, result);
  return text.empty() ? true : fail(text);
}

extern "C" void PortAndroid_HttpCancel(void) {
  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    return;
  }
  jclass fetcher = AppClass(env, activity, "net.pokemonxdpc.port.HttpFetch");
  if (fetcher == nullptr) {
    return;
  }
  jfieldID cancel = env->GetStaticFieldID(fetcher, "cancel", "Z");
  env->SetStaticBooleanField(fetcher, cancel, JNI_TRUE);
}

extern "C" bool PortAndroid_DisplayName(const char* uri, char* name, size_t nameSize) {
  if (name == nullptr || nameSize == 0) {
    return false;
  }
  name[0] = 0;
  JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr) {
    return false;
  }
  jclass installer = InstallerClass(env, activity);
  if (installer == nullptr) {
    return false;
  }
  jmethodID method = env->GetStaticMethodID(installer, "displayName", "(Landroid/app/Activity;Ljava/lang/String;)Ljava/lang/String;");
  jstring address = env->NewStringUTF(uri);
  jstring result = static_cast<jstring>(env->CallStaticObjectMethod(installer, method, activity, address));
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return false;
  }
  const std::string text = TakeString(env, result);
  std::snprintf(name, nameSize, "%s", text.c_str());
  return !text.empty();
}
#endif
