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

/* Timer ------------------------------------------------------------------- */
#if (defined(MDS_TIMER_INDEPENDENT) && (MDS_TIMER_INDEPENDENT != 0))
static MDS_ListNode_t g_sysTimerSkipList[MDS_TIMER_SKIPLIST_LEVEL];

static int TIMER_SkipListCompare(const MDS_ListNode_t *node, const void *value)
{
    const MDS_Timer_t *timer = CONTAINER_OF(node, MDS_Timer_t, node);
    MDS_Tick_t ticklimit = *((const MDS_Tick_t *)(value));
    MDS_Tick_t diffTick = timer->ticklimit - ticklimit;

    return ((diffTick != 0) && (diffTick < MDS_CLOCK_TICK_TIMER_MAX)) ? (1) : (-1);
}

static void TIMER_Check(MDS_ListNode_t timerList[], size_t size)
{
    if ((timerList[0].next == NULL) || (timerList[0].prev == NULL)) {
        MDS_SkipListInitNode(timerList, size);
    }

    MDS_ListNode_t runList = {.prev = &runList, .next = &runList};
    MDS_Item_t lock = MDS_CoreInterruptLock();

    while (!MDS_ListIsEmpty(&timerList[size - 1])) {
        MDS_Tick_t currTick = MDS_ClockGetTickCount();
        MDS_Timer_t *t = CONTAINER_OF(timerList[size - 1].next, MDS_Timer_t, node[size - 1]);

        if ((currTick - t->ticklimit) >= MDS_CLOCK_TICK_TIMER_MAX) {
            break;
        }

        MDS_SkipListRemoveNode(t->node, size);
        if ((t->flags & MDS_TIMER_TYPE_PERIOD) == 0U) {
            t->flags &= ~MDS_TIMER_FLAG_ACTIVED;
        }
        MDS_ListInsertNodeNext(&runList, &(t->node[size - 1]));

        if (t->entry != NULL) {
            t->entry(t->arg);
        }

        if (MDS_ListIsEmpty(&runList)) {
            continue;
        }

        MDS_ListRemoveNode(&(t->node[size - 1]));
        if ((t->flags & (MDS_TIMER_FLAG_ACTIVED | MDS_TIMER_TYPE_PERIOD)) ==
            (MDS_TIMER_FLAG_ACTIVED | MDS_TIMER_TYPE_PERIOD)) {
            MDS_TimerStart(t, MDS_TIMEOUT_TICKS(t->ticklimit - t->tickstart));
        }
    }

    MDS_CoreInterruptRestore(lock);
}

static MDS_Timer_t *TIMER_NextTickoutTimer(MDS_ListNode_t timerList[], size_t size)
{
    MDS_Timer_t *timer = NULL;
    MDS_Item_t lock = MDS_CoreInterruptLock();

    if (!MDS_ListIsEmpty(&(timerList[size - 1]))) {
        timer = CONTAINER_OF(timerList[size - 1].next, MDS_Timer_t, node[size - 1]);
    }

    MDS_CoreInterruptRestore(lock);

    return (timer);
}

MDS_Err_t MDS_TimerInit(MDS_Timer_t *timer, const char *name, MDS_Mask_t type,
                        MDS_TimerEntry_t entry, MDS_Arg_t *arg)
{
    MDS_ASSERT(timer != NULL);

    MDS_Err_t err = MDS_ObjectInit(&(timer->object), MDS_OBJECT_TYPE_TIMER, name);
    if (err == MDS_EOK) {
        MDS_SkipListInitNode(timer->node, ARRAY_SIZE(timer->node));
        timer->entry = entry;
        timer->arg = arg;
        timer->flags = type & (~MDS_TIMER_FLAG_ACTIVED);
    }

    return (err);
}

MDS_Err_t MDS_TimerDeInit(MDS_Timer_t *timer)
{
    MDS_TimerStop(timer);

    return (MDS_ObjectDeInit(&(timer->object)));
}

MDS_Timer_t *MDS_TimerCreate(const char *name, MDS_Mask_t type, MDS_TimerEntry_t entry,
                             MDS_Arg_t *arg)
{
    MDS_Timer_t *timer = (MDS_Timer_t *)MDS_ObjectCreate(sizeof(MDS_Timer_t),
                                                         MDS_OBJECT_TYPE_TIMER, name);
    if (timer != NULL) {
        MDS_SkipListInitNode(timer->node, ARRAY_SIZE(timer->node));
        timer->entry = entry;
        timer->arg = arg;
        timer->flags = type & (~MDS_TIMER_FLAG_ACTIVED);
    }

    return (timer);
}

MDS_Err_t MDS_TimerDestroy(MDS_Timer_t *timer)
{
    MDS_TimerStop(timer);

    return (MDS_ObjectDestory(&(timer->object)));
}

MDS_Err_t MDS_TimerStart(MDS_Timer_t *timer, MDS_Timeout_t timeout)
{
    MDS_ASSERT(timer != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(timer->object)) == MDS_OBJECT_TYPE_TIMER);

    if (timeout.ticks >= MDS_CLOCK_TICK_TIMER_MAX) {
        return (MDS_EINVAL);
    }

    static size_t skipRand = 0;
    MDS_ListNode_t *timerList = g_sysTimerSkipList;

    do {
        MDS_Item_t lock = MDS_CoreInterruptLock();

        MDS_SkipListRemoveNode(timer->node, ARRAY_SIZE(timer->node));
        timer->flags &= ~MDS_TIMER_FLAG_ACTIVED;

        if (timeout.ticks > 0) {
            timer->tickstart = MDS_ClockGetTickCount();
            timer->ticklimit = timer->tickstart + timeout.ticks;

            MDS_ListNode_t *skipNode[ARRAY_SIZE(timer->node)];
            MDS_SkipListSearchNode(skipNode, timerList, ARRAY_SIZE(timer->node),
                                   &(timer->ticklimit), TIMER_SkipListCompare);

            skipRand = skipRand + timer->tickstart + 1;
            MDS_SkipListInsertNode(skipNode, timer->node, ARRAY_SIZE(timer->node), skipRand,
                                   MDS_TIMER_SKIPLIST_SHIFT);
            timer->flags |= MDS_TIMER_FLAG_ACTIVED;
        }

        MDS_CoreInterruptRestore(lock);
    } while (0);

    return (MDS_EOK);
}

MDS_Err_t MDS_TimerStop(MDS_Timer_t *timer)
{
    MDS_ASSERT(timer != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(timer->object)) == MDS_OBJECT_TYPE_TIMER);

    if ((timer->flags & MDS_TIMER_FLAG_ACTIVED) != 0U) {
        MDS_Item_t lock = MDS_CoreInterruptLock();

        MDS_SkipListRemoveNode(timer->node, ARRAY_SIZE(timer->node));
        timer->flags &= ~MDS_TIMER_FLAG_ACTIVED;

        MDS_CoreInterruptRestore(lock);
    }

    return (MDS_EOK);
}

bool MDS_TimerIsActived(const MDS_Timer_t *timer)
{
    MDS_ASSERT(timer != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(timer->object)) == MDS_OBJECT_TYPE_TIMER);

    MDS_Item_t lock = MDS_CoreInterruptLock();
    bool isActived = ((timer->flags & MDS_TIMER_FLAG_ACTIVED) != 0U) ? (true) : (false);
    MDS_CoreInterruptRestore(lock);

    return (isActived);
}
#endif

void MDS_SysTimerInit(void)
{
#if (defined(MDS_TIMER_INDEPENDENT) && (MDS_TIMER_INDEPENDENT != 0))
    MDS_SkipListInitNode(g_sysTimerSkipList, ARRAY_SIZE(g_sysTimerSkipList));
#endif
}

void MDS_SysTimerCheck(void)
{
#if (defined(MDS_TIMER_INDEPENDENT) && (MDS_TIMER_INDEPENDENT != 0))
    TIMER_Check(g_sysTimerSkipList, ARRAY_SIZE(g_sysTimerSkipList));
#endif
}

MDS_Tick_t MDS_SysTimerNextTick(void)
{
    MDS_Tick_t ticknext = MDS_CLOCK_TICK_FOREVER;

#if (defined(MDS_TIMER_INDEPENDENT) && (MDS_TIMER_INDEPENDENT != 0))
    MDS_Timer_t *timer = TIMER_NextTickoutTimer(g_sysTimerSkipList,
                                                ARRAY_SIZE(g_sysTimerSkipList));
    if (timer != NULL) {
        ticknext = timer->ticklimit;
    }
#endif

    return (ticknext);
}

/* Semaphore --------------------------------------------------------------- */
MDS_Err_t MDS_SemaphoreInit(MDS_Semaphore_t *semaphore, const char *name, size_t init, size_t max)
{
    MDS_ASSERT(semaphore != NULL);

    MDS_Err_t err = MDS_ObjectInit(&(semaphore->object), MDS_OBJECT_TYPE_SEMAPHORE, name);
    if (err == MDS_EOK) {
        semaphore->value = init;
        semaphore->max = max;
        MDS_ListInitNode(&(semaphore->list));
    }

    return (err);
}

MDS_Err_t MDS_SemaphoreDeInit(MDS_Semaphore_t *semaphore)
{
    MDS_ASSERT(semaphore != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(semaphore->object)) == MDS_OBJECT_TYPE_SEMAPHORE);

    return (MDS_ObjectDeInit(&(semaphore->object)));
}

MDS_Semaphore_t *MDS_SemaphoreCreate(const char *name, size_t init, size_t max)
{
    MDS_Semaphore_t *semaphore = (MDS_Semaphore_t *)MDS_ObjectCreate(sizeof(MDS_Semaphore_t),
                                                                     MDS_OBJECT_TYPE_SEMAPHORE,
                                                                     name);
    if (semaphore != NULL) {
        semaphore->value = init;
        semaphore->max = max;
        MDS_ListInitNode(&(semaphore->list));
    }

    return (semaphore);
}

MDS_Err_t MDS_SemaphoreDestroy(MDS_Semaphore_t *semaphore)
{
    MDS_ASSERT(semaphore != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(semaphore->object)) == MDS_OBJECT_TYPE_SEMAPHORE);

    return (MDS_ObjectDestory(&(semaphore->object)));
}

MDS_Err_t MDS_SemaphoreAcquire(MDS_Semaphore_t *semaphore, MDS_Timeout_t timeout)
{
    MDS_ASSERT(semaphore != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(semaphore->object)) == MDS_OBJECT_TYPE_SEMAPHORE);

    MDS_Err_t err = MDS_ETIMEOUT;
    MDS_Tick_t tickstart = MDS_ClockGetTickCount();

    MDS_LOOP {
        MDS_Item_t lock = MDS_CoreInterruptLock();
        if (semaphore->value > 0) {
            semaphore->value -= 1;
            err = MDS_EOK;
        }
        MDS_CoreInterruptRestore(lock);

        if ((err == MDS_EOK) || ((MDS_ClockGetTickCount() - tickstart) >= timeout.ticks)) {
            break;
        }
    }

    return (err);
}

MDS_Err_t MDS_SemaphoreRelease(MDS_Semaphore_t *semaphore)
{
    MDS_ASSERT(semaphore != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(semaphore->object)) == MDS_OBJECT_TYPE_SEMAPHORE);

    MDS_Err_t err = MDS_EOK;

    MDS_Item_t lock = MDS_CoreInterruptLock();
    if (semaphore->value < semaphore->max) {
        semaphore->value += 1;
    } else {
        err = MDS_ERANGE;
    }
    MDS_CoreInterruptRestore(lock);

    return (err);
}

size_t MDS_SemaphoreGetValue(const MDS_Semaphore_t *semaphore, size_t *max)
{
    MDS_ASSERT(semaphore != NULL);

    if (max != NULL) {
        *max = semaphore->max;
    }

    return (semaphore->value);
}

/* Mutex ------------------------------------------------------------------- */
MDS_Err_t MDS_MutexInit(MDS_Mutex_t *mutex, const char *name)
{
    MDS_ASSERT(mutex != NULL);

    MDS_Err_t err = MDS_ObjectInit(&(mutex->object), MDS_OBJECT_TYPE_MUTEX, name);
    if (err == MDS_EOK) {
        mutex->owner = NULL;
        mutex->priority = MDS_KERNEL_THREAD_PRIORITY_MAX;
        mutex->value = 1;
        mutex->nest = 0;
        MDS_ListInitNode(&(mutex->list));
    }

    return (err);
}

MDS_Err_t MDS_MutexDeInit(MDS_Mutex_t *mutex)
{
    MDS_ASSERT(mutex != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(mutex->object)) == MDS_OBJECT_TYPE_MUTEX);

    return (MDS_ObjectDeInit(&(mutex->object)));
}

MDS_Mutex_t *MDS_MutexCreate(const char *name)
{
    MDS_Mutex_t *mutex = (MDS_Mutex_t *)MDS_ObjectCreate(sizeof(MDS_Mutex_t),
                                                         MDS_OBJECT_TYPE_MUTEX, name);
    if (mutex != NULL) {
        mutex->owner = NULL;
        mutex->priority = MDS_KERNEL_THREAD_PRIORITY_MAX;
        mutex->value = 1;
        mutex->nest = 0;
        MDS_ListInitNode(&(mutex->list));
    }

    return (mutex);
}

MDS_Err_t MDS_MutexDestroy(MDS_Mutex_t *mutex)
{
    MDS_ASSERT(mutex != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(mutex->object)) == MDS_OBJECT_TYPE_MUTEX);

    return (MDS_ObjectDestory(&(mutex->object)));
}

MDS_Err_t MDS_MutexAcquire(MDS_Mutex_t *mutex, MDS_Timeout_t timeout)
{
    MDS_ASSERT(mutex != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(mutex->object)) == MDS_OBJECT_TYPE_MUTEX);

    MDS_Err_t err = MDS_ETIMEOUT;
    MDS_Tick_t tickstart = MDS_ClockGetTickCount();

    MDS_LOOP {
        MDS_Item_t lock = MDS_CoreInterruptLock();
        if (mutex->value > 0) {
            mutex->value = 0;
            err = MDS_EOK;
        }
        MDS_CoreInterruptRestore(lock);

        if ((err == MDS_EOK) || ((MDS_ClockGetTickCount() - tickstart) >= timeout.ticks)) {
            break;
        }
    }

    return (MDS_EOK);
}

MDS_Err_t MDS_MutexRelease(MDS_Mutex_t *mutex)
{
    MDS_ASSERT(mutex != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(mutex->object)) == MDS_OBJECT_TYPE_MUTEX);

    MDS_Item_t lock = MDS_CoreInterruptLock();
    mutex->value = 1;
    MDS_CoreInterruptRestore(lock);

    return (MDS_EOK);
}

MDS_Thread_t *MDS_MutexGetOwner(const MDS_Mutex_t *mutex)
{
    MDS_ASSERT(mutex != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(mutex->object)) == MDS_OBJECT_TYPE_MUTEX);

    return (mutex->owner);
}

/* Event ------------------------------------------------------------------- */
MDS_Err_t MDS_EventInit(MDS_Event_t *event, const char *name)
{
    MDS_ASSERT(event != NULL);

    MDS_Err_t err = MDS_ObjectInit(&(event->object), MDS_OBJECT_TYPE_EVENT, name);
    if (err == MDS_EOK) {
        event->value = 0U;
        MDS_ListInitNode(&(event->list));
    }

    return (err);
}

MDS_Err_t MDS_EventDeInit(MDS_Event_t *event)
{
    MDS_ASSERT(event != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(event->object)) == MDS_OBJECT_TYPE_EVENT);

    return (MDS_ObjectDeInit(&(event->object)));
}

MDS_Event_t *MDS_EventCreate(const char *name)
{
    MDS_Event_t *event = (MDS_Event_t *)MDS_ObjectCreate(sizeof(MDS_Event_t),
                                                         MDS_OBJECT_TYPE_EVENT, name);
    if (event != NULL) {
        event->value = 0U;
        MDS_ListInitNode(&(event->list));
    }

    return (event);
}

MDS_Err_t MDS_EventDestroy(MDS_Event_t *event)
{
    MDS_ASSERT(event != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(event->object)) == MDS_OBJECT_TYPE_EVENT);

    return (MDS_ObjectDestory(&(event->object)));
}

MDS_Err_t MDS_EventWait(MDS_Event_t *event, MDS_Mask_t mask, MDS_Mask_t opt, MDS_Mask_t *recv,
                        MDS_Timeout_t timeout)
{
    MDS_ASSERT(event != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(event->object)) == MDS_OBJECT_TYPE_EVENT);
    MDS_ASSERT((opt & (MDS_EVENT_OPT_AND | MDS_EVENT_OPT_OR)) !=
               (MDS_EVENT_OPT_AND | MDS_EVENT_OPT_OR));

    if ((mask == 0U) || ((opt & (MDS_EVENT_OPT_AND | MDS_EVENT_OPT_OR)) == 0U)) {
        return (MDS_EINVAL);
    }

    MDS_Err_t err = MDS_ETIMEOUT;
    MDS_Tick_t tickstart = MDS_ClockGetTickCount();

    MDS_LOOP {
        MDS_Item_t lock = MDS_CoreInterruptLock();
        if ((((opt & MDS_EVENT_OPT_AND) != 0U) && ((event->value & mask) == mask)) ||
            (((opt & MDS_EVENT_OPT_OR) != 0U) && ((event->value & mask) != 0U))) {
            if (recv != NULL) {
                *recv = event->value;
            }
            if ((opt & MDS_EVENT_OPT_NOCLR) == 0U) {
                event->value &= (MDS_Mask_t)(~mask);
            }
            err = MDS_EOK;
        }
        MDS_CoreInterruptRestore(lock);

        if ((err == MDS_EOK) || ((MDS_ClockGetTickCount() - tickstart) >= timeout.ticks)) {
            break;
        }
    }

    return (err);
}

MDS_Err_t MDS_EventSet(MDS_Event_t *event, MDS_Mask_t mask)
{
    MDS_ASSERT(event != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(event->object)) == MDS_OBJECT_TYPE_EVENT);

    MDS_Item_t lock = MDS_CoreInterruptLock();
    event->value |= mask;
    MDS_CoreInterruptRestore(lock);

    return (MDS_EOK);
}

MDS_Err_t MDS_EventClr(MDS_Event_t *event, MDS_Mask_t mask)
{
    MDS_ASSERT(event != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(event->object)) == MDS_OBJECT_TYPE_EVENT);

    MDS_Item_t lock = MDS_CoreInterruptLock();
    event->value &= (MDS_Mask_t)(~mask);
    MDS_CoreInterruptRestore(lock);

    return (MDS_EOK);
}

MDS_Mask_t MDS_EventGetValue(const MDS_Event_t *event)
{
    MDS_ASSERT(event != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(event->object)) == MDS_OBJECT_TYPE_EVENT);

    return (event->value);
}

/* Thread ------------------------------------------------------------------ */
MDS_Err_t MDS_ThreadDelayTick(MDS_Tick_t delay)
{
    MDS_ClockDelayRawTick(delay);

    return (MDS_EOK);
}

/* Kernel ------------------------------------------------------------------ */
static volatile int g_sysCriticalNest = 0;

MDS_Thread_t *MDS_KernelCurrentThread(void)
{
    return (NULL);
}

void MDS_KernelEnterCritical(void)
{
    MDS_Item_t lock = MDS_CoreInterruptLock();

    g_sysCriticalNest += 1;

    MDS_CoreInterruptRestore(lock);
}

void MDS_KernelExitCritical(void)
{
    MDS_Item_t lock = MDS_CoreInterruptLock();

    g_sysCriticalNest -= 1;

    MDS_CoreInterruptRestore(lock);
}

size_t MDS_KernelGetCritical(void)
{
    return (g_sysCriticalNest);
}

MDS_Tick_t MDS_KernelGetSleepTick(void)
{
    MDS_Tick_t sleepTick = 0;

    MDS_Item_t lock = MDS_CoreInterruptLock();

    MDS_Tick_t nextTick = MDS_SysTimerNextTick();
    MDS_Tick_t currTick = MDS_ClockGetTickCount();
    sleepTick = (nextTick > currTick) ? (nextTick - currTick) : (0);

    MDS_CoreInterruptRestore(lock);

    return (sleepTick);
}

void MDS_KernelCompensateTick(MDS_Tick_t ticks)
{
    MDS_Item_t lock = MDS_CoreInterruptLock();

    MDS_Tick_t currTick = MDS_ClockGetTickCount() + ticks;
    MDS_ClockSetTickCount(currTick);
    MDS_SysTimerCheck();

    MDS_CoreInterruptRestore(lock);
}

/* Clock ------------------------------------------------------------------- */
static volatile MDS_Tick_t g_sysClockTickCount = 0U;
static int8_t g_unixTimeZone = MDS_CLOCK_TIMEZONE_DEFAULT;
static MDS_Time_t g_unixTimeBase = MDS_CLOCK_TIMESTAMP_DEFAULT;

MDS_Tick_t MDS_ClockGetTickCount(void)
{
    return (g_sysClockTickCount);
}

void MDS_ClockSetTickCount(MDS_Tick_t tickcnt)
{
    MDS_Item_t lock = MDS_CoreInterruptLock();

    g_sysClockTickCount = tickcnt;

    MDS_CoreInterruptRestore(lock);
}

void MDS_ClockIncTickCount(void)
{
    MDS_Item_t lock = MDS_CoreInterruptLock();

    g_sysClockTickCount += 1U;

    MDS_CoreInterruptRestore(lock);

    MDS_SysTimerCheck();
}

MDS_Time_t MDS_ClockGetTimestamp(int8_t *tz)
{
    MDS_Tick_t ticks = MDS_ClockGetTickCount();
    MDS_Time_t ts = g_unixTimeBase + MDS_CLOCK_TICK_TO_MS(ticks);

    if (tz != NULL) {
        *tz = g_unixTimeZone;
    }

    return (ts);
}

void MDS_ClockSetTimestamp(MDS_Time_t ts, int8_t tz)
{
    MDS_Tick_t ticks = MDS_ClockGetTickCount();

    g_unixTimeBase = ts - MDS_CLOCK_TICK_TO_MS(ticks);
    g_unixTimeZone = tz;
}
