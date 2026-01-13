/*******************************************************************************
 * This program and the accompanying materials
 * are made available under the terms of the Common Public License v1.0
 * which accompanies this distribution, and is available at 
 * http://www.eclipse.org/legal/cpl-v10.html
 * 
 * Contributors:
 *     Peter Smith
 *******************************************************************************/

#include "INI.h"
#include "Log.h"

#define ALLOW_INI_OVERRIDE    ":ini.override"
#define INI_FILE_LOCATION     ":ini.file.location"
#define INI_REGISTRY_LOCATION ":ini.registry.location"

static dictionary* g_ini = NULL;

UINT INI::GetNumberedKeysMax(dictionary* ini, TCHAR* keyName)
{
    // Use the new helper that scans the dictionary once (O(n))
    int max = dictionary_find_max(ini, keyName);

    // Preserve original return type and semantics
    return (max > 0) ? (UINT)max : 0;
}

void INI::GetNumberedKeysFromIni(dictionary* ini,
                                 const TCHAR* keyName,
                                 TCHAR*** entries,
                                 UINT& index)
{
    UINT i = 0;
    TCHAR entryName[MAX_PATH];

    unsigned max = dictionary_find_max(ini, keyName);

    while (true)
    {
        // Build key: keyName.N
#ifdef UNICODE
    	_snwprintf_s(entryName, MAX_PATH, _TRUNCATE, L"%s.%u", keyName, i + 1);
#else
    	_snprintf_s(entryName, MAX_PATH, _TRUNCATE, "%s.%u", keyName, i + 1);
#endif

        TCHAR* entry = iniparser_getstr(ini, entryName);

        if (entry != NULL)
        {
            // Duplicate the string
#ifdef UNICODE
            TCHAR* dup = _wcsdup(entry);
#else
            TCHAR* dup = _strdup(entry);
#endif
            if (!dup)
                return;

            // Grow array
            TCHAR** newArr = (TCHAR**)realloc(*entries, sizeof(TCHAR*) * (index + 1));
            if (!newArr)
            {
                free(dup);
                return;
            }

            *entries = newArr;
            (*entries)[index++] = dup;
        }

        i++;

        // Stop after a gap beyond max
        if (i > max)
            break;
    }

    // NULL-terminate the array
    TCHAR** newArr = (TCHAR**)realloc(*entries, sizeof(TCHAR*) * (index + 1));
    if (!newArr)
        return;

    *entries = newArr;
    (*entries)[index] = NULL;
}

void INI::SetNumberedKeys(dictionary* ini, TCHAR* keyName, TCHAR** entries, UINT count)
{
    UINT max = GetNumberedKeysMax(ini, keyName);
    TCHAR entryName[MAX_PATH];

    for (UINT i = 0; i < count; i++) {
        sprintf_s(entryName, sizeof(entryName), "%s.%d", keyName, i + max + 1);
        iniparser_setstr(ini, entryName, entries[i]);
    }
}

/* 
 * The ini filename is in the same directory as the executable and 
 * called the same (except with ini at the end). 
 */
dictionary* INI::LoadIniFile(HINSTANCE hInstance)
{
    TCHAR filename[MAX_PATH], inifile[MAX_PATH];

    GetModuleFileName(hInstance, filename, MAX_PATH);
    strcpy_s(inifile, sizeof(inifile), filename);

    int len = (int)strlen(inifile);
    // It is assumed the executable ends with "exe"
    if (len >= 3) {
        inifile[len - 1] = 'i';
        inifile[len - 2] = 'n';
        inifile[len - 3] = 'i';
    }

    return LoadIniFile(hInstance, inifile);
}

dictionary* INI::LoadIniFile(HINSTANCE hInstance, LPSTR inifile)
{
    dictionary* ini = NULL;

    // Set DIR environment variable so that it can be used in the INI file
    TCHAR inidir[MAX_PATH];
    GetFileDirectory(inifile, inidir);
    SetEnvironmentVariable("INI_DIR", inidir);

    // First attempt to load INI from exe
    HRSRC hi = FindResource(hInstance, MAKEINTRESOURCE(1), RT_INI_FILE);
    if (hi) {
        HGLOBAL hg = LoadResource(hInstance, hi);
        PBYTE   pb = (PBYTE)LockResource(hg);
        DWORD*  pd = (DWORD*)pb;
        if (pd && *pd == INI_RES_MAGIC) {
            ini = iniparser_load((char*)&pb[RES_MAGIC_SIZE], true);
            if (!ini) {
                Log::Warning("Could not load embedded INI file");
            }
        }
    }

    // Check if we have already loaded an embedded INI file - if so
    // then we only need to load and merge the INI file (if present)
    if (ini && iniparser_getboolean(ini, (char*)ALLOW_INI_OVERRIDE, 1)) {
        dictionary* ini2 = iniparser_load(inifile);
        if (ini2) {
            for (int i = 0; i < ini2->n; i++) {
                char* key   = ini2->key[i];
                char* value = ini2->val[i];
                iniparser_setstr(ini, key, value);
            }
            iniparser_freedict(ini2);
        }
    } else if (!ini) {
        ini = iniparser_load(inifile);
        if (ini == NULL) {
            Log::Error("Could not load INI file: %s", inifile);
            return NULL;
        }
    }

    // Expand environment variables
    ExpandVariables(ini);

    // Expand registry variables
    ExpandRegistryVariables(ini);

    // Now check if we have an external file to load
    char* iniFileLocation = iniparser_getstr(ini, (char*)INI_FILE_LOCATION);
    if (iniFileLocation) {
        Log::Info("Loading INI keys from file location: %s", iniFileLocation);
        dictionary* ini3 = iniparser_load(iniFileLocation);
        if (ini3) {
            ExpandVariables(ini3);
            for (int i = 0; i < ini3->n; i++) {
                char* key   = ini3->key[i];
                char* value = ini3->val[i];
                iniparser_setstr(ini, key, value);
            }
            iniparser_freedict(ini3);
        } else {
            Log::Warning("Could not load INI keys from file: %s", iniFileLocation);
        }
    }

    // Attempt to parse registry location to include keys if present
    ParseRegistryKeys(ini);

    iniparser_setstr(ini, (char*)MODULE_INI, inifile);
    iniparser_setstr(ini, (char*)INI_DIR, inidir);

    // Add module name to ini
    TCHAR filename[MAX_PATH];
    GetModuleFileName(hInstance, filename, MAX_PATH);
    iniparser_setstr(ini, (char*)MODULE_NAME, filename);

    // strip off filename to get module directory
    TCHAR filedir[MAX_PATH];
    GetFileDirectory(filename, filedir);
    iniparser_setstr(ini, (char*)MODULE_DIR, filedir);

    // Log init
    Log::Init(hInstance,
              iniparser_getstr(ini, (char*)LOG_FILE),
              iniparser_getstr(ini, (char*)LOG_LEVEL),
              ini);
    Log::Info("Module Name: %s", filename);
    Log::Info("Module INI: %s", inifile);
    Log::Info("Module Dir: %s", filedir);
    Log::Info("INI Dir: %s", filedir);

    // Store a reference to be used by JNI functions
    g_ini = ini;

    return ini;
}

char* INI::GetString(dictionary* ini, const TCHAR* section, const TCHAR* key, TCHAR* defValue, bool defFromMainSection)
{
    char tmp[MAX_PATH];
    tmp[0] = 0;

    if (section)
        strcat_s(tmp, sizeof(tmp), section);
    strcat_s(tmp, sizeof(tmp), key);

    if (section && defFromMainSection)
        defValue = iniparser_getstring(ini, key, defValue);

    return iniparser_getstring(ini, tmp, defValue);
}

int INI::GetInteger(dictionary* ini, const TCHAR* section, const TCHAR* key, int defValue, bool defFromMainSection)
{
    char tmp[MAX_PATH];
    tmp[0] = 0;

    if (section)
        strcat_s(tmp, sizeof(tmp), section);
    strcat_s(tmp, sizeof(tmp), key);

    if (section && defFromMainSection)
        defValue = iniparser_getint(ini, key, defValue);

    return iniparser_getint(ini, tmp, defValue);
}

bool INI::GetBoolean(dictionary* ini, const TCHAR* section, const TCHAR* key, bool defValue, bool defFromMainSection)
{
    char tmp[MAX_PATH];
    tmp[0] = 0;

    if (section)
        strcat_s(tmp, sizeof(tmp), section);
    strcat_s(tmp, sizeof(tmp), key);

    if (section && defFromMainSection)
        defValue = iniparser_getboolean(ini, key, defValue);

    return iniparser_getboolean(ini, tmp, defValue);
}

void INI::ParseRegistryKeys(dictionary* ini)
{
    // Now check if we have a registry location to load from
    char* iniRegistryLocation = iniparser_getstr(ini, (char*)INI_REGISTRY_LOCATION);
    if (!iniRegistryLocation) {
        return;
    }

    Log::Info("Loading INI keys from registry: %s", iniRegistryLocation);

    // find root key
    int len   = (int)strlen(iniRegistryLocation);
    int slash = 0;
    while (slash < len && iniRegistryLocation[slash] != '\\')
        slash++;

    if (slash == len) {
        Log::Warning("Unable to parse registry location (%s) - keys not included", iniRegistryLocation);
        return;
    }

    char* rootKey = _strdup(iniRegistryLocation);
    if (!rootKey) {
        Log::Warning("Out of memory duplicating registry root key");
        return;
    }
    rootKey[slash] = 0;

    HKEY hKey = GetHKey(rootKey);
    free(rootKey);

    if (hKey == 0) {
        Log::Warning("Unrecognized registry root key");
        return;
    }

    HKEY subKey;
    if (RegOpenKeyEx(hKey, &iniRegistryLocation[slash + 1], 0, KEY_READ, &subKey) != ERROR_SUCCESS) {
        Log::Warning("Unable to open registry location (%s)", iniRegistryLocation);
        return;
    }

    DWORD index   = 0;
    char  name[MAX_PATH + 2];
    char  data[4096];
    DWORD type;
    DWORD nameLen;
    DWORD dataLen;

    name[0] = ':';
    while (true) {
        nameLen = MAX_PATH;
        dataLen = (DWORD)sizeof(data);
        if (RegEnumValue(subKey, index, &name[1], &nameLen, NULL, &type, (LPBYTE)data, &dataLen) != ERROR_SUCCESS)
            break;

        bool  hasNamespace = StrContains(&name[1], ':');
        char* key          = hasNamespace ? &name[1] : name;

        if (type == REG_DWORD) {
            DWORD val = *((LPDWORD)data);
            sprintf_s(data, sizeof(data), "%d", val);
            iniparser_setstr(ini, key, data);
        } else if (type == REG_SZ && dataLen > 1) {
            iniparser_setstr(ini, key, data);
        }

        index++;
    }
}

HKEY INI::GetHKey(char* key)
{
    HKEY hKey = 0;

    if (strcmp(key, "HKEY_LOCAL_MACHINE") == 0) {
        hKey = HKEY_LOCAL_MACHINE;
    } else if (strcmp(key, "HKLM") == 0) {
        hKey = HKEY_LOCAL_MACHINE;
    } else if (strcmp(key, "HKEY_CURRENT_USER") == 0) {
        hKey = HKEY_CURRENT_USER;
    } else if (strcmp(key, "HKCU") == 0) {
        hKey = HKEY_CURRENT_USER;
    } else if (strcmp(key, "HKEY_CLASSES_ROOT") == 0) {
        hKey = HKEY_CLASSES_ROOT;
    } else if (strcmp(key, "HKCR") == 0) {
        hKey = HKEY_CLASSES_ROOT;
    }
    return hKey;
}

int INI::GetRegistryValue(char* input, char* output, int len)
{
    Log::Info("GetRegistryValue input (%s), output (%s), len (%d)", input, output, len);

    char rootKey[4096];
    strcpy_s(rootKey, sizeof(rootKey), input);

    char* slash = strchr(rootKey, '\\');
    if (slash == NULL) {
        Log::Warning("Invalid registry key, no backslash found (%s)", input);
        return ERROR_INVALID_DATA;
    }
    *slash = 0;
    char* key = slash + 1;

    Log::Info("GetRegistryValue rootKey (%s)", rootKey);

    HKEY hKey = GetHKey(rootKey);

    Log::Info("GetRegistryValue full key (%s)", key);

    char* colon = strchr(key, ':');
    if (colon == NULL) {
        Log::Warning("Invalid registry key, no key name found (%s)", input);
        return ERROR_INVALID_DATA;
    }

    *colon = 0;

    Log::Info("GetRegistryValue stripped key (%s)", key);

    char* valueName = colon + 1;

    Log::Info("GetRegistryValue valueName (%s)", valueName);

    HKEY subKey;
    long result = RegOpenKeyEx(hKey, key, 0, KEY_READ | KEY_WOW64_64KEY, &subKey);
    if (result != ERROR_SUCCESS) {
        Log::Warning("Unable to open registry key (%s) error (%d)", input, result);
        return ERROR_INVALID_DATA;
    }

    DWORD type;
    DWORD size = (DWORD)len;
    if (RegQueryValueEx(subKey, valueName, NULL, &type, (LPBYTE)output, &size) != ERROR_SUCCESS) {
        Log::Warning("Unable to get registry value (%s)", input);
        return ERROR_INVALID_DATA;
    }

    if (type != REG_DWORD && type != REG_SZ) {
        return ERROR_INVALID_DATA;
    }

    if (type == REG_DWORD) {
        DWORD val = *((LPDWORD)output);
        sprintf_s(output, (size_t)len, "%d", val);
    }

    return ERROR_SUCCESS;
}

void INI::ExpandRegistryVariables(dictionary* ini)
{
    char tmp[4096];
    char result[4096];
    int  len = (int)sizeof(result);

    for (int i = 0; i < ini->size; i++) {
        char* key   = ini->key[i];
        char* value = ini->val[i];
        if (value == NULL) {
            continue;
        }

        strcpy_s(tmp, sizeof(tmp), value);
        char* expansionStart = strstr(tmp, "$REG{");
        if (expansionStart == NULL) {
            continue;
        }
        char* keyStart = expansionStart + 5;
        *expansionStart = 0;
        char* regEnd = strchr(keyStart, '}');
        if (regEnd == NULL) {
            continue;
        }
        *regEnd = 0;
        if (GetRegistryValue(keyStart, result, len) == ERROR_SUCCESS) {
            char ev[4096];
            strcpy_s(ev, sizeof(ev), tmp);
            strcat_s(ev, sizeof(ev), result);
            strcat_s(ev, sizeof(ev), regEnd + 1);
            Log::Info("Reg: %s = '%s' to '%s'", key, value, ev);
            iniparser_setstr(ini, key, ev);
        }
    }
}

void INI::ExpandVariables(dictionary* ini)
{
    char tmp[4096];

    for (int i = 0; i < ini->size; i++) {
        char* key   = ini->key[i];
        char* value = ini->val[i];
        if (!value)
            continue;

        DWORD size = ExpandEnvironmentStrings(value, tmp, (DWORD)sizeof(tmp));
        if (size == 0 || size > sizeof(tmp)) {
            Log::Warning("Could not expand variable: %s", value);
            continue;
        }
        iniparser_setstr(ini, key, tmp);
    }
}

extern "C" __declspec(dllexport) dictionary* __cdecl INI_GetDictionary()
{
    return g_ini;
}

extern "C" __declspec(dllexport) const char* __cdecl INI_GetProperty(const char* key)
{
    return iniparser_getstr(g_ini, (char*)key);
}
