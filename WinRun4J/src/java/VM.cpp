/*******************************************************************************
 * This program and the accompanying materials
 * are made available under the terms of the Common Public License v1.0
 * which accompanies this distribution, and is available at 
 * http://www.eclipse.org/legal/cpl-v10.html
 * 
 * Contributors:
 *     Peter Smith
 *******************************************************************************/

#include "VM.h"
#include "JNI.h"
#include "../common/Log.h"
#include "../common/INI.h"
#include "../launcher/Service.h"

#include <windows.h>
#include <string.h>
#include <stdlib.h>

// VM Registry keys
#define JRE_REG_PATH             TEXT("Software\\JavaSoft\\Java Runtime Environment")
#define JRE_REG_PATH_NEW         TEXT("Software\\JavaSoft\\JRE")
#define JRE_REG_PATH_WOW6432     TEXT("Software\\Wow6432Node\\JavaSoft\\Java Runtime Environment")
#define IBM_JRE_REG_PATH         TEXT("Software\\IBM\\Java2 Runtime Environment")
#define IBM_JRE_REG_PATH_WOW6432 TEXT("Software\\Wow6432Node\\IBM\\Java2 Runtime Environment")
#define JRE_VERSION_KEY          TEXT("CurrentVersion")
#define JRE_LIB_KEY              TEXT("RuntimeLib")

namespace 
{
    HINSTANCE g_hInstance  = 0;
    HMODULE   g_jniLibrary = 0;
    JavaVM*   jvm          = 0;
    JNIEnv*   env          = 0;
}

typedef jint (JNICALL *JNI_createJavaVM)(JavaVM **pvm, JNIEnv **env, void *args);

JavaVM* VM::GetJavaVM()
{
    return jvm;
}

JNIEnv* VM::GetJNIEnv(bool daemon)
{
    if (!jvm)
        return NULL;

    JNIEnv* e = 0;
    if (daemon) {
        jvm->AttachCurrentThreadAsDaemon((void**)&e, NULL);
    } else {
        jvm->AttachCurrentThread((void**)&e, NULL);
    }
    return e;
}

void VM::DetachCurrentThread()
{
    if (jvm)
        jvm->DetachCurrentThread();
}

char* VM::FindJavaVMLibrary(dictionary *ini)
{
    int findSystemVmFirst = iniparser_getboolean(ini, (char*)VM_SYSFIRST, 0);

    char* vmDefaultLocation = GetJavaVMLibrary(
        iniparser_getstr(ini, (char*)VM_VERSION),
        iniparser_getstr(ini, (char*)VM_VERSION_MIN),
        iniparser_getstr(ini, (char*)VM_VERSION_MAX)
    );

    if (findSystemVmFirst && vmDefaultLocation != NULL)
        return vmDefaultLocation;

    char* vmLocations = iniparser_getstr(ini, (char*)VM_LOCATION);
    Log::Info("Configured vm.location: %s", vmLocations ? vmLocations : "(null)");

    if (vmLocations != NULL)
    {
        char defWorkingDir[MAX_PATH];
        char* workingDir = iniparser_getstr(ini, (char*)WORKING_DIR);

        if (!workingDir) {
            GetCurrentDirectoryA(MAX_PATH, defWorkingDir);
            SetCurrentDirectoryA(iniparser_getstr(ini, (char*)INI_DIR));
        }

        char* ctx = NULL;
        char* vmLocation = strtok_s(vmLocations, "|", &ctx);

        while (vmLocation != NULL)
        {
            DWORD fileAttr = GetFileAttributesA(vmLocation);
            if (fileAttr != INVALID_FILE_ATTRIBUTES)
            {
                char vmFull[MAX_PATH];
                GetFullPathNameA(vmLocation, MAX_PATH, vmFull, NULL);

                if (!workingDir) {
                    SetCurrentDirectoryA(defWorkingDir);
                }

                return _strdup(vmFull);
            }

            Log::Info("vm.location item not found: %s", vmLocation);
            vmLocation = strtok_s(NULL, "|", &ctx);
        }

        if (!workingDir) {
            SetCurrentDirectoryA(defWorkingDir);
        }

        return NULL;
    }

    return vmDefaultLocation;
}

// Find an appropriate VM library
char* VM::GetJavaVMLibrary(LPSTR version, LPSTR min, LPSTR max)
{
    CHAR   filename[MAX_PATH];
    HKEY   hKey, hVersionKey;
    DWORD  numVersions = 255;
    Version versions[255];

    FindVersions(versions, &numVersions);

    Version* v = FindVersion(versions, numVersions, version, min, max);
    if (!v)
        return NULL;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, v->GetRegPath(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return NULL;

    if (RegOpenKeyExA(hKey, v->GetVersionStr(), 0, KEY_READ, &hVersionKey) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return NULL;
    }

    DWORD length = sizeof(filename);
    if (RegQueryValueExA(hVersionKey, "RuntimeLib", NULL, NULL, (LPBYTE)&filename, &length) != ERROR_SUCCESS) {
        RegCloseKey(hVersionKey);
        RegCloseKey(hKey);
        return NULL;
    }

#ifdef X64
    HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (hFile == INVALID_HANDLE_VALUE) {
        int len = (int)strlen(filename);
        if (len > 14 && strcmp(&filename[len - 14], "client\\jvm.dll") == 0) {
            const char replace[] = "server";
            for (int i = 0; i < 6; i++) {
                filename[len - 14 + i] = replace[i];
            }
        }
    } else {
        CloseHandle(hFile);
    }
#endif

    RegCloseKey(hVersionKey);
    RegCloseKey(hKey);

    return _strdup(filename);
}

Version* VM::FindVersion(Version* versions, DWORD numVersions, LPSTR version, LPSTR min, LPSTR max)
{
    if (version != NULL)
    {
        Version v;
        v.Parse(version);
        for (DWORD i = 0; i < numVersions; i++) {
            if (v.Compare(versions[i]) == 0)
                return &versions[i];
        }
        return NULL;
    }

    Version minV, maxV;
    if (min != NULL) minV.Parse(min);
    if (max != NULL) maxV.Parse(max);

    Version* maxVer = NULL;
    for (DWORD i = 0; i < numVersions; i++) {
        bool higher =
            (min == NULL || minV.Compare(versions[i]) <= 0) &&
            (max == NULL || maxV.Compare(versions[i]) >= 0) &&
            (maxVer == NULL || maxVer->Compare(versions[i]) < 0);

        if (higher)
            maxVer = &versions[i];
    }

    return maxVer;
}

void VM::FindVersions(Version* versions, DWORD* numVersions)
{
    HKEY  hKey;
    DWORD length;
    CHAR  version[MAX_PATH];
    DWORD size = *numVersions;

    *numVersions = 0;

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\JavaSoft\\Java Runtime Environment", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        for (; *numVersions < size; (*numVersions)++) {
            length = MAX_PATH;
            if (RegEnumKeyExA(hKey, *numVersions, version, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;

            versions[*numVersions].Parse(version);
            versions[*numVersions].SetRegPath("Software\\JavaSoft\\Java Runtime Environment");
        }
        RegCloseKey(hKey);
    }

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\JavaSoft\\JRE", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        for (; *numVersions < size; (*numVersions)++) {
            length = MAX_PATH;
            if (RegEnumKeyExA(hKey, *numVersions, version, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;

            versions[*numVersions].Parse(version);
            versions[*numVersions].SetRegPath("Software\\JavaSoft\\JRE");
        }
        RegCloseKey(hKey);
    }

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\IBM\\Java2 Runtime Environment", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD offset = *numVersions;
        for (; *numVersions < size; (*numVersions)++) {
            length = MAX_PATH;
            if (RegEnumKeyExA(hKey, *numVersions - offset, version, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;

            versions[*numVersions].Parse(version);
            versions[*numVersions].SetRegPath("Software\\IBM\\Java2 Runtime Environment");
        }
        RegCloseKey(hKey);
    }

#ifndef X64
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Wow6432Node\\JavaSoft\\Java Runtime Environment", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        for (; *numVersions < size; (*numVersions)++) {
            length = MAX_PATH;
            if (RegEnumKeyExA(hKey, *numVersions, version, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;

            versions[*numVersions].Parse(version);
            versions[*numVersions].SetRegPath("Software\\Wow6432Node\\JavaSoft\\Java Runtime Environment");
        }
        RegCloseKey(hKey);
    }

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Software\\Wow6432Node\\IBM\\Java2 Runtime Environment", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD offset = *numVersions;
        for (; *numVersions < size; (*numVersions)++) {
            length = MAX_PATH;
            if (RegEnumKeyExA(hKey, *numVersions - offset, version, &length, NULL, NULL, NULL, NULL) != ERROR_SUCCESS)
                break;

            versions[*numVersions].Parse(version);
            versions[*numVersions].SetRegPath("Software\\Wow6432Node\\IBM\\Java2 Runtime Environment");
        }
        RegCloseKey(hKey);
    }
#endif
}

int Version::Compare(Version& other) 
{
    for (int index = 0; index < 10; index++) {
        DWORD v1 = VersionPart[index];
        DWORD v2 = other.VersionPart[index];
        if (v1 != v2)
            return (v1 > v2) ? 1 : -1;
    }
    return 0;
}

void Version::Parse(LPSTR version)
{
    strcpy_s(VersionStr, sizeof(VersionStr), version);

    int   index = 0;
    CHAR  v[MAX_PATH];
    char* ctx   = NULL;

    strcpy_s(v, sizeof(v), version);
    char* token = strtok_s(v, "._", &ctx);

    while (token != NULL && index < 10) {
        VersionPart[index++] = (DWORD)atoi(token);
        token = strtok_s(NULL, "._", &ctx);
    }

    for (; index < 10; index++) {
        VersionPart[index] = 0;
    }
    Parsed = true;
}

void VM::ExtractSpecificVMArgs(dictionary* ini, char*** args, UINT& count)
{
    MEMORYSTATUS ms;
    GlobalMemoryStatus(&ms);

#ifdef X64
    int overallMax = 8000;
#else
    int overallMax = 1530;
#endif

    int availMax = (int)(ms.dwTotalPhys / 1024 / 1024) - 80;

    auto appendArg = [&](const char* value)
    {
        char* dup = _strdup(value);
        if (!dup)
            return;

        char** newArr = (char**)realloc(*args, sizeof(char*) * (count + 1));
        if (!newArr) {
            free(dup);
            return;
        }

        *args = newArr;
        (*args)[count++] = dup;
    };

    // ------------------------------------------------------------
    // Preferred heap size: -XmxNNNm
    // ------------------------------------------------------------
    char* PreferredHeapSizeStr = iniparser_getstr(ini, (char*)HEAP_SIZE_PREFERRED);
    if (PreferredHeapSizeStr != NULL) {
        int sizeMeg = atoi(PreferredHeapSizeStr);
        if (sizeMeg > availMax)
            sizeMeg = availMax;

        char sizeArg[64];
        _snprintf_s(sizeArg, sizeof(sizeArg), _TRUNCATE, "-Xmx%um", sizeMeg);
        appendArg(sizeArg);
    }

    // ------------------------------------------------------------
    // Max heap size percent: -XmxNNNm
    // ------------------------------------------------------------
    char* MaxHeapSizePercentStr = iniparser_getstr(ini, (char*)HEAP_SIZE_MAX_PERCENT);
    if (MaxHeapSizePercentStr != NULL && PreferredHeapSizeStr == NULL) {

        double percent = atof(MaxHeapSizePercentStr);
        if (percent < 0 || percent > 100) {
            Log::Error("Error with heap size percent. Should be between 0 and 100.");
        } else {
            Log::Info("Percent is: %u", (unsigned int)percent);
            Log::Info("Avail Phys: %dm", availMax);

            double size = (percent / 100.0) * (double)availMax;
            if (size > overallMax)
                size = overallMax;

            char sizeArg[64];
            _snprintf_s(sizeArg, sizeof(sizeArg), _TRUNCATE, "-Xmx%um", (UINT)size);
            appendArg(sizeArg);
        }
    }

    // ------------------------------------------------------------
    // Min heap size percent: -XmsNNNm
    // ------------------------------------------------------------
    char* MinHeapSizePercentStr = iniparser_getstr(ini, (char*)HEAP_SIZE_MIN_PERCENT);
    if (MinHeapSizePercentStr != NULL) {

        double percent = atof(MinHeapSizePercentStr);
        if (percent < 0 || percent > 100) {
            Log::Warning("Error with heap size percent. Should be between 0 and 100.");
        } else {
            Log::Info("Percent is: %f", percent);
            Log::Info("Avail Phys: %dm", availMax);

            int size = (int)((percent / 100.0) * (double)availMax);
            if (size > overallMax)
                size = overallMax;

            char sizeArg[64];
            _snprintf_s(sizeArg, sizeof(sizeArg), _TRUNCATE, "-Xms%um", size);
            appendArg(sizeArg);
        }
    }

    // ------------------------------------------------------------
    // java.library.path entries
    // ------------------------------------------------------------
    char** libPaths = NULL;
    UINT   libPathsCount = 0;

    INI::GetNumberedKeysFromIni(ini, (char*)JAVA_LIBRARY_PATH, &libPaths, libPathsCount, 10);

    if (libPathsCount > 0) {

        char libPathArg[4096];
        libPathArg[0] = 0;

        strcpy_s(libPathArg, "-Djava.library.path=");

        for (UINT i = 0; i < libPathsCount; i++) {
            strcat_s(libPathArg, libPaths[i]);
            strcat_s(libPathArg, ";");
            free(libPaths[i]);
        }

        free(libPaths);

        appendArg(libPathArg);
    }
}

void VM::LoadRuntimeLibrary(TCHAR* libPath)
{
    int len = (int)strlen(libPath);
    char binPath[MAX_PATH];
    strcpy_s(binPath, sizeof(binPath), libPath);

    int i, sc = 0;
    for (i = len - 1; i >= 0; i--) {
        if (binPath[i] == '\\') {
            binPath[i] = 0;
            sc++;
            if (sc > 1)
                break;
        }
    }

    strcat_s(binPath, sizeof(binPath), "\\msvcr71.dll");
    if (!LoadLibraryA(binPath)) {
        binPath[i] = 0;
        strcat_s(binPath, sizeof(binPath), "\\msvcrt.dll");
        if (!LoadLibraryA(binPath)) {
            binPath[i] = 0;
            strcat_s(binPath, sizeof(binPath), "\\msvcr100.dll");
            if (!LoadLibraryA(binPath)) {
                typedef BOOL (WINAPI *LPFNSetDllDirectoryA)(LPCSTR lpPathname);
                HINSTANCE hKernel32 = GetModuleHandleA("kernel32");
                LPFNSetDllDirectoryA lpfnSetDllDirectory =
                    (LPFNSetDllDirectoryA)GetProcAddress(hKernel32, "SetDllDirectoryA");
                if (lpfnSetDllDirectory != NULL) {
                    binPath[i] = 0;
                    lpfnSetDllDirectory(binPath);
                }
            }
        }
    }
}

int VM::StartJavaVM(TCHAR* libPath, TCHAR* vmArgs[], HINSTANCE hInstance)
{
    g_hInstance = hInstance;

    LoadRuntimeLibrary(libPath);

    g_jniLibrary = LoadLibraryA(libPath);
    if (!g_jniLibrary) {
        Log::Error("ERROR: Could not load library: %s", libPath);
        return -1;
    }

    JNI_createJavaVM createJavaVM =
        (JNI_createJavaVM)GetProcAddress(g_jniLibrary, "JNI_CreateJavaVM");

    if (!createJavaVM) {
        Log::Error("ERROR: Could not find JNI_CreateJavaVM function");
        return -1;
    }

    int numVMArgs = 0;
    while (vmArgs[numVMArgs] != NULL)
        numVMArgs++;

    const int numHooks = 2;
    JavaVMOption* options = (JavaVMOption*)malloc((numVMArgs + numHooks) * sizeof(JavaVMOption));

    for (int i = 0; i < numVMArgs; i++) {
        options[i].optionString = _strdup(vmArgs[i]);
        options[i].extraInfo    = 0;
    }

    options[numVMArgs].optionString     = (char*)"abort";
    options[numVMArgs].extraInfo        = (void*)&VM::AbortHook;
    options[numVMArgs + 1].optionString = (char*)"exit";
    options[numVMArgs + 1].extraInfo    = (void*)&VM::ExitHook;

    JavaVMInitArgs init_args;
    init_args.version            = JNI_VERSION_1_2;
    init_args.options            = options;
    init_args.nOptions           = numVMArgs + numHooks;
    init_args.ignoreUnrecognized = JNI_TRUE;

    int result = createJavaVM(&jvm, &env, &init_args);

    for (int i = 0; i < numVMArgs; i++) {
        free(options[i].optionString);
    }
    free(options);

    return result;
}

int VM::CleanupVM() 
{
    if (jvm == 0 || env == 0) {
        if (g_jniLibrary) {
            FreeLibrary(g_jniLibrary);
            g_jniLibrary = 0;
        }
        return 1;
    }

    JNIEnv* e = VM::GetJNIEnv(true);
    if (e) {
        JNI::PrintStackTrace(e);
    }

    int result = jvm->DestroyJavaVM();

    if (g_jniLibrary) {
        FreeLibrary(g_jniLibrary);
        g_jniLibrary = 0;
    }

    env = 0;
    jvm = 0;

    return result;
}

void VM::AbortHook()
{
    Log::Error("Application aborted.");
    Service::Shutdown(255);
}

void VM::ExitHook(int status)
{
    Log::Info("Application exited (%d).", status);
    Service::Shutdown(status);
}
