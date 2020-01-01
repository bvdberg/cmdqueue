#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "cmdqueue.h"

typedef enum {
    Kind_Init,
    Kind_Work,
    Kind_Done,
    Kind_Fast,
} Kind;

static const char* kind2str(Kind k) {
    switch (k) {
    case Kind_Init: return "INIT";
    case Kind_Work: return "WORK";
    case Kind_Done: return "DONE";
    case Kind_Fast: return "FAST";
    }
    return "?";
}

typedef struct {
    Cmd cmd;
    Kind kind;
    uint32_t idx;
} MyCmd;

static void callback(void* arg, Cmd* c) {
    MyCmd* cmd = to_container(MyCmd, cmd, c);
    printf("CB %s  %u\n", kind2str(cmd->kind), cmd->idx);
    switch (cmd->kind) {
    case Kind_Init:
        usleep(1000);
        break;
    case Kind_Work:
        usleep(10000);
        break;
    case Kind_Done:
        break;
    case Kind_Fast:
        usleep(5000);
        break;
    }
}

static void schedule_sync(CmdQueue* queue, Kind k, uint32_t idx) {
    MyCmd* cmd = (MyCmd*)cmdqueue_getcmd_sync(queue);
    cmd->kind = k;
    cmd->idx = idx;
    cmdqueue_sync_cmd(queue, (Cmd*)cmd);
}

static void schedule_async(CmdQueue* queue, Kind k, uint32_t idx) {
    MyCmd* cmd = (MyCmd*)cmdqueue_getcmd_sync(queue);
    cmd->kind = k;
    cmd->idx = idx;
    cmdqueue_async_cmd(queue, (Cmd*)cmd);
}

static void schedule_hi(CmdQueue* queue, Kind k, uint32_t idx) {
    MyCmd* cmd = (MyCmd*)cmdqueue_getcmd_sync(queue);
    cmd->kind = k;
    cmd->idx = idx;
    cmdqueue_sync_highprio_cmd(queue, (Cmd*)cmd);
}

int main() {
    CmdQueue* queue = cmdqueue_create("myqueue", callback, NULL, 16, sizeof(MyCmd));

    schedule_sync(queue, Kind_Init, 1);
    schedule_sync(queue, Kind_Work, 2);
    schedule_sync(queue, Kind_Work, 3);
    schedule_async(queue, Kind_Work, 4);
    schedule_async(queue, Kind_Work, 5);
    schedule_hi(queue, Kind_Fast, 6);       // should pass async above
    schedule_async(queue, Kind_Work, 7);
    schedule_sync(queue, Kind_Work, 8);
    schedule_sync(queue, Kind_Work, 9);
    schedule_async(queue, Kind_Work, 10);
    schedule_async(queue, Kind_Work, 11);
    printf("done scheduling\n");

    schedule_sync(queue, Kind_Done, 12);
    printf("done\n");

    cmdqueue_destroy(queue);
    return 0;
}

