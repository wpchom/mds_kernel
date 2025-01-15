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
#include "mds_def.h"
#include "mds_log.h"

static void (*g_logVaPrintf)(size_t level, const void *fmt, size_t cnt, va_list ap) = NULL;

/* Function ---------------------------------------------------------------- */
void MDS_LOG_Register(void (*logVaPrintf)(size_t level, const void *fmt, size_t cnt, va_list ap))
{
    g_logVaPrintf = logVaPrintf;
}

void MDS_LOG_VaPrintf(size_t level, const void *fmt, size_t cnt, va_list ap)
{
    if (g_logVaPrintf != NULL) {
        g_logVaPrintf(level, fmt, cnt, ap);
    }
}

void MDS_LOG_Printf(size_t level, const void *fmt, size_t cnt, ...)
{
    va_list ap;

    va_start(ap, cnt);
    MDS_LOG_VaPrintf(level, fmt, cnt, ap);
    va_end(ap);
}

__attribute__((weak)) void MDS_CoreBacktrace(void)
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

    MDS_CoreBacktrace();

    for (;;) {
    }
}

__attribute__((noreturn)) void MDS_AssertPrintf(const char *assertion, const char *function)
{
    __attribute__((unused)) void *caller = __builtin_return_address(0);

    MDS_PanicPrintf("[ASSERT] '%s' in function:'%s' caller:%p,%p", assertion, function, caller,
                    __builtin_extract_return_addr(caller));
}
