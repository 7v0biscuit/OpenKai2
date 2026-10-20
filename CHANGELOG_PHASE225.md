# Phase 225 changes
- New standalone native C++ handler entry -> actual linked-list traversal -> threshold -> original helpers -> optional second tail-call chain.
- Virtual gate ensures no handler executes on suppressed dispatch; loop budget prevents endless walks.
- Expanded virtual node memory window from two to three aligned fixtures (0x210040..0x210100).
- New independent Python composition oracle; synthetic/retail results in separate JSON.
- Preserved historical native regression targets and optional PS2 observation comparison, no real guest IRQ or game boot.
- Added Phase225 Windows CMD runner and diagnostics collector.
