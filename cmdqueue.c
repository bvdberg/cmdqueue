#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#include "cmdqueue.h"
#include "util.h"

typedef enum {
    CMDQUEUE_ASYNC = 0x0,
    CMDQUEUE_SYNC  = 0x1,
} Mode;

typedef enum {
    CMDQUEUE_PRIO_LOW  = 0x0,
    CMDQUEUE_PRIO_HIGH = 0x1,
} Prio;


typedef enum {
    CMDFREE = 0,
    CMDTODO,
    CMDDONE,
} QueueType;

typedef struct {
    struct list_tag head_prio;  // list of prio commands
    struct list_tag head;       // list of command
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} Queue;

struct CmdQueue_ {
    Queue queues[3];        // QueueType: FREE, TODO, DONE
    const char* name;       // no ownership
    pthread_t tid;
    int32_t stop;
    void* cookie;
    void (*cmd_callback)(void* cookie, Cmd*  cmd);
    void* cmdlist;
};

static void cmdqueue_wait_getcmd(CmdQueue* handle, Cmd** cmd)
{
    PTHREAD_CHK(pthread_mutex_lock(&handle->queues[CMDFREE].mutex));

    while (!list_count(&handle->queues[CMDFREE].head)) {
        PTHREAD_CHK(pthread_cond_wait(&handle->queues[CMDFREE].cond,
                                      &handle->queues[CMDFREE].mutex));
    }

    list_t node = handle->queues[CMDFREE].head.next;
    list_remove(node);
    *cmd = (Cmd*)node;

    PTHREAD_CHK(pthread_mutex_unlock(&handle->queues[CMDFREE].mutex));
}

void cmdqueue_getcmd_sync(CmdQueue* handle, Cmd** cmd)
{
    cmdqueue_getcmd_async(handle, cmd);
    if (!*cmd) cmdqueue_wait_getcmd(handle, cmd);
}

void cmdqueue_getcmd_async(CmdQueue* handle, Cmd** cmd)
{
    *cmd = NULL;

    PTHREAD_CHK(pthread_mutex_lock(&handle->queues[CMDFREE].mutex));

    if (list_count(&handle->queues[CMDFREE].head)) {
        list_t node = handle->queues[CMDFREE].head.next;
        list_remove(node);
        *cmd = (Cmd*)node;
    }

    PTHREAD_CHK(pthread_mutex_unlock(&handle->queues[CMDFREE].mutex));
}

static void cmdqueue_schedule_cmd(CmdQueue* handle, Cmd* cmd, uint32_t sync, int32_t prio)
{
    list_t list = (prio == CMDQUEUE_PRIO_LOW) ? &handle->queues[CMDTODO].head : &handle->queues[CMDTODO].head_prio;
    cmd->type = sync;
    PTHREAD_CHK(pthread_mutex_lock(&handle->queues[CMDTODO].mutex));
    list_add_tail(list, &cmd->head);
    PTHREAD_CHK(pthread_cond_broadcast(&handle->queues[CMDTODO].cond));
    PTHREAD_CHK(pthread_mutex_unlock(&handle->queues[CMDTODO].mutex));
}

static int32_t cmd_finished(list_t const src, Cmd* cmd)
{
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
}

static void cmdqueue_wait_cmd(CmdQueue* handle, Cmd* cmd)
{

    PTHREAD_CHK(pthread_mutex_lock(&handle->queues[CMDDONE].mutex));

    while (!cmd_finished(&handle->queues[CMDDONE].head, cmd) ) {
        PTHREAD_CHK(pthread_cond_wait(&handle->queues[CMDDONE].cond, &handle->queues[CMDDONE].mutex));
    }

    list_remove(&cmd->head);

    PTHREAD_CHK(pthread_mutex_unlock(&handle->queues[CMDDONE].mutex));

    PTHREAD_CHK(pthread_mutex_lock(&handle->queues[CMDFREE].mutex));
    list_add_front(&handle->queues[CMDFREE].head, &cmd->head);
    PTHREAD_CHK(pthread_cond_broadcast(&handle->queues[CMDFREE].cond));
    PTHREAD_CHK(pthread_mutex_unlock(&handle->queues[CMDFREE].mutex));
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

        PTHREAD_CHK(pthread_mutex_lock(&handle->queues[CMDTODO].mutex));

        while (!list_count(&handle->queues[CMDTODO].head) && !list_count(&handle->queues[CMDTODO].head_prio) && !handle->stop) {
            PTHREAD_CHK(pthread_cond_wait(&handle->queues[CMDTODO].cond, &handle->queues[CMDTODO].mutex));
        }

        if (!handle->stop) {
            if (list_count(&handle->queues[CMDTODO].head_prio)) {
                cmd = (Cmd*)handle->queues[CMDTODO].head_prio.next;
                list_remove(&cmd->head);
            } else {
                cmd = (Cmd*)handle->queues[CMDTODO].head.next;
                list_remove(&cmd->head);
            }
        }

        PTHREAD_CHK(pthread_mutex_unlock(&handle->queues[CMDTODO].mutex));

        if (handle->stop) break;

        handle->cmd_callback(handle->cookie, cmd);

        QueueType type = cmd->type ? CMDDONE : CMDFREE;

        PTHREAD_CHK(pthread_mutex_lock(&handle->queues[type].mutex));
        list_add_tail(&handle->queues[type].head, &cmd->head);
        PTHREAD_CHK(pthread_cond_broadcast(&handle->queues[type].cond));
        PTHREAD_CHK(pthread_mutex_unlock(&handle->queues[type].mutex));
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
    handle->tid = 0;
    handle->stop = 0;
    handle->cookie = cookie;
    handle->cmd_callback = cmd_callback;
    handle->cmdlist = malloc(num_commands*size_cmd);

    uint8_t* iter = (uint8_t*)handle->cmdlist;
    for (uint32_t i=0; i<num_commands; i++) {
        Cmd* cmd = (Cmd*)iter;
        list_add_tail(&handle->queues[CMDFREE].head, &cmd->head);
        iter += size_cmd;
    }

    PTHREAD_CHK(pthread_create(&handle->tid, 0, thread_func, handle));
    return handle;
}

void cmdqueue_destroy(CmdQueue* handle)
{
    PTHREAD_CHK(pthread_mutex_lock(&handle->queues[CMDTODO].mutex));
    handle->stop = 1;
    PTHREAD_CHK(pthread_cond_broadcast(&handle->queues[CMDTODO].cond));
    PTHREAD_CHK(pthread_mutex_unlock(&handle->queues[CMDTODO].mutex));

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
    PTHREAD_CHK(pthread_mutex_lock(&handle->queues[CMDTODO].mutex));
    PTHREAD_CHK(pthread_mutex_lock(&handle->queues[CMDFREE].mutex));

    list_t src = &handle->queues[CMDTODO].head;
    list_t dest = &handle->queues[CMDFREE].head;
    list_t node = src->next;

    while (node != src) {
        list_t const tmp_node = node;
        node = node->next;
        list_remove(tmp_node);
        flush_callback(cookie, (Cmd*)tmp_node, count);
        list_add_tail(dest, tmp_node);
    }

    PTHREAD_CHK(pthread_mutex_unlock(&handle->queues[CMDFREE].mutex));
    PTHREAD_CHK(pthread_mutex_unlock(&handle->queues[CMDTODO].mutex));
}

