#pragma once
#include <stddef.h>
#include <stdint.h>
#include <optional>
#include <CommonState.h>
#include <Command.h>
#include <ConfigEngine.h>
#include <EventTypes.h>
#include <HardwareStatus.h>
#include "Ds18b20Codec.h"
#include "HwConfig.h"
#include "OneWireBus.h"
#include "SensorAddress.h"
#include "SensorDebounce.h"

// DS18B20 bus service (D16-D18): resolves the per-logical-sensor Text mapping
// settings, keeps them in sync with ConfigEngine (invalid/duplicate
// clearing), runs the non-blocking 1-Wire bus cycle (request/read every
// CYCLE_MS, conversion wait CONVERSION_MS, D17), feeds each assigned
// sensor's SensorDebounce and evaluates the missing-sensor alarm (D18).
// Pure logic -- HwRuntime drives tick()/fillStatus() and owns the
// OneWireBus adapter.
class SensorService {
public:
    static constexpr uint32_t CYCLE_MS = 2000;
    static constexpr uint32_t CONVERSION_MS = 800;  // >= 750 ms for 12-bit resolution

    SensorService(OneWireBus& bus, ConfigEngine& config, EventSink& events);

    // Resolves every settingKey via config.indexOf (false if any is missing
    // or not Text), loads the mapping (validation + duplicate clearing,
    // D16), resets the debouncers to Unknown and runs the boot scan.
    bool begin(const LogicalSensorDesc* sensors, size_t count, uint64_t nowMs);

    // ~1 s tick: rescan (if requested) -> mapping sync (poll settings) -> bus
    // cycle (D17) -> missing/alarm evaluation.
    void tick(uint64_t nowMs);

    CommandStatus assign(uint8_t logical, const uint8_t address[8], EventReason origin, uint64_t nowMs);
    CommandStatus clear(uint8_t logical, EventReason origin, uint64_t nowMs);
    void requestRescan();  // executed at the start of the next tick()

    size_t count() const { return _count; }
    SensorState state(uint8_t logical) const;           // Unassigned if not mapped
    std::optional<float> value(uint8_t logical) const;  // empty unless Ok
    bool missing(uint8_t logical) const;

    // Completed bus-read cycles (D17's request+read pair), counted even when
    // readCount == 0 (no devices/sensors). Not "healthy sensors" -- it is the
    // OTA rollback health check's liveness input (stage 04): it only proves the
    // service keeps cycling, not that any sensor is Ok.
    uint32_t readCycleCount() const { return _readCycles; }

    // Writes CommonState.sensors, .oneWire, and bits 24..31 of .alarms.activeMask
    // (other bits untouched).
    void fillStatus(CommonState& state) const;

private:
    struct Sensor {
        int settingIndex = -1;
        char cachedText[SENSOR_ADDRESS_TEXT_LEN + 1] = {};
        bool assigned = false;
        uint8_t address[8] = {};
        SensorDebounce debounce;
        bool missingScan = false;    // absent from the latest scan
        bool goodSinceScan = false;  // a good reading arrived since that scan
        bool missing = false;        // computed (D18)
        bool alarmRaised = false;
    };

    static constexpr size_t MAX_READ_SET = ONE_WIRE_MAX_DEVICES + MAX_LOGICAL_SENSORS;

    void syncMapping(uint64_t nowMs, EventReason reason);
    void doScan(uint64_t nowMs);
    void busCycle(uint64_t nowMs);
    void evaluateMissing(uint64_t nowMs);
    void recomputeMissingScan(size_t logical);
    void refreshScanLogical();
    void addUniqueAddress(uint8_t addrs[][8], size_t& n, const uint8_t addr[8]) const;

    OneWireBus& _bus;
    ConfigEngine& _config;
    EventSink& _events;

    Sensor _sensors[MAX_LOGICAL_SENSORS];
    size_t _count = 0;

    uint8_t _scanAddr[ONE_WIRE_MAX_DEVICES][8] = {};
    size_t _scanDeviceCount = 0;
    bool _scanOverflow = false;
    bool _scanDone = false;
    uint32_t _scanRunCount = 0;
    uint8_t _scanLogical[ONE_WIRE_MAX_DEVICES] = {};
    float _scanTempC[ONE_WIRE_MAX_DEVICES] = {};
    bool _scanTempValid[ONE_WIRE_MAX_DEVICES] = {};

    bool _pendingRead = false;
    bool _everRequested = false;
    uint64_t _requestMs = 0;
    uint64_t _lastRequestMs = 0;
    bool _rescanRequested = false;
    uint32_t _readCycles = 0;
};
