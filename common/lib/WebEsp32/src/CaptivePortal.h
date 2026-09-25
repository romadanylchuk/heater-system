#pragma once
#include <DNSServer.h>
#include <atomic>
#include <CommonState.h>

// Captive-portal DNS for the setup AP (D12). update() runs on the loop task
// every slow tick and starts DNSServer (port 53, every name -> the AP IP) on
// the rising edge of apActive and stops it on the falling edge; nothing runs
// in STA mode. process() (fast tick) polls one pending query, non-blocking.
//
// active()/apIp() are read by the web server's final catch-all handler on the
// AsyncTCP task: the AP IP is copied into _apIp BEFORE _active is published
// (release), and readers load _active with acquire, so a reader that sees
// active() == true also sees the complete IP text. _apIp is only rewritten
// after _active was cleared.
class CaptivePortal {
public:
    static constexpr uint16_t DNS_PORT = 53;

    void update(bool apActive, const char* apIp);   // loop task
    void process();                                 // loop task
    bool active() const { return _active.load(std::memory_order_acquire); }
    const char* apIp() const { return _apIp; }       // valid while active()

private:
    DNSServer _dns;
    bool _started = false;                           // loop task only
    std::atomic<bool> _active{false};
    char _apIp[NET_IP_TEXT_LEN + 1] = {};
};
