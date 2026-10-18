# RP2350 firmware for the NTT chip (Spec 1 harness and demo).
#   cmake -S rp2350 -B rp2350/build [-DTT_TARGET=dbv3|pico2w_tang] [-DTT_PROJECT_ADDR=n]
#   dbv3         Tiny Tapeout demo board v3 with the real chip (default) -> build/tt_ntt_dbv3.uf2
#   pico2w_tang  Pico 2 W wired to a Tang Nano 20K running the chip      -> build/tt_ntt_pico2w_tang.uf2
#                (wiring: tt-viktor-chiptor docs/PICO_TANG_RIG.md)
# Use one build directory per target.
cmake_minimum_required(VERSION 3.13)

set(TT_TARGET dbv3 CACHE STRING "test environment: dbv3 (TT demo board) or pico2w_tang (Pico 2 W + Tang Nano 20K)")
set_property(CACHE TT_TARGET PROPERTY STRINGS dbv3 pico2w_tang)
if (TT_TARGET STREQUAL "dbv3")
    set(PICO_BOARD tt_demoboard_v3 CACHE STRING "board")
    set(TT_TARGET_DEFINE TT_TARGET_DBV3=1)
elseif (TT_TARGET STREQUAL "pico2w_tang")
    set(PICO_BOARD pico2_w CACHE STRING "board")
    set(TT_TARGET_DEFINE TT_TARGET_PICO2W_TANG=1)
else ()
    message(FATAL_ERROR "TT_TARGET must be dbv3 or pico2w_tang, not '${TT_TARGET}'")
endif ()
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards)
set(TT_PROJECT_ADDR -1 CACHE STRING "our project's address on the TT mux (-1: select at run time; dbv3 only)")

include(pico_sdk_import.cmake)
project(tt_ntt C CXX ASM)
pico_sdk_init()

set(APP tt_ntt_${TT_TARGET})
add_executable(${APP}
    rp/main.c
    rp/tt_rp.c
    core/ntt_ref.c
    core/ntt_chip.c
    core/swchip.c
    core/app.c
    core/oplog.c
    core/sha3_sw.c
)
pico_generate_pio_header(${APP} ${CMAKE_CURRENT_LIST_DIR}/rp/tt_lockstep.pio)
target_include_directories(${APP} PRIVATE core rp)
target_compile_options(${APP} PRIVATE -O2 -Wall -Wextra)
target_compile_definitions(${APP} PRIVATE
    ${TT_TARGET_DEFINE}
    TT_PROJECT_ADDR=${TT_PROJECT_ADDR}
    PICO_STDIO_USB_CONNECT_WAIT_TIMEOUT_MS=0
)
target_link_libraries(${APP} pico_stdlib pico_rand hardware_pio hardware_dma hardware_clocks)
pico_enable_stdio_usb(${APP} 1)
pico_enable_stdio_uart(${APP} 0)
pico_add_extra_outputs(${APP})
