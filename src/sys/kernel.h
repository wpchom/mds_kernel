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
#ifndef __MDS_KERNEL_H__
#define __MDS_KERNEL_H__

/* Include ----------------------------------------------------------------- */
#include "mds_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Core -------------------------------------------------------------------- */
void *MDS_CoreThreadStackInit(void *stackBase, size_t stackSize, void *entry, void *arg,
                              void *exit);
void MDS_CoreSchedulerStartup(void *toSP);
void MDS_CoreSchedulerSwitch(void *from, void *to);
bool MDS_CoreThreadStackCheck(void *sp);

/* Scheduler --------------------------------------------------------------- */
void MDS_SchedulerInit(void);
void MDS_SchedulerInsertThread(MDS_Thread_t *thread);
void MDS_SchedulerRemoveThread(MDS_Thread_t *thread);
MDS_Thread_t *MDS_SchedulerHighestPriorityThread(void);

/* Kernel ------------------------------------------------------------------ */
void MDS_KernelSchedulerCheck(void);
void MDS_KernelPushDefunct(MDS_Thread_t *thread);
MDS_Thread_t *MDS_KernelPopDefunct(void);
void MDS_KernelRemainThread(void);
MDS_Thread_t *MDS_KernelIdleThread(void);
void MDS_IdleThreadInit(void);

/* Timer ------------------------------------------------------------------- */
void MDS_SysTimerInit(void);
void MDS_SysTimerCheck(void);
MDS_Tick_t MDS_SysTimerNextTick(void);

#ifdef __cplusplus
}
#endif

#endif /* __MDS_KERNEL_H__ */
