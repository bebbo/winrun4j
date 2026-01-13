/*******************************************************************************
 * This program and the accompanying materials
 * are made available under the terms of the Common Public License v1.0
 * which accompanies this distribution, and is available at 
 * http://www.eclipse.org/legal/cpl-v10.html
 * 
 * Contributors:
 *     Peter Smith
 *******************************************************************************/

#include "Runtime.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

// ------------------------------------------------------------
// Safe string truncate
// ------------------------------------------------------------
extern void _cdecl StrTruncate(LPSTR target, LPSTR source, size_t len)
{
    if (!source || !target || len == 0)
        return;

    size_t srclen = strlen(source);

    if (srclen < len) {
        strcpy_s(target, len, source);
        return;
    }

    // Copy len-1 chars and terminate
    memcpy(target, source, len - 1);
    target[len - 1] = 0;
}

// ------------------------------------------------------------
// StartsWith
// ------------------------------------------------------------
extern bool _cdecl StartsWith(LPSTR str, LPSTR substr)
{
    if (!str || !substr)
        return false;

    size_t n = strlen(substr);
    return strncmp(str, substr, n) == 0;
}

// ------------------------------------------------------------
// StripArg0
// ------------------------------------------------------------
extern LPSTR _cdecl StripArg0(LPSTR lpCmdLine)
{
    if (!lpCmdLine)
        return lpCmdLine;

    size_t len = strlen(lpCmdLine);
    size_t point = FindNextArg(lpCmdLine, 0, len);
    return &lpCmdLine[point];
}

// ------------------------------------------------------------
// FindNextArg
// ------------------------------------------------------------
extern size_t _cdecl FindNextArg(LPSTR lpCmdLine, size_t start, size_t len)
{
    bool inQuotes = false;

    for (; start < len; start++) {
        char c = lpCmdLine[start];
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ' ') {
            if (!inQuotes)
                break;
        }
    }
    return (start == len) ? start : start + 1;
}

// ------------------------------------------------------------
// StrContains
// ------------------------------------------------------------
extern bool _cdecl StrContains(LPSTR str, char c)
{
    if (!str)
        return false;

    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        if (str[i] == c)
            return true;
    }
    return false;
}

// ------------------------------------------------------------
// StrReplace
// ------------------------------------------------------------
extern void _cdecl StrReplace(LPSTR str, char old, char nu)
{
    if (!str)
        return;

    size_t len = strlen(str);
    for (size_t i = 0; i < len; i++) {
        if (str[i] == old)
            str[i] = nu;
    }
}

// ------------------------------------------------------------
// StrTrim
// ------------------------------------------------------------
extern void _cdecl StrTrim(LPSTR str, LPSTR trimChars)
{
    if (!str || !trimChars)
        return;

    size_t len = strlen(str);
    if (len == 0)
        return;

    size_t start = 0;
    size_t end = len - 1;

    // Left trim
    while (start < len && StrContains(trimChars, str[start]))
        start++;

    // Right trim
    while (end > start && StrContains(trimChars, str[end]))
        end--;

    if (start > 0 || end < len - 1) {
        size_t newLen = end - start + 1;
        memmove(str, &str[start], newLen);
        str[newLen] = 0;
    }
}

// ------------------------------------------------------------
// ParseCommandLine
// ------------------------------------------------------------
extern void _cdecl ParseCommandLine(LPSTR lpCmdLine, TCHAR** args, UINT& count, bool includeFirst)
{
    if (!lpCmdLine || *lpCmdLine == 0)
        return;

    StrTrim(lpCmdLine, " ");

    int len = (int)strlen(lpCmdLine);
    if (len == 0)
        return;

    int startPos[1024], endPos[1024];
    int currentIndex = -1;

    bool insideQuotes = false;
    bool insideArgSeparator = true;

    int i;
    for (i = 0; i < len; i++) {
        char c = lpCmdLine[i];

        if (c == ' ' || c == '\t') {
            if (insideQuotes)
                continue;
            if (insideArgSeparator)
                continue;

            endPos[currentIndex] = i;
            insideArgSeparator = true;
        } else {
            if (insideArgSeparator) {
                startPos[++currentIndex] = i;
                endPos[currentIndex] = -1;
                insideArgSeparator = false;
            }
            if (c == '"')
                insideQuotes = !insideQuotes;
        }
    }

    if (endPos[currentIndex] < 0)
        endPos[currentIndex] = i;

    int index = count;

    for (i = includeFirst ? 0 : 1; i <= currentIndex; i++) {
        int begin = startPos[i];
        int end = endPos[i];

        if (lpCmdLine[begin] == '"' && lpCmdLine[end - 1] == '"') {
            begin++;
            end--;
        }

        int valueLen = end - begin;
        if (valueLen > 0) {
            TCHAR* value = (TCHAR*)malloc((valueLen + 1) * sizeof(TCHAR));
            memcpy(value, &lpCmdLine[begin], valueLen);
            value[valueLen] = 0;
            args[index++] = value;
        }
    }

    count = index;
}

// ------------------------------------------------------------
// GetFileDirectory
// ------------------------------------------------------------
extern void _cdecl GetFileDirectory(LPSTR filename, LPSTR output)
{
    if (!filename || !output) {
        if (output) output[0] = 0;
        return;
    }

    int len = (int)strlen(filename);
    if (len == 0) {
        output[0] = 0;
        return;
    }

    int i = len - 1;
    while (i >= 0 && filename[i] != '\\' && filename[i] != '/')
        i--;

    if (i >= 0) {
        memcpy(output, filename, i + 1);
        output[i + 1] = 0;
    } else {
        output[0] = 0;
    }
}

// ------------------------------------------------------------
// GetFileName
// ------------------------------------------------------------
extern void _cdecl GetFileName(LPSTR filename, LPSTR output)
{
    if (!filename || !output) {
        if (output) output[0] = 0;
        return;
    }

    int len = (int)strlen(filename);
    if (len == 0) {
        output[0] = 0;
        return;
    }

    int i = len - 1;
    while (i >= 0 && filename[i] != '\\' && filename[i] != '/')
        i--;

    strcpy_s(output, MAX_PATH, &filename[i + 1]);
}

// ------------------------------------------------------------
// GetFileExtension
// ------------------------------------------------------------
extern void _cdecl GetFileExtension(LPSTR filename, LPSTR output)
{
    if (!filename || !output) {
        if (output) output[0] = 0;
        return;
    }

    int len = (int)strlen(filename);
    if (len == 0) {
        output[0] = 0;
        return;
    }

    int i = len - 1;
    while (i >= 0 && filename[i] != '.')
        i--;

    if (i >= 0)
        strcpy_s(output, MAX_PATH, &filename[i]);
    else
        output[0] = 0;
}

// ------------------------------------------------------------
// GetFileNameSansExtension
// ------------------------------------------------------------
extern void _cdecl GetFileNameSansExtension(LPSTR filename, LPSTR output)
{
    if (!filename || !output) {
        if (output) output[0] = 0;
        return;
    }

    int len = (int)strlen(filename);
    if (len == 0) {
        output[0] = 0;
        return;
    }

    int i = len - 1;
    int dotPos = -1;

    while (i >= 0) {
        if (dotPos == -1 && filename[i] == '.')
            dotPos = i;
        if (dotPos != -1 && (filename[i] == '\\' || filename[i] == '/'))
            break;
        i--;
    }

    if (dotPos != -1) {
        int start = (i >= 0) ? i + 1 : 0;
        int count = dotPos - start;
        memcpy(output, &filename[start], count);
        output[count] = 0;
    } else {
        strcpy_s(output, MAX_PATH, filename);
    }
}

// ------------------------------------------------------------
// strrev
// ------------------------------------------------------------
#if !defined(_MSC_VER)
extern "C" char* _cdecl strrev(char* str)
{
    if (!str) return NULL;

    char* end = str + strlen(str) - 1;
    while (str < end) {
        char c = *str;
        *str = *end;
        *end = c;
        str++;
        end--;
    }
    return str;
}
#endif

// ------------------------------------------------------------
// Legacy MSVC compatibility shims
// ------------------------------------------------------------
#if _MSC_VER < 1400
extern "C" char* _cdecl _strdup(const char* str)
{
    if (!str)
        return NULL;

    size_t len = strlen(str) + 1;
    char* r = (char*)malloc(len);
    if (!r)
        return NULL;

    strcpy_s(r, len, str);
    return r;
}
#endif

extern "C" void __cdecl _wassert(int e)
{
	UNREFERENCED_PARAMETER(e);
    // no-op
}

// ------------------------------------------------------------
// Tiny CRT stubs
// ------------------------------------------------------------
#ifdef TINY

extern "C" int __cdecl _purecall()
{
	return 0;
}

extern "C" int* _errno()
{
	return 0;
}

extern "C" void * __cdecl malloc(size_t size)
{
    return HeapAlloc( GetProcessHeap(), 0, size );
}

extern "C" void __cdecl free(void * p)
{
    HeapFree( GetProcessHeap(), 0, p );
}

extern "C" errno_t _cdecl strcpy_s(char *dest, rsize_t size, const char *source)
{
	strcpy(dest, source);
	return 0;
}

extern "C" int _cdecl vsprintf_s(char *buffer, size_t sizeInBytes, const char *format, va_list argptr)
{
	return vsprintf(buffer, format, argptr);
}

extern "C" int _cdecl _ftol_sse(int c)
{
	return 0;
}

extern "C" int _cdecl setvbuf(FILE* file, char* buf, int mode, size_t size)
{
	return 0;
}

extern "C" int _cdecl _ftol2_sse()
{
	return 0;
}

extern "C" FILE* _cdecl __iob_func()
{
	static FILE _iob[3] = {
	  { NULL, 0, NULL, 0, 0, 0, 0 },
	  { NULL, 0, NULL, 0, 1, 0, 0 },
	  { NULL, 0, NULL, 0, 2, 0, 0 }
	};

	return _iob;
}

extern "C" FILE* _cdecl _fdopen(int fd, const char *mode)
{
	FILE* ret = (FILE *) malloc(sizeof(FILE));
	ret->_file = fd;
	ret->_base = 0;
	ret->_cnt = 0;
	ret->_ptr = NULL;
	ret->_flag = _IOREAD | _IOWRT;
	ret->_bufsiz = 0;
	ret->_charbuf = 0;

	return ret;
}

extern "C" int _cdecl _open_osfhandle(int c)
{
	return c;
}

extern "C" int __cdecl _fileno(FILE* _File)
{
	return _File->_file;
}

HANDLE __cdecl _get_osfhandle(int _FileHandle)
{
	return (HANDLE) _FileHandle;
}

#endif
