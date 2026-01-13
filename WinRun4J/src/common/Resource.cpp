/*******************************************************************************
 * This program and the accompanying materials
 * are made available under the terms of the Common Public License v1.0
 * which accompanies this distribution, and is available at 
 * http://www.eclipse.org/legal/cpl-v10.html
 * 
 * Contributors:
 *     Peter Smith
 *******************************************************************************/

#include "Resource.h"
#include "Log.h"
#include <stdio.h>
#include <ctype.h>

// ------------------------------------------------------------
// Set icon on exe file
// ------------------------------------------------------------
bool Resource::SetIcon(LPSTR exeFile, LPSTR iconFile)
{
    ICONHEADER*     pHeader;
    ICONIMAGE**     pIcons;
    GRPICONHEADER*  pGrpHeader;

    if (!LoadIcon(iconFile, pHeader, pIcons, pGrpHeader))
        return false;

    HANDLE hUpdate = BeginUpdateResourceA(exeFile, FALSE);
    if (!hUpdate) {
        Log::Error("Could not load exe to set icon: %s", exeFile);
        return false;
    }

    UpdateResourceA(
        hUpdate,
        RT_GROUP_ICON,
        MAKEINTRESOURCEA(1),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
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

// ------------------------------------------------------------
// Add icon to exe file
// ------------------------------------------------------------
bool Resource::AddIcon(LPSTR exeFile, LPSTR iconFile)
{
    HMODULE hm = LoadLibraryExA(exeFile, NULL, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
    if (!hm) {
        Log::Error("Could not load exe to add icon: %s", exeFile);
        return false;
    }

    int gresId = 1;
    while (FindResourceA(hm, MAKEINTRESOURCEA(gresId), RT_GROUP_ICON))
        gresId++;

    int iresId = 1;
    while (FindResourceA(hm, MAKEINTRESOURCEA(iresId), RT_ICON))
        iresId++;

    FreeLibrary(hm);

    ICONHEADER*     pHeader;
    ICONIMAGE**     pIcons;
    GRPICONHEADER*  pGrpHeader;

    if (!LoadIcon(iconFile, pHeader, pIcons, pGrpHeader, iresId))
        return false;

    HANDLE hUpdate = BeginUpdateResourceA(exeFile, FALSE);
    if (!hUpdate) {
        Log::Error("Could not load exe to add icon: %s", exeFile);
        return false;
    }

    if (!UpdateResourceA(
        hUpdate,
        RT_GROUP_ICON,
        MAKEINTRESOURCEA(gresId),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
        pGrpHeader,
        sizeof(WORD) * 3 + pHeader->count * sizeof(GRPICONENTRY)))
    {
        Log::Error("Could not insert group icon into binary");
    }

    for (int i = 0; i < pHeader->count; i++) {
        if (!UpdateResourceA(
            hUpdate,
            RT_ICON,
            MAKEINTRESOURCEA(i + iresId),
            MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
            pIcons[i],
            pHeader->entries[i].bytesInRes))
        {
            Log::Error("Could not insert icon into binary");
        }
    }

    EndUpdateResourceA(hUpdate, FALSE);
    return true;
}

// ------------------------------------------------------------
// Set INI file
// ------------------------------------------------------------
bool Resource::SetINI(LPSTR exeFile, LPSTR iniFile)
{
    return SetFile(exeFile, iniFile, RT_INI_FILE, MAKEINTRESOURCEA(1), INI_RES_MAGIC, true);
}

// ------------------------------------------------------------
// Set splash file
// ------------------------------------------------------------
bool Resource::SetSplash(LPSTR exeFile, LPSTR splashFile)
{
    return SetFile(exeFile, splashFile, RT_SPLASH_FILE, MAKEINTRESOURCEA(1), 0, false);
}

// ------------------------------------------------------------
// Set manifest file
// ------------------------------------------------------------
bool Resource::SetManifest(LPSTR exeFile, LPSTR manifestFile)
{
    return SetFile(exeFile, manifestFile, RT_MANIFEST, MAKEINTRESOURCEA(1), 0, true);
}

// ------------------------------------------------------------
// List INI contents
// ------------------------------------------------------------
bool Resource::ListINI(LPSTR exeFile)
{
    HMODULE hm = LoadLibraryExA(exeFile, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!hm) {
        Log::Error("Could not load exe to list INI contents: %s", exeFile);
        return false;
    }

    HRSRC h = FindResourceA(hm, MAKEINTRESOURCEA(1), RT_INI_FILE);
    if (!h) {
        Log::Error("Could not find INI resource in %s", exeFile);
        FreeLibrary(hm);
        return false;
    }

    HGLOBAL hg = LoadResource(hm, h);
    PBYTE pb = (PBYTE)LockResource(hg);
    DWORD* pd = (DWORD*)pb;

    if (*pd == INI_RES_MAGIC) {
        puts((char*)&pb[4]);
        puts("");
    } else {
        printf("Unknown resource\n");
    }

    FreeLibrary(hm);
    return true;
}

// ------------------------------------------------------------
// Add JAR file
// ------------------------------------------------------------
bool Resource::AddJar(LPSTR exeFile, LPSTR jarFile)
{
    char jarName[MAX_PATH];
    int len = (int)strlen(jarFile) - 1;

    while (len > 0 && jarFile[len] != '\\' && jarFile[len] != '/')
        len--;

    strcpy_s(jarName, sizeof(jarName), &jarFile[len + 1]);

    HMODULE hm = LoadLibraryExA(exeFile, NULL, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
    if (!hm) {
        Log::Error("Could not load exe to add JAR: %s", exeFile);
        return false;
    }

    int resId = 1;
    HRSRC hr = 0;

    while ((hr = FindResourceA(hm, MAKEINTRESOURCEA(resId), RT_JAR_FILE)) != NULL) {
        HGLOBAL hg = LoadResource(hm, hr);
        PBYTE pb = (PBYTE)LockResource(hg);
        DWORD* pd = (DWORD*)pb;

        if (*pd == JAR_RES_MAGIC) {
            if (strcmp(jarName, (char*)&pb[RES_MAGIC_SIZE]) == 0)
                break;
        }
        resId++;
    }

    FreeLibrary(hm);

    HANDLE hFile = CreateFileA(jarFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        Log::Error("Could not open JAR file: %s", jarFile);
        return false;
    }

    DWORD cbBuffer = GetFileSize(hFile, 0);
    DWORD cbPadding = RES_MAGIC_SIZE + (DWORD)strlen(jarName) + 1;

    PBYTE pBuffer = (PBYTE)malloc(cbBuffer + cbPadding);
    ReadFile(hFile, &pBuffer[cbPadding], cbBuffer, &cbBuffer, NULL);

    DWORD* pMagic = (DWORD*)pBuffer;
    *pMagic = JAR_RES_MAGIC;

    memcpy(&pBuffer[RES_MAGIC_SIZE], jarName, strlen(jarName) + 1);

    HANDLE hUpdate = BeginUpdateResourceA(exeFile, FALSE);
    if (!hUpdate) {
        Log::Error("Could not load exe to add JAR: %s", exeFile);
        return false;
    }

    UpdateResourceA(
        hUpdate,
        RT_JAR_FILE,
        MAKEINTRESOURCEA(resId),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
        pBuffer,
        cbBuffer + cbPadding
    );

    EndUpdateResourceA(hUpdate, FALSE);
    return true;
}

// ------------------------------------------------------------
// Add HTML file
// ------------------------------------------------------------
bool Resource::AddHTML(LPSTR exeFile, LPSTR htmlFile)
{
    char htmlName[MAX_PATH];
    int len = (int)strlen(htmlFile) - 1;

    while (len > 0 && htmlFile[len] != '\\' && htmlFile[len] != '/')
        len--;

    strcpy_s(htmlName, sizeof(htmlName), &htmlFile[len + 1]);

    len = (int)strlen(htmlName);
    for (int i = 0; i < len; i++)
        htmlName[i] = (char)toupper(htmlName[i]);

    return SetFile(exeFile, htmlFile, RT_HTML, htmlName, 0, false);
}

// ------------------------------------------------------------
// SetFile (INI, splash, manifest, HTML, JAR)
// ------------------------------------------------------------
bool Resource::SetFile(LPSTR exeFile, LPSTR resFile, LPCTSTR lpType, LPCTSTR lpName, DWORD magic, bool zeroTerminate)
{
    HANDLE hFile = CreateFileA(resFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        Log::Error("Could not open resource file: %s", resFile);
        return false;
    }

    DWORD cbBuffer = GetFileSize(hFile, 0);
    DWORD ztPadding = zeroTerminate ? 1 : 0;
    DWORD magicSize = magic ? RES_MAGIC_SIZE : 0;

    PBYTE pBuffer = (PBYTE)malloc(cbBuffer + magicSize + ztPadding);
    ReadFile(hFile, &pBuffer[magicSize], cbBuffer, &cbBuffer, NULL);

    if (magic) {
        DWORD* pMagic = (DWORD*)pBuffer;
        *pMagic = magic;
    }

    if (zeroTerminate)
        pBuffer[magicSize + cbBuffer] = 0;

    HANDLE hUpdate = BeginUpdateResourceA(exeFile, FALSE);
    if (!hUpdate) {
        Log::Error("Could not load exe to load resource: %s", exeFile);
        return false;
    }

    if (!UpdateResourceA(
        hUpdate,
        lpType,
        lpName,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
        pBuffer,
        cbBuffer + magicSize + ztPadding))
    {
        Log::Error("Could not insert resource into binary");
    }

    EndUpdateResourceA(hUpdate, FALSE);
    return true;
}

// ------------------------------------------------------------
// Load icon file
// ------------------------------------------------------------
bool Resource::LoadIcon(LPSTR iconFile, ICONHEADER*& pHeader, ICONIMAGE**& pIcons, GRPICONHEADER*& pGrpHeader, int index)
{
    HANDLE hFile = CreateFileA(iconFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        Log::Error("Could not open icon file: %s", iconFile);
        return false;
    }

    DWORD bytesRead;

    pHeader = (ICONHEADER*)malloc(sizeof(ICONHEADER));
    ReadFile(hFile, &(pHeader->reserved), sizeof(WORD), &bytesRead, NULL);
    ReadFile(hFile, &(pHeader->type), sizeof(WORD), &bytesRead, NULL);
    ReadFile(hFile, &(pHeader->count), sizeof(WORD), &bytesRead, NULL);

    pHeader = (ICONHEADER*)realloc(pHeader, sizeof(WORD) * 3 + sizeof(ICONENTRY) * pHeader->count);
    ReadFile(hFile, pHeader->entries, pHeader->count * sizeof(ICONENTRY), &bytesRead, NULL);

    pIcons = (ICONIMAGE**)malloc(sizeof(ICONIMAGE*) * pHeader->count);

    for (int i = 0; i < pHeader->count; i++) {
        pIcons[i] = (ICONIMAGE*)malloc(pHeader->entries[i].bytesInRes);
        SetFilePointer(hFile, pHeader->entries[i].imageOffset, NULL, FILE_BEGIN);
        ReadFile(hFile, pIcons[i], pHeader->entries[i].bytesInRes, &bytesRead, NULL);
    }

    pGrpHeader = (GRPICONHEADER*)malloc(sizeof(WORD) * 3 + pHeader->count * sizeof(GRPICONENTRY));
    pGrpHeader->reserved = 0;
    pGrpHeader->type = 1;
    pGrpHeader->count = pHeader->count;

    for (int i = 0; i < pHeader->count; i++) {
        ICONENTRY* icon = &pHeader->entries[i];
        GRPICONENTRY* entry = &pGrpHeader->entries[i];

        entry->width       = icon->width;
        entry->height      = icon->height;
        entry->colourCount = icon->colorCount;
        entry->reserved    = icon->reserved;
        entry->planes      = (BYTE)icon->planes;
        entry->bitCount    = (BYTE)icon->bitCount;
        entry->bytesInRes  = (WORD)icon->bytesInRes;
        entry->id          = (WORD)(i + 1 + index);
    }

    CloseHandle(hFile);
    return true;
}

// ------------------------------------------------------------
// Resource enumeration helpers
// ------------------------------------------------------------
typedef struct
{
    LPCTSTR lpType;
    LPCTSTR lpName;
    WORD    wLang;
} ResourceInfo;

typedef struct
{
    WORD count;
    WORD max;
    ResourceInfo* ri;
} ResourceInfoList;

BOOL EnumLangsFunc(HANDLE hModule, LPCTSTR lpType, LPCTSTR lpName, WORD wLang, LONG lParam)
{
	UNREFERENCED_PARAMETER(hModule);

    ResourceInfoList* pRil = (ResourceInfoList*)lParam;

    pRil->ri[pRil->count].lpType = lpType;

    if (IS_INTRESOURCE(lpName))
        pRil->ri[pRil->count].lpName = lpName;
    else
        pRil->ri[pRil->count].lpName = _strdup(lpName);

    pRil->ri[pRil->count].wLang = wLang;
    pRil->count++;

    return pRil->count < pRil->max;
}

BOOL EnumNamesFunc(HANDLE hModule, LPCTSTR lpType, LPTSTR lpName, LONG lParam)
{
    return EnumResourceLanguages(
        (HMODULE)hModule,
        lpType,
        lpName,
        (ENUMRESLANGPROC)EnumLangsFunc,
        lParam
    );
}

BOOL EnumTypesFunc(HANDLE hModule, LPTSTR lpType, LONG lParam)
{
    return EnumResourceNames(
        (HMODULE)hModule,
        lpType,
        (ENUMRESNAMEPROC)EnumNamesFunc,
        lParam
    );
}

// ------------------------------------------------------------
// Clear all resources
// ------------------------------------------------------------
bool Resource::ClearResources(LPSTR exeFile)
{
    HMODULE hMod = LoadLibraryExA(exeFile, NULL, LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE);
    if (!hMod) {
        Log::Error("Could not load exe to clear resources: %s", exeFile);
        return false;
    }

    ResourceInfoList ril;
    ril.ri = (ResourceInfo*)malloc(sizeof(ResourceInfo) * 100);
    ril.max = 100;
    ril.count = 0;

    EnumResourceTypesA((HMODULE)hMod, (ENUMRESTYPEPROC)EnumTypesFunc, (LONG_PTR)&ril);
    FreeLibrary(hMod);

    HANDLE hUpdate = BeginUpdateResourceA(exeFile, FALSE);
    if (!hUpdate) {
        Log::Error("Could not load exe to clear resources: %s", exeFile);
        return false;
    }

    for (int i = 0; i < ril.count; i++) {
        UpdateResourceA(
            hUpdate,
            ril.ri[i].lpType,
            ril.ri[i].lpName,
            ril.ri[i].wLang,
            NULL,
            0
        );
    }

    EndUpdateResourceA(hUpdate, FALSE);
    free(ril.ri);

    return true;
}

// ------------------------------------------------------------
// List all resources
// ------------------------------------------------------------
bool Resource::ListResources(LPSTR exeFile)
{
    HMODULE hMod = LoadLibraryExA(exeFile, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!hMod) {
        Log::Error("Could not load exe to list resources: %s", exeFile);
        return false;
    }

    ResourceInfoList ril;
    ril.ri = (ResourceInfo*)malloc(sizeof(ResourceInfo) * 100);
    ril.max = 100;
    ril.count = 0;

    EnumResourceTypesA((HMODULE)hMod, (ENUMRESTYPEPROC)EnumTypesFunc, (LONG_PTR)&ril);

    for (int i = 0; i < ril.count; i++) {
        LPCTSTR lpType = ril.ri[i].lpType;
        LPCTSTR lpName = ril.ri[i].lpName;

        if (lpType == RT_GROUP_ICON) {
            printf("Group Icon\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_ICON) {
            printf("Icon      \t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_JAR_FILE) {
            HRSRC h = FindResourceA(hMod, lpName, lpType);
            HGLOBAL hg = LoadResource(hMod, h);
            PBYTE pb = (PBYTE)LockResource(hg);
            DWORD* pd = (DWORD*)pb;

            if (*pd == JAR_RES_MAGIC) {
                printf("JAR File  \t%s\n", (char*)&pb[RES_MAGIC_SIZE]);
            } else {
                printf("Unknown   \t%04x, %04x\n", (UINT_PTR)lpType, (UINT_PTR)lpName);
            }
        } else if (lpType == RT_INI_FILE) {
            printf("INI File\n");
        } else if (lpType == RT_SPLASH_FILE) {
            printf("Splash File\n");
        } else if (lpType == RT_ACCELERATOR) {
            printf("Accelerator\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_ANICURSOR) {
            printf("Ani Cursor\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_ANIICON) {
            printf("Ani Icon\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_BITMAP) {
            printf("Bitmap\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_CURSOR) {
            printf("Cursor\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_DIALOG) {
            printf("Dialog\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_DLGINCLUDE) {
            printf("Dialog Include\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_FONT) {
            printf("Font\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_FONTDIR) {
            printf("Font Dir\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_HTML) {
            printf("HTML\t\t%s\n", (char*)lpName);
        } else if (lpType == RT_GROUP_CURSOR) {
            printf("Group Cursor\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_MANIFEST) {
            printf("Manifest\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_MENU) {
            printf("Menu\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_MESSAGETABLE) {
            printf("Message Table\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_PLUGPLAY) {
            printf("Plug Play\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_RCDATA) {
            printf("RC Data\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_STRING) {
            printf("String\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_VERSION) {
            printf("Version\t%04x\n", (UINT_PTR)lpName);
        } else if (lpType == RT_VXD) {
            printf("VXD\t%04x\n", (UINT_PTR)lpName);
        } else {
            printf("Unknown   \t%04x, %04x\n", (UINT_PTR)lpType, (UINT_PTR)lpName);
        }
    }

    free(ril.ri);
    FreeLibrary(hMod);
    return true;
}
