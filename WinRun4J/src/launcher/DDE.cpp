/*******************************************************************************
* This program and the accompanying materials
* are made available under the terms of the Common Public License v1.0
* which accompanies this distribution, and is available at
* http://www.eclipse.org/legal/cpl-v10.html
*
* Contributors:
*     Peter Smith
*******************************************************************************/

#include "DDE.h"
#include "../common/Log.h"
#include "../java/VM.h"
#include "../java/JNI.h"

#include <string.h>
#include <stdlib.h>

static dictionary* g_ini           = 0;
static HWND        g_hWnd          = NULL;
static DWORD       g_pidInst       = 0;
static HSZ         g_serverName    = 0;
static HSZ         g_topic         = 0;
static char        g_execute[MAX_PATH];
static jclass      g_class         = 0;
static jmethodID   g_executeMethodID  = 0;
static jmethodID   g_activateMethodID = 0;
static bool        g_ready         = 0;
static LPSTR*      g_buffer        = NULL;
static int         g_buffer_ix     = 0;
static int         g_buffer_siz    = 0;

// INI keys
#define DDE_CLASS          ":dde.class"
#define DDE_ENABLED        ":dde.enabled"
#define DDE_WINDOW_CLASS   ":dde.window.class"
#define DDE_SERVER_NAME    ":dde.server.name"
#define DDE_TOPIC          ":dde.topic"

// Single instance
#define DDE_EXECUTE_ACTIVATE "ACTIVATE"

LRESULT CALLBACK DdeMainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

HDDEDATA CALLBACK DdeCallback(
    UINT      uType,
    UINT      /*uFmt*/,
    HCONV     /*hconv*/,
    HDDEDATA  hsz1,
    HDDEDATA  hsz2,
    HDDEDATA  hdata,
    HDDEDATA  /*dwData1*/,
    HDDEDATA  /*dwData2*/)
{
    switch (uType)
    {
    case XTYP_CONNECT:
        if (hsz2 == (HDDEDATA)g_serverName && hsz1 == (HDDEDATA)g_topic)
            return (HDDEDATA)1;
        break;

    case XTYP_EXECUTE:
    {
        UINT size = DdeGetData(hdata, NULL, 0, 0);
        if (size == 0)
            break;

        LPSTR execData = (LPSTR)malloc(size);
        if (!execData)
            break;

        DdeGetData(hdata, (LPBYTE)execData, size, 0);
        DDE::Execute(execData);
        free(execData);

        return (HDDEDATA)1;
    }
    }

    return 0;
}

bool DDE::RegisterDDE()
{
    UINT result = DdeInitialize(&g_pidInst, (PFNCALLBACK)&DdeCallback, 0, 0);
    if (result != DMLERR_NO_ERROR) {
        Log::Error("Unable to initialize DDE: %d", result);
        return false;
    }

    char* appName = iniparser_getstr(g_ini, (char*)DDE_SERVER_NAME);
    char* topic   = iniparser_getstr(g_ini, (char*)DDE_TOPIC);

    g_serverName = DdeCreateStringHandleA(
        g_pidInst,
        appName ? appName : (char*)"WinRun4J",
        CP_WINANSI
    );
    g_topic = DdeCreateStringHandleA(
        g_pidInst,
        topic ? topic : (char*)"system",
        CP_WINANSI
    );

    DdeNameService(g_pidInst, g_serverName, NULL, DNS_REGISTER);
    return true;
}

DWORD WINAPI DdeWindowThreadProc(LPVOID lpParam)
{
    DDE::RegisterWindow((HINSTANCE)lpParam);

    if (!DDE::RegisterDDE())
        return 1;

    char* clsName = iniparser_getstr(g_ini, (char*)DDE_WINDOW_CLASS);

    g_hWnd = CreateWindowExA(
        0,
        clsName ? clsName : "WinRun4J.DDEWndClass",
        "WinRun4J.DDEWindow",
        0,
        0, 0,
        0, 0,
        NULL, NULL, NULL, NULL
    );

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

bool DDE::Initialize(HINSTANCE hInstance, JNIEnv* env, dictionary* ini)
{
    char* ddeEnabled = iniparser_getstr(ini, (char*)DDE_ENABLED);
    if (!ddeEnabled || strcmp("true", ddeEnabled) != 0)
        return false;

    Log::Info("Initializing DDE");
    g_ini = ini;

    if (!RegisterNatives(env, ini))
        return false;

    CreateThread(NULL, 0, DdeWindowThreadProc, (LPVOID)hInstance, 0, NULL);
    return true;
}

void DDE::Uninitialize()
{
    if (g_serverName)
        DdeFreeStringHandle(g_pidInst, g_serverName);
    if (g_topic)
        DdeFreeStringHandle(g_pidInst, g_topic);

    DdeUninitialize(g_pidInst);
}

bool DDE::NotifySingleInstance(dictionary* ini)
{
    g_ini = ini;

    UINT result = DdeInitialize(&g_pidInst, (PFNCALLBACK)&DdeCallback, 0, 0);
    if (result != DMLERR_NO_ERROR) {
        Log::Error("Unable to initialize DDE: %d", result);
        return false;
    }

    char* appName = iniparser_getstr(g_ini, (char*)DDE_SERVER_NAME);
    char* topic   = iniparser_getstr(g_ini, (char*)DDE_TOPIC);

    g_serverName = DdeCreateStringHandleA(
        g_pidInst,
        appName ? appName : (char*)"WinRun4J",
        CP_WINANSI
    );
    g_topic = DdeCreateStringHandleA(
        g_pidInst,
        topic ? topic : (char*)"system",
        CP_WINANSI
    );

    HCONV conv = DdeConnect(g_pidInst, g_serverName, g_topic, NULL);
    if (conv != NULL) {
        LPSTR cmdline = StripArg0(GetCommandLineA());
        size_t len_activate = strlen(DDE_EXECUTE_ACTIVATE);
        size_t len_cmd      = strlen(cmdline);
        size_t total        = len_activate + 1 + len_cmd + 1;

        char* activate = (char*)malloc(total);
        if (!activate) {
            DDE::Uninitialize();
            return false;
        }

        strcpy_s(activate, total, DDE_EXECUTE_ACTIVATE);
        strcat_s(activate, total, " ");
        strcat_s(activate, total, cmdline);

        HDDEDATA txResult = DdeClientTransaction(
            (LPBYTE)activate,
            (DWORD)total,
            conv,
            NULL,
            0,
            XTYP_EXECUTE,
            TIMEOUT_ASYNC,
            NULL
        );

        free(activate);

        if (txResult == 0) {
            Log::Error("Failed to send DDE single instance notification");
            DDE::Uninitialize();
            return false;
        }
    } else {
        Log::Error("Unable to create DDE conversation");
    }

    DDE::Uninitialize();
    return true;
}

void DDE::Execute(LPSTR lpExecuteStr)
{
    JNIEnv* env = VM::GetJNIEnv(true);
    if (!env)
        return;
    if (!g_class)
        return;
    if (!g_executeMethodID)
        return;

    if (g_ready) {
        Log::Info("DDE Execute: %s", lpExecuteStr ? lpExecuteStr : "");

        if (lpExecuteStr && memcmp(lpExecuteStr, DDE_EXECUTE_ACTIVATE, 8) == 0) {
            if (g_activateMethodID != NULL) {
                jstring str = 0;
                if (lpExecuteStr && strlen(lpExecuteStr) > 9)
                    str = env->NewStringUTF(&lpExecuteStr[9]);
                env->CallStaticVoidMethod(g_class, g_activateMethodID, str);
            } else {
                Log::Error("Ignoring DDE single instance activate message");
            }
        } else {
            jstring str = 0;
            if (lpExecuteStr)
                str = env->NewStringUTF(lpExecuteStr);
            env->CallStaticVoidMethod(g_class, g_executeMethodID, str);
        }

        if (env->ExceptionOccurred()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
    } else {
        if (!lpExecuteStr)
            return;

        size_t len = strlen(lpExecuteStr) + 1;
        LPSTR buffered = (LPSTR)malloc(len);
        if (!buffered)
            return;

        strcpy_s(buffered, len, lpExecuteStr);

        if (g_buffer == NULL) {
            g_buffer_siz = 10;
            g_buffer = (LPSTR*)malloc(sizeof(LPSTR) * g_buffer_siz);
            if (!g_buffer) {
                free(buffered);
                return;
            }
        } else if (g_buffer_ix >= g_buffer_siz) {
            g_buffer_siz += 10;
            LPSTR* new_buffer = (LPSTR*)malloc(sizeof(LPSTR) * g_buffer_siz);
            if (!new_buffer) {
                free(buffered);
                return;
            }
            memcpy(new_buffer, g_buffer, sizeof(LPSTR) * g_buffer_ix);
            free(g_buffer);
            g_buffer = new_buffer;
        }
        g_buffer[g_buffer_ix++] = buffered;
    }
}

void DDE::Ready()
{
    if (g_ready)
        return;

    g_ready = true;

    for (int i = 0; i < g_buffer_ix; i++) {
        LPSTR lpExecuteStr = g_buffer[i];
        DDE::Execute(lpExecuteStr);
        free(lpExecuteStr);
    }
    free(g_buffer);
    g_buffer = NULL;
    g_buffer_ix = 0;
    g_buffer_siz = 0;
}

extern "C" __declspec(dllexport) void DDE_Ready()
{
    DDE::Ready();
}

void DDE::RegisterWindow(HINSTANCE hInstance)
{
    WNDCLASSEXA wcx;
    ZeroMemory(&wcx, sizeof(wcx));

    wcx.cbSize        = sizeof(wcx);
    wcx.style         = CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW;
    wcx.lpfnWndProc   = DdeMainWndProc;
    wcx.cbClsExtra    = 0;
    wcx.cbWndExtra    = DLGWINDOWEXTRA;
    wcx.hInstance     = hInstance;
    wcx.hIcon         = 0;
    wcx.hCursor       = LoadCursor(NULL, IDC_WAIT);
    wcx.hbrBackground = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
    wcx.lpszMenuName  = 0;

    char* clsName     = iniparser_getstr(g_ini, (char*)DDE_WINDOW_CLASS);
    wcx.lpszClassName = clsName ? clsName : (char*)"WinRun4J.DDEWndClass";
    wcx.hIconSm       = 0;

    if (!RegisterClassExA(&wcx)) {
        Log::Error("Could not register DDE window class");
        return;
    }
}

bool DDE::RegisterNatives(JNIEnv* env, dictionary* ini)
{
    char* ddeClassName = iniparser_getstr(ini, (char*)DDE_CLASS);
    if (ddeClassName != NULL) {
        int strl = (int)strlen(ddeClassName);
        for (int i = 0; i < strl; i++) {
            if (ddeClassName[i] == '.')
                ddeClassName[i] = '/';
        }
        g_class = JNI::FindClass(env, ddeClassName);
    } else {
        g_class = JNI::FindClass(env, "org/boris/winrun4j/DDE");
    }

    if (g_class == NULL) {
        Log::Error("Could not find DDE class.");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }

    g_class = (jclass)env->NewGlobalRef(g_class);

    g_executeMethodID = env->GetStaticMethodID(g_class, "execute", "(Ljava/lang/String;)V");
    if (g_executeMethodID == NULL) {
        Log::Error("Could not find execute method");
        if (env->ExceptionCheck()) env->ExceptionClear();
        return false;
    }

    g_activateMethodID = env->GetStaticMethodID(g_class, "activate", "(Ljava/lang/String;)V");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    return true;
}

int DDE::EnumFileAssocations(dictionary* ini, bool isRegister, int (*CallbackFunc)(DDEInfo&))
{
    char key[MAX_PATH];
    int  res = 0;

    for (int i = 1;; i++) {
        DDEInfo info;
        info.ini = ini;

        sprintf_s(key, sizeof(key), "FileAssociations:file.%d.extension", i);
        info.extension = iniparser_getstr(ini, key);
        if (info.extension == NULL)
            break;

        Log::Info(isRegister ? "Registering %s" : "Unregistering %s", info.extension);

        sprintf_s(key, sizeof(key), "FileAssociations:file.%d.name", i);
        info.name = iniparser_getstr(ini, key);
        if (info.name == NULL) {
            Log::Error("Name not specified for extension: %s", info.extension);
            return 1;
        }

        sprintf_s(key, sizeof(key), "FileAssociations:file.%d.description", i);
        info.description = iniparser_getstr(ini, key);
        if (info.description == NULL) {
            Log::Warning("Description not specified for extension: %s", info.extension);
        }

        res = CallbackFunc(info);
        if (res)
            return res;
    }

    return res;
}

int DDE::RegisterFileAssociations(dictionary* ini)
{
    return EnumFileAssocations(ini, true, RegisterFileAssociation);
}

int DDE::RegisterFileAssociation(DDEInfo& info)
{
    DWORD dwDisp;
    HKEY  hKey;

    if (RegCreateKeyExA(HKEY_CLASSES_ROOT, info.extension, 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, &dwDisp))
    {
        Log::Error("ERROR: Could not create extension key: %s", info.extension);
        return 1;
    }

    if (RegSetValueExA(hKey, NULL, 0, REG_SZ,
                       (const BYTE*)info.name,
                       (DWORD)strlen(info.name) + 1))
    {
        Log::Error("ERROR: Could not set name for extension: %s", info.extension);
        return 1;
    }

    if (RegCreateKeyExA(HKEY_CLASSES_ROOT, info.name, 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, &dwDisp))
    {
        Log::Error("ERROR: Could not create name key: %s", info.name);
        return 1;
    }

    if (info.description) {
        if (RegSetValueExA(hKey, NULL, 0, REG_SZ,
                           (const BYTE*)info.description,
                           (DWORD)strlen(info.description) + 1))
        {
            Log::Error("ERROR: Could not set description for extension: %s", info.extension);
            return 1;
        }
    }

    if (RegCreateKeyExA(HKEY_CLASSES_ROOT, info.name, 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, &dwDisp))
    {
        Log::Error("ERROR: Could not create name key (second): %s", info.name);
        return 1;
    }

    HKEY hDep;
    if (RegCreateKeyExA(hKey, "DefaultIcon", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hDep, &dwDisp))
    {
        Log::Error("ERROR: Could not create DefaultIcon key: %s", info.name);
        return 1;
    }

    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    if (RegSetValueExA(hDep, NULL, 0, REG_SZ,
                       (const BYTE*)path,
                       (DWORD)strlen(path) + 1))
    {
        Log::Error("ERROR: Could not set default icon for extension: %s", info.extension);
        return 1;
    }

    if (RegCreateKeyExA(hKey, "shell", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, &dwDisp))
    {
        Log::Error("ERROR: Could not create shell key: %s", info.name);
        return 1;
    }

    if (RegCreateKeyExA(hKey, "Open", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, &dwDisp))
    {
        Log::Error("ERROR: Could not create Open key: %s", info.name);
        return 1;
    }

    HKEY hCmd;
    if (RegCreateKeyExA(hKey, "command", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hCmd, &dwDisp))
    {
        Log::Error("ERROR: Could not create command key: %s", info.name);
        return 1;
    }

    strcat_s(path, sizeof(path), " \"%1\"");
    if (RegSetValueExA(hCmd, NULL, 0, REG_SZ,
                       (const BYTE*)path,
                       (DWORD)strlen(path) + 1))
    {
        Log::Error("ERROR: Could not set command for extension: %s", info.extension);
        return 1;
    }

    HKEY hDde;
    if (RegCreateKeyExA(hKey, "ddeexec", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hDde, &dwDisp))
    {
        Log::Error("ERROR: Could not create ddeexec key: %s", info.name);
        return 1;
    }

    const char* cmd = "%1";
    if (RegSetValueExA(hDde, NULL, 0, REG_SZ,
                       (const BYTE*)cmd,
                       (DWORD)strlen(cmd) + 1))
    {
        Log::Error("ERROR: Could not set DDE command string for extension: %s", info.extension);
        return 1;
    }

    HKEY hApp;
    if (RegCreateKeyExA(hDde, "application", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hApp, &dwDisp))
    {
        Log::Error("ERROR: Could not create ddeexec->application key: %s", info.name);
        return 1;
    }

    char* appname = iniparser_getstr(info.ini, (char*)DDE_SERVER_NAME);
    if (!appname)
        appname = (char*)"WinRun4J";

    if (RegSetValueExA(hApp, NULL, 0, REG_SZ,
                       (const BYTE*)appname,
                       (DWORD)strlen(appname) + 1))
    {
        Log::Error("ERROR: Could not set appname for extension: %s", info.extension);
        return 1;
    }

    HKEY hTopic;
    if (RegCreateKeyExA(hDde, "topic", 0, NULL,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hTopic, &dwDisp))
    {
        Log::Error("ERROR: Could not create ddeexec->topic key: %s", info.name);
        return 1;
    }

    char* topic = iniparser_getstr(info.ini, (char*)DDE_TOPIC);
    if (!topic)
        topic = (char*)"system";

    if (RegSetValueExA(hTopic, NULL, 0, REG_SZ,
                       (const BYTE*)topic,
                       (DWORD)strlen(topic) + 1))
    {
        Log::Error("ERROR: Could not set topic for extension: %s", info.extension);
        return 1;
    }

    return 0;
}

int DDE::UnregisterFileAssociation(DDEInfo& info)
{
    if (RegDeleteKeyA(HKEY_CLASSES_ROOT, info.extension))
        return 1;
    return 0;
}

int DDE::UnregisterFileAssociations(dictionary* ini)
{
    return EnumFileAssocations(ini, false, UnregisterFileAssociation);
}
