// SPDX-License-Identifier: GPL-3.0-only
#include "input_bindings.h"
#include "raylib.h"
#include <algorithm>
#include <cstring>
#include <mutex>

namespace openkai2::input {
namespace {
std::mutex bindingMutex;
Bindings activeBindings;

uint8_t axisByte(float value) { return uint8_t(std::clamp(128.0f + value * 127.0f, 0.0f, 255.0f)); }
bool keyHeld(int key) { return key != no_input && (IsKeyDown(key) || IsKeyPressed(key)); }
bool padHeld(int pad, int button) {
    return button != no_input && (IsGamepadButtonDown(pad, button) || IsGamepadButtonPressed(pad, button));
}
}

void set_active(const Bindings &bindings) {
    std::lock_guard lock(bindingMutex);
    activeBindings = valid(bindings) ? bindings : Bindings{};
}

Bindings active() {
    std::lock_guard lock(bindingMutex);
    return activeBindings;
}

bool sample(int port, uint8_t *data) {
    if (!data)
        return false;
    const auto bindings = active();
    std::memset(data, 0, 32);
    data[0] = 0x01;
    data[1] = 0x73;
    data[4] = data[5] = data[6] = data[7] = 0x80;
    uint16_t held = 0;
    if (IsGamepadAvailable(port)) {
        for (unsigned i = 0; i < button_count; ++i)
            if (padHeld(port, bindings.pad[i]))
                held |= pad_masks[i];
        data[6] = axisByte(GetGamepadAxisMovement(port, GAMEPAD_AXIS_LEFT_X));
        data[7] = axisByte(GetGamepadAxisMovement(port, GAMEPAD_AXIS_LEFT_Y));
        data[4] = axisByte(GetGamepadAxisMovement(port, GAMEPAD_AXIS_RIGHT_X));
        data[5] = axisByte(GetGamepadAxisMovement(port, GAMEPAD_AXIS_RIGHT_Y));
    }
    if (port == 0)
        for (unsigned i = 0; i < button_count; ++i)
            if (keyHeld(bindings.keys[i]) || keyHeld(bindings.alternate[i]))
                held |= pad_masks[i];
    const uint16_t buttons = uint16_t(~held);
    data[2] = uint8_t(buttons & 0xFFu);
    data[3] = uint8_t(buttons >> 8);
    return true;
}
}
