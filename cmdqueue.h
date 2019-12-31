#ifndef CMDQUEUE_H
#define CMDQUEUE_H

#include <stdint.h>
#include "pthread.h"

#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    struct list_tag head;
    uint32_t type;
} Cmd;

typedef struct CmdQueue_ CmdQueue;

CmdQueue* cmdqueue_create(const char* name,
                          void (*cmd_callback)(void* cookie, Cmd* cmd),
                          void* cookie,
                          uint32_t num_commands,
                          uint32_t size_cmd);

void cmdqueue_destroy(CmdQueue* handle);

void cmdqueue_flush(CmdQueue* handle,
                    void (*flush_callback)(void* cookie, Cmd* cmd, uint32_t* count),
                    void* cookie,
                    uint32_t* count);

void cmdqueue_getcmd_sync(CmdQueue* handle, Cmd** cmd);

void cmdqueue_getcmd_async(CmdQueue* handle, Cmd** cmd);

void cmdqueue_sync_cmd(CmdQueue* handle, Cmd* cmd);

void cmdqueue_sync_highprio_cmd(CmdQueue* handle, Cmd* cmd);

void cmdqueue_async_cmd(CmdQueue* handle, Cmd* cmd);

#ifdef __cplusplus
}
#endif

#endif

