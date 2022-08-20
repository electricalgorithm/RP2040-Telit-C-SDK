cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(TELIT_SDK_PICO_C C CXX ASM)

set(CMAKE_C_STANDART 11)
set(CMAKE_CXX_STANDART 17)

pico_sdk_init()

add_executable(firmware firmware.c)

pico_enable_stdio_usb(firmware 1)
pico_enable_stdio_uart(firmware 0)

pico_add_extra_outputs(firmware)

target_link_libraries(firmware 
                        pico_stdlib 
                        pico_stdio 
                        pico_time
                        pico_runtime
                        hardware_gpio 
                        hardware_uart 
                        hardware_irq 
                        hardware_timer)
