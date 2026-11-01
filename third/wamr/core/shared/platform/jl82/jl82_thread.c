/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * JieLi AC79xx (pi32v2) threads/locks, modeled on the WAMR zephyr port.
 * The MicroPixel mini host runs WAMR single-threaded (no thread-mgr, no
 * lib-pthread), so the cond/join paths only need to be correct enough for
 * wasm_blocking_op.c to compile and for the runtime's own static locks.
 */

#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

typedef struct os_thread_wait_node {
    korp_sem sem;
    os_thread_wait_list next;
} os_thread_wait_node;

typedef struct os_thread_data {
    struct os_thread_data *next;
    korp_tid tid;
} os_thread_data;

static bool is_thread_sys_inited = false;

static os_thread_data supervisor_thread_data;
static korp_mutex thread_data_lock;
static os_thread_data *thread_data_list = NULL;

static void
thread_data_list_add(os_thread_data *thread_data)
{
    os_mutex_lock(&thread_data_lock);
    if (!thread_data_list)
        thread_data_list = thread_data;
    else {
        os_thread_data *p = thread_data_list;
        while (p) {
            if (p == thread_data) {
                os_mutex_unlock(&thread_data_lock);
                return;
            }
            p = p->next;
        }
        thread_data->next = thread_data_list;
        thread_data_list = thread_data;
    }
    os_mutex_unlock(&thread_data_lock);
}

static void
thread_data_list_remove(os_thread_data *thread_data)
{
    os_mutex_lock(&thread_data_lock);
    if (thread_data_list) {
        if (thread_data_list == thread_data)
            thread_data_list = thread_data_list->next;
        else {
            os_thread_data *p = thread_data_list;
            while (p && p->next != thread_data)
                p = p->next;
            if (p && p->next == thread_data)
                p->next = p->next->next;
        }
    }
    os_mutex_unlock(&thread_data_lock);
}

int
os_thread_sys_init()
{
    if (is_thread_sys_inited)
        return BHT_OK;

    if (os_mutex_create(&thread_data_lock) != 0)
        return BHT_ERROR;

    memset(&supervisor_thread_data, 0, sizeof(supervisor_thread_data));
    supervisor_thread_data.tid = os_current_task();
    thread_data_list = &supervisor_thread_data;

    is_thread_sys_inited = true;
    return BHT_OK;
}

void
os_thread_sys_destroy(void)
{
    if (is_thread_sys_inited) {
        is_thread_sys_inited = false;
    }
}

int
os_thread_env_init(void)
{
    /* Runtime-created or developer-created thread; no TLS to set up here. */
    return 0;
}

bool
os_thread_env_inited(void)
{
    return true;
}

void
os_thread_env_destroy(void)
{
}

int
os_thread_signal_init(void)
{
    return BHT_OK;
}

void
os_thread_signal_destroy(void)
{
}

bool
os_thread_signal_inited(void)
{
    return true;
}

/* ---- mutex ---- */

int
os_mutex_init(korp_mutex *mutex)
{
    if (os_mutex_create(mutex) != 0)
        return BHT_ERROR;
    return BHT_OK;
}

int
os_recursive_mutex_init(korp_mutex *mutex)
{
    return os_mutex_init(mutex);
}

int
os_mutex_destroy(korp_mutex *mutex)
{
    os_mutex_del(mutex, 0);
    return BHT_OK;
}

int
os_mutex_lock(korp_mutex *mutex)
{
    /* timeout 0 = wait forever in the JieLi API */
    if (os_mutex_pend(mutex, 0) != 0)
        return BHT_ERROR;
    return BHT_OK;
}

int
os_mutex_unlock(korp_mutex *mutex)
{
    os_mutex_post(mutex);
    return BHT_OK;
}

/* ---- cond (wait-list of binary semaphores, zephyr style) ---- */

int
os_cond_init(korp_cond *cond)
{
    if (os_mutex_init(&cond->wait_list_lock) != BHT_OK)
        return BHT_ERROR;
    cond->thread_wait_list = NULL;
    return BHT_OK;
}

int
os_cond_destroy(korp_cond *cond)
{
    (void)cond;
    return BHT_OK;
}

static int
os_cond_wait_internal(korp_cond *cond, korp_mutex *mutex, bool timed,
                      uint64 useconds)
{
    os_thread_wait_node *node;

    if (!(node = BH_MALLOC(sizeof(os_thread_wait_node))))
        return BHT_ERROR;

    if (os_sem_create(&node->sem, 0) != 0) {
        BH_FREE(node);
        return BHT_ERROR;
    }
    node->next = NULL;

    os_mutex_lock(&cond->wait_list_lock);
    if (!cond->thread_wait_list)
        cond->thread_wait_list = node;
    else {
        os_thread_wait_node *p = cond->thread_wait_list;
        while (p->next)
            p = p->next;
        p->next = node;
    }
    os_mutex_unlock(&cond->wait_list_lock);

    os_mutex_unlock(mutex);

    if (timed) {
        /* 10 ms tick; pend timeout 0 means forever, so clamp to 1 tick. */
        int ticks = (int)(useconds / 10000);
        if (ticks < 1)
            ticks = 1;
        if (os_sem_pend(&node->sem, ticks) != 0) {
            os_mutex_lock(mutex);
            os_mutex_lock(&cond->wait_list_lock);
            if (cond->thread_wait_list == node)
                cond->thread_wait_list = node->next;
            else {
                os_thread_wait_node *p = cond->thread_wait_list;
                while (p && p->next != node)
                    p = p->next;
                if (p && p->next == node)
                    p->next = node->next;
            }
            os_mutex_unlock(&cond->wait_list_lock);
            BH_FREE(node);
            return BHT_TIMED_OUT;
        }
    }
    else {
        os_sem_pend(&node->sem, 0);
    }

    os_mutex_lock(mutex);

    os_mutex_lock(&cond->wait_list_lock);
    if (cond->thread_wait_list == node)
        cond->thread_wait_list = node->next;
    else {
        os_thread_wait_node *p = cond->thread_wait_list;
        while (p && p->next != node)
            p = p->next;
        if (p && p->next == node)
            p->next = node->next;
    }
    os_mutex_unlock(&cond->wait_list_lock);
    BH_FREE(node);

    return BHT_OK;
}

int
os_cond_wait(korp_cond *cond, korp_mutex *mutex)
{
    return os_cond_wait_internal(cond, mutex, false, 0);
}

int
os_cond_reltimedwait(korp_cond *cond, korp_mutex *mutex, uint64 useconds)
{
    if (useconds == BHT_WAIT_FOREVER)
        return os_cond_wait_internal(cond, mutex, false, 0);
    return os_cond_wait_internal(cond, mutex, true, useconds);
}

int
os_cond_signal(korp_cond *cond)
{
    os_mutex_lock(&cond->wait_list_lock);
    if (cond->thread_wait_list)
        os_sem_post(&cond->thread_wait_list->sem);
    os_mutex_unlock(&cond->wait_list_lock);
    return BHT_OK;
}

int
os_cond_broadcast(korp_cond *cond)
{
    os_thread_wait_node *node;
    os_mutex_lock(&cond->wait_list_lock);
    node = cond->thread_wait_list;
    while (node) {
        os_sem_post(&node->sem);
        node = node->next;
    }
    os_mutex_unlock(&cond->wait_list_lock);
    return BHT_OK;
}

/* ---- threads ---- */

int
os_thread_create_with_prio(korp_tid *p_tid, thread_start_routine_t start,
                           void *arg, unsigned int stack_size, int prio)
{
    if (!p_tid || !stack_size)
        return BHT_ERROR;

    /* thread_fork stack size is counted in 4-byte words. */
    if (stack_size < 8 * BH_KB)
        stack_size = 8 * BH_KB;
    if (thread_fork("wasm", prio, (int)(stack_size / 4), 0, NULL,
                    (void (*)(void *))start, arg) != 0)
        return BHT_ERROR;

    /* os_current_task() inside the new thread is the stable identity;
       the creator cannot know it yet, so hand back a placeholder that the
       (single-threaded) runtime never dereferences. */
    *p_tid = NULL;
    return BHT_OK;
}

int
os_thread_create(korp_tid *p_tid, thread_start_routine_t start, void *arg,
                 unsigned int stack_size)
{
    return os_thread_create_with_prio(p_tid, start, arg, stack_size,
                                      BH_THREAD_DEFAULT_PRIORITY);
}

korp_tid
os_self_thread()
{
    return os_current_task();
}

int
os_thread_join(korp_tid thread, void **value_ptr)
{
    (void)thread;
    (void)value_ptr;
    /* Single-threaded runtime: nothing to join. */
    return BHT_OK;
}

int
os_thread_detach(korp_tid thread)
{
    (void)thread;
    return BHT_OK;
}

void
os_thread_exit(void *retval)
{
    (void)retval;
    /* thread_fork'ed JieLi tasks must never return; park forever. */
    for (;;)
        os_time_dly(100);
}

uint8 *
os_thread_get_stack_boundary()
{
    return NULL;
}

void
os_thread_jit_write_protect_np(bool enabled)
{
    (void)enabled;
}

/* ---- rwlock (unused without lib-pthread; degenerate impls) ---- */

int
os_rwlock_init(korp_rwlock *lock)
{
    if (!lock)
        return BHT_ERROR;
    if (os_mutex_init(&lock->mtx) != BHT_OK)
        return BHT_ERROR;
    lock->read_count = 0;
    return BHT_OK;
}

int
os_rwlock_rdlock(korp_rwlock *lock)
{
    return os_mutex_lock(&lock->mtx);
}

int
os_rwlock_wrlock(korp_rwlock *lock)
{
    return os_mutex_lock(&lock->mtx);
}

int
os_rwlock_unlock(korp_rwlock *lock)
{
    return os_mutex_unlock(&lock->mtx);
}

int
os_rwlock_destroy(korp_rwlock *lock)
{
    return os_mutex_destroy(&lock->mtx);
}

/* ---- named semaphores ----
 * os_sem_open/close/wait/trywait/getvalue/unlink are only referenced by
 * lib-pthread-semaphore builds (disabled here). os_sem_post is intentionally
 * NOT defined in this file: the JieLi closed lib (os_api.c) exports
 * os_sem_post(OS_SEM*) and our korp_sem is OS_SEM, so the real one resolves
 * the WAMR references directly. */

/* ---- blocking-op support (stubbed; no cross-thread wakeup here) ---- */

int
os_blocking_op_init()
{
    return BHT_OK;
}

void
os_begin_blocking_op()
{
}

void
os_end_blocking_op()
{
}

int
os_wakeup_blocking_op(korp_tid tid)
{
    (void)tid;
    return BHT_ERROR;
}
