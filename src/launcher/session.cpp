// SPDX-License-Identifier: GPL-3.0-only
#include "launcher/session.h"
#include <stdexcept>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace {
constexpr wchar_t sessionName[] = L"Local\\OpenKai2.GameAndSaveSession";
}
#endif
namespace openkai2::launcher {
SessionLock::SessionLock() {
#ifdef _WIN32
    auto h = CreateMutexW(nullptr, FALSE, sessionName);
    if (!h)
        throw std::runtime_error("Cannot lock the game save session");
    const auto result = WaitForSingleObject(h, 0);
    if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
        CloseHandle(h);
        throw std::runtime_error("Another game or save operation is already active");
    }
    handle_ = h;
#endif
}
SessionLock::~SessionLock() {
#ifdef _WIN32
    if (handle_) {
        ReleaseMutex(handle_);
        CloseHandle(handle_);
    }
#endif
}
bool session_active() noexcept {
#ifdef _WIN32
    auto h = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, sessionName);
    if (!h)
        return false;
    const auto result = WaitForSingleObject(h, 0);
    const bool busy = result != WAIT_OBJECT_0 && result != WAIT_ABANDONED;
    if (!busy)
        ReleaseMutex(h);
    CloseHandle(h);
    return busy;
#else
    return false;
#endif
}
} // namespace openkai2::launcher
