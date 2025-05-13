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
#include "kernel.h"

/* Define ------------------------------------------------------------------ */
MDS_LOG_MODULE_DECLARE(kernel, CONFIG_MDS_KERNEL_LOG_LEVEL);

/* IPC thread -------------------------------------------------------------- */
static void IPC_ListSuspendThread(MDS_ListNode_t *list, MDS_Thread_t *thread, bool isPrio,
                                  MDS_Timeout_t timeout)
{
    MDS_Thread_t *find = NULL;

    if (MDS_ThreadSuspend(thread) != MDS_EOK) {
        return;
    }

    if (isPrio) {
        MDS_Thread_t *iter = NULL;
        MDS_LIST_FOREACH_NEXT (iter, node, list) {
            if (thread->currPrio < iter->currPrio) {
                find = iter;
                break;
            }
        }
    }

    if (find != NULL) {
        MDS_ListInsertNodePrev(&(find->node), &(thread->node));
    } else {
        MDS_ListInsertNodePrev(list, &(thread->node));
    }

    if (timeout.ticks < MDS_CLOCK_TICK_TIMER_MAX) {
        MDS_TimerStart(&(thread->timer), timeout);
    }
}

static MDS_Err_t IPC_ListSuspendWait(MDS_Item_t *lock, MDS_ListNode_t *list,
                                     MDS_Thread_t *thread, MDS_Timeout_t timeout)
{
    MDS_Tick_t deltaTick = MDS_ClockGetTickCount();

    IPC_ListSuspendThread(list, thread, true, timeout);
    MDS_CoreInterruptRestore(*lock);
    MDS_KernelSchedulerCheck();
    if (thread->err != MDS_EOK) {
        return (thread->err);
    }

    *lock = MDS_CoreInterruptLock();
    deltaTick = MDS_ClockGetTickCount() - deltaTick;
    if (timeout.ticks > deltaTick) {
        timeout.ticks -= deltaTick;

        return (MDS_EOK);
    } else {
        return (MDS_ETIMEOUT);
    }
}

static void IPC_ListResumeThread(MDS_ListNode_t *list)
{
    MDS_Thread_t *thread = CONTAINER_OF(list->next, MDS_Thread_t, node);

    MDS_ThreadResume(thread);
}

static void IPC_ListResumeAllThread(MDS_ListNode_t *list)
{
    MDS_Thread_t *thread = NULL;

    while (!MDS_ListIsEmpty(list)) {
        MDS_Item_t lock = MDS_CoreInterruptLock();

        thread = CONTAINER_OF(list->next, MDS_Thread_t, node);
        thread->err = MDS_EAGAIN;

        MDS_ThreadResume(thread);

        MDS_CoreInterruptRestore(lock);
    }
}

/* Semaphore --------------------------------------------------------------- */
MDS_Err_t MDS_SemaphoreInit(MDS_Semaphore_t *semaphore, const char *name, size_t init,
                            size_t max)
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

    IPC_ListResumeAllThread(&(semaphore->list));

    return (MDS_ObjectDeInit(&(semaphore->object)));
}

MDS_Semaphore_t *MDS_SemaphoreCreate(const char *name, size_t init, size_t max)
{
    MDS_Semaphore_t *semaphore = (MDS_Semaphore_t *)MDS_ObjectCreate(
        sizeof(MDS_Semaphore_t), MDS_OBJECT_TYPE_SEMAPHORE, name);
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

    IPC_ListResumeAllThread(&(semaphore->list));

    return (MDS_ObjectDestory(&(semaphore->object)));
}

MDS_Err_t MDS_SemaphoreAcquire(MDS_Semaphore_t *semaphore, MDS_Timeout_t timeout)
{
    MDS_ASSERT(semaphore != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(semaphore->object)) == MDS_OBJECT_TYPE_SEMAPHORE);

    MDS_Err_t err = MDS_EOK;
    MDS_Thread_t *thread = MDS_KernelCurrentThread();

    MDS_HOOK_CALL(KERNEL, semaphore,
                  (semaphore, MDS_KERNEL_TRACE_SEMAPHORE_TRY_ACQUIRE, err, timeout));

    MDS_LOG_D("[semaphore]thread(%p) acquire semephore(%p), value:%u", thread, semaphore,
              semaphore->value);

    MDS_Item_t lock = MDS_CoreInterruptLock();

    if (semaphore->value > 0) {
        semaphore->value -= 1;
        MDS_CoreInterruptRestore(lock);
    } else if (timeout.ticks == MDS_CLOCK_TICK_NO_WAIT) {
        thread->err = MDS_ETIMEOUT;
        MDS_CoreInterruptRestore(lock);
        err = MDS_ETIMEOUT;
    } else if (thread != NULL) {
        MDS_LOG_D("[semaphore]suspend thread(%p) entry:%p timer wait:%lu", thread,
                  thread->entry, timeout.ticks);

        IPC_ListSuspendThread(&(semaphore->list), thread, true, timeout);
        MDS_CoreInterruptRestore(lock);
        MDS_KernelSchedulerCheck();
        err = thread->err;
    }

    MDS_HOOK_CALL(KERNEL, semaphore,
                  (semaphore, MDS_KERNEL_TRACE_SEMAPHORE_HAS_ACQUIRE, MDS_EOK, timeout));

    return (err);
}

MDS_Err_t MDS_SemaphoreRelease(MDS_Semaphore_t *semaphore)
{
    MDS_ASSERT(semaphore != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(semaphore->object)) == MDS_OBJECT_TYPE_SEMAPHORE);

    MDS_LOG_D("[semephore]release semephore(%p) value:%u", semaphore, semaphore->value);

    MDS_Err_t err = MDS_EOK;
    MDS_Item_t lock = MDS_CoreInterruptLock();

    MDS_HOOK_CALL(
        KERNEL, semaphore,
        (semaphore, MDS_KERNEL_TRACE_SEMAPHORE_HAS_RELEASE, err, MDS_TIMEOUT_NO_WAIT));

    if (!MDS_ListIsEmpty(&(semaphore->list))) {
        IPC_ListResumeThread(&(semaphore->list));
        MDS_CoreInterruptRestore(lock);
        MDS_KernelSchedulerCheck();
    } else {
        if (semaphore->value < semaphore->max) {
            semaphore->value += 1;
        } else {
            err = MDS_ERANGE;
        }
        MDS_CoreInterruptRestore(lock);
    }

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

/* Condition --------------------------------------------------------------- */
MDS_Err_t MDS_ConditionInit(MDS_Condition_t *condition, const char *name)
{
    MDS_ASSERT(condition != NULL);

    return (MDS_SemaphoreInit(condition, name, 0, -1));
}

MDS_Err_t MDS_ConditionDeInit(MDS_Condition_t *condition)
{
    MDS_ASSERT(condition != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(condition->object)) == MDS_OBJECT_TYPE_SEMAPHORE);

    if (!MDS_ListIsEmpty(&(condition->list))) {
        return (MDS_EBUSY);
    }

    while (MDS_SemaphoreAcquire(condition, MDS_TIMEOUT_NO_WAIT) == MDS_EBUSY) {
        MDS_ConditionBroadCast(condition);
    }

    MDS_SemaphoreDeInit(condition);

    return (MDS_EOK);
}

MDS_Err_t MDS_ConditionBroadCast(MDS_Condition_t *condition)
{
    MDS_ASSERT(condition != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(condition->object)) == MDS_OBJECT_TYPE_SEMAPHORE);

    MDS_Err_t err;
    do {
        err = MDS_SemaphoreAcquire(condition, MDS_TIMEOUT_NO_WAIT);
        if ((err == MDS_ETIMEOUT) || (err == MDS_EOK)) {
            MDS_SemaphoreRelease(condition);
        }
    } while (err == MDS_ETIMEOUT);

    return (err);
}

MDS_Err_t MDS_ConditionSignal(MDS_Condition_t *condition)
{
    MDS_ASSERT(condition != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(condition->object)) == MDS_OBJECT_TYPE_SEMAPHORE);

    MDS_Err_t err = MDS_EOK;
    MDS_Item_t lock = MDS_CoreInterruptLock();
    if (MDS_ListIsEmpty(&(condition->list))) {
        MDS_CoreInterruptRestore(lock);
    } else {
        MDS_CoreInterruptRestore(lock);
        err = MDS_SemaphoreRelease(condition);
    }

    return (err);
}

MDS_Err_t MDS_ConditionWait(MDS_Condition_t *condition, MDS_Mutex_t *mutex,
                            MDS_Timeout_t timeout)
{
    MDS_ASSERT(condition != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(condition->object)) == MDS_OBJECT_TYPE_SEMAPHORE);
    MDS_ASSERT(mutex != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(mutex->object)) == MDS_OBJECT_TYPE_MUTEX);

    MDS_Err_t err = MDS_EOK;
    MDS_Thread_t *thread = MDS_KernelCurrentThread();
    if ((thread == NULL) || (thread != mutex->owner)) {
        return (MDS_EACCES);
    }

    MDS_Item_t lock = MDS_CoreInterruptLock();
    if (condition->value > 0) {
        condition->value -= 1;
        MDS_CoreInterruptRestore(lock);
    } else if (timeout.ticks == MDS_CLOCK_TICK_NO_WAIT) {
        MDS_CoreInterruptRestore(lock);
        err = MDS_ETIMEOUT;
    } else {
        MDS_ASSERT(thread != NULL);

        IPC_ListSuspendThread(&(condition->list), thread, false, timeout);
        MDS_MutexRelease(mutex);
        MDS_CoreInterruptRestore(lock);
        MDS_KernelSchedulerCheck();

        err = thread->err;
        MDS_MutexAcquire(mutex, MDS_TIMEOUT_FOREVER);
    }

    return (err);
}

/* Mutex ------------------------------------------------------------------- */
MDS_Err_t MDS_MutexInit(MDS_Mutex_t *mutex, const char *name)
{
    MDS_ASSERT(mutex != NULL);

    MDS_Err_t err = MDS_ObjectInit(&(mutex->object), MDS_OBJECT_TYPE_MUTEX, name);
    if (err == MDS_EOK) {
        mutex->owner = NULL;
        mutex->priority = CONFIG_MDS_KERNEL_THREAD_PRIORITY_MAX;
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

    IPC_ListResumeAllThread(&(mutex->list));

    return (MDS_ObjectDeInit(&(mutex->object)));
}

MDS_Mutex_t *MDS_MutexCreate(const char *name)
{
    MDS_Mutex_t *mutex =
        (MDS_Mutex_t *)MDS_ObjectCreate(sizeof(MDS_Mutex_t), MDS_OBJECT_TYPE_MUTEX, name);
    if (mutex != NULL) {
        mutex->owner = NULL;
        mutex->priority = CONFIG_MDS_KERNEL_THREAD_PRIORITY_MAX;
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

    IPC_ListResumeAllThread(&(mutex->list));

    return (MDS_ObjectDestory(&(mutex->object)));
}

MDS_Err_t MDS_MutexAcquire(MDS_Mutex_t *mutex, MDS_Timeout_t timeout)
{
    MDS_ASSERT(mutex != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(mutex->object)) == MDS_OBJECT_TYPE_MUTEX);

    MDS_Err_t err = MDS_EOK;
    MDS_Thread_t *thread = MDS_KernelCurrentThread();
    if (thread == NULL) {
        MDS_LOG_W("[mutex]thread is null try to acquire mutex");
        return (MDS_EACCES);
    }

    MDS_HOOK_CALL(KERNEL, mutex,
                  (mutex, MDS_KERNEL_TRACE_MUTEX_TRY_ACQUIRE, err, timeout));

    MDS_LOG_D("[mutex]thread(%p) entry:%p acquire mutex(%p) value:%u nest:%u onwer:%p",
              thread, thread->entry, mutex, mutex->value, mutex->nest, mutex->owner);

    MDS_Item_t lock = MDS_CoreInterruptLock();

    if (thread == mutex->owner) {
        if (mutex->nest < (__typeof__(mutex->nest))(-1)) {
            mutex->nest += 1;
        } else {
            err = MDS_ERANGE;
        }
        MDS_CoreInterruptRestore(lock);
    } else if (mutex->value > 0) {
        mutex->value -= 1;
        mutex->owner = thread;
        mutex->priority = thread->currPrio;
        if (mutex->nest < (__typeof__(mutex->nest))(-1)) {
            mutex->nest += 1;
        } else {
            err = MDS_ERANGE;
        }
        MDS_CoreInterruptRestore(lock);
    } else if (timeout.ticks == MDS_CLOCK_TICK_NO_WAIT) {
        thread->err = MDS_ETIMEOUT;
        MDS_CoreInterruptRestore(lock);
        err = MDS_ETIMEOUT;
    } else {
        MDS_LOG_D("[mutex]mutex suspend thread(%p) entry:%p timer wait:%lu", thread,
                  thread->entry, timeout.ticks);

        if (thread->currPrio < mutex->owner->currPrio) {
            MDS_ThreadChangePriority(mutex->owner, thread->currPrio);
        }
        IPC_ListSuspendThread(&(mutex->list), thread, true, timeout);
        MDS_CoreInterruptRestore(lock);
        MDS_KernelSchedulerCheck();
        err = thread->err;
    }

    MDS_HOOK_CALL(KERNEL, mutex,
                  (mutex, MDS_KERNEL_TRACE_MUTEX_HAS_ACQUIRE, err, timeout));

    return (err);
}

MDS_Err_t MDS_MutexRelease(MDS_Mutex_t *mutex)
{
    MDS_ASSERT(mutex != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(mutex->object)) == MDS_OBJECT_TYPE_MUTEX);

    MDS_Thread_t *thread = MDS_KernelCurrentThread();
    if (thread == NULL) {
        MDS_LOG_W("[mutex]thread is null try to release mutex");
        return (MDS_EACCES);
    }

    MDS_LOG_D("[mutex]thread(%p) entry:%p release mutex(%p) value:%u nest:%u onwer:%p",
              thread, thread->entry, mutex, mutex->value, mutex->nest, mutex->owner);

    MDS_Item_t lock = MDS_CoreInterruptLock();

    if (thread != mutex->owner) {
        thread->err = MDS_EACCES;
        MDS_CoreInterruptRestore(lock);
        return (MDS_EACCES);
    }

    MDS_HOOK_CALL(
        KERNEL, mutex,
        (mutex, MDS_KERNEL_TRACE_MUTEX_HAS_RELEASE, MDS_EOK, MDS_TIMEOUT_NO_WAIT));

    mutex->nest -= 1;
    if (mutex->nest == 0) {
        if (mutex->priority != mutex->owner->currPrio) {
            MDS_ThreadChangePriority(mutex->owner, mutex->priority);
        }
        if (!MDS_ListIsEmpty(&(mutex->list))) {
            mutex->owner = CONTAINER_OF(mutex->list.next, MDS_Thread_t, node);
            mutex->priority = mutex->owner->currPrio;
            if (mutex->nest < (__typeof__(mutex->nest))(-1)) {
                mutex->nest += 1;
            } else {
                MDS_CoreInterruptRestore(lock);
                return (MDS_ERANGE);
            }
            IPC_ListResumeThread(&(mutex->list));
            MDS_CoreInterruptRestore(lock);
            MDS_KernelSchedulerCheck();
            return (MDS_EOK);
        } else {
            mutex->owner = NULL;
            mutex->priority = CONFIG_MDS_KERNEL_THREAD_PRIORITY_MAX;
            if (mutex->value < (__typeof__(mutex->value))(-1)) {
                mutex->value += 1;
            } else {
                MDS_CoreInterruptRestore(lock);
                return (MDS_ERANGE);
            }
        }
    }

    MDS_CoreInterruptRestore(lock);

    return (MDS_EOK);
}

MDS_Thread_t *MDS_MutexGetOwner(const MDS_Mutex_t *mutex)
{
    MDS_ASSERT(mutex != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(mutex->object)) == MDS_OBJECT_TYPE_MUTEX);

    return (mutex->owner);
}

/* RwLock ------------------------------------------------------------------ */
MDS_Err_t MDS_RwLockInit(MDS_RwLock_t *rwlock, const char *name)
{
    MDS_ASSERT(rwlock != NULL);

    MDS_Err_t err = MDS_MutexInit(&(rwlock->mutex), name);
    if (err == MDS_EOK) {
        MDS_ConditionInit(&(rwlock->condRd), name);
        MDS_ConditionInit(&(rwlock->condWr), name);
        rwlock->readers = 0;
    }

    return (err);
}

MDS_Err_t MDS_RwLockDeInit(MDS_RwLock_t *rwlock)
{
    MDS_ASSERT(rwlock != NULL);

    MDS_Err_t err = MDS_MutexAcquire(&(rwlock->mutex), MDS_TIMEOUT_NO_WAIT);
    if (err != MDS_EOK) {
        return (err);
    }

    if ((rwlock->readers != 0) || (!MDS_ListIsEmpty(&(rwlock->condWr.list))) ||
        (!MDS_ListIsEmpty(&(rwlock->condRd.list)))) {
        err = MDS_EBUSY;
    } else {
        err = MDS_SemaphoreAcquire(&(rwlock->condRd), MDS_TIMEOUT_NO_WAIT);
        if (err == MDS_EOK) {
            err = MDS_SemaphoreAcquire(&(rwlock->condWr), MDS_TIMEOUT_NO_WAIT);
            if (err == MDS_EOK) {
                MDS_SemaphoreRelease(&(rwlock->condRd));
                MDS_SemaphoreRelease(&(rwlock->condWr));

                MDS_SemaphoreDeInit(&(rwlock->condRd));
                MDS_SemaphoreDeInit(&(rwlock->condWr));
            } else {
                MDS_SemaphoreRelease(&(rwlock->condRd));
                err = MDS_EBUSY;
            }
        } else {
            err = MDS_EBUSY;
        }
    }
    MDS_MutexRelease(&(rwlock->mutex));

    if (err == MDS_EOK) {
        MDS_MutexDeInit(&(rwlock->mutex));
    }

    return (err);
}

MDS_Err_t MDS_RwLockAcquireRead(MDS_RwLock_t *rwlock, MDS_Timeout_t timeout)
{
    MDS_ASSERT(rwlock != NULL);

    MDS_Tick_t startTick = MDS_ClockGetTickCount();
    MDS_Err_t err = MDS_MutexAcquire(&(rwlock->mutex), timeout);
    if (err != MDS_EOK) {
        return (err);
    }

    while ((rwlock->readers < 0) || (!MDS_ListIsEmpty(&(rwlock->condWr.list)))) {
        if (timeout.ticks != MDS_CLOCK_TICK_FOREVER) {
            MDS_Tick_t elapsedTick = MDS_ClockGetTickCount() - startTick;
            timeout.ticks = (elapsedTick <= timeout.ticks) ?
                                (timeout.ticks - elapsedTick) :
                                (MDS_CLOCK_TICK_NO_WAIT);
        }
        err = MDS_ConditionWait(&(rwlock->condRd), &(rwlock->mutex), timeout);
        if (err != MDS_EOK) {
            break;
        }
    }

    if (err == MDS_EOK) {
        rwlock->readers += 1;
    }

    MDS_MutexRelease(&(rwlock->mutex));

    return (err);
}

MDS_Err_t MDS_RwLockAcquireWrite(MDS_RwLock_t *rwlock, MDS_Timeout_t timeout)
{
    MDS_ASSERT(rwlock != NULL);

    MDS_Tick_t startTick = MDS_ClockGetTickCount();
    MDS_Err_t err = MDS_MutexAcquire(&(rwlock->mutex), timeout);
    if (err != MDS_EOK) {
        return (err);
    }

    while (rwlock->readers != 0) {
        if (timeout.ticks != MDS_CLOCK_TICK_FOREVER) {
            MDS_Tick_t elapsedTick = MDS_ClockGetTickCount() - startTick;
            timeout.ticks = (elapsedTick <= timeout.ticks) ?
                                (timeout.ticks - elapsedTick) :
                                (MDS_CLOCK_TICK_NO_WAIT);
        }
        err = MDS_ConditionWait(&(rwlock->condWr), &(rwlock->mutex), timeout);
        if (err != MDS_EOK) {
            break;
        }
    }

    if (err == MDS_EOK) {
        rwlock->readers -= 1;
    }

    MDS_MutexRelease(&(rwlock->mutex));

    return (err);
}

MDS_Err_t MDS_RwLockRelease(MDS_RwLock_t *rwlock)
{
    MDS_ASSERT(rwlock != NULL);

    MDS_Err_t err = MDS_MutexAcquire(&(rwlock->mutex), MDS_TIMEOUT_FOREVER);
    if (err != MDS_EOK) {
        return (err);
    }

    if (rwlock->readers > 0) {
        rwlock->readers -= 1;
    } else if (rwlock->readers == -1) {
        rwlock->readers = 0;
    }

    if (!MDS_ListIsEmpty(&(rwlock->condWr.list))) {
        if (rwlock->readers == 0) {
            err = MDS_ConditionSignal(&(rwlock->condWr));
        }
    } else if (!MDS_ListIsEmpty(&(rwlock->condRd.list))) {
        err = MDS_ConditionBroadCast(&(rwlock->condRd));
    }

    MDS_MutexRelease(&(rwlock->mutex));

    return (err);
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

    IPC_ListResumeAllThread(&(event->list));

    return (MDS_ObjectDeInit(&(event->object)));
}

MDS_Event_t *MDS_EventCreate(const char *name)
{
    MDS_Event_t *event =
        (MDS_Event_t *)MDS_ObjectCreate(sizeof(MDS_Event_t), MDS_OBJECT_TYPE_EVENT, name);
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

    IPC_ListResumeAllThread(&(event->list));

    return (MDS_ObjectDestory(&(event->object)));
}

MDS_Err_t MDS_EventWait(MDS_Event_t *event, MDS_Mask_t mask, MDS_Mask_t opt,
                        MDS_Mask_t *recv, MDS_Timeout_t timeout)
{
    MDS_ASSERT(event != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(event->object)) == MDS_OBJECT_TYPE_EVENT);
    MDS_ASSERT((opt & (MDS_EVENT_OPT_AND | MDS_EVENT_OPT_OR)) !=
               (MDS_EVENT_OPT_AND | MDS_EVENT_OPT_OR));

    if ((mask == 0U) || ((opt & (MDS_EVENT_OPT_AND | MDS_EVENT_OPT_OR)) == 0U)) {
        return (MDS_EINVAL);
    }

    MDS_Err_t err = MDS_EOK;
    MDS_Thread_t *thread = MDS_KernelCurrentThread();

    MDS_HOOK_CALL(KERNEL, event,
                  (event, MDS_KERNEL_TRACE_EVENT_TRY_ACQUIRE, err, timeout));

    MDS_LOG_D("[event]thread(%p) wait event(%p) which value:%x mask:%x opt:%x", thread,
              event, event->value, mask, opt);

    MDS_Item_t lock = MDS_CoreInterruptLock();

    if ((((opt & MDS_EVENT_OPT_AND) != 0U) && ((event->value & mask) == mask)) ||
        (((opt & MDS_EVENT_OPT_OR) != 0U) && ((event->value & mask) != 0U))) {
        if (recv != NULL) {
            *recv = thread->eventMask;
        }
        thread->eventMask = event->value & mask;
        thread->eventOpt = opt;
        if ((opt & MDS_EVENT_OPT_NOCLR) == 0U) {
            event->value &= (MDS_Mask_t)(~mask);
        }
        MDS_CoreInterruptRestore(lock);
    } else if (timeout.ticks == MDS_CLOCK_TICK_NO_WAIT) {
        thread->err = MDS_ETIMEOUT;
        MDS_CoreInterruptRestore(lock);
        err = MDS_ETIMEOUT;
    } else if (thread != NULL) {
        MDS_LOG_D("[event]event suspend thread(%p) entry:%p timer wait:%lu", thread,
                  thread->entry, timeout.ticks);

        thread->eventMask = mask;
        thread->eventOpt = opt;
        IPC_ListSuspendThread(&(event->list), thread, true, timeout);
        MDS_CoreInterruptRestore(lock);
        MDS_KernelSchedulerCheck();
        err = thread->err;

        if (recv != NULL) {
            *recv = thread->eventMask;
        }
    }

    MDS_HOOK_CALL(KERNEL, event,
                  (event, MDS_KERNEL_TRACE_EVENT_HAS_ACQUIRE, err, timeout));

    return (err);
}

MDS_Err_t MDS_EventSet(MDS_Event_t *event, MDS_Mask_t mask)
{
    MDS_ASSERT(event != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(event->object)) == MDS_OBJECT_TYPE_EVENT);

    MDS_Err_t err = MDS_EOK;

    MDS_LOG_D("[event]event(%p) which value:%x set mask:%x", event, event->value, mask);

    MDS_Thread_t *iter = NULL;
    MDS_Item_t lock = MDS_CoreInterruptLock();

    MDS_HOOK_CALL(KERNEL, event,
                  (event, MDS_KERNEL_TRACE_EVENT_HAS_SET, err, MDS_TIMEOUT_TICKS(mask)));

    event->value |= mask;
    MDS_LIST_FOREACH_NEXT (iter, node, &(event->list)) {
        err = MDS_EINVAL;
        if ((iter->eventOpt & MDS_EVENT_OPT_AND) != 0U) {
            if ((iter->eventMask & event->value) == iter->eventMask) {
                err = MDS_EOK;
            }
        } else if ((iter->eventOpt & MDS_EVENT_OPT_OR) != 0U) {
            if ((iter->eventMask & event->value) != 0U) {
                iter->eventMask &= event->value;
                err = MDS_EOK;
            }
        } else {
            break;
        }
        if (err == MDS_EOK) {
            if ((iter->eventOpt & MDS_EVENT_OPT_NOCLR) == 0U) {
                event->value &= ~iter->eventMask;
            }
            IPC_ListResumeThread(&(event->list));
            MDS_CoreInterruptRestore(lock);
            MDS_KernelSchedulerCheck();
            return (MDS_EOK);
        }
    }

    MDS_CoreInterruptRestore(lock);

    return (err);
}

MDS_Err_t MDS_EventClr(MDS_Event_t *event, MDS_Mask_t mask)
{
    MDS_ASSERT(event != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(event->object)) == MDS_OBJECT_TYPE_EVENT);

    MDS_LOG_D("[event]event(%p) which value:%x clr mask:%x", event, event->value, mask);

    MDS_Item_t lock = MDS_CoreInterruptLock();

    MDS_HOOK_CALL(KERNEL, event,
                  (event, MDS_KERNEL_TRACE_EVENT_HAS_CLR, MDS_EOK,
                   (MDS_Timeout_t) {.ticks = mask}));

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

/* MsgQueue ---------------------------------------------------------------- */
#ifdef MDS_MSGQUEUE_SIZE_TYPE
typedef MDS_MSGQUEUE_SIZE_TYPE MDS_MsgQueueSize_t;
#else
typedef size_t MDS_MsgQueueSize_t;
#endif

typedef struct MDS_MsgQueueHeader {
    struct MDS_MsgQueueHeader *next;
    MDS_MsgQueueSize_t len;
} __attribute__((packed)) MDS_MsgQueueHeader_t;

static void MDS_MsgQueueListInit(MDS_MsgQueue_t *msgQueue, size_t msgNums)
{
    msgQueue->lfree = NULL;
    msgQueue->lhead = NULL;
    msgQueue->ltail = NULL;

    for (size_t idx = 0; idx < msgNums; idx++) {
        MDS_MsgQueueHeader_t *list = (MDS_MsgQueueHeader_t *)(&(
            ((uint8_t *)(msgQueue->queBuff))[idx * (sizeof(MDS_MsgQueueHeader_t) +
                                                    msgQueue->msgSize)]));
        list->next = (MDS_MsgQueueHeader_t *)(msgQueue->lfree);
        msgQueue->lfree = list;
    }
}

MDS_Err_t MDS_MsgQueueInit(MDS_MsgQueue_t *msgQueue, const char *name, void *queBuff,
                           size_t bufSize, size_t msgSize)
{
    MDS_ASSERT(msgQueue != NULL);
    MDS_ASSERT(bufSize > (sizeof(MDS_MsgQueueHeader_t) + msgSize));
    MDS_ASSERT(msgSize <= ((__typeof__(((MDS_MsgQueueHeader_t *)0)->len))(-1)));

    MDS_Err_t err = MDS_ObjectInit(&(msgQueue->object), MDS_OBJECT_TYPE_MSGQUEUE, name);
    if (err == MDS_EOK) {
        msgQueue->queBuff = queBuff;
        msgQueue->msgSize =
            VALUE_ALIGN(msgSize + MDS_SYSMEM_ALIGN_SIZE - 1, MDS_SYSMEM_ALIGN_SIZE);
        MDS_ListInitNode(&(msgQueue->listRecv));
        MDS_ListInitNode(&(msgQueue->listSend));
        MDS_MsgQueueListInit(
            msgQueue, bufSize / (msgQueue->msgSize + sizeof(MDS_MsgQueueHeader_t)));
    }

    return (err);
}

MDS_Err_t MDS_MsgQueueDeInit(MDS_MsgQueue_t *msgQueue)
{
    MDS_ASSERT(msgQueue != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(msgQueue->object)) == MDS_OBJECT_TYPE_MSGQUEUE);

    IPC_ListResumeAllThread(&(msgQueue->listRecv));
    IPC_ListResumeAllThread(&(msgQueue->listSend));

    return (MDS_ObjectDeInit(&(msgQueue->object)));
}

MDS_MsgQueue_t *MDS_MsgQueueCreate(const char *name, size_t msgSize, size_t msgNums)
{
    MDS_ASSERT(msgSize <= ((__typeof__(((MDS_MsgQueueHeader_t *)0)->len))(-1)));

    MDS_MsgQueue_t *msgQueue = (MDS_MsgQueue_t *)MDS_ObjectCreate(
        sizeof(MDS_MsgQueue_t), MDS_OBJECT_TYPE_MSGQUEUE, name);

    if (msgQueue != NULL) {
        msgQueue->msgSize =
            VALUE_ALIGN(msgSize + MDS_SYSMEM_ALIGN_SIZE - 1, MDS_SYSMEM_ALIGN_SIZE);
        msgQueue->queBuff =
            MDS_SysMemAlloc((msgQueue->msgSize + sizeof(MDS_MsgQueueHeader_t)) * msgNums);
        if (msgQueue->queBuff == NULL) {
            MDS_ObjectDestory(&(msgQueue->object));
            return (NULL);
        }
        MDS_ListInitNode(&(msgQueue->listRecv));
        MDS_ListInitNode(&(msgQueue->listSend));
        MDS_MsgQueueListInit(msgQueue, msgNums);
    }

    return (msgQueue);
}

MDS_Err_t MDS_MsgQueueDestroy(MDS_MsgQueue_t *msgQueue)
{
    MDS_ASSERT(msgQueue != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(msgQueue->object)) == MDS_OBJECT_TYPE_MSGQUEUE);

    IPC_ListResumeAllThread(&(msgQueue->listRecv));
    IPC_ListResumeAllThread(&(msgQueue->listSend));

    MDS_SysMemFree(msgQueue->queBuff);

    return (MDS_ObjectDestory(&(msgQueue->object)));
}

MDS_Err_t MDS_MsgQueueRecvAcquire(MDS_MsgQueue_t *msgQueue, void *recv, size_t *len,
                                  MDS_Timeout_t timeout)
{
    MDS_ASSERT(msgQueue != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(msgQueue->object)) == MDS_OBJECT_TYPE_MSGQUEUE);

    MDS_Err_t err = MDS_EOK;
    MDS_Thread_t *thread = MDS_KernelCurrentThread();

    MDS_HOOK_CALL(KERNEL, msgqueue,
                  (msgQueue, MDS_KERNEL_TRACE_MSGQUEUE_TRY_RECV, err, timeout));

    MDS_MsgQueueHeader_t *msg = NULL;
    MDS_Item_t lock = MDS_CoreInterruptLock();
    do {
        if (msgQueue->lhead == NULL) {
            if (timeout.ticks == MDS_CLOCK_TICK_NO_WAIT) {
                err = MDS_ETIMEOUT;
                break;
            }
            if (thread == NULL) {
                MDS_LOG_W("[msgqueue]thread is null try to recv msgqueue");
                err = MDS_EACCES;
                break;
            }

            MDS_LOG_D(
                "[msgqueue]msgqueue(%p) recv suspend thread(%p) entry:%p timer wait:%lu",
                msgQueue, thread, thread->entry, timeout.ticks);
        }

        while (msgQueue->lhead == NULL) {
            err = IPC_ListSuspendWait(&lock, &(msgQueue->listRecv), thread, timeout);
            if (err != MDS_EOK) {
                break;
            }
        }

        if (err == MDS_EOK) {
            msg = (MDS_MsgQueueHeader_t *)(msgQueue->lhead);
            msgQueue->lhead = (void *)(msg->next);
            if (msgQueue->lhead == NULL) {
                msgQueue->ltail = NULL;
            }
        }
    } while (0);
    MDS_CoreInterruptRestore(lock);

    MDS_HOOK_CALL(KERNEL, msgqueue,
                  (msgQueue, MDS_KERNEL_TRACE_MSGQUEUE_HAS_RECV, err, timeout));

    if (err == MDS_EOK) {
        if (recv != NULL) {
            *((uintptr_t *)recv) = (uintptr_t)(msg + 1);
        }
        if (len != NULL) {
            *len = msg->len;
        }

        MDS_LOG_D("[msgqueue]thread[(%p) recv message from msgqueue(%p) len:%u", thread,
                  msgQueue, msg->len);
    }

    return (err);
}

MDS_Err_t MDS_MsgQueueRecvRelease(MDS_MsgQueue_t *msgQueue, void *recv)
{
    MDS_ASSERT(msgQueue != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(msgQueue->object)) == MDS_OBJECT_TYPE_MSGQUEUE);

    if (recv == NULL) {
        return (MDS_EINVAL);
    }

    MDS_MsgQueueHeader_t *msg = ((MDS_MsgQueueHeader_t *)(*(uintptr_t *)recv)) - 1;
    if ((((uintptr_t)(msg) - (uintptr_t)(msgQueue->queBuff)) %
         (sizeof(MDS_MsgQueueHeader_t) + msgQueue->msgSize)) != 0) {
        return (MDS_EINVAL);
    }

    MDS_Item_t lock = MDS_CoreInterruptLock();

    msg->next = (MDS_MsgQueueHeader_t *)(msgQueue->lfree);
    msgQueue->lfree = (void *)msg;

    if (!MDS_ListIsEmpty(&(msgQueue->listSend))) {
        IPC_ListResumeThread(&(msgQueue->listSend));
        MDS_CoreInterruptRestore(lock);
        MDS_KernelSchedulerCheck();
    } else {
        MDS_CoreInterruptRestore(lock);
    }

    return (MDS_EOK);
}

MDS_Err_t MDS_MsgQueueRecvCopy(MDS_MsgQueue_t *msgQueue, void *buff, size_t size,
                               size_t *len, MDS_Timeout_t timeout)
{
    MDS_ASSERT(buff != NULL);
    MDS_ASSERT(size > 0);

    void *recv = NULL;
    size_t rlen = 0;

    MDS_Err_t err = MDS_MsgQueueRecvAcquire(msgQueue, &recv, &rlen, timeout);
    if (err == MDS_EOK) {
        MDS_MemBuffCopy(buff, size, recv, rlen);
        err = MDS_MsgQueueRecvRelease(msgQueue, &recv);
        if (len != NULL) {
            *len = rlen;
        }
    }

    return (err);
}

MDS_Err_t MDS_MsgQueueSendMsg(MDS_MsgQueue_t *msgQueue, const MDS_MsgList_t *msgList,
                              MDS_Timeout_t timeout)
{
    MDS_ASSERT(msgQueue != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(msgQueue->object)) == MDS_OBJECT_TYPE_MSGQUEUE);
    MDS_ASSERT(msgList != NULL);

    size_t len = MDS_MsgListGetLength(msgList);
    if ((len == 0) || (len > msgQueue->msgSize)) {
        return (MDS_EINVAL);
    }

    MDS_Err_t err = MDS_EOK;
    MDS_Thread_t *thread = MDS_KernelCurrentThread();

    MDS_HOOK_CALL(KERNEL, msgqueue,
                  (msgQueue, MDS_KERNEL_TRACE_MSGQUEUE_TRY_SEND, err, timeout));

    MDS_MsgQueueHeader_t *msg = NULL;
    MDS_Item_t lock = MDS_CoreInterruptLock();
    do {
        if (msgQueue->lfree == NULL) {
            if (timeout.ticks == MDS_CLOCK_TICK_NO_WAIT) {
                err = MDS_ERANGE;
                break;
            }
            if (thread == NULL) {
                MDS_LOG_W("[msgqueue]thread is null try to send msgqueue");
                err = MDS_EACCES;
                break;
            }

            MDS_LOG_D(
                "[msgqueue]msgqueue(%p) send suspend thread(%p) entry:%p timer wait:%lu",
                msgQueue, thread, thread->entry, timeout.ticks);
        }

        while (msgQueue->lfree == NULL) {
            err = IPC_ListSuspendWait(&lock, &(msgQueue->listSend), thread, timeout);
            if (err != MDS_EOK) {
                break;
            }
        }

        if (err == MDS_EOK) {
            msg = (MDS_MsgQueueHeader_t *)(msgQueue->lfree);
            msgQueue->lfree = (void *)(msg->next);
            msg->next = NULL;
        }
    } while (0);
    MDS_CoreInterruptRestore(lock);

    MDS_HOOK_CALL(KERNEL, msgqueue,
                  (msgQueue, MDS_KERNEL_TRACE_MSGQUEUE_HAS_SEND, err, timeout));

    if (err != MDS_EOK) {
        return ((err == MDS_ETIMEOUT) ? (MDS_ERANGE) : (err));
    }

    msg->len = len;
    MDS_MsgListCopyBuff(msg + 1, msgQueue->msgSize, msgList);

    MDS_LOG_D("[msgqueue]send message to msgqueue(%p) len:%u", msgQueue, len);

    lock = MDS_CoreInterruptLock();

    if (msgQueue->ltail != NULL) {
        ((MDS_MsgQueueHeader_t *)(msgQueue->ltail))->next = msg;
    }
    msgQueue->ltail = msg;
    if (msgQueue->lhead == NULL) {
        msgQueue->lhead = msg;
    }

    if (!MDS_ListIsEmpty(&(msgQueue->listRecv))) {
        IPC_ListResumeThread(&(msgQueue->listRecv));
        MDS_CoreInterruptRestore(lock);
        MDS_KernelSchedulerCheck();
    } else {
        MDS_CoreInterruptRestore(lock);
    }

    return (MDS_EOK);
}

MDS_Err_t MDS_MsgQueueSend(MDS_MsgQueue_t *msgQueue, const void *buff, size_t len,
                           MDS_Timeout_t timeout)
{
    MDS_ASSERT(buff != NULL);

    const MDS_MsgList_t msgList = {.buff = buff, .len = len, .next = NULL};

    return (MDS_MsgQueueSendMsg(msgQueue, &msgList, timeout));
}

MDS_Err_t MDS_MsgQueueUrgentMsg(MDS_MsgQueue_t *msgQueue, const MDS_MsgList_t *msgList)
{
    MDS_ASSERT(msgQueue != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(msgQueue->object)) == MDS_OBJECT_TYPE_MSGQUEUE);
    MDS_ASSERT(msgList != NULL);

    size_t len = MDS_MsgListGetLength(msgList);
    if ((len == 0) || (len > msgQueue->msgSize)) {
        return (MDS_EINVAL);
    }

    MDS_Err_t err = MDS_EOK;

    MDS_HOOK_CALL(
        KERNEL, msgqueue,
        (msgQueue, MDS_KERNEL_TRACE_MSGQUEUE_TRY_SEND, err, MDS_TIMEOUT_NO_WAIT));

    MDS_Item_t lock = MDS_CoreInterruptLock();

    MDS_MsgQueueHeader_t *msg = (MDS_MsgQueueHeader_t *)(msgQueue->lfree);
    if (msg == NULL) {
        err = MDS_ERANGE;
    } else {
        msgQueue->lfree = (void *)(msg->next);
    }

    MDS_CoreInterruptRestore(lock);

    MDS_HOOK_CALL(
        KERNEL, msgqueue,
        (msgQueue, MDS_KERNEL_TRACE_MSGQUEUE_HAS_SEND, err, MDS_TIMEOUT_NO_WAIT));

    if (err != MDS_EOK) {
        return (err);
    }

    msg->len = len;
    MDS_MsgListCopyBuff(msg + 1, msgQueue->msgSize, msgList);

    MDS_LOG_D("[msgqueue]send urgent message to msgqueue(%p) len:%u", msgQueue, len);

    lock = MDS_CoreInterruptLock();

    msg->next = (MDS_MsgQueueHeader_t *)(msgQueue->lhead);
    msgQueue->lhead = msg;
    if (msgQueue->ltail == NULL) {
        msgQueue->ltail = msg;
    }

    if (!MDS_ListIsEmpty(&(msgQueue->listRecv))) {
        IPC_ListResumeThread(&(msgQueue->listRecv));
        MDS_CoreInterruptRestore(lock);
        MDS_KernelSchedulerCheck();
    } else {
        MDS_CoreInterruptRestore(lock);
    }

    return (MDS_EOK);
}

MDS_Err_t MDS_MsgQueueUrgent(MDS_MsgQueue_t *msgQueue, const void *buff, size_t len)
{
    MDS_ASSERT(buff == NULL);

    const MDS_MsgList_t msgList = {.buff = buff, .len = len, .next = NULL};

    return (MDS_MsgQueueUrgentMsg(msgQueue, &msgList));
}

size_t MDS_MsgQueueGetMsgSize(const MDS_MsgQueue_t *msgQueue)
{
    MDS_ASSERT(msgQueue != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(msgQueue->object)) == MDS_OBJECT_TYPE_MSGQUEUE);

    return (msgQueue->msgSize);
}

size_t MDS_MsgQueueGetMsgCount(const MDS_MsgQueue_t *msgQueue)
{
    MDS_ASSERT(msgQueue != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(msgQueue->object)) == MDS_OBJECT_TYPE_MSGQUEUE);

    size_t cnt = 0;
    MDS_Item_t lock = MDS_CoreInterruptLock();
    MDS_MsgQueueHeader_t *msg = (MDS_MsgQueueHeader_t *)(msgQueue->lhead);
    for (; msg != NULL; msg = msg->next) {
        cnt += 1;
    }
    MDS_CoreInterruptRestore(lock);

    return (cnt);
}

size_t MDS_MsgQueueGetMsgFree(const MDS_MsgQueue_t *msgQueue)
{
    MDS_ASSERT(msgQueue != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(msgQueue->object)) == MDS_OBJECT_TYPE_MSGQUEUE);

    size_t cnt = 0;
    MDS_Item_t lock = MDS_CoreInterruptLock();
    MDS_MsgQueueHeader_t *msg = (MDS_MsgQueueHeader_t *)(msgQueue->lfree);
    for (; msg != NULL; msg = msg->next) {
        cnt += 1;
    }
    MDS_CoreInterruptRestore(lock);

    return (cnt);
}

/* MemPool --------------------------------------------------------------- */
union MDS_MemPoolHeader {
    union MDS_MemPoolHeader *next;
    MDS_MemPool_t *memPool;
};

static void MDS_MemPoolListInit(MDS_MemPool_t *memPool, size_t blkNums)
{
    memPool->lfree = memPool->memBuff;

    for (size_t idx = 0; idx < blkNums; idx++) {
        union MDS_MemPoolHeader *list = (union MDS_MemPoolHeader *)(&(
            ((uint8_t *)(memPool->memBuff))[idx * (sizeof(union MDS_MemPoolHeader) +
                                                   memPool->blkSize)]));
        list->next = (union MDS_MemPoolHeader *)(memPool->lfree);
        memPool->lfree = list;
    }
}

MDS_Err_t MDS_MemPoolInit(MDS_MemPool_t *memPool, const char *name, void *memBuff,
                          size_t bufSize, size_t blkSize)
{
    MDS_ASSERT(memPool != NULL);
    MDS_ASSERT(bufSize > (sizeof(union MDS_MemPoolHeader) + blkSize));

    MDS_Err_t err = MDS_ObjectInit(&(memPool->object), MDS_OBJECT_TYPE_MEMPOOL, name);
    if (err == MDS_EOK) {
        MDS_ListInitNode(&(memPool->list));
        memPool->memBuff = memBuff;
        memPool->blkSize =
            VALUE_ALIGN(blkSize + MDS_SYSMEM_ALIGN_SIZE - 1, MDS_SYSMEM_ALIGN_SIZE);
        MDS_MemPoolListInit(
            memPool, bufSize / (memPool->blkSize + sizeof(union MDS_MemPoolHeader)));
    }

    return (err);
}

MDS_Err_t MDS_MemPoolDeInit(MDS_MemPool_t *memPool)
{
    MDS_ASSERT(memPool != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(memPool->object)) == MDS_OBJECT_TYPE_MEMPOOL);

    IPC_ListResumeAllThread(&(memPool->list));

    return (MDS_ObjectDeInit(&(memPool->object)));
}

MDS_MemPool_t *MDS_MemPoolCreate(const char *name, size_t blkSize, size_t blkNums)
{
    MDS_MemPool_t *memPool = (MDS_MemPool_t *)MDS_ObjectCreate(
        sizeof(MDS_MemPool_t), MDS_OBJECT_TYPE_MEMPOOL, name);

    if (memPool != NULL) {
        memPool->blkSize =
            VALUE_ALIGN(blkSize + MDS_SYSMEM_ALIGN_SIZE - 1, MDS_SYSMEM_ALIGN_SIZE);
        memPool->memBuff =
            MDS_SysMemAlloc((memPool->blkSize + sizeof(MDS_MsgQueueHeader_t)) * blkNums);
        if (memPool->memBuff == NULL) {
            MDS_ObjectDestory(&(memPool->object));
            return (NULL);
        }
        MDS_ListInitNode(&(memPool->list));
        MDS_MemPoolListInit(memPool, blkNums);
    }

    return (memPool);
}

MDS_Err_t MDS_MemPoolDestroy(MDS_MemPool_t *memPool)
{
    MDS_ASSERT(memPool != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(memPool->object)) == MDS_OBJECT_TYPE_MEMPOOL);

    IPC_ListResumeAllThread(&(memPool->list));

    MDS_SysMemFree(memPool->memBuff);

    return (MDS_ObjectDestory(&(memPool->object)));
}

void *MDS_MemPoolAlloc(MDS_MemPool_t *memPool, MDS_Timeout_t timeout)
{
    MDS_ASSERT(memPool != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(memPool->object)) == MDS_OBJECT_TYPE_MEMPOOL);

    MDS_Err_t err = MDS_EOK;

    MDS_HOOK_CALL(KERNEL, mempool,
                  (memPool, MDS_KERNEL_TRACE_MEMPOOL_TRY_ALLOC, err, timeout, NULL));

    MDS_Thread_t *thread = MDS_KernelCurrentThread();

    union MDS_MemPoolHeader *blk = NULL;
    MDS_Item_t lock = MDS_CoreInterruptLock();
    do {
        if (memPool->lfree == NULL) {
            if (timeout.ticks == MDS_CLOCK_TICK_NO_WAIT) {
                err = MDS_ETIMEOUT;
                break;
            }
            if (thread == NULL) {
                MDS_LOG_W("[mempool]thread is null try alloc mempool");
                err = MDS_EACCES;
                break;
            }

            MDS_LOG_D(
                "[mempool]mempool(%p) alloc blocksize:%u suspend thread(%p) entry:%p timer wait:%lu",
                memPool, memPool->blkSize, thread, thread->entry, timeout.ticks);
        }

        while (memPool->lfree == NULL) {
            err = IPC_ListSuspendWait(&lock, &(memPool->list), thread, timeout);
            if (err != MDS_EOK) {
                break;
            }
        }

        if (err == MDS_EOK) {
            blk = (union MDS_MemPoolHeader *)(memPool->lfree);
            memPool->lfree = (void *)(blk->next);
            blk->memPool = memPool;
        }
    } while (0);
    MDS_CoreInterruptRestore(lock);

    MDS_HOOK_CALL(KERNEL, mempool,
                  (memPool, MDS_KERNEL_TRACE_MEMPOOL_HAS_ALLOC, err, timeout, blk));

    return (((err == MDS_EOK) && (blk != NULL)) ? ((void *)(blk + 1)) : (NULL));
}

static void MDS_MemPoolFreeBlk(union MDS_MemPoolHeader *blk)
{
    MDS_MemPool_t *memPool = blk->memPool;
    if (memPool == NULL) {
        return;
    }

    MDS_ASSERT(MDS_ObjectGetType(&(memPool->object)) == MDS_OBJECT_TYPE_MEMPOOL);

    MDS_Item_t lock = MDS_CoreInterruptLock();

    blk->next = memPool->lfree;
    memPool->lfree = blk;

    if (!MDS_ListIsEmpty(&(memPool->list))) {
        IPC_ListResumeThread(&(memPool->list));
        MDS_CoreInterruptRestore(lock);
        MDS_KernelSchedulerCheck();
    } else {
        MDS_CoreInterruptRestore(lock);
    }

    MDS_HOOK_CALL(
        KERNEL, mempool,
        (memPool, MDS_KERNEL_TRACE_MEMPOOL_HAS_FREE, MDS_EOK, MDS_TIMEOUT_NO_WAIT, blk));

    MDS_LOG_D("[mempool]free block to mempool(%p) which blksize:%u", memPool,
              memPool->blkSize);
}

void MDS_MemPoolFree(void *blkPtr)
{
    if (blkPtr == NULL) {
        return;
    }

    union MDS_MemPoolHeader *blk =
        (union MDS_MemPoolHeader *)((uint8_t *)blkPtr - sizeof(union MDS_MemPoolHeader));
    if (blk != NULL) {
        MDS_MemPoolFreeBlk(blk);
    }
}

size_t MDS_MemPoolGetBlkSize(const MDS_MemPool_t *memPool)
{
    MDS_ASSERT(memPool != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(memPool->object)) == MDS_OBJECT_TYPE_MEMPOOL);

    return (memPool->blkSize);
}

size_t MDS_MemPoolGetBlkFree(const MDS_MemPool_t *memPool)
{
    MDS_ASSERT(memPool != NULL);
    MDS_ASSERT(MDS_ObjectGetType(&(memPool->object)) == MDS_OBJECT_TYPE_MEMPOOL);

    size_t cnt = 0;
    MDS_Item_t lock = MDS_CoreInterruptLock();
    union MDS_MemPoolHeader *blk = (union MDS_MemPoolHeader *)(memPool->lfree);
    for (; (blk != NULL) || (blk != memPool->memBuff); blk = blk->next) {
        cnt += 1;
    }
    MDS_CoreInterruptRestore(lock);

    return (cnt);
}
