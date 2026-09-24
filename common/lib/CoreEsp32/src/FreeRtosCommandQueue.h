#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Command.h"

// CommandQueue implementation over a FreeRTOS xQueue holding fixed-size Command
// PODs (D13). Non-blocking post/receive (0 ticks to wait). The constructor
// touches no hardware/RTOS objects; begin() creates the queue.
class FreeRtosCommandQueue : public CommandQueue {
public:
    static constexpr size_t DEPTH = 16;

    bool begin();   // xQueueCreate(DEPTH, sizeof(Command)); false on allocation failure

    bool post(const Command& cmd) override;
    bool tryReceive(Command& out) override;

private:
    QueueHandle_t _queue = nullptr;
};
