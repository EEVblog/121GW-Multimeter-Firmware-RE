cmake_minimum_required(VERSION 3.16)
# Cross-compile with the GNU Arm toolchain unless another toolchain file is given.
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/arm-none-eabi.cmake)
endif()
project(eevblog_121gw_bootloader C)

if(NOT CMAKE_C_COMPILER_VERSION VERSION_EQUAL 9.2.1)
    message(WARNING "Compiler is GCC ${CMAKE_C_COMPILER_VERSION}; bit-equivalence with the original "
                    "was only verified for GCC 9.2.1 (GNU Arm Embedded 9-2019-q4).")
endif()

set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../common)
set(FATFS_DIR  ${COMMON_DIR}/fatfs)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE MinSizeRel)
endif()

# Deviation from the original bootloader. Off by default: the image is then
# bit-equivalent to the original. See docs/bootloader.md.
option(BL_FIX_BUGS "Fix confirmed bugs in the SD update path" OFF)
set(BL_OPTION_DEFINITIONS)
if(BL_FIX_BUGS)
    list(APPEND BL_OPTION_DEFINITIONS BL_FIX_BUGS=1)
endif()

add_executable(bootloader.elf
    src/startup.c
    src/system_stm32l1xx.c
    src/main.c
    src/sys_init.c
    src/gpio.c
    src/flash_if.c
    src/flash_ramfunc.c
    src/lcd.c
    src/sd_card.c
    src/diskio.c
    src/irq.c
    ${FATFS_DIR}/ff.c
)

target_include_directories(bootloader.elf PRIVATE
    include
    ${FATFS_DIR}
    ${COMMON_DIR}/cmsis/core
    ${COMMON_DIR}/cmsis/device
)

target_compile_definitions(bootloader.elf PRIVATE STM32L152xD ${BL_OPTION_DEFINITIONS})
target_compile_options(bootloader.elf PRIVATE -std=c11 -Wall -Wextra)
set_source_files_properties(${FATFS_DIR}/ff.c PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter")

target_link_options(bootloader.elf PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/stm32l152xd_bl.ld
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/bootloader.map
    -nostdlib
)
target_link_libraries(bootloader.elf PRIVATE gcc)

add_custom_command(TARGET bootloader.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary bootloader.elf bootloader.bin
    COMMAND ${CMAKE_SIZE} bootloader.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Creating bootloader.bin"
)
