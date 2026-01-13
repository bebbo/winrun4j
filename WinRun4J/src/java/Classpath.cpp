/*******************************************************************************
* This program and the accompanying materials
* are made available under the terms of the Common Public License v1.0
* which accompanies this distribution, and is available at 
* http://www.eclipse.org/legal/cpl-v10.html
* 
* Contributors:
*     Peter Smith
*******************************************************************************/

#include "Classpath.h"
#include "../common/Log.h"
#include "../common/Dictionary.h"
#include "../common/Runtime.h"

#include <windows.h>
#include <string.h>
#include <stdlib.h>

namespace
{
    bool g_classpathMaxWarned = false;
}

static void ExpandClassPathEntry(char* arg, char*** result, int* count)
{
    Log::Info("Expanding Classpath: %s", arg);

    char fullpath[MAX_PATH];
    GetFullPathNameA(arg, MAX_PATH, fullpath, NULL);

    WIN32_FIND_DATAA fd;
    WIN32_FIND_DATAA fdcheck;

    // No wildcard -> direct file check
    if (strchr(arg, '*') == NULL) {
        HANDLE h = FindFirstFileA(fullpath, &fd);
        if (h != INVALID_HANDLE_VALUE) {

            char* dup = _strdup(fullpath);
            if (!dup) {
                FindClose(h);
                return;
            }

            char** newArr = (char**)realloc(*result, sizeof(char*) * (*count + 1));
            if (!newArr) {
                free(dup);
                FindClose(h);
                return;
            }

            *result = newArr;
            (*result)[(*count)++] = dup;

            FindClose(h);
            return;
        }
    }

    int len  = (int)strlen(fullpath);
    int prev = 0;
    bool hasStar = false;
    char search[MAX_PATH];

    for (int i = 0; i <= len; i++) {

        if (fullpath[i] == '/' || fullpath[i] == '\\' || fullpath[i] == 0) {

            if (hasStar) {
                char saved = fullpath[i];
                fullpath[i] = 0;

                HANDLE h = FindFirstFileA(fullpath, &fd);
                if (h == INVALID_HANDLE_VALUE) {
                    fullpath[i] = saved;
                    return;
                }

                do {
                    if (prev != 0)
                        fullpath[prev] = 0;

                    strcpy_s(search, fullpath);
                    if (prev != 0)
                        fullpath[prev] = '\\';

                    strcat_s(search, "\\");
                    strcat_s(search, fd.cFileName);

                    if (i < len - 1) {
                        HANDLE h2 = FindFirstFileA(search, &fdcheck);
                        if (h2 != INVALID_HANDLE_VALUE) {
                            bool isDir = (fdcheck.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                            FindClose(h2);

                            if (isDir) {
                                strcat_s(search, "\\");
                                strcat_s(search, &fullpath[i + 1]);
                            } else {
                                continue;
                            }
                        }
                    }

                    ExpandClassPathEntry(search, result, count);

                } while (FindNextFileA(h, &fd));

                FindClose(h);
                fullpath[i] = saved;
                return;
            }

            hasStar = false;
            prev = i;
        }
        else if (fullpath[i] == '*') {
            hasStar = true;
        }
    }
}

void Classpath::BuildClassPath(dictionary* ini, char*** args, UINT& count)
{
    char currentDir[MAX_PATH];
    char* workingDirectory = iniparser_getstr(ini, (char*)WORKING_DIR);

    // Temporarily switch to INI directory if no working directory is set
    if (!workingDirectory) {
        GetCurrentDirectoryA(MAX_PATH, currentDir);
        SetCurrentDirectoryA(iniparser_getstr(ini, (char*)INI_DIR));
    }

    // Dynamic list of expanded classpath entries
    char** entries = NULL;
    int    entryCount = 0;

    char entryName[MAX_PATH];
    int i = 0;

    while (true) {
        _snprintf_s(entryName, MAX_PATH, _TRUNCATE, "%s.%d", CLASS_PATH, i + 1);
        char* entry = iniparser_getstr(ini, entryName);

        if (entry != NULL) {
            // Expand wildcards -> produces multiple entries
            ExpandClassPathEntry(entry, &entries, &entryCount);
            i = 0;
        }

        i++;
        if (i > 42 && entry == NULL)
            break;
    }

    // Build final classpath string
    char* classpath = NULL;

    for (int j = 0; j < entryCount; j++) {

        size_t newLen =
            (classpath ? strlen(classpath) + 1 : 0) +
            strlen(entries[j]) + 1;

        char* temp = (char*)malloc(newLen + 1);
        temp[0] = 0;

        if (classpath) {
            strcat_s(temp, newLen + 1, classpath);
            strcat_s(temp, newLen + 1, ";");
            free(classpath);
        }

        strcat_s(temp, newLen + 1, entries[j]);
        classpath = temp;

        free(entries[j]);
    }

    free(entries);

    char* built = _strdup(classpath ? classpath : "");
    free(classpath);

    // Log truncated classpath
    char argl[MAX_LOG_LENGTH - 100];
    StrTruncate(argl, built, MAX_LOG_LENGTH - 100);
    Log::Info("Generated Classpath: %s", argl);

    // Build final -cp argument
    size_t cpLen = strlen(built) + strlen(CLASS_PATH_ARG) + 2;
    char* cpArg = (char*)malloc(cpLen);
    strcpy_s(cpArg, cpLen, CLASS_PATH_ARG);
    strcat_s(cpArg, cpLen, built);

    // Append to args (dynamic)
    char** newArgs = (char**)realloc(*args, sizeof(char*) * (count + 1));
    if (!newArgs) {
        free(cpArg);
        free(built);
        return;
    }

    *args = newArgs;
    (*args)[count++] = cpArg;

    free(built);

    // Restore working directory
    if (!workingDirectory) {
        SetCurrentDirectoryA(currentDir);
    }
}
