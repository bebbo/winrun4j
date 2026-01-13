/*******************************************************************************
* This program and the accompanying materials
* are made available under the terms of the Common Public License v1.0
* which accompanies this distribution, and is available at 
* http://www.eclipse.org/legal/cpl-v10.html
* 
* Contributors:
*     Peter Smith
*******************************************************************************/

#include "Service.h"
#include "../common/INI.h"
#include "../common/Log.h"
#include "../java/JNI.h"
#include "../java/VM.h"
#include "../WinRun4J.h"

#include <windows.h>
#include <string.h>
#include <stdlib.h>

namespace 
{
    dictionary*           g_ini                = 0;
    char*                 g_serviceId          = 0;
    int                   g_controlsAccepted   = 0;
    int                   g_returnCode         = 0;
    SERVICE_STATUS        g_serviceStatus      = {};
    SERVICE_STATUS_HANDLE g_serviceStatusHandle = NULL;
    jclass                g_serviceClass       = 0;
    jobject               g_serviceInstance    = 0;
    jmethodID             g_controlMethod      = 0;
    jmethodID             g_mainMethod         = 0;
    HANDLE                g_event              = NULL;
}

#define SERVICE_ID               ":service.id"
#define SERVICE_NAME             ":service.name"
#define SERVICE_DESCRIPTION      ":service.description"
#define SERVICE_CONTROLS         ":service.controls"
#define SERVICE_STARTUP          ":service.startup"
#define SERVICE_DEPENDENCY       ":service.dependency"
#define SERVICE_USER             ":service.user"
#define SERVICE_PWD              ":service.password"
#define SERVICE_LOAD_ORDER_GROUP ":service.loadordergroup"

void WINAPI ServiceCtrlHandler(DWORD opCode)
{
    Log::Info("ServiceCtrlHandler: %d", opCode);

    switch (opCode)
    {
    case SERVICE_CONTROL_PAUSE:
        Service::Control(opCode);
        g_serviceStatus.dwCurrentState = SERVICE_PAUSED;
        break;

    case SERVICE_CONTROL_CONTINUE:
        Service::Control(opCode);
        g_serviceStatus.dwCurrentState = SERVICE_RUNNING;
        break;

    case SERVICE_CONTROL_SHUTDOWN:
    case SERVICE_CONTROL_STOP:
        Service::Control(opCode);
        g_serviceStatus.dwWin32ExitCode = 0;
        g_serviceStatus.dwCurrentState  = SERVICE_STOP_PENDING;
        g_serviceStatus.dwCheckPoint    = 0;
        g_serviceStatus.dwWaitHint      = 0;

        if (!SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus)) {
            Log::Error("Error in SetServiceStatus: %d", GetLastError());
        }

        VM::DetachCurrentThread();
        return;

    case SERVICE_INTERROGATE:
        break;
    }

    if (!SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus)) {
        Log::Error("Error in SetServiceStatus: %d", GetLastError());
    }
}

void WINAPI ServiceStart(DWORD argc, LPTSTR* argv)
{
    ZeroMemory(&g_serviceStatus, sizeof(g_serviceStatus));
    g_serviceStatus.dwServiceType             = SERVICE_WIN32;
    g_serviceStatus.dwCurrentState           = SERVICE_START_PENDING;
    g_serviceStatus.dwControlsAccepted       = g_controlsAccepted;
    g_serviceStatus.dwWin32ExitCode          = 0;
    g_serviceStatus.dwServiceSpecificExitCode = 0;
    g_serviceStatus.dwWaitHint               = 0;

    g_serviceStatusHandle = RegisterServiceCtrlHandlerA(g_serviceId, ServiceCtrlHandler);
    if (g_serviceStatusHandle == 0) {
        Log::Error("Error registering service control handler: %d", GetLastError());
        return;
    }

    Service::Main((int)argc, (char**)argv);
}

int Service::Initialise(dictionary* ini)
{
    g_ini = ini;

    g_serviceId = iniparser_getstr(ini, (char*)SERVICE_ID);
    if (g_serviceId == NULL) {
        Log::Error("Service ID not specified");
        return 1;
    }

    // Parse controls accepted
    char* controls = iniparser_getstr(ini, (char*)SERVICE_CONTROLS);
    if (controls) {
        int len = (int)strlen(controls);
        int nb  = 0;

        for (int i = 0; i < len; i++) {
            if (controls[i] == '|') {
                controls[i] = 0;
                nb++;
            }
        }

        char* p = controls;
        char* e = controls + len;
        for (int i = 0; i <= nb; i++) {
            int plen = (int)strlen(p);
            StrTrim(p, (char*)" ");
            if (strcmp("stop", p) == 0) {
                g_controlsAccepted |= SERVICE_ACCEPT_STOP;
            } else if (strcmp("shutdown", p) == 0) {
                g_controlsAccepted |= SERVICE_ACCEPT_SHUTDOWN;
            } else if (strcmp("pause", p) == 0) {
                g_controlsAccepted |= SERVICE_ACCEPT_PAUSE_CONTINUE;
            } else if (strcmp("param", p) == 0) {
                g_controlsAccepted |= SERVICE_ACCEPT_PARAMCHANGE;
            } else if (strcmp("netbind", p) == 0) {
                g_controlsAccepted |= SERVICE_ACCEPT_NETBINDCHANGE;
            } else if (strcmp("hardware", p) == 0) {
                g_controlsAccepted |= SERVICE_ACCEPT_HARDWAREPROFILECHANGE;
            } else if (strcmp("power", p) == 0) {
                g_controlsAccepted |= SERVICE_ACCEPT_POWEREVENT;
            } else if (strcmp("session", p) == 0) {
                g_controlsAccepted |= SERVICE_ACCEPT_SESSIONCHANGE;
            }

            p += plen + 1;
            if (p >= e)
                break;
        }
    } else {
        g_controlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    // Initialise JNI members
    JNIEnv* env = VM::GetJNIEnv();
    if (env == NULL) {
        Log::Error("JNIEnv is null");
        return 1;
    }

    char* svcClass = iniparser_getstr(ini, (char*)SERVICE_CLASS);
    if (!svcClass) {
        Log::Error("Service class not specified");
        return 1;
    }

    StrReplace(svcClass, '.', '/');
    g_serviceClass = JNI::FindClass(env, svcClass);
    if (g_serviceClass == NULL) {
        Log::Error("Could not find service class %s", svcClass);
        if (env->ExceptionCheck()) env->ExceptionClear();
        return 1;
    }

    jmethodID scon = env->GetMethodID(g_serviceClass, "<init>", "()V");
    if (scon == NULL) {
        Log::Error("Could not find service class default constructor");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return 1;
    }

    jobject localInstance = env->NewObject(g_serviceClass, scon);
    if (localInstance == NULL) {
        Log::Error("Could not create service class");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return 1;
    }

    g_serviceInstance = env->NewGlobalRef(localInstance);
    if (g_serviceInstance == NULL) {
        Log::Error("Could not create global ref for service instance");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return 1;
    }

    g_controlMethod = env->GetMethodID(g_serviceClass, "serviceRequest", "(I)I");
    if (g_controlMethod == NULL) {
        Log::Error("Could not find control method on service class");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return 1;
    }

    g_mainMethod = env->GetMethodID(g_serviceClass, "serviceMain", "([Ljava/lang/String;)I");
    if (g_mainMethod == NULL) {
        Log::Error("Could not find serviceMain method on service class");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return 1;
    }

    return 0;
}

int Service::Run(HINSTANCE /*hInstance*/, dictionary* ini, int argc, char* argv[])
{
	UNREFERENCED_PARAMETER(argc);
	UNREFERENCED_PARAMETER(argv);
    int result = Initialise(ini);
    if (result != 0) {
        Log::Error("Failed to initialise service: %d", result);
        return result;
    }

    SERVICE_TABLE_ENTRYA dispatchTable[] = {
        { g_serviceId, ServiceStart },
        { NULL,        NULL        }
    };

    if (!StartServiceCtrlDispatcherA(dispatchTable)) {
        Log::Error("Service control dispatcher error: %d", GetLastError());
        return 2;
    }

    return 0;
}

// Command line: "--WinRun4J:RegisterService"
int Service::Register(dictionary* ini)
{
    Log::Info("Registering Service...");

    g_serviceId = iniparser_getstr(ini, (char*)SERVICE_ID);
    if (g_serviceId == NULL) {
        Log::Error("Service ID not specified");
        return 1;
    }

    char* name = iniparser_getstr(ini, (char*)SERVICE_NAME);
    if (!name) {
        Log::Error("Service name not specified");
        return 1;
    }

    char* description = iniparser_getstr(ini, (char*)SERVICE_DESCRIPTION);
    if (!description) {
        Log::Error("Service description not specified");
        return 1;
    }

    DWORD startupMode = SERVICE_DEMAND_START;
    char* startup = iniparser_getstr(ini, (char*)SERVICE_STARTUP);
    if (startup != NULL) {
        if (strcmp(startup, "auto") == 0) {
            startupMode = SERVICE_AUTO_START;
            Log::Info("Service startup mode: SERVICE_AUTO_START");
        } else if (strcmp(startup, "boot") == 0) {
            startupMode = SERVICE_BOOT_START;
            Log::Info("Service startup mode: SERVICE_BOOT_START");
        } else if (strcmp(startup, "demand") == 0) {
            startupMode = SERVICE_DEMAND_START;
            Log::Info("Service startup mode: SERVICE_DEMAND_START");
        } else if (strcmp(startup, "disabled") == 0) {
            startupMode = SERVICE_DISABLED;
            Log::Info("Service startup mode: SERVICE_DISABLED");
        } else if (strcmp(startup, "system") == 0) {
            startupMode = SERVICE_SYSTEM_START;
            Log::Info("Service startup mode: SERVICE_SYSTEM_START");
        } else {
            Log::Warning("Unrecognized service startup mode: %s", startup);
        }
    }

    // Dependencies
    TCHAR* dependencies[MAX_PATH];
    UINT   depCount = 0;
    INI::GetNumberedKeysFromIni(ini, (char*)SERVICE_DEPENDENCY, dependencies, depCount);

    TCHAR* depList     = NULL;
    int    depListSize = 0;

    for (UINT i = 0; i < depCount; i++) {
        depListSize += (int)strlen(dependencies[i]) + 1;
    }
    depListSize++;

    if (depListSize > 0 && depCount > 0) {
        depList = (TCHAR*)malloc(depListSize);
        if (!depList) {
            Log::Error("Could not create dependency list");
            return 1;
        }

        TCHAR* depPointer = depList;
        for (UINT i = 0; i < depCount; i++) {
            size_t len = strlen(dependencies[i]);
            memcpy(depPointer, dependencies[i], len);
            depPointer += len;
            *depPointer++ = 0;
        }
        *depPointer = 0;
    }

    char* loadOrderGroup = iniparser_getstr(ini, (char*)SERVICE_LOAD_ORDER_GROUP);

    char* user = iniparser_getstr(ini, (char*)SERVICE_USER);
    char* pwd  = iniparser_getstr(ini, (char*)SERVICE_PWD);

    CHAR path[MAX_PATH];
    CHAR quotePath[MAX_PATH];

    quotePath[0] = '"';
    quotePath[1] = 0;

    GetModuleFileNameA(NULL, path, MAX_PATH);
    strcat_s(quotePath, sizeof(quotePath), path);
    strcat_s(quotePath, sizeof(quotePath), "\"");

    SC_HANDLE hScm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!hScm) {
        DWORD error = GetLastError();
        Log::Error("Could not access service manager: %d", error);
        if (depList) free(depList);
        return (int)error;
    }

    SC_HANDLE hSvc = CreateServiceA(
        hScm,
        g_serviceId,
        name,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        startupMode,
        SERVICE_ERROR_NORMAL,
        quotePath,
        loadOrderGroup,
        NULL,
        depList,
        user,
        pwd
    );

    if (!hSvc) {
        DWORD error = GetLastError();
        if (error == ERROR_SERVICE_EXISTS) {
            Log::Warning("Service already exists");
        } else {
            Log::Error("Could not create service: %d", error);
        }
        CloseServiceHandle(hScm);
        if (depList) free(depList);
        return (int)error;
    }

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);
    if (depList) free(depList);

    // Add description
    CHAR regPath[MAX_PATH];
    strcpy_s(regPath, sizeof(regPath), "System\\CurrentControlSet\\Services\\");
    strcat_s(regPath, sizeof(regPath), g_serviceId);

    HKEY key;
    if (RegOpenKeyA(HKEY_LOCAL_MACHINE, regPath, &key) == ERROR_SUCCESS) {
        RegSetValueExA(
            key,
            "Description",
            0,
            REG_SZ,
            (const BYTE*)description,
            (DWORD)(strlen(description) + 1)
        );
        RegCloseKey(key);
    } else {
        Log::Warning("Could not open registry key to set description");
    }

    return 0;
}

// Command line: "--WinRun4J:UnregisterService"
int Service::Unregister(dictionary* ini)
{
    Log::Info("Unregistering Service...");

    const char* serviceId = iniparser_getstr(ini, (char*)SERVICE_ID);
    if (serviceId == NULL) {
        Log::Error("Service ID not specified");
        return 1;
    }

    SC_HANDLE hScm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CREATE_SERVICE);
    if (!hScm) {
        DWORD error = GetLastError();
        Log::Error("Could not access service manager: %d", error);
        return (int)error;
    }

    SC_HANDLE hSvc = OpenServiceA(hScm, serviceId, DELETE | SERVICE_STOP);
    if (!hSvc) {
        DWORD error = GetLastError();
        Log::Error("Could not open service: %d", error);
        CloseServiceHandle(hScm);
        return (int)error;
    }

    BOOL ok = DeleteService(hSvc);
    DWORD lastError = GetLastError();

    CloseServiceHandle(hSvc);
    CloseServiceHandle(hScm);

    if (!ok) {
        Log::Error("DeleteService failed: %d", lastError);
        return (int)lastError;
    }

    return 0;
}

int Service::Control(DWORD opCode)
{
    JNIEnv* env = VM::GetJNIEnv();
    if (!env || !g_serviceInstance || !g_controlMethod)
        return 0;

    jint result = env->CallIntMethod(g_serviceInstance, g_controlMethod, (jint)opCode);
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
    return (int)result;
}

DWORD ServiceMainThread(LPVOID lpParam)
{
    JNIEnv* env = VM::GetJNIEnv(false);

    JNI::SetContextClassLoader(env, g_serviceInstance);

    jobject args = env->NewGlobalRef((jobject)lpParam);

    SetEvent(g_event);

    Log::Info("Service method starting...");

    g_returnCode = env->CallIntMethod(g_serviceInstance, g_mainMethod, args);

    Log::Info("Service method completed...");
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }

    VM::DetachCurrentThread();

    VM::CleanupVM();

    g_serviceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);

    env->DeleteGlobalRef(args);

    return (DWORD)g_returnCode;
}

int Service::Main(int argc, char* argv[])
{
    JNIEnv* env = VM::GetJNIEnv();
    if (!env)
        return 1;

    // INI args
    TCHAR* progargs[MAX_PATH];
    UINT   progargsCount = 0;
    INI::GetNumberedKeysFromIni(g_ini, (char*)PROG_ARG, progargs, progargsCount);

    jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass) {
        Log::Error("Could not find java/lang/String");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return 1;
    }

    jint totalArgs = (jint)(argc - 1 + progargsCount);
    jobjectArray jargs = env->NewObjectArray(totalArgs, stringClass, NULL);
    if (!jargs) {
        Log::Error("Could not allocate argument array");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return 1;
    }

    for (UINT i = 0; i < progargsCount; i++) {
        jstring s = env->NewStringUTF(progargs[i]);
        env->SetObjectArrayElement(jargs, (jsize)i, s);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return 1;
        }
    }

    for (int i = 0; i < argc - 1; i++) {
        jstring s = env->NewStringUTF(argv[i + 1]);
        env->SetObjectArrayElement(jargs, (jsize)(progargsCount + i), s);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return 1;
        }
    }

    jargs = (jobjectArray)env->NewGlobalRef(jargs);

    Log::Info("Service startup initiated with %u INI args and %d Ctrl Manager args",
              progargsCount, argc - 1);

    g_event = CreateEventA(NULL, TRUE, FALSE, NULL);

    g_serviceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);

    CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ServiceMainThread, jargs, 0, NULL);

    WaitForSingleObject(g_event, INFINITE);

    env->DeleteGlobalRef(jargs);

    VM::DetachCurrentThread();

    return 0;
}

void Service::Shutdown(int exitCode)
{
    if (g_serviceId != 0) {
        g_serviceStatus.dwWin32ExitCode = (DWORD)exitCode;
        g_serviceStatus.dwCurrentState  = SERVICE_STOPPED;
        g_serviceStatus.dwCheckPoint    = 0;
        g_serviceStatus.dwWaitHint      = 0;

        if (!SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus)) {
            Log::Error("Error in SetServiceStatus: 0x%x", GetLastError());
        }
    }
}

extern "C" __declspec(dllexport) BOOL __cdecl Service_SetStatus(DWORD dwCurrentState, DWORD dwWaitHint)
{
    g_serviceStatus.dwCurrentState = dwCurrentState;
    g_serviceStatus.dwWaitHint     = dwWaitHint;
    return SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
}
