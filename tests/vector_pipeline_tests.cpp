// SPDX-License-Identifier: GPL-3.0-only
#include "MiniTest.h"
#include "vector_checks.h"
#include "runtime_layout.h"
#include "vector_hotpath_checks.h"
void register_ps2_vu1_tests();
int main() {
    register_ps2_vu1_tests();
    MiniTest::Case("Vector execution references",[](TestCase& tc){
        tc.Run("64 original state, cycle, memory and packet references",[](TestCase&){openkai2::verify_vector_execution();});
    });
    MiniTest::Case("Vector pipeline scheduling references",[](TestCase& tc){
        tc.Run("128 original snapshots across resume budgets and units",[](TestCase&){openkai2::verify_vector_pipeline_scheduling();});
    });
    MiniTest::Case("Runtime VIF1 integration",[](TestCase& tc){
        tc.Run("Embedded VU0/VU1 layouts and PATH1 triangle rendering",[](TestCase&){openkai2::verify_vector_runtime();});
    });
    MiniTest::Case("Original arithmetic flag references",[](TestCase& tc){
        tc.Run("73728 original cases including bounds, cancellation and destination masks",[](TestCase&){openkai2::verify_vector_arithmetic();});
    });
    MiniTest::Case("Original VIF unpack references",[](TestCase& tc){
        tc.Run("1664 original memory and register snapshots including overlap",[](TestCase&){openkai2::verify_vif_unpack();});
    });
    MiniTest::Case("Original product pipeline references",[](TestCase& tc){
        tc.Run("32 original multiply-add/subtract pipeline snapshots",[](TestCase&){openkai2::verify_vector_products();});
    });
    MiniTest::Case("Original graphics transfer references",[](TestCase& tc){
        tc.Run("128 original packet, callback cycle, memory and resume snapshots",[](TestCase&){openkai2::verify_vector_transfers();});
    });
    MiniTest::Case("Selected vector operand references",[](TestCase& tc){tc.Run("128 original state and cycle snapshots with unused operand data",[](TestCase&){openkai2::verify_vector_operands();});});
    MiniTest::Case("Vector code upload references",[](TestCase& tc){tc.Run("64 original code edit and resume snapshots across both units",[](TestCase&){openkai2::verify_vector_code_updates();});});
    return MiniTest::Run();
}
