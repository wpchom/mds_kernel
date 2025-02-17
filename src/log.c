/**
 * Copyright (c) [2022] [pchom]
 * [MDS] is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 **/
/* Include ----------------------------------------------------------------- */
#include "mds_sys.h"

/* Define ------------------------------------------------------------------ */
#ifndef MDS_LOG_PRINT_LEVEL
#define MDS_LOG_PRINT_LEVEL MDS_LOG_LEVEL_INFO
#endif

#ifndef MDS_LOG_COMPRESS_MAGIC
#define MDS_LOG_COMPRESS_MAGIC 0xD6 /* log 109 => 0x6D */
#endif

#ifndef MDS_LOG_COMPRESS_ARG_FIX
/* 0xFFFFFFFF => -1111111111 (2's complement: 0xBDC5CA39) */
#define MDS_LOG_COMPRESS_ARG_FIX(x) ((x == 0xFFFFFFFF) ? (0xBDC5CA39) : (x))
#endif

/* Variable ---------------------------------------------------------------- */
static size_t g_logPrintLevel = MDS_LOG_PRINT_LEVEL;
static size_t g_logCompressPsn = 0;
static void (*g_logVaPrintf)(size_t level, const void *fmt, size_t cnt, va_list ap) = NULL;

/* Function ---------------------------------------------------------------- */
size_t MDS_LOG_GetPrintLevel(void)
{
    return (g_logPrintLevel);
}

void MDS_LOG_SetPrintLevel(size_t level)
{
    g_logPrintLevel = level;
}

size_t MDS_LOG_CompressStructVa(MDS_LOG_Compress_t *log, size_t level, const char *fmt, size_t cnt, va_list ap)
{
    if (log == NULL) {
        return (0);
    }

    MDS_Item_t lock = MDS_CoreInterruptLock();
    g_logCompressPsn++;
    MDS_CoreInterruptRestore(lock);

    if (cnt > MDS_LOG_COMPRESS_ARGS_NUMS) {
        cnt = MDS_LOG_COMPRESS_ARGS_NUMS;
    }

    log->magic = MDS_LOG_COMPRESS_MAGIC;
    log->address = (uintptr_t)fmt & 0x00FFFFFF;
    log->level = level;
    log->count = cnt;
    log->psn = g_logCompressPsn;
    log->timestamp = MDS_ClockGetTimestamp(NULL);

    for (size_t idx = 0; idx < cnt; idx++) {
        uint32_t val = va_arg(ap, uint32_t);
        log->args[idx] = MDS_LOG_COMPRESS_ARG_FIX(val);
    }

    return (sizeof(MDS_LOG_Compress_t) - (sizeof(uint32_t) * (MDS_LOG_COMPRESS_ARGS_NUMS + cnt)));
}

size_t MDS_LOG_CompressSturctPrint(MDS_LOG_Compress_t *log, size_t level, const char *fmt, size_t cnt, ...)
{
    va_list ap;

    va_start(ap, cnt);
    size_t len = MDS_LOG_CompressStructVa(log, level, fmt, cnt, ap);
    va_end(ap);

    return (len);
}

void MDS_LOG_Register(void (*logVaPrintf)(size_t level, const void *fmt, size_t cnt, va_list ap))
{
    g_logVaPrintf = logVaPrintf;
}

__attribute__((weak)) void MDS_LOG_VaPrintf(size_t level, const void *fmt, size_t cnt, va_list ap)
{
    if (g_logVaPrintf != NULL) {
        g_logVaPrintf(level, fmt, cnt, ap);
    }
}

void MDS_LOG_Printf(size_t level, const void *fmt, size_t cnt, ...)
{
    va_list ap;

    if (level > g_logPrintLevel) {
        return;
    }

    va_start(ap, cnt);
    MDS_LOG_VaPrintf(level, fmt, cnt, ap);
    va_end(ap);
}

__attribute__((weak)) void MDS_CorePanicTrace(void)
{
    __attribute__((unused)) void *caller = __builtin_return_address(0);

    MDS_LOG_F("[PANIC] caller:%p,%p", caller, __builtin_extract_return_addr(caller));
}

__attribute__((noreturn)) void MDS_PanicPrintf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    MDS_LOG_VaPrintf(MDS_LOG_LEVEL_FATAL, fmt, 0, ap);
    va_end(ap);

    MDS_CorePanicTrace();

    for (;;) {
    }
}

__attribute__((noreturn)) void MDS_AssertPrintf(const char *assertion, const char *function)
{
    __attribute__((unused)) void *caller = __builtin_return_address(0);

    MDS_PanicPrintf("[ASSERT] '%s' in function:'%s' caller:%p,%p", assertion, function, caller,
                    __builtin_extract_return_addr(caller));
}
