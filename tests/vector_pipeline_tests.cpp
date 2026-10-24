// SPDX-License-Identifier: GPL-3.0-only
#include "MiniTest.h"
#include "vector_checks.h"
void register_ps2_vu1_tests();
int main() {
    register_ps2_vu1_tests();
    MiniTest::Case("Vector execution references",[](TestCase& tc){
        tc.Run("64 original state, cycle, memory and packet references",[](TestCase&){openkai2::verify_vector_execution();});
    });
    return MiniTest::Run();
}
