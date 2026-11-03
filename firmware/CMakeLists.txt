cmake_minimum_required(VERSION 3.16)
# Cross-compile with the GNU Arm toolchain unless another toolchain file is given.
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/arm-none-eabi.cmake)
endif()
project(eevblog_121gw_firmware C ASM)

if(NOT CMAKE_C_COMPILER_VERSION VERSION_EQUAL 9.2.1)
    message(WARNING "Compiler is GCC ${CMAKE_C_COMPILER_VERSION}; bit-equivalence with the original "
                    "was only verified for GCC 9.2.1 (GNU Arm Embedded 9-2019-q4).")
endif()

set(ROOT_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(COMMON_DIR ${ROOT_DIR}/common)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

# Deviations from the original firmware. Both off by default: the image is
# then bit-equivalent to v2.05. See docs/app-architecture.md.
option(APP_FIX_BUGS "Fix confirmed bugs in the reading/auto-range path" OFF)
option(APP_AUTORANGE_FAST "Auto-range jumps several ranges at once" OFF)
# Without the fixes the fake reading after a range change uses up the only
# skipped reading of several ranges (g_setting_238 = 1), so a fast step could
# act on an unsettled first average. A normal variable, so the cached value
# is left as the user set it.
if(APP_AUTORANGE_FAST AND NOT APP_FIX_BUGS)
    message(STATUS "APP_AUTORANGE_FAST needs APP_FIX_BUGS: turning APP_FIX_BUGS on")
    set(APP_FIX_BUGS ON)
endif()
set(APP_OPTION_DEFINITIONS)
if(APP_FIX_BUGS)
    list(APPEND APP_OPTION_DEFINITIONS APP_FIX_BUGS=1)
endif()
if(APP_AUTORANGE_FAST)
    list(APPEND APP_OPTION_DEFINITIONS APP_AUTORANGE_FAST=1)
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
    src/front_end.c
    src/buzzer.c
    src/display_state.c
    src/measure_derived.c
    src/iar_math.c
    src/filters.c
    src/temperature.c
    src/range_select.c
    src/measure_funcs.c
    src/ac_response.c
    src/trim_correction.c
    src/dual_correction.c
    src/measure_peak.c
    src/measure_counter.c
    src/annunciators.c
    src/display_symbols.c
    src/display_text.c
    src/display_bargraph.c
    src/display_update.c
    src/sd_card.c
    src/diskio.c
    src/irq.c
    src/settings.c
    src/mem_records.c
    src/ble_packet.c
    src/ble_command.c
    src/power.c
    src/setup_menu.c
    src/modes.c
    src/battery.c
    src/sd_log.c
    src/key_handlers.c
    src/main.c
    src/libc_min.c
    ${COMMON_DIR}/fatfs/ff.c
)

add_library(app_modules STATIC ${APP_SOURCES})
target_include_directories(app_modules PUBLIC
    include
    ${COMMON_DIR}/cmsis/core
    ${COMMON_DIR}/cmsis/device
    ${COMMON_DIR}/fatfs
)
target_compile_definitions(app_modules PUBLIC STM32L152xD ${APP_OPTION_DEFINITIONS})
target_compile_options(app_modules PRIVATE -std=c11 -Wall -Wextra)
# FatFs R0.08a, unmodified. Its two file-scope variables get sections of
# their own so the hybrid image can place them at the original addresses.
set_source_files_properties(${COMMON_DIR}/fatfs/ff.c PROPERTIES
    COMPILE_OPTIONS "-Wno-unused-parameter;-fdata-sections")

# ---- Hybrid test image -----------------------------------------------------
# Rebuilt modules + bindings of everything else to the original image, for
# function-level equivalence tests (tools/emu/test_app_functions.py).
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)   # python -P needs 3.11+
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

# ---- Standalone application image ------------------------------------------
# Every function rebuilt; globals at their original RAM addresses
# (tools/hybrid_syms.py --ram-only fails if anything else is still needed).
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ram_syms.s
    COMMAND ${Python3_EXECUTABLE} -P ${ROOT_DIR}/tools/hybrid_syms.py
            $<TARGET_FILE:app_modules> ${CMAKE_CURRENT_BINARY_DIR}/ram_syms.s --ram-only
    DEPENDS app_modules ${ROOT_DIR}/tools/hybrid_syms.py ${ROOT_DIR}/ghidra/app_names.tsv
    COMMENT "Binding globals to their original RAM addresses"
)
add_executable(app.elf src/startup.c src/ram_init.c ${CMAKE_CURRENT_BINARY_DIR}/ram_syms.s)
target_include_directories(app.elf PRIVATE include ${COMMON_DIR}/cmsis/core ${COMMON_DIR}/cmsis/device)
target_compile_definitions(app.elf PRIVATE STM32L152xD ${APP_OPTION_DEFINITIONS})
target_compile_options(app.elf PRIVATE $<$<COMPILE_LANGUAGE:C>:-std=c11 -Wall -Wextra>)
target_link_options(app.elf PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/stm32l152xd_app.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/app.map
    -nostdlib -Wl,--gc-sections
)
target_link_libraries(app.elf PRIVATE app_modules gcc)
add_custom_command(TARGET app.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary app.elf app.bin
    COMMAND ${CMAKE_SIZE} app.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
