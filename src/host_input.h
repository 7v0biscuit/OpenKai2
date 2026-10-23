// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <mutex>

namespace openkai2 {
// Host input is sampled on the window thread. Remember presses until the EE
// asks for a packet, including a press released between two slow game frames.
class LatchedPadState {
public:
    using Packet=std::array<uint8_t,32>;
    static Packet neutral() {
        Packet value{};value[0]=1;value[1]=0x73;
        value[2]=value[3]=255;
        value[4]=value[5]=value[6]=value[7]=128;
        return value;
    }
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        current_=neutral();pending_=0;
    }
    void publish(const Packet& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_|=uint16_t(buttons(current_)&~buttons(value));
        current_=value;
    }
    Packet consume() {
        std::lock_guard<std::mutex> lock(mutex_);
        Packet result=current_;
        const auto held=uint16_t(buttons(result)&~pending_);
        result[2]=uint8_t(held);result[3]=uint8_t(held>>8);
        pending_=0;
        return result;
    }
private:
    static uint16_t buttons(const Packet& value) {
        return uint16_t(value[2])|(uint16_t(value[3])<<8);
    }
    std::mutex mutex_;
    Packet current_=neutral();
    uint16_t pending_=0;
};
void reset_host_input();
void poll_host_input();
void verify_host_input();
}
