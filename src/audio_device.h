// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>

namespace openkai2 {
using AudioDeviceCallback = void (*)(float *samples, unsigned frames);
bool open_audio_device(unsigned rate, AudioDeviceCallback callback, std::string &info);
void close_audio_device();
}
