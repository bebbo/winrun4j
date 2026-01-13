/*******************************************************************************
* This program and the accompanying materials
* are made available under the terms of the Common Public License v1.0
* which accompanies this distribution, and is available at 
* http://www.eclipse.org/legal/cpl-v10.html
* 
* Contributors:
*     Peter Smith
*******************************************************************************/

#include "EventLog.h"
#include "../common/Log.h"
#include "../java/JNI.h"

#include <windows.h>
#include <string.h>

bool EventLog::RegisterNatives(JNIEnv *env)
{
    Log::Info("Registering natives for EventLog class");

    jclass clazz = JNI::FindClass(env, "org/boris/winrun4j/EventLog");
    if (clazz == NULL) {
        Log::Warning("Could not find EventLog class");
        if (env->ExceptionCheck())
            env->ExceptionClear();
        return false;
    }

    JNINativeMethod methods[1];
    methods[0].name      = (char*)"report";
    methods[0].signature = (char*)"(Ljava/lang/String;ILjava/lang/String;)Z";
    methods[0].fnPtr     = (void*)Report;

    env->RegisterNatives(clazz, methods, 1);
    if (env->ExceptionCheck()) {
        JNI::PrintStackTrace(env);
        env->ExceptionClear();
    }

    return true;
}

bool EventLog::Report(JNIEnv* env, jobject /*self*/, jstring source, jint type, jstring msg)
{
    if (source == NULL || msg == NULL)
        return false;

    jboolean isCopySrc = JNI_FALSE;
    jboolean isCopyMsg = JNI_FALSE;

    const char* src = env->GetStringUTFChars(source, &isCopySrc);
    if (!src) {
        return false;
    }

    const char* m = env->GetStringUTFChars(msg, &isCopyMsg);
    if (!m) {
        env->ReleaseStringUTFChars(source, src);
        return false;
    }

    HANDLE h = RegisterEventSourceA(NULL, src);
    if (h == NULL) {
        env->ReleaseStringUTFChars(source, src);
        env->ReleaseStringUTFChars(msg, m);
        return false;
    }

    // Classic EventLog pattern: one insertion string (m), no binary data.
    LPCSTR strings[1];
    strings[0] = m;

    BOOL ok = ReportEventA(
        h,
        (WORD)type,   // wType
        0,            // wCategory
        0,            // dwEventID
        NULL,         // lpUserSid
        1,            // wNumStrings
        0,            // dwDataSize
        strings,      // lpStrings
        NULL          // lpRawData
    );

    DeregisterEventSource(h);

    env->ReleaseStringUTFChars(source, src);
    env->ReleaseStringUTFChars(msg, m);

    return ok ? true : false;
}
