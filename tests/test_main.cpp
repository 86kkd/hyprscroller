#include <cstdlib>
#include <iostream>

#include "test_suite.h"
#include "test_support.h"

int main() {
    run_core_logic_tests();
    run_layout_logic_tests();
    run_overview_logic_tests();
    run_overview_model_tests();
    run_overview_scene_tests();
    run_overview_session_logic_tests();

    if (failures != 0) {
        std::cerr << failures << " logic test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All logic tests passed\n";
    return EXIT_SUCCESS;
}
