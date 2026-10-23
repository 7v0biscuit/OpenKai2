// SPDX-License-Identifier: GPL-3.0-only
#include "host_input.h"
#include <stdexcept>

namespace openkai2 {
void verify_host_input() {
    auto require=[](bool ok) {if(!ok)throw std::runtime_error("Runtime input regression: lost or repeated button press");};
    auto down=[](const LatchedPadState::Packet& p,uint16_t mask) {
        return ((uint16_t(p[2])|(uint16_t(p[3])<<8))&mask)==0;
    };
    LatchedPadState pad;
    auto released=LatchedPadState::neutral(),cross=released,start=released;
    cross[3]&=~0x40u;start[2]&=~0x08u;
    // A full tap occurs between two guest reads, as at the save confirmation
    // when rendering is slow. The following read must see it exactly once.
    pad.publish(cross);pad.publish(released);
    require(down(pad.consume(),0x4000u));
    require(!down(pad.consume(),0x4000u));
    // Held input stays down on repeated reads and releases without a tail.
    pad.publish(cross);require(down(pad.consume(),0x4000u));
    require(down(pad.consume(),0x4000u));pad.publish(released);
    require(!down(pad.consume(),0x4000u));
    // Independent short presses survive several host polls; newest sticks
    // remain live. Reset clears pending input between runtime sessions.
    pad.publish(cross);pad.publish(released);pad.publish(start);
    released[6]=211u;pad.publish(released);
    const auto both=pad.consume();
    require(down(both,0x4000u)&&down(both,8u)&&both[6]==211u);
    require(!down(pad.consume(),8u));
    pad.publish(cross);pad.reset();require(pad.consume()==LatchedPadState::neutral());
    LatchedPadState second;pad.publish(cross);
    require(second.consume()==LatchedPadState::neutral());
}
}
