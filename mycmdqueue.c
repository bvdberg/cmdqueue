#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include "cmdqueue.h"
#include "mycmdqueue.h"

struct myCmdQueue_ {
    CmdQueue* cmdqueue;
    void *cookie;
    int32_t (*cmd_callback)(void *cookie, int32_t callback_type, void *data, int32_t len);
};

static void mycmd_callback(void *cookie, Cmd *cmd)
{
    MyCmd* mycmd = (MyCmd*)cmd;
    myCmdQueue handle = (myCmdQueue)cookie;

    switch (mycmd->type) {
    case INIT:
        break;
    case DEINIT:
        break;
    case START:
        if (handle->cmd_callback) handle->cmd_callback(handle->cookie, START_CALLED, 0, 0);
        break;
    case STOP:
        if (handle->cmd_callback) handle->cmd_callback(handle->cookie, STOP_CALLED, 0, 0);
        break;
    }
}

void mycmdqueue_init(myCmdQueue* handle,
                     void *cookie,
                     int32_t (*mycmdqueue_callback)(void *cookie, int32_t callback_type, void *data, int32_t len))
{
    *handle = malloc(sizeof(struct myCmdQueue_));
    assert(*handle);

    const int32_t num_commands = 1;
    myCmdQueue phandle = *handle;

    phandle->cmdqueue = cmdqueue_create("mycmdqueue", mycmd_callback, phandle, num_commands, sizeof(MyCmd));

    MyCmd* cmd = (MyCmd*)cmdqueue_getcmd_sync(phandle->cmdqueue);
    cmd->type = INIT;
    cmdqueue_sync_cmd(phandle->cmdqueue, &cmd->cmd);

    phandle->cookie = cookie;
    phandle->cmd_callback = mycmdqueue_callback;
}

void mycmdqueue_deinit(myCmdQueue handle)
{
    MyCmd* cmd = (MyCmd*)cmdqueue_getcmd_sync(handle->cmdqueue);
    cmd->type = DEINIT;
    cmdqueue_sync_cmd(handle->cmdqueue, &cmd->cmd);

    cmdqueue_destroy(handle->cmdqueue);
    free(handle);
}

void mycmdqueue_start(myCmdQueue handle)
{
    MyCmd* cmd = (MyCmd*)cmdqueue_getcmd_sync(handle->cmdqueue);
    cmd->type = START;
    cmdqueue_sync_cmd(handle->cmdqueue, &cmd->cmd);
}

void mycmdqueue_stop(myCmdQueue handle)
{
    MyCmd* cmd = (MyCmd*)cmdqueue_getcmd_sync(handle->cmdqueue);
    cmd->type = STOP;
    cmdqueue_sync_cmd(handle->cmdqueue, &cmd->cmd);
}

