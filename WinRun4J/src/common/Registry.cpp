/*******************************************************************************
* This program and the accompanying materials
* are made available under the terms of the Common Public License v1.0
* which accompanies this distribution, and is available at 
* http://www.eclipse.org/legal/cpl-v10.html
* 
* Contributors:
*     Peter Smith
*******************************************************************************/

#include "Registry.h"
#include "Log.h"
#include "../java/JNI.h"

#include <windows.h>
#include <string.h>

jlong Registry::OpenKey(JNIEnv* env, jobject /*self*/, jlong rootKey, jstring subKey, bool readOnly)
{
    jboolean iscopy = JNI_FALSE;
    const char* sk  = subKey ? env->GetStringUTFChars(subKey, &iscopy) : NULL;

    HKEY  key   = NULL;
    DWORD access = readOnly ? KEY_READ : KEY_ALL_ACCESS;
#ifdef X64
    access |= KEY_WOW64_64KEY;
#else
    access |= KEY_WOW64_32KEY;
#endif

    LONG result = RegOpenKeyExA((HKEY)rootKey, sk, 0, access, &key);

    if (subKey && sk)
        env->ReleaseStringUTFChars(subKey, sk);

    if (result == ERROR_SUCCESS)
        return (jlong)key;
    else
        return 0;
}

jlong Registry::CreateSubKey(JNIEnv* env, jobject /*self*/, jlong rootKey, jstring subKey)
{
    jboolean iscopy = JNI_FALSE;
    const char* sk  = subKey ? env->GetStringUTFChars(subKey, &iscopy) : NULL;

    HKEY  key    = NULL;
    DWORD access = KEY_ALL_ACCESS;
#ifdef X64
    access |= KEY_WOW64_64KEY;
#endif

    LONG result = RegCreateKeyExA((HKEY)rootKey, sk, 0, NULL, REG_OPTION_NON_VOLATILE, access, NULL, &key, NULL);

    if (subKey && sk)
        env->ReleaseStringUTFChars(subKey, sk);

    if (result == ERROR_SUCCESS)
        return (jlong)key;
    else
        return 0;
}

void Registry::CloseKey(JNIEnv* /*env*/, jobject /*self*/, jlong handle)
{
    if (handle == 0)
        return;
    RegCloseKey((HKEY)handle);
}

jobjectArray Registry::GetSubKeyNames(JNIEnv* env, jobject /*self*/, jlong handle)
{
    if (handle == 0)
        return 0;

    DWORD keyCount = 0;
    LONG result = RegQueryInfoKeyA((HKEY)handle, NULL, NULL, NULL, &keyCount, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    if (result != ERROR_SUCCESS)
        return NULL;

    char tmp[MAX_PATH];

    jclass clazz = env->FindClass("java/lang/String");
    if (!clazz)
        return NULL;

    jobjectArray arr = env->NewObjectArray((jsize)keyCount, clazz, NULL);
    if (!arr)
        return NULL;

    for (DWORD i = 0; i < keyCount; i++) {
        DWORD size = MAX_PATH;
        if (RegEnumKeyExA((HKEY)handle, i, tmp, &size, 0, NULL, NULL, NULL) == ERROR_SUCCESS) {
            jstring s = env->NewStringUTF(tmp);
            env->SetObjectArrayElement(arr, (jsize)i, s);
        }
    }

    return arr;
}

jobjectArray Registry::GetValueNames(JNIEnv* env, jobject /*self*/, jlong handle)
{
    if (handle == 0)
        return 0;

    DWORD valueCount = 0;
    LONG result = RegQueryInfoKeyA((HKEY)handle, NULL, NULL, NULL, NULL, NULL, NULL, &valueCount, NULL, NULL, NULL, NULL);
    if (result != ERROR_SUCCESS)
        return NULL;

    char tmp[MAX_PATH];

    jclass clazz = env->FindClass("java/lang/String");
    if (!clazz)
        return NULL;

    jobjectArray arr = env->NewObjectArray((jsize)valueCount, clazz, NULL);
    if (!arr)
        return NULL;

    for (DWORD i = 0; i < valueCount; i++) {
        DWORD size = MAX_PATH;
        if (RegEnumValueA((HKEY)handle, i, tmp, &size, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
            jstring s = env->NewStringUTF(tmp);
            env->SetObjectArrayElement(arr, (jsize)i, s);
        }
    }

    return arr;
}

void Registry::DeleteSubKey(JNIEnv* env, jobject /*self*/, jlong handle, jstring subKey)
{
    if (handle == 0 || !subKey)
        return;

    jboolean iscopy = JNI_FALSE;
    const char* sk  = env->GetStringUTFChars(subKey, &iscopy);

    if (sk) {
#ifdef X64
        RegDeleteKeyExA((HKEY)handle, sk, KEY_WOW64_64KEY, 0);
#else
        RegDeleteKeyA((HKEY)handle, sk);
#endif
        env->ReleaseStringUTFChars(subKey, sk);
    }
}

void Registry::DeleteValue(JNIEnv* env, jobject /*self*/, jlong parent, jstring name)
{
    if (parent == 0 || !name)
        return;

    jboolean iscopy = JNI_FALSE;
    const char* str = env->GetStringUTFChars(name, &iscopy);

    if (str) {
        RegDeleteValueA((HKEY)parent, str);
        env->ReleaseStringUTFChars(name, str);
    }
}

jlong Registry::GetType(JNIEnv* env, jobject /*self*/, jlong parent, jstring name)
{
    if (parent == 0 || !name)
        return 0;

    jboolean iscopy = JNI_FALSE;
    const char* str = env->GetStringUTFChars(name, &iscopy);

    DWORD type   = 0;
    LONG  result = RegQueryValueExA((HKEY)parent, str, NULL, &type, NULL, NULL);

    env->ReleaseStringUTFChars(name, str);

    if (result == ERROR_SUCCESS)
        return (jlong)type;
    else
        return 0;
}

jstring Registry::GetString(JNIEnv* env, jobject /*self*/, jlong parent, jstring name)
{
    if (parent == 0 || !name)
        return 0;

    jboolean iscopy = JNI_FALSE;
    const char* str = env->GetStringUTFChars(name, &iscopy);

    DWORD type = 0;
    char  buffer[4096];
    DWORD len = sizeof(buffer);

    LONG result = RegQueryValueExA((HKEY)parent, str, 0, &type, (LPBYTE)buffer, &len);
    env->ReleaseStringUTFChars(name, str);

    if (result == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        buffer[sizeof(buffer) - 1] = 0;
        return env->NewStringUTF(buffer);
    } else {
        return 0;
    }
}

jbyteArray Registry::GetBinary(JNIEnv* env, jobject /*self*/, jlong parent, jstring name)
{
    if (parent == 0 || !name)
        return 0;

    jboolean iscopy = JNI_FALSE;
    const char* str = env->GetStringUTFChars(name, &iscopy);

    DWORD type = REG_BINARY;
    BYTE  buffer[4096];
    DWORD len = sizeof(buffer);

    LONG result = RegQueryValueExA((HKEY)parent, str, 0, &type, buffer, &len);
    env->ReleaseStringUTFChars(name, str);

    if (result == ERROR_SUCCESS && type == REG_BINARY) {
        jbyteArray arr = env->NewByteArray((jsize)len);
        if (arr)
            env->SetByteArrayRegion(arr, 0, (jsize)len, (jbyte*)buffer);
        return arr;
    } else {
        return 0;
    }
}

jlong Registry::GetDoubleWord(JNIEnv* env, jobject /*self*/, jlong parent, jstring name)
{
    if (parent == 0 || !name)
        return 0;

    jboolean iscopy = JNI_FALSE;
    const char* str = env->GetStringUTFChars(name, &iscopy);

    DWORD type  = REG_DWORD;
    DWORD value = 0;
    DWORD len   = sizeof(DWORD);

    LONG result = RegQueryValueExA((HKEY)parent, str, 0, &type, (LPBYTE)&value, &len);
    env->ReleaseStringUTFChars(name, str);

    if (result == ERROR_SUCCESS && type == REG_DWORD)
        return (jlong)value;
    else
        return 0;
}

jstring Registry::GetExpandedString(JNIEnv* env, jobject /*self*/, jlong parent, jstring name)
{
    if (parent == 0 || !name)
        return 0;

    jboolean iscopy = JNI_FALSE;
    const char* str = env->GetStringUTFChars(name, &iscopy);

    DWORD type   = REG_EXPAND_SZ;
    char  buffer[4096];
    DWORD len    = sizeof(buffer);

    LONG result = RegQueryValueExA((HKEY)parent, str, 0, &type, (LPBYTE)buffer, &len);
    env->ReleaseStringUTFChars(name, str);

    if (result == ERROR_SUCCESS && type == REG_EXPAND_SZ) {
        char expanded[4096];
        DWORD outLen = ExpandEnvironmentStringsA(buffer, expanded, (DWORD)sizeof(expanded));
        if (outLen == 0 || outLen > sizeof(expanded))
            return 0;
        expanded[sizeof(expanded) - 1] = 0;
        return env->NewStringUTF(expanded);
    } else {
        return 0;
    }
}

jobjectArray Registry::GetMultiString(JNIEnv* env, jobject /*self*/, jlong parent, jstring name)
{
    if (parent == 0 || !name)
        return 0;

    jboolean iscopy = JNI_FALSE;
    const char* str = env->GetStringUTFChars(name, &iscopy);

    DWORD type   = REG_MULTI_SZ;
    char  buffer[4096];
    DWORD len    = sizeof(buffer);

    LONG result = RegQueryValueExA((HKEY)parent, str, 0, &type, (LPBYTE)buffer, &len);
    env->ReleaseStringUTFChars(name, str);

    if (result != ERROR_SUCCESS || type != REG_MULTI_SZ)
        return 0;

    buffer[sizeof(buffer) - 1] = 0;

    int   count = 0;
    char* p     = buffer;
    while (*p) {
        size_t l = strlen(p);
        count++;
        p += l + 1;
    }

    jclass clazz = env->FindClass("java/lang/String");
    if (!clazz)
        return 0;

    jobjectArray arr = env->NewObjectArray(count, clazz, NULL);
    if (!arr)
        return 0;

    p = buffer;
    for (int i = 0; i < count; i++) {
        jstring s = env->NewStringUTF(p);
        env->SetObjectArrayElement(arr, i, s);
        p += strlen(p) + 1;
    }

    return arr;
}

void Registry::SetString(JNIEnv* env, jobject /*self*/, jlong parent, jstring name, jstring value)
{
    if (parent == 0 || !name || !value)
        return;

    jboolean iscopyName  = JNI_FALSE;
    jboolean iscopyValue = JNI_FALSE;

    const char* nameStr  = env->GetStringUTFChars(name, &iscopyName);
    const char* valueStr = env->GetStringUTFChars(value, &iscopyValue);

    if (nameStr && valueStr) {
        DWORD len = (DWORD)strlen(valueStr) + 1;
        RegSetValueExA((HKEY)parent, nameStr, 0, REG_SZ, (const BYTE*)valueStr, len);
    }

    if (nameStr)
        env->ReleaseStringUTFChars(name, nameStr);
    if (valueStr)
        env->ReleaseStringUTFChars(value, valueStr);
}

void Registry::SetBinary(JNIEnv* env, jobject /*self*/, jlong parent, jstring name, jarray value)
{
    if (parent == 0 || !name || !value)
        return;

    jboolean iscopy = JNI_FALSE;
    const char* nameStr = env->GetStringUTFChars(name, &iscopy);

    if (!nameStr)
        return;

    jsize len = env->GetArrayLength(value);
    void* data = env->GetPrimitiveArrayCritical(value, NULL);

    if (data) {
        RegSetValueExA((HKEY)parent, nameStr, 0, REG_BINARY, (const BYTE*)data, (DWORD)len);
        env->ReleasePrimitiveArrayCritical(value, data, 0);
    }

    env->ReleaseStringUTFChars(name, nameStr);
}

void Registry::SetDoubleWord(JNIEnv* env, jobject /*self*/, jlong parent, jstring name, jlong value)
{
    if (parent == 0 || !name)
        return;

    jboolean iscopy = JNI_FALSE;
    const char* nameStr = env->GetStringUTFChars(name, &iscopy);

    if (nameStr) {
        DWORD v = (DWORD)value;
        RegSetValueExA((HKEY)parent, nameStr, 0, REG_DWORD, (const BYTE*)&v, sizeof(DWORD));
        env->ReleaseStringUTFChars(name, nameStr);
    }
}

void Registry::SetMultiString(JNIEnv* /*env*/, jobject /*self*/, jlong /*parent*/, jstring /*name*/, jobjectArray /*value*/)
{
    // Not implemented
}

bool Registry::RegisterNatives(JNIEnv *env)
{
    Log::Info("Registering natives for Registry class");
    jclass clazz = JNI::FindClass(env, "org/boris/winrun4j/RegistryKey");
    if (clazz == NULL) {
        Log::Warning("Could not find RegistryKey class");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }

    JNINativeMethod methods[17];

    methods[0].name      = (char*)"closeKeyHandle";
    methods[0].signature = (char*)"(J)V";
    methods[0].fnPtr     = (void*)CloseKey;

    methods[1].name      = (char*)"deleteSubKey";
    methods[1].signature = (char*)"(JLjava/lang/String;)V";
    methods[1].fnPtr     = (void*)DeleteSubKey;

    methods[2].name      = (char*)"deleteValue";
    methods[2].signature = (char*)"(JLjava/lang/String;)V";
    methods[2].fnPtr     = (void*)DeleteValue;

    methods[3].name      = (char*)"getBinary";
    methods[3].signature = (char*)"(JLjava/lang/String;)[B";
    methods[3].fnPtr     = (void*)GetBinary;

    methods[4].name      = (char*)"getDoubleWord";
    methods[4].signature = (char*)"(JLjava/lang/String;)J";
    methods[4].fnPtr     = (void*)GetDoubleWord;

    methods[5].name      = (char*)"getExpandedString";
    methods[5].signature = (char*)"(JLjava/lang/String;)Ljava/lang/String;";
    methods[5].fnPtr     = (void*)GetExpandedString;

    methods[6].name      = (char*)"getMultiString";
    methods[6].signature = (char*)"(JLjava/lang/String;)[Ljava/lang/String;";
    methods[6].fnPtr     = (void*)GetMultiString;

    methods[7].name      = (char*)"getString";
    methods[7].signature = (char*)"(JLjava/lang/String;)Ljava/lang/String;";
    methods[7].fnPtr     = (void*)GetString;

    methods[8].name      = (char*)"getSubKeyNames";
    methods[8].signature = (char*)"(J)[Ljava/lang/String;";
    methods[8].fnPtr     = (void*)GetSubKeyNames;

    methods[9].name      = (char*)"getType";
    methods[9].signature = (char*)"(JLjava/lang/String;)J";
    methods[9].fnPtr     = (void*)GetType;

    methods[10].name      = (char*)"getValueNames";
    methods[10].signature = (char*)"(J)[Ljava/lang/String;";
    methods[10].fnPtr     = (void*)GetValueNames;

    methods[11].name      = (char*)"openKeyHandle";
    methods[11].signature = (char*)"(JLjava/lang/String;Z)J";
    methods[11].fnPtr     = (void*)OpenKey;

    methods[12].name      = (char*)"setBinary";
    methods[12].signature = (char*)"(JLjava/lang/String;[B)V";
    methods[12].fnPtr     = (void*)SetBinary;

    methods[13].name      = (char*)"setDoubleWord";
    methods[13].signature = (char*)"(JLjava/lang/String;J)V";
    methods[13].fnPtr     = (void*)SetDoubleWord;

    methods[14].name      = (char*)"setMultiString";
    methods[14].signature = (char*)"(JLjava/lang/String;[Ljava/lang/String;)V";
    methods[14].fnPtr     = (void*)SetMultiString;

    methods[15].name      = (char*)"setString";
    methods[15].signature = (char*)"(JLjava/lang/String;Ljava/lang/String;)V";
    methods[15].fnPtr     = (void*)SetString;

    methods[16].name      = (char*)"createSubKey";
    methods[16].signature = (char*)"(JLjava/lang/String;)J";
    methods[16].fnPtr     = (void*)CreateSubKey;

    env->RegisterNatives(clazz, methods, 17);
    if (env->ExceptionOccurred()) {
        JNI::PrintStackTrace(env);
        env->ExceptionClear();
        return false;
    }

    return true;
}
