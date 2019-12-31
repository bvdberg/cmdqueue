#ifndef MYCMDQUEUE_H
#define MYCMDQUEUE_H

#include <stdint.h>

#include "cmdqueue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INIT,
    DEINIT,
    START,
    STOP,
} MyCmdType;

typedef enum {
    START_CALLED,
    STOP_CALLED,
} MyCallbackType;

typedef struct {
    Cmd cmd;    // must be first
    uint32_t type;
    union {
        struct {
            int32_t dontcare;
        } mycmd_start;

        struct {
            int32_t dontcare;
        } mycmd_stop;
    } mycmd;
} MyCmd;

typedef struct myCmdQueue_* myCmdQueue;

void mycmdqueue_init(myCmdQueue* handle,
                     void *cookie,
                     int32_t (*mycmdqueue_callback)(void *cookie,
                                                    int32_t callback_type,
                                                    void *data,
                                                    int32_t len));
void mycmdqueue_deinit(myCmdQueue handle);
void mycmdqueue_start(myCmdQueue handle);
void mycmdqueue_stop(myCmdQueue handle);

#ifdef __cplusplus
}
#endif

#endif
