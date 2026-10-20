# Phase 223 — standalone virtual IRQ11 to original handler translation

**Run:** Double-click `DOUBLE_CLICK_PHASE223.cmd` on Windows, with Python 3.10+, CMake and Visual Studio C++ Build Tools installed. Select your owned Budokai 2 USA ISO when prompted. After completion upload `UPLOAD_PHASE223_RESULTS.zip` and `UPLOAD_PHASE223_DIAGNOSTICS.zip`. No prior phases or external registry are needed.

## What this does

The native C++ test executes the already translated original handler prefix / list / branch / JAL and two post-call continuations, invoking both translated wrappers and a **virtual** CP0/MMIO shared helper in the same process. Unlike Phase 220's wrapper-only IRQ gate, Phase 223's admitted virtual IRQ enters original handler code.

The virtual admission flags are **test-harness rules**, not validated PS2 INTC behavior: bit 0 queued event, bit 1 unmasked route, bit 2 registered callback, bit 3 explicit acknowledgment. Across 180 base scenarios × 16 flags = 2,880 cases, the gate admits 360 and suppresses 2,520; 180 acknowledged dispatches.

Expected state is reconstructed independently using the SHA-pinned original retail handler instruction interpreter (not the C++ implementation), decoded wrapper instructions, and virtual shared-helper oracle. The SHA-256-verified executable is loaded directly from the user's privately owned ISO, or use `RUN_PHASE223.py --game PATH`. Without game input only synthetic tests run; no hardware authenticity is claimed.

## Limitations

Not a full original handler, not a Windows native game executable, not PS2Runtime scheduler integration. The shared helper, EI, CP0 and MMIO are **software models** with externally supplied transitions. No host hardware is accessed. No copyright game binary is bundled, no real guest callback is registered, and real PS2 interrupt timing is not verified.

For inspection, launch `ONE_CLICK_PHASE223_DIAGNOSTICS.cmd`. If Windows build fails, upload the results and diagnostics ZIPs plus the CMake error logs included therein.
