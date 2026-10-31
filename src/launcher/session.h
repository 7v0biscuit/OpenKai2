// SPDX-License-Identifier: GPL-3.0-only
#pragma once
namespace openkai2::launcher {
class SessionLock {
  public:
    SessionLock();
    ~SessionLock();
    SessionLock(const SessionLock &) = delete;
    SessionLock &operator=(const SessionLock &) = delete;

  private:
    void *handle_{};
};
bool session_active() noexcept;
}
