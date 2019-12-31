#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#include "cmdqueue.h"
#include "util.h"

#define Q_LOCK(q)       PTHREAD_CHK(pthread_mutex_lock(&handle->queues[q].mutex))
#define Q_UNLOCK(q)     PTHREAD_CHK(pthread_mutex_unlock(&handle->queues[q].mutex))
#define Q_WAIT(q)       PTHREAD_CHK(pthread_cond_wait(&handle->queues[q].cond, &handle->queues[q].mutex))
#define Q_BROADCAST(q)  PTHREAD_CHK(pthread_cond_broadcast(&handle->queues[q].cond))

typedef enum {
    CMDQUEUE_ASYNC = 0x0,
    CMDQUEUE_SYNC  = 0x1,
} Mode;

typedef enum {
    CMDQUEUE_PRIO_LOW  = 0x0,
    CMDQUEUE_PRIO_HIGH = 0x1,
} Prio;

typedef enum {
    CMD_FREE = 0,
    CMD_TODO,
    CMD_DONE,
} QueueType;

typedef struct {
    struct list_tag head_prio;  // list of prio commands
    struct list_tag head;       // list of command
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} Queue;

struct CmdQueue_ {
    Queue queues[3];        // CMD_FREE, CMD_TODO, CMD_DONE
    const char* name;       // no ownership
    pthread_t tid;
    int32_t stop;
    void* cookie;
    void (*cmd_callback)(void* cookie, Cmd*  cmd);
    void* cmdlist;
};

Cmd* cmdqueue_getcmd_sync(CmdQueue* handle)
{
    Cmd* cmd = cmdqueue_getcmd_async(handle);
    if (!cmd) {
        Q_LOCK(CMD_FREE);

        while (!list_count(&handle->queues[CMD_FREE].head)) {
            Q_WAIT(CMD_FREE);
        }

        list_t node = handle->queues[CMD_FREE].head.next;
        list_remove(node);
        cmd = (Cmd*)node;

        Q_UNLOCK(CMD_FREE);
    }
    return cmd;
}

Cmd* cmdqueue_getcmd_async(CmdQueue* handle)
{
    Cmd* cmd = NULL;
    Q_LOCK(CMD_FREE);

    if (list_count(&handle->queues[CMD_FREE].head)) {
        list_t node = handle->queues[CMD_FREE].head.next;
        list_remove(node);
        cmd = (Cmd*)node;
    }

    Q_UNLOCK(CMD_FREE);
    return cmd;
}

static void cmdqueue_schedule_cmd(CmdQueue* handle, Cmd* cmd, uint32_t sync, int32_t prio)
{
    list_t list = (prio == CMDQUEUE_PRIO_LOW) ? &handle->queues[CMD_TODO].head : &handle->queues[CMD_TODO].head_prio;
    cmd->type = sync;
    Q_LOCK(CMD_TODO);
    list_add_tail(list, &cmd->head);
    Q_BROADCAST(CMD_TODO);
    Q_UNLOCK(CMD_TODO);
}

static int32_t cmd_finished(list_t const src, const Cmd* cmd)
{
#if 0
    uint64_t count = 0;
    int32_t done = 0;
    list_t node = src->next;

    while (node != src) {
        count++;
        node = node->next;
    }

    if (count > 0) {
        node = src->next;

        while (node != src && !done) {
            done = (Cmd* )node == cmd;
            node = node->next;
        }
    }

    return done;
#else
    // BB: smaller version
    list_t node = src->next;
    while (node != src) {
        if (node == &cmd->head) return 1;
        node = node->next;
    }
    return 0;
#endif
}

static void cmdqueue_wait_cmd(CmdQueue* handle, Cmd* cmd)
{
    Q_LOCK(CMD_DONE);

    while (!cmd_finished(&handle->queues[CMD_DONE].head, cmd) ) {
        Q_WAIT(CMD_DONE);
    }

    list_remove(&cmd->head);
    Q_UNLOCK(CMD_DONE);

    Q_LOCK(CMD_FREE);
    list_add_front(&handle->queues[CMD_FREE].head, &cmd->head);
    Q_BROADCAST(CMD_FREE);
    Q_UNLOCK(CMD_FREE);
}

void cmdqueue_sync_cmd(CmdQueue* handle, Cmd* cmd)
{
    cmdqueue_schedule_cmd(handle, cmd, CMDQUEUE_SYNC, CMDQUEUE_PRIO_LOW);
    cmdqueue_wait_cmd(handle, cmd);
}

void cmdqueue_sync_highprio_cmd(CmdQueue* handle, Cmd* cmd)
{
    cmdqueue_schedule_cmd(handle, cmd, CMDQUEUE_SYNC, CMDQUEUE_PRIO_HIGH);
    cmdqueue_wait_cmd(handle, cmd);
}

void cmdqueue_async_cmd(CmdQueue* handle, Cmd* cmd)
{
    cmdqueue_schedule_cmd(handle, cmd, CMDQUEUE_ASYNC, CMDQUEUE_PRIO_LOW);
}

static void* thread_func(void* arg)
{
    CmdQueue* handle= (CmdQueue*)arg;
    Cmd dummy_cmd;

    while (1) {
        Cmd* cmd = &dummy_cmd;

        Q_LOCK(CMD_TODO);

        // TODO BB dont count, just check not-empty
        //while (!list_count(&handle->queues[CMD_TODO].head) &&
        //       !list_count(&handle->queues[CMD_TODO].head_prio) &&
        while (list_empty(&handle->queues[CMD_TODO].head) &&
               list_empty(&handle->queues[CMD_TODO].head_prio) &&
               !handle->stop) {
            Q_WAIT(CMD_TODO);
        }

        if (!handle->stop) {
            if (!list_empty(&handle->queues[CMD_TODO].head_prio)) {
                // TODO BB to_container
                cmd = (Cmd*)handle->queues[CMD_TODO].head_prio.next;
                list_remove(&cmd->head);
            } else {
                // TODO BB to_container
                cmd = (Cmd*)handle->queues[CMD_TODO].head.next;
                list_remove(&cmd->head);
            }
        }

        Q_UNLOCK(CMD_TODO);

        if (handle->stop) break;

        handle->cmd_callback(handle->cookie, cmd);

        QueueType type = (cmd->type == CMDQUEUE_SYNC) ? CMD_DONE : CMD_FREE;

        Q_LOCK(type);
        list_add_tail(&handle->queues[type].head, &cmd->head);
        Q_BROADCAST(type);
        Q_UNLOCK(type);
    }

    return 0;
}

CmdQueue* cmdqueue_create(const char* name,
                          void (*cmd_callback)(void* cookie, Cmd* cmd),
                          void* cookie,
                          uint32_t num_commands,
                          uint32_t size_cmd)
{
    CmdQueue* handle = calloc(1, sizeof(CmdQueue));
    assert(handle);

    for (uint32_t i=0; i<ARRAY_SIZE(handle->queues); i++) {
        list_init(&handle->queues[i].head_prio);
        list_init(&handle->queues[i].head);
        PTHREAD_CHK(pthread_mutex_init(&handle->queues[i].mutex, 0));
        PTHREAD_CHK(pthread_cond_init(&handle->queues[i].cond, 0));
    }

    handle->name = name;
    handle->stop = 0;
    handle->cookie = cookie;
    handle->cmd_callback = cmd_callback;
    handle->cmdlist = malloc(num_commands*size_cmd);

    // NOTE: could use index instead of pointers, just add to circular buffer? (no linked lists)
    uint8_t* iter = (uint8_t*)handle->cmdlist;
    for (uint32_t i=0; i<num_commands; i++) {
        Cmd* cmd = (Cmd*)iter;
        list_add_tail(&handle->queues[CMD_FREE].head, &cmd->head);
        iter += size_cmd;
    }

    PTHREAD_CHK(pthread_create(&handle->tid, 0, thread_func, handle));
    return handle;
}

void cmdqueue_destroy(CmdQueue* handle)
{
    Q_LOCK(CMD_TODO);
    handle->stop = 1;
    Q_BROADCAST(CMD_TODO);
    Q_UNLOCK(CMD_TODO);

    PTHREAD_CHK(pthread_join(handle->tid, 0));

    for (uint32_t i=0; i<ARRAY_SIZE(handle->queues); i++) {
        PTHREAD_CHK(pthread_mutex_destroy(&handle->queues[i].mutex));
        PTHREAD_CHK(pthread_cond_destroy(&handle->queues[i].cond));
    }

    free(handle->cmdlist);
    free(handle);
}

/* only for async commands on the non prio head */
void cmdqueue_flush(CmdQueue* handle,
                    void (*flush_callback)(void* cookie, Cmd* cmd, uint32_t* count),
                    void* cookie,
                    uint32_t* count)
{
    Q_LOCK(CMD_TODO);
    Q_LOCK(CMD_FREE);

    list_t src = &handle->queues[CMD_TODO].head;
    list_t dest = &handle->queues[CMD_FREE].head;

    list_t node = src->next;
    while (node != src) {
        list_t tmp_node = node;
        node = node->next;
        list_remove(tmp_node);
        // TODO BB use to_container
        if (flush_callback) flush_callback(cookie, (Cmd*)tmp_node, count);
        list_add_tail(dest, tmp_node);
    }

    Q_UNLOCK(CMD_FREE);
    Q_UNLOCK(CMD_TODO);
}

