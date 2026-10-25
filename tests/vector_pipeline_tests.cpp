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
    return MiniTest::Run();
}
