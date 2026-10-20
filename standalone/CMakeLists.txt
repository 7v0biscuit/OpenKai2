cmake_minimum_required(VERSION 3.16)
project(budokai2_phase217_wrapper_shared_chain LANGUAGES CXX)
add_executable(budokai2_phase214_shared_entry ../src/phase214_shared_entry.cpp)
add_executable(budokai2_phase214_tail_wrappers ../src/phase213_tail_wrappers.cpp)
foreach(target budokai2_phase214_shared_entry budokai2_phase214_tail_wrappers)
 target_compile_features(${target} PRIVATE cxx_std_17)
 if(MSVC)
  target_compile_options(${target} PRIVATE /W4 /EHsc)
 else()
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
 endif()
endforeach()
add_executable(budokai2_phase215_virtual_cp0 ../src/phase215_virtual_cp0.cpp)
target_compile_features(budokai2_phase215_virtual_cp0 PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(budokai2_phase215_virtual_cp0 PRIVATE /W4 /EHsc)
else()
 target_compile_options(budokai2_phase215_virtual_cp0 PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(budokai2_phase216_virtual_mmio_eret ../src/phase216_virtual_mmio_eret.cpp)
target_compile_features(budokai2_phase216_virtual_mmio_eret PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(budokai2_phase216_virtual_mmio_eret PRIVATE /W4 /EHsc)
else()
 target_compile_options(budokai2_phase216_virtual_mmio_eret PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The integrated probe compiles the *same* bounded native wrapper and shared
# CP0/MMIO models from Phases 213/216 into a single host process.
add_executable(budokai2_phase217_wrapper_shared_chain ../src/phase217_wrapper_shared_chain.cpp)
target_compile_features(budokai2_phase217_wrapper_shared_chain PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(budokai2_phase217_wrapper_shared_chain PRIVATE /W4 /EHsc)
else()
 target_compile_options(budokai2_phase217_wrapper_shared_chain PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Phase 218 independent EE EI/EDI privilege plus event-checkpoint integration.
add_executable(budokai2_phase218_interrupt_transition_chain ../src/phase218_interrupt_transition_chain.cpp)
target_compile_features(budokai2_phase218_interrupt_transition_chain PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(budokai2_phase218_interrupt_transition_chain PRIVATE /W4 /EHsc)
else()
 target_compile_options(budokai2_phase218_interrupt_transition_chain PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Phase 219 bounded host-only CP0-observation predictor.
add_executable(budokai2_phase219_cp0_observation_replay ../src/phase219_cp0_observation_replay.cpp)
target_compile_features(budokai2_phase219_cp0_observation_replay PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(budokai2_phase219_cp0_observation_replay PRIVATE /W4 /EHsc)
else()
 target_compile_options(budokai2_phase219_cp0_observation_replay PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Phase 220 virtual IRQ11 admission/handoff; deliberately NO genuine PS2Runtime
# callbacks, guest Timer2 IRQ handler, or host IRQ/MMIO accesses.
add_executable(budokai2_phase220_irq11_handoff ../src/phase220_irq11_handoff.cpp)
target_compile_features(budokai2_phase220_irq11_handoff PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(budokai2_phase220_irq11_handoff PRIVATE /W4 /EHsc)
else()
 target_compile_options(budokai2_phase220_irq11_handoff PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Phase221 resumes the independently validated ORIGINAL handler blocks.
# The bridge links genuine translated handler call-site effects to the existing
# wrapper and shared-helper virtual model IN ONE native process.
add_executable(budokai2_handler_probe ../src/handler_list_probe.cpp)
add_executable(budokai2_phase221_handler_helper_bridge ../src/phase221_handler_helper_bridge.cpp)
foreach(target budokai2_handler_probe budokai2_phase221_handler_helper_bridge)
 target_compile_features(${target} PRIVATE cxx_std_17)
 if(MSVC)
  target_compile_options(${target} PRIVATE /W4 /EHsc)
 else()
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
 endif()
endforeach()

# Phase 222 translates both original post-helper continuation blocks, then
# performs the second tail-jump and virtual shared-helper invocation.
add_executable(budokai2_phase222_handler_double_helper ../src/phase222_handler_double_helper_continuation.cpp)
target_compile_features(budokai2_phase222_handler_double_helper PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(budokai2_phase222_handler_double_helper PRIVATE /W4 /EHsc)
else()
 target_compile_options(budokai2_phase222_handler_double_helper PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Phase 223 virtual IRQ admission to the exact bounded ORIGINAL handler blocks,
# original post-call paths, native helper wrappers and virtual shared routine.
add_executable(budokai2_phase223_virtual_irq_original_handler ../src/phase223_virtual_irq_original_handler.cpp)
target_compile_features(budokai2_phase223_virtual_irq_original_handler PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(budokai2_phase223_virtual_irq_original_handler PRIVATE /W4 /EHsc)
else()
 target_compile_options(budokai2_phase223_virtual_irq_original_handler PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Phase 224 new independently tested MIPS linked-list loop region.
add_executable(budokai2_phase224_original_list_walk ../src/phase224_original_list_walk.cpp)
target_compile_features(budokai2_phase224_original_list_walk PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(budokai2_phase224_original_list_walk PRIVATE /W4 /EHsc)
else()
 target_compile_options(budokai2_phase224_original_list_walk PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Phase225 original prelude + bounded linked-list + threshold and wrappers.
add_executable(budokai2_phase225_original_list_irq_chain ../src/phase225_original_list_to_irq_helper.cpp)
target_compile_features(budokai2_phase225_original_list_irq_chain PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(budokai2_phase225_original_list_irq_chain PRIVATE /W4 /EHsc)
else()
 target_compile_options(budokai2_phase225_original_list_irq_chain PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Phase 226: same translated handler+helper chain, original LD/LW via live byte-addressable guest memory.
add_executable(budokai2_phase226_live_guest_memory_chain ../src/phase226_live_guest_memory_chain.cpp)
target_compile_features(budokai2_phase226_live_guest_memory_chain PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(budokai2_phase226_live_guest_memory_chain PRIVATE /W4 /EHsc)
else()
 target_compile_options(budokai2_phase226_live_guest_memory_chain PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Phase227 uses the SAME CPU/Mem backend as the translated original entry and
# Phase226 list walker; KSEG direct mapping and access faults are sandbox-only.
add_executable(budokai2_phase227_mapped_guest_memory ../src/phase227_mapped_guest_memory.cpp)
target_compile_features(budokai2_phase227_mapped_guest_memory PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(budokai2_phase227_mapped_guest_memory PRIVATE /W4 /EHsc)
else()
 target_compile_options(budokai2_phase227_mapped_guest_memory PRIVATE -Wall -Wextra -Wpedantic)
endif()
