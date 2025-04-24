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
#ifndef __MDS_LOG_H__
#define __MDS_LOG_H__

/* Include ----------------------------------------------------------------- */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Define ------------------------------------------------------------------ */
#ifndef MDS_LOG_STRING_SECTION
#define MDS_LOG_STRING_SECTION ".logstr."
#endif

#ifndef MDS_LOG_BUILD_LEVEL
#define MDS_LOG_BUILD_LEVEL MDS_LOG_LEVEL_INFO
#endif

#ifndef MDS_LOG_TAG
#define MDS_LOG_TAG ""
#endif

#define MDS_LOG_LEVEL_OFF   0x00U
#define MDS_LOG_LEVEL_TRACK 0x01U
#define MDS_LOG_LEVEL_FATAL 0x02U
#define MDS_LOG_LEVEL_ERROR 0x03U
#define MDS_LOG_LEVEL_WARN  0x04U
#define MDS_LOG_LEVEL_INFO  0x05U
#define MDS_LOG_LEVEL_DEBUG 0x06U
#define MDS_LOG_LEVEL_TRACE 0x07U

#define MDS_LOG_ARG_SEQS(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, N, ...) N
#define MDS_LOG_ARG_NUMS(...)                                                                                          \
    MDS_LOG_ARG_SEQS(0, ##__VA_ARGS__, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)

/* Log ---------------------------------------------------------------------- */
size_t MDS_LOG_GetPrintLevel(void);
void MDS_LOG_SetPrintLevel(size_t level);
void MDS_LOG_Register(void (*logVaPrintf)(size_t level, const void *fmt, size_t cnt, va_list ap));
void MDS_LOG_VaPrintf(size_t level, const void *fmt, size_t cnt, va_list ap);
void MDS_LOG_Printf(size_t level, const void *fmt, size_t cnt, ...);

#if (MDS_LOG_BUILD_LEVEL >= MDS_LOG_LEVEL_TRACK)
#define MDS_LOG_K(fmt, ...)                                                                                            \
    do {                                                                                                               \
        static const __attribute__((section(MDS_LOG_STRING_SECTION "track"))) char fmtstr[] = MDS_LOG_TAG fmt "\n";    \
        MDS_LOG_Printf(MDS_LOG_LEVEL_TRACK, fmtstr, MDS_LOG_ARG_NUMS(__VA_ARGS__), ##__VA_ARGS__);                     \
    } while (0)
#else
#define MDS_LOG_K(fmt, ...)
#endif

#if (MDS_LOG_BUILD_LEVEL >= MDS_LOG_LEVEL_FATAL)
#define MDS_LOG_F(fmt, ...)                                                                                            \
    do {                                                                                                               \
        static const __attribute__((section(MDS_LOG_STRING_SECTION "fatal"))) char fmtstr[] = MDS_LOG_TAG fmt "\n";    \
        MDS_LOG_Printf(MDS_LOG_LEVEL_FATAL, fmtstr, MDS_LOG_ARG_NUMS(__VA_ARGS__), ##__VA_ARGS__);                     \
    } while (0)
#else
#define MDS_LOG_F(fmt, ...)
#endif

#if (MDS_LOG_BUILD_LEVEL >= MDS_LOG_LEVEL_ERROR)
#define MDS_LOG_E(fmt, ...)                                                                                            \
    do {                                                                                                               \
        static const __attribute__((section(MDS_LOG_STRING_SECTION "error"))) char fmtstr[] = MDS_LOG_TAG fmt "\n";    \
        MDS_LOG_Printf(MDS_LOG_LEVEL_ERROR, fmtstr, MDS_LOG_ARG_NUMS(__VA_ARGS__), ##__VA_ARGS__);                     \
    } while (0)
#else
#define MDS_LOG_E(fmt, ...)
#endif

#if (MDS_LOG_BUILD_LEVEL >= MDS_LOG_LEVEL_WARN)
#define MDS_LOG_W(fmt, ...)                                                                                            \
    do {                                                                                                               \
        static const __attribute__((section(MDS_LOG_STRING_SECTION "warn"))) char fmtstr[] = MDS_LOG_TAG fmt "\n";     \
        MDS_LOG_Printf(MDS_LOG_LEVEL_WARN, fmtstr, MDS_LOG_ARG_NUMS(__VA_ARGS__), ##__VA_ARGS__);                      \
    } while (0)
#else
#define MDS_LOG_W(fmt, ...)
#endif

#if (MDS_LOG_BUILD_LEVEL >= MDS_LOG_LEVEL_INFO)
#define MDS_LOG_I(fmt, ...)                                                                                            \
    do {                                                                                                               \
        static const __attribute__((section(MDS_LOG_STRING_SECTION "info"))) char fmtstr[] = MDS_LOG_TAG fmt "\n";     \
        MDS_LOG_Printf(MDS_LOG_LEVEL_INFO, fmtstr, MDS_LOG_ARG_NUMS(__VA_ARGS__), ##__VA_ARGS__);                      \
    } while (0)
#else
#define MDS_LOG_I(fmt, ...)
#endif

#if (MDS_LOG_BUILD_LEVEL >= MDS_LOG_LEVEL_DEBUG)
#define MDS_LOG_D(fmt, ...)                                                                                            \
    do {                                                                                                               \
        static const __attribute__((section(MDS_LOG_STRING_SECTION "debug"))) char fmtstr[] = MDS_LOG_TAG fmt "\n";    \
        MDS_LOG_Printf(MDS_LOG_LEVEL_DEBUG, fmtstr, MDS_LOG_ARG_NUMS(__VA_ARGS__), ##__VA_ARGS__);                     \
    } while (0)
#else
#define MDS_LOG_D(fmt, ...)
#endif

#if (MDS_LOG_BUILD_LEVEL >= MDS_LOG_LEVEL_TRACE)
#define MDS_LOG_T(fmt, ...)                                                                                            \
    do {                                                                                                               \
        static const __attribute__((section(MDS_LOG_STRING_SECTION "trace"))) char fmtstr[] = MDS_LOG_TAG fmt "\n";    \
        MDS_LOG_Printf(MDS_LOG_LEVEL_TRACE, fmtstr, MDS_LOG_ARG_NUMS(__VA_ARGS__), ##__VA_ARGS__);                     \
    } while (0)
#else
#define MDS_LOG_T(fmt, ...)
#endif

/* Panic ------------------------------------------------------------------- */
__attribute__((noreturn)) void MDS_PanicPrintf(const char *fmt, ...);

#define MDS_PANIC(fmt, ...)                                                                                            \
    do {                                                                                                               \
        static const __attribute__((section(MDS_LOG_STRING_SECTION "panic"))) char fmtstr[] = "[PANIC]" fmt "\n";      \
        MDS_PanicPrintf(fmtstr, ##__VA_ARGS__);                                                                        \
    } while (0)

/* Assert ------------------------------------------------------------------ */
#if (defined(MDS_LOG_ASSERT_ENABLE) && (MDS_LOG_ASSERT_ENABLE > 0))
__attribute__((noreturn)) void MDS_AssertPrintf(const char *assertion, const char *function);

#define MDS_ASSERT(condition)                                                                                          \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            static const __attribute__((section(MDS_LOG_STRING_SECTION "assert"))) char cond[] = #condition;           \
            MDS_AssertPrintf(cond, __FUNCTION__);                                                                      \
        }                                                                                                              \
    } while (0)
#elif (!defined(MDS_ASSERT))
#define MDS_ASSERT(condition) (void)(condition)
#endif

/* Compress ---------------------------------------------------------------- */
typedef struct MDS_LOG_Compress {
    uint32_t magic     : 8;
    uint32_t address   : 24;  // 0xFFxxxxxx
    uint32_t level     : 4;
    uint32_t count     : 4;
    uint32_t psn       : 12;
    uint64_t timestamp : 44;  // ms
    uint32_t args[0];
} MDS_LOG_Compress_t;

size_t MDS_LOG_CompressStructVa(MDS_LOG_Compress_t *log, size_t level, const char *fmt, size_t cnt, va_list ap);
size_t MDS_LOG_CompressSturctPrint(MDS_LOG_Compress_t *log, size_t level, const char *fmt, size_t cnt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __MDS_LOG_H__ */
