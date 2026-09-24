#include "EventEntry.h"

uint16_t crc16Ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

void sealEventEntry(EventEntry& e) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&e);
    e.crc = crc16Ccitt(bytes, offsetof(EventEntry, crc));
}

bool isEventEntryValid(const EventEntry& e) {
    if (e.seq == 0) {
        return false;
    }
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&e);
    return crc16Ccitt(bytes, offsetof(EventEntry, crc)) == e.crc;
}
