#ifndef _TUSB_OSAL_CMSIS_OS2_H_
#define _TUSB_OSAL_CMSIS_OS2_H_

#include <stdint.h>
#include <stdbool.h>
#include "cmsis_os2.h"

#ifndef OSAL_TIMEOUT_WAIT_FOREVER
  #define OSAL_TIMEOUT_WAIT_FOREVER   0xFFFFFFFFUL
#endif

#ifndef OSAL_TIMEOUT_CONTROL_XFER
  #define OSAL_TIMEOUT_CONTROL_XFER   5000
#endif

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------+
// Time conversion
//--------------------------------------------------------------------+
// CMSIS-RTOS2 timeouts are expressed in kernel ticks, not milliseconds.
// Convert ms -> ticks (rounded up, so a non-zero ms never becomes a 0-tick poll).
// ИЗМЕНЕНО: новая функция, раньше msec передавался в API как есть.
TU_ATTR_ALWAYS_INLINE static inline uint32_t osal_ms2ticks(uint32_t msec) {
  if (msec == OSAL_TIMEOUT_WAIT_FOREVER) {
    return osWaitForever;
  }

  uint32_t freq = osKernelGetTickFreq();
  if (freq == 0U || freq == 1000U) {
    return msec;
  }

  uint64_t ticks = ((uint64_t) msec * freq + 999U) / 1000U;
  if (ticks >= osWaitForever) {
    ticks = osWaitForever - 1U; // never turn a finite timeout into "forever"
  }
  return (uint32_t) ticks;
}

//--------------------------------------------------------------------+
// TASK API
//--------------------------------------------------------------------+
TU_ATTR_ALWAYS_INLINE static inline void osal_task_delay(uint32_t msec) {
  osDelay(osal_ms2ticks(msec));
}

//--------------------------------------------------------------------+
// Spinlock API (stub, same as RTX4 reference)
//--------------------------------------------------------------------+
typedef uint8_t osal_spinlock_t;

#define OSAL_SPINLOCK_DEF(_name, _int_set) \
  osal_spinlock_t _name

TU_ATTR_ALWAYS_INLINE static inline void osal_spin_init(osal_spinlock_t *ctx) {
  (void) ctx;
}

TU_ATTR_ALWAYS_INLINE static inline void osal_spin_lock(osal_spinlock_t *ctx, bool in_isr) {
  (void) ctx; (void) in_isr;
}

TU_ATTR_ALWAYS_INLINE static inline void osal_spin_unlock(osal_spinlock_t *ctx, bool in_isr) {
  (void) ctx; (void) in_isr;
}

//--------------------------------------------------------------------+
// SEMAPHORE API (binary)
//--------------------------------------------------------------------+
typedef osSemaphoreId_t   osal_semaphore_t;
typedef osSemaphoreAttr_t osal_semaphore_def_t;

TU_ATTR_ALWAYS_INLINE static inline osal_semaphore_t osal_semaphore_create(osal_semaphore_def_t* semdef) {
  return osSemaphoreNew(1, 0, semdef); // returns NULL on failure (e.g. out of dynamic memory)
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_semaphore_delete(osal_semaphore_t sem_hdl) {
  return osSemaphoreDelete(sem_hdl) == osOK;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_semaphore_post(osal_semaphore_t sem_hdl, bool in_isr) {
  (void) in_isr; // osSemaphoreRelease() is ISR-safe in CMSIS-RTOS2
  return osSemaphoreRelease(sem_hdl) == osOK;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_semaphore_wait(osal_semaphore_t sem_hdl, uint32_t msec) {
  return osSemaphoreAcquire(sem_hdl, osal_ms2ticks(msec)) == osOK;
}

TU_ATTR_ALWAYS_INLINE static inline void osal_semaphore_reset(osal_semaphore_t const sem_hdl) {
  while (osSemaphoreAcquire(sem_hdl, 0) == osOK) {}
}

//--------------------------------------------------------------------+
// MUTEX API (priority inheritance)
//--------------------------------------------------------------------+
typedef osMutexId_t   osal_mutex_t;
typedef osMutexAttr_t osal_mutex_def_t;

TU_ATTR_ALWAYS_INLINE static inline osal_mutex_t osal_mutex_create(osal_mutex_def_t* mdef) {
  // ИЗМЕНЕНО: добавлен osMutexPrioInherit, раньше мьютекс был без наследования приоритетов.
  osMutexAttr_t attr = {0};
  if (mdef != NULL) {
    attr = *mdef; // keep user-supplied name / static control block, if any
  }
  attr.attr_bits |= osMutexPrioInherit;
  return osMutexNew(&attr);
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_mutex_delete(osal_mutex_t mutex_hdl) {
  return osMutexDelete(mutex_hdl) == osOK;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_mutex_lock(osal_mutex_t mutex_hdl, uint32_t msec) {
  return osMutexAcquire(mutex_hdl, osal_ms2ticks(msec)) == osOK;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_mutex_unlock(osal_mutex_t mutex_hdl) {
  return osMutexRelease(mutex_hdl) == osOK;
}

//--------------------------------------------------------------------+
// QUEUE API
//--------------------------------------------------------------------+
typedef struct {
  uint16_t depth;
  uint16_t item_sz;
} osal_queue_def_t;

typedef osMessageQueueId_t osal_queue_t;

#define OSAL_QUEUE_DEF(_int_set, _name, _depth, _type) \
  osal_queue_def_t _name = { .depth = _depth, .item_sz = sizeof(_type) }

TU_ATTR_ALWAYS_INLINE static inline osal_queue_t osal_queue_create(osal_queue_def_t* qdef) {
  // ИЗМЕНЕНО: убран неиспользуемый static queue_attr, атрибуты теперь локальные.
  const osMessageQueueAttr_t attr = { .name = "TinyUSB_Queue" };
  return osMessageQueueNew(qdef->depth, qdef->item_sz, &attr); // NULL on failure
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_receive(osal_queue_t qhdl, void* data, uint32_t msec) {
  return osMessageQueueGet(qhdl, data, NULL, osal_ms2ticks(msec)) == osOK;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_send(osal_queue_t qhdl, void const* data, bool in_isr) {
  (void) in_isr; // timeout must be 0 when called from ISR; also avoids self-deadlock
                 // if the usbd task itself posts into a full queue
  return osMessageQueuePut(qhdl, data, 0, 0) == osOK;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_empty(osal_queue_t qhdl) {
  return osMessageQueueGetCount(qhdl) == 0;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_delete(osal_queue_t qhdl) {
  return osMessageQueueDelete(qhdl) == osOK;
}

#ifdef __cplusplus
}
#endif

#endif // _TUSB_OSAL_CMSIS_OS2_H_