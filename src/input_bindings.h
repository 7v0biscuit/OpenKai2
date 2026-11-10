// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace openkai2::input {
inline constexpr unsigned button_count = 16;
inline constexpr int no_input = 0;

enum Button : unsigned {
    Up, Down, Left, Right, Cross, Circle, Square, Triangle, L1, R1, L2, R2, Start, Select, L3, R3
};

inline constexpr std::array<uint16_t, button_count> pad_masks{
    0x0010u, 0x0040u, 0x0080u, 0x0020u, 0x4000u, 0x2000u, 0x8000u, 0x1000u,
    0x0400u, 0x0800u, 0x0100u, 0x0200u, 0x0008u, 0x0001u, 0x0002u, 0x0004u};

inline constexpr std::array<const char *, button_count> button_names{
    "Up", "Down", "Left", "Right", "Cross", "Circle", "Square", "Triangle",
    "L1", "R1", "L2", "R2", "Start", "Select", "L3", "R3"};

inline constexpr std::array<int, button_count> default_keys{
    87, 83, 65, 68, 88, 67, 90, 86, 81, 69, 340, 344, 257, 258, no_input, no_input};
inline constexpr std::array<int, button_count> default_alternate_keys{
    265, 264, 263, 262, 32, 256, 320, 321, no_input, no_input, no_input, no_input, no_input, no_input, no_input, no_input};
inline constexpr std::array<int, button_count> default_pad_buttons{
    1, 3, 4, 2, 7, 6, 8, 5, 9, 11, 10, 12, 15, 13, 16, 17};

inline constexpr int pad_button_last = 17;

struct Bindings {
    std::array<int, button_count> keys = default_keys;
    std::array<int, button_count> alternate = default_alternate_keys;
    std::array<int, button_count> pad = default_pad_buttons;
    bool operator==(const Bindings &other) const {
        return keys == other.keys && alternate == other.alternate && pad == other.pad;
    }
};

constexpr bool reserved_key(int key) { return (key >= 290 && key <= 301) || key == 341 || key == 345; }

constexpr bool valid_key(int key) {
    if (key == no_input)
        return true;
    if (reserved_key(key))
        return false;
    return key == 32 || key == 39 || (key >= 44 && key <= 57) || key == 59 || key == 61 || (key >= 65 && key <= 93) ||
           key == 96 || (key >= 256 && key <= 269) || key == 280 || (key >= 320 && key <= 336) || key == 340 ||
           key == 342 || key == 344 || key == 346;
}

constexpr bool valid_pad_button(int button) { return button >= no_input && button <= pad_button_last && button != 14; }

constexpr bool valid(const Bindings &bindings) {
    for (unsigned i = 0; i < button_count; ++i)
        if (!valid_key(bindings.keys[i]) || !valid_key(bindings.alternate[i]) || !valid_pad_button(bindings.pad[i]))
            return false;
    return true;
}

inline std::string key_name(int key) {
    if (key == no_input)
        return "None";
    if ((key >= 65 && key <= 90) || (key >= 48 && key <= 57))
        return std::string(1, char(key));
    if (key >= 320 && key <= 329)
        return "Num " + std::to_string(key - 320);
    switch (key) {
    case 32: return "Space";
    case 39: return "'";
    case 44: return ",";
    case 45: return "-";
    case 46: return ".";
    case 47: return "/";
    case 59: return ";";
    case 61: return "=";
    case 91: return "[";
    case 92: return "\\";
    case 93: return "]";
    case 96: return "`";
    case 256: return "Esc";
    case 257: return "Enter";
    case 258: return "Tab";
    case 259: return "Backspace";
    case 260: return "Insert";
    case 261: return "Delete";
    case 262: return "Right";
    case 263: return "Left";
    case 264: return "Down";
    case 265: return "Up";
    case 266: return "Page Up";
    case 267: return "Page Down";
    case 268: return "Home";
    case 269: return "End";
    case 280: return "Caps Lock";
    case 330: return "Num .";
    case 331: return "Num /";
    case 332: return "Num *";
    case 333: return "Num -";
    case 334: return "Num +";
    case 335: return "Num Enter";
    case 336: return "Num =";
    case 340: return "Left Shift";
    case 342: return "Left Alt";
    case 344: return "Right Shift";
    case 346: return "Right Alt";
    default: return "Key " + std::to_string(key);
    }
}

inline constexpr std::array<const char *, pad_button_last + 1> pad_button_names{
    "None", "D-pad Up", "D-pad Right", "D-pad Down", "D-pad Left",
    "Y / Triangle", "B / Circle", "A / Cross", "X / Square",
    "LB / L1", "LT / L2", "RB / R1", "RT / R2",
    "Back / Select", "Guide", "Start", "Left stick press", "Right stick press"};

void set_active(const Bindings &bindings);
Bindings active();
bool sample(int port, uint8_t *data);

constexpr int key_from_virtual(unsigned vk, bool extended) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
        return int(vk);
    if (vk >= 0x60 && vk <= 0x69)
        return 320 + int(vk - 0x60);
    switch (vk) {
    case 0x20: return 32;
    case 0xDE: return 39;
    case 0xBC: return 44;
    case 0xBD: return 45;
    case 0xBE: return 46;
    case 0xBF: return 47;
    case 0xBA: return 59;
    case 0xBB: return 61;
    case 0xDB: return 91;
    case 0xDC: return 92;
    case 0xDD: return 93;
    case 0xC0: return 96;
    case 0x1B: return 256;
    case 0x0D: return extended ? 335 : 257;
    case 0x09: return 258;
    case 0x08: return 259;
    case 0x2D: return 260;
    case 0x2E: return 261;
    case 0x27: return 262;
    case 0x25: return 263;
    case 0x28: return 264;
    case 0x26: return 265;
    case 0x21: return 266;
    case 0x22: return 267;
    case 0x24: return 268;
    case 0x23: return 269;
    case 0x14: return 280;
    case 0x6E: return 330;
    case 0x6F: return 331;
    case 0x6A: return 332;
    case 0x6D: return 333;
    case 0x6B: return 334;
    case 0xA0: return 340;
    case 0xA4: return 342;
    case 0xA1: return 344;
    case 0xA5: return 346;
    default: return no_input;
    }
}
}
