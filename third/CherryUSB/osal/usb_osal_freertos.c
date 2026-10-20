/*
 * Copyright (c) 2022, sakumisu
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* JL 头最先: JL 的 bool typedef 需先于 stdbool 落地 */
#include "typedef.h"
#include "system/timer.h"
extern int cpu_in_irq(void);   /* JL: 判断是否中断上下文 */   /* sys_timer_add/del: JL 未开 FreeRTOS 软定时器(configUSE_TIMERS=0) */
#include "usb_osal.h"
#include "usb_errno.h"
#include "usb_config.h"
#include "usb_log.h"
#include <FreeRTOS.h>
#include "semphr.h"
#include "event_groups.h"

usb_osal_thread_t usb_osal_thread_create(const char *name, uint32_t stack_size, uint32_t prio, usb_thread_entry_t entry, void *args)
{
    TaskHandle_t htask = NULL;
    stack_size /= sizeof(StackType_t);
    xTaskCreate(entry, name, stack_size, args, configMAX_PRIORITIES - 1 - prio, &htask);
    if (htask == NULL) {
        return NULL;
    }
    return (usb_osal_thread_t)htask;
}

void usb_osal_thread_delete(usb_osal_thread_t thread)
{
    vTaskDelete(thread);
}

void usb_osal_thread_schedule_other(void)
{
    TaskHandle_t xCurrentTask = xTaskGetCurrentTaskHandle();
    const UBaseType_t old_priority = uxTaskPriorityGet(xCurrentTask);

    vTaskPrioritySet(xCurrentTask, tskIDLE_PRIORITY);

    taskYIELD();

    vTaskPrioritySet(xCurrentTask, old_priority);
}

usb_osal_sem_t usb_osal_sem_create(uint32_t initial_count)
{
    return (usb_osal_sem_t)xSemaphoreCreateCounting(1, initial_count);
}

usb_osal_sem_t usb_osal_sem_create_counting(uint32_t max_count)
{
    return (usb_osal_sem_t)xSemaphoreCreateCounting(max_count, 0);
}

void usb_osal_sem_delete(usb_osal_sem_t sem)
{
    vSemaphoreDelete((SemaphoreHandle_t)sem);
}

int usb_osal_sem_take(usb_osal_sem_t sem, uint32_t timeout)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    int ret;

    if (cpu_in_irq()) {
        /* ISR context: only non-blocking take is allowed */
        ret = xSemaphoreTakeFromISR((SemaphoreHandle_t)sem, &xHigherPriorityTaskWoken);
        if (ret == pdPASS) {
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    } else if (timeout == USB_OSAL_WAITING_FOREVER) {
        ret = xSemaphoreTake((SemaphoreHandle_t)sem, portMAX_DELAY);
    } else {
        ret = xSemaphoreTake((SemaphoreHandle_t)sem, pdMS_TO_TICKS(timeout));
    }

    return (ret == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
}

int usb_osal_sem_give(usb_osal_sem_t sem)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    int ret;

    if (cpu_in_irq()) {
        ret = xSemaphoreGiveFromISR((SemaphoreHandle_t)sem, &xHigherPriorityTaskWoken);
        if (ret == pdPASS) {
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    } else {
        ret = xSemaphoreGive((SemaphoreHandle_t)sem);
    }

    return (ret == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
}

void usb_osal_sem_reset(usb_osal_sem_t sem)
{
    xQueueReset((QueueHandle_t)sem);
}

usb_osal_mutex_t usb_osal_mutex_create(void)
{
    return (usb_osal_mutex_t)xSemaphoreCreateMutex();
}

void usb_osal_mutex_delete(usb_osal_mutex_t mutex)
{
    vSemaphoreDelete((SemaphoreHandle_t)mutex);
}

int usb_osal_mutex_take(usb_osal_mutex_t mutex)
{
    return (xSemaphoreTake((SemaphoreHandle_t)mutex, portMAX_DELAY) == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
}

int usb_osal_mutex_give(usb_osal_mutex_t mutex)
{
    return (xSemaphoreGive((SemaphoreHandle_t)mutex) == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
}

usb_osal_mq_t usb_osal_mq_create(uint32_t max_msgs)
{
    return (usb_osal_mq_t)xQueueCreate(max_msgs, sizeof(uintptr_t));
}

void usb_osal_mq_delete(usb_osal_mq_t mq)
{
    vQueueDelete((QueueHandle_t)mq);
}

int usb_osal_mq_send(usb_osal_mq_t mq, uintptr_t addr)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    int ret;

    if (cpu_in_irq()) {
        ret = xQueueSendFromISR((usb_osal_mq_t)mq, &addr, &xHigherPriorityTaskWoken);
        if (ret == pdPASS) {
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
    } else {
        ret = xQueueSend((usb_osal_mq_t)mq, &addr, 0xffffffff);
    }

    return (ret == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
}

int usb_osal_mq_recv(usb_osal_mq_t mq, uintptr_t *addr, uint32_t timeout)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    int ret;

    if (cpu_in_irq()) {
        ret = xQueueReceiveFromISR((usb_osal_mq_t)mq, addr, &xHigherPriorityTaskWoken);
        if (ret == pdPASS) {
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
        return (ret == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
    } else {
        if (timeout == USB_OSAL_WAITING_FOREVER) {
            return (xQueueReceive((usb_osal_mq_t)mq, addr, portMAX_DELAY) == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
        } else {
            return (xQueueReceive((usb_osal_mq_t)mq, addr, pdMS_TO_TICKS(timeout)) == pdPASS) ? 0 : -USB_ERR_TIMEOUT;
        }
    }
}

struct usb_osal_timer_jl {
    u16 id;
    usb_timer_handler_t handler;
    void *argument;
};

static void __usb_timeout(void *priv)
{
    struct usb_osal_timer_jl *timer = (struct usb_osal_timer_jl *)priv;

    timer->handler(timer->argument);
}

struct usb_osal_timer *usb_osal_timer_create(const char *name, uint32_t timeout_ms, usb_timer_handler_t handler, void *argument, bool is_period)
{
    struct usb_osal_timer_jl *timer;
    (void)name;
    (void)is_period; /* JL sys_timer 为周期定时器, 恒定周期 */

    timer = usb_osal_malloc(sizeof(struct usb_osal_timer_jl));
    if (timer == NULL) {
        return NULL;
    }
    memset(timer, 0, sizeof(struct usb_osal_timer_jl));

    timer->handler = handler;
    timer->argument = argument;
    timer->id = sys_timer_add(argument, handler, timeout_ms);
    if (timer->id == 0) {
        usb_osal_free(timer);
        return NULL;
    }
    return (struct usb_osal_timer *)timer;
}

void usb_osal_timer_delete(struct usb_osal_timer *timer)
{
    struct usb_osal_timer_jl *t = (struct usb_osal_timer_jl *)timer;

    sys_timer_del(t->id);
    usb_osal_free(t);
}

void usb_osal_timer_start(struct usb_osal_timer *timer)
{
    /* JL sys_timer 创建即运行 */
    (void)timer;
}

void usb_osal_timer_stop(struct usb_osal_timer *timer)
{
    (void)timer;
}

size_t usb_osal_enter_critical_section(void)
{
    size_t ret;

    if (cpu_in_irq()) {
        ret = taskENTER_CRITICAL_FROM_ISR();
    } else {
        taskENTER_CRITICAL();
        ret = 1;
    }

    return ret;
}

void usb_osal_leave_critical_section(size_t flag)
{
    if (cpu_in_irq()) {
        taskEXIT_CRITICAL_FROM_ISR(flag);
    } else {
        taskEXIT_CRITICAL();
    }
}

void usb_osal_msleep(uint32_t delay)
{
    vTaskDelay(pdMS_TO_TICKS(delay));
}

void *usb_osal_malloc(size_t size)
{
    return pvPortMalloc(size);
}

void usb_osal_free(void *ptr)
{
    vPortFree(ptr);
}