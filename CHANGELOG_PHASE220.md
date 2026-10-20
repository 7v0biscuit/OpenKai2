# Phase 220 changes

- Verified uploaded Phase 219 Windows reports: new observation predictor 432/432,
  virtual EE interrupt model 384/384, integrated native wrapper chain 448/448,
  virtual MMIO/ERET 560/560, prior CP0 112/112, shared 63/63, wrappers 26/26,
  and correct USA retail identity. No external trace was submitted.
- Added standalone C++ **virtual** IRQ11 admission/handoff adapter. Its four
  fixture flags are not represented as verified hardware/scheduler rules.
- Existing native retail-derived wrapper and shared helper are invoked only
  on admitted virtual events; suppressed events do not call guest code.
- Added independent opcode-driven differential comparison and boundary checks
  covering both wrapper addresses, queued/unmasked/registered gates,
  explicit acknowledgment, modeled return, and bounded polling.
- Kept external trace replay and every previous 219 regression in the package.
- Fixed Phase219's report directory label typo in the new Phase220 runner;
  packaged results are under phase220/ and diagnostics under phase220/.
- No retail executable, PS2 assets, generated original game functions,
  staged registry, native full-game runtime, or historical phase files bundled.
