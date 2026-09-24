#pragma once
#include <stddef.h>
#include <stdint.h>

// One persisted event-log entry. Naturally aligned, 24 bytes; this is the exact
// on-disk (NVS blob) layout, so the field order and types must not change without
// a migration plan.
constexpr uint8_t EVENT_FLAG_REAL_TIME = 0x01;

struct EventEntry {
    uint32_t seq;
    uint32_t timestamp;
    uint16_t type;
    uint16_t source;
    float value;
    float aux;
    uint8_t reason;
    uint8_t flags;
    uint16_t crc;
};
static_assert(sizeof(EventEntry) == 24, "EventEntry must be 24 bytes (persisted layout)");

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout.
// crc16Ccitt("123456789", 9) == 0x29B1.
uint16_t crc16Ccitt(const uint8_t* data, size_t len);

// Computes the CRC over the first offsetof(EventEntry, crc) bytes and stores it in e.crc.
void sealEventEntry(EventEntry& e);

// True if seq != 0 and the stored crc matches the recomputed crc.
bool isEventEntryValid(const EventEntry& e);
