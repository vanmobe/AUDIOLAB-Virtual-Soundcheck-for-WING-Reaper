enable_testing()

add_executable(source_monitor_tests
    source_monitor_tests.cpp
)

add_executable(wing_osc_probe
    osc_probe.cpp
)

target_include_directories(source_monitor_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../include/wingconnector
        ${CMAKE_CURRENT_SOURCE_DIR}/../include/internal
)

target_include_directories(wing_osc_probe
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
        ${CMAKE_CURRENT_SOURCE_DIR}/../include/wingconnector
        ${CMAKE_CURRENT_SOURCE_DIR}/../include/internal
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib/oscpack
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib/nlohmann
)

if(MSVC)
    target_compile_options(source_monitor_tests PRIVATE /W4 /WX-)
    target_compile_options(wing_osc_probe PRIVATE /W4 /WX-)
else()
    target_compile_options(source_monitor_tests PRIVATE -Wall -Wextra -Wpedantic -Wno-error)
    target_compile_options(wing_osc_probe PRIVATE -Wall -Wextra -Wpedantic -Wno-error)
endif()

file(GLOB OSCPACK_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../lib/oscpack/osc/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../lib/oscpack/ip/*.cpp"
)

if(WIN32)
    file(GLOB OSCPACK_PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lib/oscpack/ip/win32/*.cpp")
    target_link_libraries(wing_osc_probe PRIVATE ws2_32 winmm)
else()
    file(GLOB OSCPACK_PLATFORM_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../lib/oscpack/ip/posix/*.cpp")
    target_link_libraries(wing_osc_probe PRIVATE pthread)
endif()

target_sources(wing_osc_probe PRIVATE
    ${OSCPACK_SOURCES}
    ${OSCPACK_PLATFORM_SOURCES}
)

add_test(NAME source_monitor_tests COMMAND source_monitor_tests)
