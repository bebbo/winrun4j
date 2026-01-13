/*******************************************************************************
 * This program and the accompanying materials
 * are made available under the terms of the Common Public License v1.0
 * which accompanies this distribution, and is available at 
 * http://www.eclipse.org/legal/cpl-v10.html
 * 
 * Contributors:
 *     Peter Smith
 *******************************************************************************/

#include "Icon.h"
#include "Log.h"
#include "Runtime.h"   // for StripArg0

#include <windows.h>
#include <string.h>
#include <stdlib.h>

// Since we can't replace the icon resource while the program is running we do the following:
//  1. copy exe to a random filename.
//  2. execute random filename with the argument containing the original filename
//  3. replace the icon of the original filename
//  4. execute the original to delete the random filename

#define SET_ICON_CMD               "--WinRun4J:SetIcon SetIcon"
#define SET_ICON_DELETE_EXE_CMD    "--WinRun4J:SetIcon Delete"
#define ADD_ICON_CMD               "--WinRun4J:AddIcon AddIcon"
#define ADD_ICON_DELETE_EXE_CMD    "--WinRun4J:AddIcon Delete"
#define REMOVE_ICON_CMD            "--WinRun4J:RemoveIcon RemoveIcon"
#define REMOVE_ICON_DELETE_EXE_CMD "--WinRun4J:RemoveIcon Delete"

void Icon::SetExeIcon(LPSTR commandLine)
{
    if (strncmp(commandLine, SET_ICON_CMD, (unsigned)strlen(SET_ICON_CMD)) == 0) {
        SetIcon(commandLine);
    } else if (strncmp(commandLine, SET_ICON_DELETE_EXE_CMD, (unsigned)strlen(SET_ICON_DELETE_EXE_CMD)) == 0) {
        DeleteRandomFile(commandLine);
    } else {
        CopyToRandomAndRun((LPSTR)SET_ICON_CMD);
    }
}

void Icon::SetIcon(LPSTR commandLine)
{
    Sleep(1000);

    char filename[MAX_PATH];
    char iconfile[MAX_PATH];
    GetFilenames(commandLine, filename, iconfile);

    SetIcon(filename, iconfile);

    RunDeleteRandom(filename, (LPSTR)SET_ICON_DELETE_EXE_CMD);
}

void Icon::GetFilenames(LPSTR commandLine, LPSTR filename, LPSTR iconfile)
{
    commandLine = StripArg0(commandLine);
    commandLine = StripArg0(commandLine);

    strcpy_s(filename, MAX_PATH, commandLine);

    strcpy_s(iconfile, MAX_PATH, filename);
    int len = (int)strlen(filename);
    if (len >= 3) {
        iconfile[len - 1] = 'o';
        iconfile[len - 2] = 'c';
        iconfile[len - 3] = 'i';
    }

    Log::Info("Setting icon file...");
    Log::Info("Icon File: %s", iconfile);
    Log::Info("Exe File: %s", filename);
}

void Icon::RunDeleteRandom(LPSTR filename, LPSTR command)
{
    Sleep(1000);

    char random[MAX_PATH];
    char cmd[MAX_PATH];

    GetModuleFileNameA(NULL, random, MAX_PATH);

    _snprintf_s(cmd, sizeof(cmd), _TRUNCATE, "\"%s\" %s %s", filename, command, random);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(filename, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        Log::Error("Could not run delete process");
    }
}

// Create a random filename based on original and call set/add/remove icon on this executable
void Icon::CopyToRandomAndRun(LPSTR command)
{
    char filename[MAX_PATH];
    char random[MAX_PATH];
    char cmdline[MAX_PATH];

    GetModuleFileNameA(NULL, filename, MAX_PATH);

    srand(GetTickCount());
    int r = rand();

    _snprintf_s(random, sizeof(random), _TRUNCATE, "%s.%d.exe", filename, r);
    _snprintf_s(cmdline, sizeof(cmdline), _TRUNCATE, "\"%s\" %s %s", random, command, filename);

    if (!CopyFileA(filename, random, TRUE)) {
        Log::Error("Could not copy file to random name");
        return;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(random, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        Log::Error("Could not run random process");
    }
}

void Icon::DeleteRandomFile(LPSTR cmdLine)
{
    cmdLine = StripArg0(cmdLine);
    cmdLine = StripArg0(cmdLine);
    Sleep(1000);
    DeleteFileA(cmdLine);
}

// Set icon on original exe file
bool Icon::SetIcon(LPSTR exeFile, LPSTR iconFile)
{
    ICONHEADER*    pHeader    = NULL;
    ICONIMAGE**    pIcons     = NULL;
    GRPICONHEADER* pGrpHeader = NULL;

    bool res = LoadIcon(iconFile, pHeader, pIcons, pGrpHeader);
    if (!res) {
        return false;
    }

    HANDLE hUpdate = BeginUpdateResourceA(exeFile, FALSE);
    if (!hUpdate) {
        Log::Error("BeginUpdateResource failed for %s", exeFile);
        return false;
    }

    UpdateResourceA(
        hUpdate,
        RT_GROUP_ICON,
        MAKEINTRESOURCEA(1),
        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
        pGrpHeader,
        sizeof(WORD) * 3 + pHeader->count * sizeof(GRPICONENTRY)
    );

    for (int i = 0; i < pHeader->count; i++) {
        UpdateResourceA(
            hUpdate,
            RT_ICON,
            MAKEINTRESOURCEA(i + 1),
            MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
            pIcons[i],
            pHeader->entries[i].bytesInRes
        );
    }

    EndUpdateResourceA(hUpdate, FALSE);
    return true;
}

// Load an icon image from a file
bool Icon::LoadIcon(LPSTR iconFile, ICONHEADER*& pHeader, ICONIMAGE**& pIcons, GRPICONHEADER*& pGrpHeader)
{
    HANDLE hFile = CreateFileA(iconFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        Log::Error("ERROR: Could not open icon file: %s", iconFile);
        return false;
    }

    DWORD bytesRead = 0;

    pHeader = (ICONHEADER*)malloc(sizeof(ICONHEADER));
    if (!pHeader) {
        CloseHandle(hFile);
        return false;
    }

    ReadFile(hFile, &(pHeader->reserved), sizeof(WORD), &bytesRead, NULL);
    ReadFile(hFile, &(pHeader->type),     sizeof(WORD), &bytesRead, NULL);
    ReadFile(hFile, &(pHeader->count),    sizeof(WORD), &bytesRead, NULL);

    pHeader = (ICONHEADER*)realloc(pHeader, sizeof(WORD) * 3 + sizeof(ICONENTRY) * pHeader->count);
    if (!pHeader) {
        CloseHandle(hFile);
        return false;
    }

    ReadFile(hFile, pHeader->entries, pHeader->count * sizeof(ICONENTRY), &bytesRead, NULL);

    pIcons = (ICONIMAGE**)malloc(sizeof(ICONIMAGE*) * pHeader->count);
    if (!pIcons) {
        CloseHandle(hFile);
        return false;
    }

    for (int i = 0; i < pHeader->count; i++) {
        pIcons[i] = (ICONIMAGE*)malloc(pHeader->entries[i].bytesInRes);
        if (!pIcons[i]) {
            CloseHandle(hFile);
            return false;
        }
        SetFilePointer(hFile, pHeader->entries[i].imageOffset, NULL, FILE_BEGIN);
        ReadFile(hFile, pIcons[i], pHeader->entries[i].bytesInRes, &bytesRead, NULL);
    }

    pGrpHeader = (GRPICONHEADER*)malloc(sizeof(WORD) * 3 + pHeader->count * sizeof(GRPICONENTRY));
    if (!pGrpHeader) {
        CloseHandle(hFile);
        return false;
    }

    pGrpHeader->reserved = 0;
    pGrpHeader->type     = 1;
    pGrpHeader->count    = pHeader->count;

    for (int i = 0; i < pHeader->count; i++) {
        ICONENTRY*    icon  = &pHeader->entries[i];
        GRPICONENTRY* entry = &pGrpHeader->entries[i];

        entry->width       = icon->width;
        entry->height      = icon->height;
        entry->colourCount = icon->colorCount;
        entry->reserved    = icon->reserved;
        entry->planes      = (BYTE)icon->planes;
        entry->bitCount    = (BYTE)icon->bitCount;
        entry->bytesInRes  = (WORD)icon->bytesInRes;
        entry->id          = (WORD)(i + 1);
    }

    CloseHandle(hFile);
    return true;
}

void Icon::AddExeIcon(LPSTR commandLine)
{
    if (strncmp(commandLine, ADD_ICON_CMD, (unsigned)strlen(ADD_ICON_CMD)) == 0) {
        AddIcon(commandLine);
    } else if (strncmp(commandLine, ADD_ICON_DELETE_EXE_CMD, (unsigned)strlen(ADD_ICON_DELETE_EXE_CMD)) == 0) {
        DeleteRandomFile(commandLine);
    } else {
        CopyToRandomAndRun((LPSTR)ADD_ICON_CMD);
    }
}

void Icon::AddIcon(LPSTR commandLine)
{
    char filename[MAX_PATH];
    char iconfile[MAX_PATH];

    GetFilenames(commandLine, filename, iconfile);

    AddIcon(filename, iconfile);

    RunDeleteRandom(filename, (LPSTR)ADD_ICON_DELETE_EXE_CMD);
}

bool Icon::AddIcon(LPSTR exeFile, LPSTR iconFile)
{
    ICONHEADER*    pHeader    = NULL;
    ICONIMAGE**    pIcons     = NULL;
    GRPICONHEADER* pGrpHeader = NULL;

    bool res = LoadIcon(iconFile, pHeader, pIcons, pGrpHeader);
    if (!res) {
        return false;
    }

    HANDLE hUpdate = BeginUpdateResourceA(exeFile, FALSE);
    if (!hUpdate) {
        Log::Error("BeginUpdateResource failed for %s", exeFile);
        return false;
    }

    int nextId = FindNextId((HMODULE)hUpdate);

    UpdateResourceA(
        hUpdate,
        RT_GROUP_ICON,
        MAKEINTRESOURCEA(nextId),
        MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
        pGrpHeader,
        sizeof(WORD) * 3 + pHeader->count * sizeof(GRPICONENTRY)
    );

    for (int i = 0; i < pHeader->count; i++) {
        UpdateResourceA(
            hUpdate,
            RT_ICON,
            MAKEINTRESOURCEA(i + nextId + 1),
            MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
            pIcons[i],
            pHeader->entries[i].bytesInRes
        );
    }

    EndUpdateResourceA(hUpdate, FALSE);
    return true;
}

void Icon::RemoveExeIcons(LPSTR commandLine)
{
    if (strncmp(commandLine, REMOVE_ICON_CMD, (unsigned)strlen(REMOVE_ICON_CMD)) == 0) {
        RemoveIcons(commandLine);
    } else if (strncmp(commandLine, REMOVE_ICON_DELETE_EXE_CMD, (unsigned)strlen(REMOVE_ICON_DELETE_EXE_CMD)) == 0) {
        DeleteRandomFile(commandLine);
    } else {
        CopyToRandomAndRun((LPSTR)REMOVE_ICON_CMD);
    }
}

void Icon::RemoveIcons(LPSTR commandLine)
{
    char filename[MAX_PATH];
    char iconfile[MAX_PATH];

    GetFilenames(commandLine, filename, iconfile);

    RemoveIconResources(filename);

    RunDeleteRandom(filename, (LPSTR)REMOVE_ICON_DELETE_EXE_CMD);
}

bool Icon::RemoveIconResources(LPSTR exeFile)
{
    HANDLE hUpdate = BeginUpdateResourceA(exeFile, FALSE);
    if (!hUpdate) {
        Log::Error("BeginUpdateResource failed for %s", exeFile);
        return false;
    }

    for (int i = 1; i < 1000; i++) {
        HRSRC hsrc = FindResourceA((HMODULE)hUpdate, MAKEINTRESOURCEA(i), RT_GROUP_ICON);
        if (hsrc != NULL) {
            UpdateResourceA(
                hUpdate,
                RT_GROUP_ICON,
                MAKEINTRESOURCEA(i),
                MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                NULL,
                0
            );
        }

        hsrc = FindResourceA((HMODULE)hUpdate, MAKEINTRESOURCEA(i), RT_ICON);
        if (hsrc != NULL) {
            UpdateResourceA(
                hUpdate,
                RT_ICON,
                MAKEINTRESOURCEA(i),
                MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                NULL,
                0
            );
        }
    }

    EndUpdateResourceA(hUpdate, FALSE);
    return true;
}

int Icon::FindNextId(HMODULE hModule)
{
    for (int i = 1;; i++) {
        HRSRC hsrc = FindResourceA(hModule, MAKEINTRESOURCEA(i), RT_GROUP_ICON);
        if (!hsrc) {
            hsrc = FindResourceA(hModule, MAKEINTRESOURCEA(i), RT_ICON);
        }
        if (!hsrc)
            return i;
    }
}
