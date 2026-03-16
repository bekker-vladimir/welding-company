cmake_minimum_required(VERSION 3.16)
project(WeldingCompany CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Compiler diagnostics --------------------------------------------------
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

# ---- Core library ----------------------------------------------------------
add_library(welding_lib STATIC
    src/CWeldingCompany.cpp
)
target_include_directories(welding_lib PUBLIC include)
target_link_libraries(welding_lib PUBLIC Threads::Threads)

# ---- Test-stub library -----------------------------------------------------
# CProducerSync / CProducerAsync / CCustomerTest (portable source replacement
# for the platform-specific sample_tester.o shipped with the original task).
add_library(test_stubs STATIC
    sample_tester.cpp
)
target_include_directories(test_stubs PUBLIC .)
target_link_libraries(test_stubs PUBLIC Threads::Threads)

# ---- Main executable -------------------------------------------------------
add_executable(welding_main src/main.cpp)
target_include_directories(welding_main PRIVATE .)
target_link_libraries(welding_main PRIVATE welding_lib test_stubs)

# ---- Unit / integration tests ----------------------------------------------
enable_testing()

add_executable(run_tests tests/test_welding.cpp)
target_include_directories(run_tests PRIVATE .)
target_link_libraries(run_tests PRIVATE welding_lib)

add_test(NAME AllTests COMMAND run_tests)

# `cmake --build build --target check`
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS run_tests
)
