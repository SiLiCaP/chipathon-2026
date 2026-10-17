# RP2350 firmware for the NTT chip on the Tiny Tapeout demo board v3 (Spec 1 harness and demo).
# Build: see README.md. Output: build/tt_ntt.uf2 (drag onto the board in BOOTSEL mode).
cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD tt_demoboard_v3 CACHE STRING "board")
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(TT_PROJECT_ADDR -1 CACHE STRING "our project's address on the TT mux (-1: select at run time)")

include(pico_sdk_import.cmake)
project(tt_ntt C CXX ASM)
pico_sdk_init()

add_executable(tt_ntt
    rp/main.c
    rp/tt_rp.c
    core/ntt_ref.c
    core/ntt_chip.c
    core/swchip.c
    core/app.c
)
pico_generate_pio_header(tt_ntt ${CMAKE_CURRENT_LIST_DIR}/rp/tt_lockstep.pio)
target_include_directories(tt_ntt PRIVATE core rp)
target_compile_options(tt_ntt PRIVATE -O2 -Wall -Wextra)
target_compile_definitions(tt_ntt PRIVATE
    TT_PROJECT_ADDR=${TT_PROJECT_ADDR}
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0
)
target_link_libraries(tt_ntt pico_stdlib pico_rand hardware_pio hardware_dma hardware_clocks)
pico_enable_stdio_usb(tt_ntt 1)
pico_enable_stdio_uart(tt_ntt 0)
pico_add_extra_outputs(tt_ntt)
