/*******************************************************************************
 * This program and the accompanying materials
 * are made available under the terms of the Common Public License v1.0
 * which accompanies this distribution, and is available at 
 * http://www.eclipse.org/legal/cpl-v10.html
 * 
 * Contributors:
 *     Peter Smith
 *******************************************************************************/

#include "WinRun4J.h"
#include "launcher/SplashScreen.h"
#include "launcher/Shell.h"
#include "launcher/DDE.h"
#include "launcher/Service.h"
#include "launcher/EventLog.h"
#include "launcher/Native.h"
#include "common/Registry.h"

#define CONSOLE_TITLE                       ":console.title"
#define PROCESS_PRIORITY                    ":process.priority"
#define DISABLE_NATIVE_METHODS              ":disable.native.methods"
#define ARGS_ALLOW_OVERRIDES                ":args.allow.overrides"
#define ARGS_ALLOW_VMARGS                   ":args.allow.vmargs"
#define ARGS_OVERRIDE_PREFIX                ":args.override.prefix"
#define ERROR_MESSAGES_SHOW_POPUP           "ErrorMessages:show.popup"
#define ERROR_MESSAGES_JAVA_NOT_FOUND       "ErrorMessages:java.not.found"
#define ERROR_MESSAGES_JAVA_START_FAILED    "ErrorMessages:java.failed"
#define ERROR_MESSAGES_MAIN_CLASS_NOT_FOUND "ErrorMessages:main.class.not.found"

namespace
{
    char** vmargs = NULL;
    UINT  vmargsCount = 0;

    char** progargs = NULL;
    UINT  progargsCount  = 0;
    UINT  progargsOffset = 0;

    bool  workingDirectorySet = false;
}

void WinRun4J::SetWorkingDirectory(dictionary* ini, bool defaultToIniDir)
{
    if (workingDirectorySet)
        return;

    char* dir = iniparser_getstr(ini, (char*)WORKING_DIR);
    if (dir != NULL || defaultToIniDir) {

        char* iniDir = iniparser_getstr(ini, (char*)INI_DIR);
        if (iniDir)
            SetCurrentDirectoryA(iniDir);

        if (dir != NULL)
            SetCurrentDirectoryA(dir);

        if (Log::GetLevel() == info) {
            char temp[MAX_PATH];
            GetCurrentDirectoryA(MAX_PATH, temp);
            Log::Info("Working directory set to: %s", temp);
        }
    }

    workingDirectorySet = true;
}

void WinRun4J::SetProcessPriority(dictionary* ini)
{
    char* priority = iniparser_getstr(ini, (char*)PROCESS_PRIORITY);
    if (!priority)
        return;

    DWORD p = 0;

    if (strcmp("idle", priority) == 0) {
        p = IDLE_PRIORITY_CLASS;
    } else if (strcmp("below_normal", priority) == 0) {
        p = BELOW_NORMAL_PRIORITY_CLASS;
    } else if (strcmp("normal", priority) == 0) {
        p = NORMAL_PRIORITY_CLASS;
    } else if (strcmp("above_normal", priority) == 0) {
        p = ABOVE_NORMAL_PRIORITY_CLASS;
    } else if (strcmp("high", priority) == 0) {
        p = HIGH_PRIORITY_CLASS;
    } else if (strcmp("realtime", priority) == 0) {
        p = REALTIME_PRIORITY_CLASS;
    } else {
        Log::Warning("Invalid process priority class: %s", priority);
    }

    if (p != 0)
        SetPriorityClass(GetCurrentProcess(), p);
}

int WinRun4J::DoBuiltInCommand(HINSTANCE hInstance)
{
    char* lpArg1 = progargs[0];

    Log::SetLogFileAndConsole(true);

    if (StartsWith(lpArg1, (char*)"--WinRun4J:RegisterFileAssociations")) {
        return DDE::RegisterFileAssociations(WinRun4J::LoadIniFile(hInstance));
    }

    if (StartsWith(lpArg1, (char*)"--WinRun4J:UnregisterFileAssociations")) {
        return DDE::UnregisterFileAssociations(WinRun4J::LoadIniFile(hInstance));
    }

    if (StartsWith(lpArg1, (char*)"--WinRun4J:RegisterService")) {
        dictionary* ini = INI::LoadIniFile(hInstance);
        if (!ini)
            return 1;
        return Service::Register(ini);
    }

    if (StartsWith(lpArg1, (char*)"--WinRun4J:UnregisterService")) {
        dictionary* ini = INI::LoadIniFile(hInstance);
        if (!ini)
            return 1;
        return Service::Unregister(ini);
    }

    if (StartsWith(lpArg1, (char*)"--WinRun4J:PrintINI")) {
        dictionary* ini = INI::LoadIniFile(hInstance);
        if (!ini)
            return 1;
        for (int i = 0; i < ini->n; i++)
            printf("%s=%s\n", ini->key[i], ini->val[i]);
        return 0;
    }

    if (StartsWith(lpArg1, (char*)"--WinRun4J:ExecuteINI")) {
        if (progargsCount < 2) {
            Log::Error("INI file not specified");
            return 1;
        }
        dictionary* ini = INI::LoadIniFile(hInstance, progargs[1]);
        progargsOffset = 2;
        return WinRun4J::ExecuteINI(hInstance, ini);
    }

    if (StartsWith(lpArg1, (char*)"--WinRun4J:Version")) {
        Log::Info("0.4.6\n");
        return 0;
    }

    Log::Error("Unrecognized command: %s", lpArg1);
    return 1;
}

dictionary* WinRun4J::LoadIniFile(HINSTANCE hInstance)
{
    dictionary* ini = INI::LoadIniFile(hInstance);
    if (!ini) {
        Log::Error("Failed to find or load ini file.");
        MessageBoxA(NULL, "Failed to find or load ini file.", "Startup Error", MB_OK | MB_ICONERROR);
        Log::Close();
        return NULL;
    }
    return ini;
}

int WinRun4J::StartVM(dictionary* ini)
{
    bool showErrorPopup = iniparser_getboolean(ini, (char*)ERROR_MESSAGES_SHOW_POPUP, 1);

    char* vmlibrary = VM::FindJavaVMLibrary(ini);
    if (!vmlibrary) {
        char* javaNotFound = iniparser_getstring(ini,
            (char*)ERROR_MESSAGES_JAVA_NOT_FOUND,
            (char*)"Failed to find Java VM.");
        Log::Error(javaNotFound);
        if (showErrorPopup)
            MessageBoxA(NULL, javaNotFound, "Startup Error", MB_OK | MB_ICONERROR);
        Log::Close();
        return 1;
    }

    Log::Info("Found VM: %s", vmlibrary);

    INI::GetNumberedKeysFromIni(ini, VM_ARG, &vmargs, vmargsCount);

    Classpath::BuildClassPath(ini, &vmargs, vmargsCount);

    VM::ExtractSpecificVMArgs(ini, &vmargs, vmargsCount);

    if (vmargsCount > 0)
        Log::Info("VM Args:");

    char argl[MAX_PATH];
    for (UINT i = 0; i < vmargsCount; i++) {
        StrTruncate(argl, vmargs[i], MAX_PATH);
        Log::Info("vmarg.%d=%s", i, argl);
    }

    vmargs[vmargsCount] = NULL;

    if (VM::StartJavaVM(vmlibrary, vmargs, NULL) != 0) {
        char* javaFailed = iniparser_getstring(ini,
            (char*)ERROR_MESSAGES_JAVA_START_FAILED,
            (char*)"Error starting Java VM.");
        Log::Error(javaFailed);
        if (showErrorPopup)
            MessageBoxA(NULL, javaFailed, "Startup Error", MB_OK | MB_ICONERROR);
        Log::Close();
        return 1;
    }

    return 0;
}

void WinRun4J::FreeArgs()
{
    for (UINT i = 0; i < vmargsCount; i++) {
        free(vmargs[i]);
        vmargs[i] = NULL;
    }
    free(vmargs);
    vmargsCount   = 0;

    for (UINT i = 0; i < progargsCount; i++)
        free(progargs[i]);

    free(progargs);
    progargs = NULL;
    progargsCount = 0;
}

void WinRun4J::ProcessCommandLineArgs(dictionary* ini)
{
    bool allowOverrides = iniparser_getboolean(ini, (char*)ARGS_ALLOW_OVERRIDES, 1);
    bool allowVmargs    = iniparser_getboolean(ini, (char*)ARGS_ALLOW_VMARGS, 1);
    char* overrideArg   = iniparser_getstring(ini, (char*)ARGS_OVERRIDE_PREFIX, (char*)"-W");
    UINT oaLen          = (UINT)strlen(overrideArg);

    UINT paMax = INI::GetNumberedKeysMax(ini, (char*)":arg");
    UINT vmMax = INI::GetNumberedKeysMax(ini, (char*)":vmarg");

    char entryName[MAX_PATH];

    for (UINT i = progargsOffset; i < progargsCount; i++) {
        char* arg = progargs[i];

        if (allowOverrides && StartsWith(arg, overrideArg)) {
            char* nmptr = &arg[oaLen];
            char* eqptr = strchr(nmptr, '=');
            char* scptr = strchr(nmptr, ':');

            bool inSection = (scptr && (scptr < eqptr || !eqptr));

            int offset = 0;
            if (!inSection) {
                entryName[0] = ':';
                offset = 1;
            }

            if (eqptr) {
                size_t nameLen = (size_t)(eqptr - nmptr);
                if (nameLen + offset >= MAX_PATH)
                    continue;

                memcpy(entryName + offset, nmptr, nameLen);
                entryName[nameLen + offset] = 0;

                iniparser_setstr(ini, entryName, eqptr + 1);
            } else {
                strcpy_s(entryName + offset, MAX_PATH - offset, nmptr);
                iniparser_unset(ini, entryName);
            }

        } else if (allowVmargs && (StartsWith(arg, (char*)"-X") || StartsWith(arg, (char*)"-D"))) {
            sprintf_s(entryName, ":vmarg.%d", ++vmMax);
            iniparser_setstr(ini, entryName, arg);

        } else {
            sprintf_s(entryName, ":arg.%d", ++paMax);
            iniparser_setstr(ini, entryName, arg);
        }
    }
}

int WinRun4J::ExecuteINI(HINSTANCE hInstance, dictionary* ini)
{
    if (!ini)
        return 1;

    ProcessCommandLineArgs(ini);

    if (Shell::CheckSingleInstance(ini))
        return 0;

    char* serviceCls = iniparser_getstr(ini, (char*)SERVICE_CLASS);
    char* mainCls    = iniparser_getstr(ini, (char*)MAIN_CLASS);
    bool  serviceMode =
        iniparser_getboolean(ini, (char*)SERVICE_MODE, serviceCls != NULL);

    bool defaultToIniDir = serviceMode;

    WinRun4J::SetWorkingDirectory(ini, defaultToIniDir);

    if (!serviceMode)
        SplashScreen::ShowSplashImage(hInstance, ini);

    WinRun4J::SetProcessPriority(ini);

    int result = WinRun4J::StartVM(ini);
    if (result)
        return result;

    JNIEnv* env = VM::GetJNIEnv();

    JNI::Init(env);
    if (!iniparser_getboolean(ini, (char*)DISABLE_NATIVE_METHODS, 0))
        Native::RegisterNatives(env);

    bool ddeInit = DDE::Initialize(hInstance, env, ini);

#ifdef CONSOLE
    {
        char* title = iniparser_getstr(ini, (char*)CONSOLE_TITLE);
        if (title)
            SetConsoleTitleA(title);
    }
#endif

    TCHAR** argv = NULL;
    UINT  argc = 0;
    INI::GetNumberedKeysFromIni(ini, ":arg", &argv, argc);

    if (serviceMode)
        result = Service::Run(hInstance, ini, (int)argc, argv);
    else
        result = JNI::RunMainClass(env, mainCls, (int)argc, argv);

    if (serviceCls == NULL)
        JNI::PrintStackTrace(env);

    if (ddeInit)
        DDE::Ready();

    WinRun4J::FreeArgs();

    result |= VM::CleanupVM();

    Log::Close();

    if (ddeInit)
        DDE::Uninitialize();

    return result;
}

#ifdef CONSOLE

int main(int /*argc*/, char* /*argv*/[])
{
    HINSTANCE hInstance = (HINSTANCE)GetModuleHandleA(NULL);
    LPSTR lpCmdLine = StripArg0(GetCommandLineA());

#else

int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR lpCmdLine, int /*nCmdShow*/)
{
    lpCmdLine = StripArg0(GetCommandLineA());

#endif

    Log::Init(hInstance, NULL, NULL, NULL);

    ParseCommandLine(lpCmdLine, &progargs, progargsCount, true);

    if (progargsCount && strncmp(progargs[0], "--WinRun4J:", 11) == 0) {
        int res = WinRun4J::DoBuiltInCommand(hInstance);
        Log::Close();
        return res;
    }

    dictionary* ini = WinRun4J::LoadIniFile(hInstance);
    if (!ini)
        return 1;

    return WinRun4J::ExecuteINI(hInstance, ini);
}
