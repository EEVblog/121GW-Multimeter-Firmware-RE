cmake_minimum_required(VERSION 3.16)
project(eevblog_121gw_firmware C ASM)

set(ROOT_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(COMMON_DIR ${ROOT_DIR}/common)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

# Reconstructed application modules.
set(APP_SOURCES
    src/keys.c
    src/board_io.c
    src/hy3131_bus.c
    src/rtc.c
    src/lcd_driver.c
    src/flash_ob.c
    src/system_init.c
    src/hy3131_tables.c
    src/hy3131_ctrl.c
    src/measure_rms.c
    src/measure_scale.c
    src/eeprom.c
    src/calibration.c
    src/measure_ad1.c
)

add_library(app_modules STATIC ${APP_SOURCES})
target_include_directories(app_modules PUBLIC
    include
    ${COMMON_DIR}/cmsis/core
    ${COMMON_DIR}/cmsis/device
)
target_compile_definitions(app_modules PUBLIC STM32L152xD)
target_compile_options(app_modules PRIVATE -std=c11 -Wall -Wextra)

# ---- Hybrid test image -----------------------------------------------------
# Rebuilt modules + bindings of everything else to the original image, for
# function-level equivalence tests (tools/emu/test_app_functions.py).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/hybrid_syms.s
    COMMAND ${Python3_EXECUTABLE} -P ${ROOT_DIR}/tools/hybrid_syms.py
            $<TARGET_FILE:app_modules> ${CMAKE_CURRENT_BINARY_DIR}/hybrid_syms.s
    DEPENDS app_modules ${ROOT_DIR}/tools/hybrid_syms.py ${ROOT_DIR}/ghidra/app_names.tsv
    COMMENT "Binding unresolved symbols to the original image"
)
add_executable(hybrid.elf ${CMAKE_CURRENT_BINARY_DIR}/hybrid_syms.s)
target_link_options(hybrid.elf PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/hybrid.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/hybrid.map
    -nostdlib -Wl,--no-gc-sections
)
target_link_libraries(hybrid.elf PRIVATE -Wl,--whole-archive app_modules -Wl,--no-whole-archive gcc)
add_custom_command(TARGET hybrid.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary hybrid.elf hybrid.bin
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
