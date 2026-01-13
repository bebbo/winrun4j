/*******************************************************************************
* This program and the accompanying materials
* are made available under the terms of the Common Public License v1.0
* which accompanies this distribution, and is available at 
* http://www.eclipse.org/legal/cpl-v10.html
* 
* Contributors:
*     Peter Smith
*******************************************************************************/

#include "JNI.h"
#include "../common/Log.h"
#include "../common/Runtime.h"

#include <windows.h>
#include <string.h>
#include <stdlib.h>

// Embedded classloader bytecode
#include "EmbeddedClasses.hpp"

// Global references
static jclass   g_classLoaderClass = NULL;
static jobject  g_classLoader      = NULL;
static jmethodID g_findClassMethod = NULL;

// Cached java.lang.Class
static jclass    CLASS_CLASS = NULL;
static jmethodID CLASS_GETCTORS_METHOD = NULL;

void JNI::Init(JNIEnv* env)
{
    jclass c = env->FindClass("java/lang/Class");
    if (!c) {
        Log::Error("Could not find Class class");
        return;
    }

    CLASS_CLASS = (jclass)env->NewGlobalRef(c);
    CLASS_GETCTORS_METHOD =
        env->GetMethodID(CLASS_CLASS, "getConstructors", "()[Ljava/lang/reflect/Constructor;");

    if (!CLASS_GETCTORS_METHOD) {
        Log::Error("Could not find Class.getConstructors");
        return;
    }

    LoadEmbeddedClassloader(env);
}

jclass JNI::FindClass(JNIEnv* env, TCHAR* classStr)
{
    if (!g_classLoader)
        return env->FindClass(classStr);

    jstring jname = env->NewStringUTF(classStr);
    jclass cl = (jclass)env->CallObjectMethod(g_classLoader, g_findClassMethod, jname);

    if (cl && CLASS_GETCTORS_METHOD)
        env->CallObjectMethod(cl, CLASS_GETCTORS_METHOD);

    return cl;
}

jstring JNI::JNU_NewStringNative(JNIEnv *env, jclass stringClass, const char *str)
{
    if (!str)
        return NULL;

    if (env->EnsureLocalCapacity(2) < 0)
        return NULL;

    int len = (int)strlen(str);
    jbyteArray bytes = env->NewByteArray(len);
    if (!bytes)
        return NULL;

    env->SetByteArrayRegion(bytes, 0, len, (const jbyte*)str);

    jmethodID ctor = env->GetMethodID(stringClass, "<init>", "([B)V");
    jstring result = (jstring)env->NewObject(stringClass, ctor, bytes);

    env->DeleteLocalRef(bytes);
    return result;
}

int JNI::RunMainClass(JNIEnv* env, TCHAR* mainClassStr, int argc, char* argv[])
{
    if (!mainClassStr) {
        Log::Error("No main class specified");
        return 1;
    }

    StrReplace(mainClassStr, '.', '/');

    jclass mainClass = FindClass(env, mainClassStr);
    if (!mainClass) {
        Log::Error("Could not find or initialize main class");
        return 2;
    }

    jobjectArray args = CreateRunArgs(env, argc, argv);
    if (!args) {
        Log::Error("Could not create args");
        return 4;
    }

    jmethodID mainMethod =
        env->GetStaticMethodID(mainClass, "main", "([Ljava/lang/String;)V");

    if (!mainMethod) {
        Log::Error("Could not find main method");
        return 8;
    }

    env->CallStaticVoidMethod(mainClass, mainMethod, args);

    PrintStackTrace(env);
    ClearException(env);

    return 0;
}

char* JNI::CallStringMethod(JNIEnv* env, jclass clazz, jobject obj, char* name)
{
    jmethodID mid = env->GetMethodID(clazz, name, "()Ljava/lang/String;");
    if (!mid) {
        Log::Error("Could not find '%s' method", name);
        return NULL;
    }

    jstring str = (jstring)env->CallObjectMethod(obj, mid);
    if (!str)
        return NULL;

    if (env->ExceptionCheck()) {
        PrintStackTrace(env);
        return NULL;
    }

    jboolean iscopy = JNI_FALSE;
    const char* chars = env->GetStringUTFChars(str, &iscopy);
    if (!chars)
        return NULL;

    char* out = _strdup(chars);
    env->ReleaseStringUTFChars(str, chars);

    return out;
}

const bool JNI::CallBooleanMethod(JNIEnv* env, jclass clazz, jobject obj, char* name)
{
    jmethodID mid = env->GetMethodID(clazz, name, "()Z");
    if (!mid) {
        Log::Error("Could not find '%s' method", name);
        return false;
    }

    return env->CallBooleanMethod(obj, mid);
}

jthrowable JNI::PrintStackTrace(JNIEnv* env)
{
    if (!env)
        return NULL;

    jthrowable thr = env->ExceptionOccurred();
    if (!thr)
        return NULL;

    jclass throwable = env->FindClass("java/lang/Throwable");
    jmethodID print0 = env->GetMethodID(throwable, "printStackTrace", "()V");

    if (print0) {
        env->CallVoidMethod(thr, print0);
    } else {
        env->ExceptionClear();
        jmethodID print1 =
            env->GetMethodID(throwable, "printStackTrace", "(Ljava/io/PrintStream;)V");

        jclass sys = env->FindClass("java/lang/System");
        jfieldID outField = env->GetStaticFieldID(sys, "out", "Ljava/io/PrintStream;");
        jobject out = env->GetStaticObjectField(sys, outField);

        env->CallVoidMethod(thr, print1, out);
    }

    env->ExceptionClear();
    return thr;
}

void JNI::ClearException(JNIEnv* env)
{
    if (env && env->ExceptionOccurred())
        env->ExceptionClear();
}

jobjectArray JNI::ListJars(JNIEnv* env, jobject self, jstring library)
{
    (void)self; // suppress C4100

    HMODULE hm = NULL;

    if (library) {
        jboolean iscopy = JNI_FALSE;
        const char* lib = env->GetStringUTFChars(library, &iscopy);
        hm = LoadLibraryA(lib);
        env->ReleaseStringUTFChars(library, lib);
        if (!hm)
            return NULL;
    }

    int resId = 1;
    while (FindResourceA(hm, MAKEINTRESOURCEA(resId), RT_JAR_FILE))
        resId++;

    jclass strClass = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray(resId - 1, strClass, NULL);

    for (int i = 1; i < resId; i++) {
        HRSRC hs = FindResourceA(hm, MAKEINTRESOURCEA(i), RT_JAR_FILE);
        if (!hs)
            continue;

        HGLOBAL hg = LoadResource(hm, hs);
        BYTE* pb = (BYTE*)LockResource(hg);
        DWORD* pd = (DWORD*)pb;

        if (*pd == JAR_RES_MAGIC) {
            const char* name = (const char*)&pb[RES_MAGIC_SIZE];
            env->SetObjectArrayElement(arr, i - 1, env->NewStringUTF(name));
        }
    }

    return arr;
}

jobject JNI::GetJar(JNIEnv* env, jobject self, jstring library, jstring jarName)
{
    (void)self; // suppress C4100

    HMODULE hm = NULL;

    if (library) {
        jboolean iscopy = JNI_FALSE;
        const char* lib = env->GetStringUTFChars(library, &iscopy);
        hm = LoadLibraryA(lib);
        env->ReleaseStringUTFChars(library, lib);
        if (!hm)
            return NULL;
    }

    if (!jarName)
        return NULL;

    jboolean iscopy = JNI_FALSE;
    const char* jn = env->GetStringUTFChars(jarName, &iscopy);

    int resId = 1;
    HRSRC hs;

    while ((hs = FindResourceA(hm, MAKEINTRESOURCEA(resId), RT_JAR_FILE)) != NULL) {

        HGLOBAL hg = LoadResource(hm, hs);
        BYTE* pb = (BYTE*)LockResource(hg);
        DWORD* pd = (DWORD*)pb;

        if (*pd == JAR_RES_MAGIC) {
            const char* stored = (const char*)&pb[RES_MAGIC_SIZE];
            if (strcmp(jn, stored) == 0) {
                DWORD nameLen = (DWORD)strlen(stored);
                DWORD offset = RES_MAGIC_SIZE + nameLen + 1;
                DWORD total = SizeofResource(NULL, hs);

                jobject buf = env->NewDirectByteBuffer(&pb[offset], total - offset);
                env->ReleaseStringUTFChars(jarName, jn);
                return buf;
            }
        }
        resId++;
    }

    env->ReleaseStringUTFChars(jarName, jn);
    return NULL;
}

jclass JNI::DefineClass(JNIEnv* env, const char* filename, const char* name, jobject loader)
{
    HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE)
        return NULL;

    DWORD size = GetFileSize(hFile, NULL);
    BYTE* buf = (BYTE*)malloc(size);

    DWORD read = 0;
    ReadFile(hFile, buf, size, &read, NULL);
    CloseHandle(hFile);

    jclass cl = env->DefineClass(name, loader, (const jbyte*)buf, size);
    free(buf);

    return cl;
}

void JNI::LoadEmbeddedClassloader(JNIEnv* env)
{
    if (!FindResourceA(NULL, MAKEINTRESOURCEA(1), RT_JAR_FILE))
        return;

    jclass loaderClass = env->FindClass("java/lang/ClassLoader");
    if (!loaderClass) {
        Log::Error("Could not access ClassLoader");
        return;
    }

    jmethodID getSys =
        env->GetStaticMethodID(loaderClass, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");

    if (!getSys) {
        Log::Error("Could not access ClassLoader.getSystemClassLoader");
        return;
    }

    jobject loader = env->CallStaticObjectMethod(loaderClass, getSys);
    loader = env->NewGlobalRef(loader);

    /* jclass bb = */
    env->DefineClass(
        "org/boris/winrun4j/classloader/ByteBufferInputStream",
        loader,
        (const jbyte*)g_byteBufferISCode,
        sizeof(g_byteBufferISCode));

    jclass cl = env->DefineClass(
        "org/boris/winrun4j/classloader/EmbeddedClassLoader",
        loader,
        (const jbyte*)g_classLoaderCode,
        sizeof(g_classLoaderCode));

    if (!cl) {
        PrintStackTrace(env);
        Log::Error("Could not load embedded classloader");
        return;
    }

    g_classLoaderClass = (jclass)env->NewGlobalRef(cl);

    env->CallObjectMethod(g_classLoaderClass, CLASS_GETCTORS_METHOD);

    JNINativeMethod m[2];
    m[0].name      = (char*)"listJars";
    m[0].signature = (char*)"(Ljava/lang/String;)[Ljava/lang/String;";
    m[0].fnPtr     = (void*)ListJars;

    m[1].name      = (char*)"getJar";
    m[1].signature = (char*)"(Ljava/lang/String;Ljava/lang/String;)Ljava/nio/ByteBuffer;";
    m[1].fnPtr     = (void*)GetJar;

    env->RegisterNatives(g_classLoaderClass, m, 2);

    if (env->ExceptionCheck()) {
        Log::Error("Could not register classloader native methods");
        env->ExceptionClear();
        return;
    }

    jmethodID ctor = env->GetMethodID(g_classLoaderClass, "<init>", "()V");
    if (!ctor) {
        Log::Error("Could not access classloader constructor");
        return;
    }

    jobject o = env->NewObject(g_classLoaderClass, ctor);
    if (!o) {
        PrintStackTrace(env);
        Log::Error("Could not create classloader instance");
        return;
    }

    g_classLoader = env->NewGlobalRef(o);

    g_findClassMethod =
        env->GetMethodID(g_classLoaderClass, "findClass", "(Ljava/lang/String;)Ljava/lang/Class;");

    if (!g_findClassMethod) {
        PrintStackTrace(env);
        Log::Error("Could not access findClass");
        g_classLoader = NULL;
    }
}

void JNI::SetContextClassLoader(JNIEnv* env, jobject refObject)
{
    jclass threadCls = env->FindClass("java/lang/Thread");
    jmethodID currentThreadMid =
        env->GetStaticMethodID(threadCls, "currentThread", "()Ljava/lang/Thread;");

    jobject currentThread =
        env->CallStaticObjectMethod(threadCls, currentThreadMid);

    jmethodID getCtx =
        env->GetMethodID(threadCls, "getContextClassLoader", "()Ljava/lang/ClassLoader;");

    jobject ctx = env->CallObjectMethod(currentThread, getCtx);
    if (ctx)
        return;

    jclass refCls = env->GetObjectClass(refObject);
    jmethodID getClassMid = env->GetMethodID(refCls, "getClass", "()Ljava/lang/Class;");
    jobject clsObj = env->CallObjectMethod(refObject, getClassMid);

    jclass clsCls = env->GetObjectClass(clsObj);
    jmethodID getLoaderMid =
        env->GetMethodID(clsCls, "getClassLoader", "()Ljava/lang/ClassLoader;");

    jobject loader = env->CallObjectMethod(clsObj, getLoaderMid);

    jmethodID setCtx =
        env->GetMethodID(threadCls, "setContextClassLoader", "(Ljava/lang/ClassLoader;)V");

    env->CallVoidMethod(currentThread, setCtx, loader);
}

jobjectArray JNI::CreateRunArgs(JNIEnv *env, int argc, char* argv[])
{
    jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass) {
        Log::Error("Could not find String class");
        return NULL;
    }

    jobjectArray arr = env->NewObjectArray(argc, stringClass, NULL);
    if (!arr)
        return NULL;

    for (int i = 0; i < argc; i++) {
        jstring s = JNU_NewStringNative(env, stringClass, argv[i]);
        env->SetObjectArrayElement(arr, i, s);
    }

    return arr;
}
