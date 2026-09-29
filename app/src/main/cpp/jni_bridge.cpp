#include <jni.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "llama_bridge.h"
#include "zim_reader.h"

namespace {

const char *kZimArchiveClass = "org/odweta/androidwikiai/offlineai/ZimArchive";
const char *kZimArticleClass = "org/odweta/androidwikiai/offlineai/ZimArchive$Article";
const char *kLlamaModelClass = "org/odweta/androidwikiai/offlineai/LlamaModel";

thread_local std::string g_lastError;

jstring toJString(JNIEnv *env, const std::string &s) {
    return env->NewStringUTF(s.c_str());
}

std::string toStdString(JNIEnv *env, jstring s) {
    if (!s) return "";
    const char *chars = env->GetStringUTFChars(s, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(s, chars);
    return result;
}

jlong Zim_nativeOpen(JNIEnv *env, jclass, jstring jpath) {
    std::string path = toStdString(env, jpath);
    std::string error;
    auto archive = zim::Archive::open(path, &error);
    if (!archive) {
        g_lastError = error;
        return 0;
    }
    return reinterpret_cast<jlong>(archive.release());
}

jobjectArray Zim_nativeSearch(JNIEnv *env, jclass, jlong handle, jstring jquery, jint maxResults) {
    jclass articleClass = env->FindClass(kZimArticleClass);
    jobjectArray empty = env->NewObjectArray(0, articleClass, nullptr);
    if (handle == 0) return empty;

    auto *archive = reinterpret_cast<zim::Archive *>(handle);
    std::string query = toStdString(env, jquery);
    std::vector<zim::SearchResult> results = archive->search(query, maxResults);
    if (results.empty()) return empty;

    jmethodID ctor = env->GetMethodID(articleClass, "<init>",
                                       "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;F)V");
    jobjectArray array = env->NewObjectArray(static_cast<jsize>(results.size()), articleClass, nullptr);
    for (size_t i = 0; i < results.size(); i++) {
        const auto &r = results[i];
        jobject article = env->NewObject(articleClass, ctor, toJString(env, r.title),
                                          toJString(env, r.snippet), toJString(env, r.content),
                                          static_cast<jfloat>(r.score));
        env->SetObjectArrayElement(array, static_cast<jsize>(i), article);
        env->DeleteLocalRef(article);
    }
    return array;
}

void Zim_nativeClose(JNIEnv *, jclass, jlong handle) {
    delete reinterpret_cast<zim::Archive *>(handle);
}

jstring Zim_nativeLastError(JNIEnv *env, jclass) {
    return toJString(env, g_lastError);
}

jlong Llama_nativeLoad(JNIEnv *env, jclass, jstring jpath, jint contextSize) {
    std::string path = toStdString(env, jpath);
    std::string error;
    auto model = llamabridge::Model::load(path, contextSize, &error);
    if (!model) {
        g_lastError = error;
        return 0;
    }
    return reinterpret_cast<jlong>(model.release());
}

jstring Llama_nativeGenerate(JNIEnv *env, jclass, jlong handle, jstring jprompt, jint maxTokens) {
    if (handle == 0) return toJString(env, "");
    auto *model = reinterpret_cast<llamabridge::Model *>(handle);
    std::string prompt = toStdString(env, jprompt);
    std::string error;
    std::string result = model->generate(prompt, maxTokens, &error);
    if (result.empty() && !error.empty()) {
        g_lastError = error;
    }
    return toJString(env, result);
}

void Llama_nativeUnload(JNIEnv *, jclass, jlong handle) {
    delete reinterpret_cast<llamabridge::Model *>(handle);
}

jstring Llama_nativeLastError(JNIEnv *env, jclass) {
    return toJString(env, g_lastError);
}

} // namespace

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *) {
    JNIEnv *env;
    if (vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }

    {
        jclass clazz = env->FindClass(kZimArchiveClass);
        if (!clazz) return JNI_ERR;
        static const JNINativeMethod methods[] = {
            {"nativeOpen", "(Ljava/lang/String;)J", reinterpret_cast<void *>(Zim_nativeOpen)},
            {"nativeSearch", "(JLjava/lang/String;I)[Lorg/odweta/androidwikiai/offlineai/ZimArchive$Article;",
             reinterpret_cast<void *>(Zim_nativeSearch)},
            {"nativeClose", "(J)V", reinterpret_cast<void *>(Zim_nativeClose)},
            {"nativeLastError", "()Ljava/lang/String;", reinterpret_cast<void *>(Zim_nativeLastError)},
        };
        if (env->RegisterNatives(clazz, methods, 4) != 0) return JNI_ERR;
    }

    {
        jclass clazz = env->FindClass(kLlamaModelClass);
        if (!clazz) return JNI_ERR;
        static const JNINativeMethod methods[] = {
            {"nativeLoad", "(Ljava/lang/String;I)J", reinterpret_cast<void *>(Llama_nativeLoad)},
            {"nativeGenerate", "(JLjava/lang/String;I)Ljava/lang/String;",
             reinterpret_cast<void *>(Llama_nativeGenerate)},
            {"nativeUnload", "(J)V", reinterpret_cast<void *>(Llama_nativeUnload)},
            {"nativeLastError", "()Ljava/lang/String;", reinterpret_cast<void *>(Llama_nativeLastError)},
        };
        if (env->RegisterNatives(clazz, methods, 4) != 0) return JNI_ERR;
    }

    return JNI_VERSION_1_6;
}
