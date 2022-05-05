cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(Telit2Host C CXX ASM)

set(CMAKE_C_STANDART 11)
set(CMAKE_CXX_STANDART 17)

pico_sdk_init()

add_executable(telitHost telitHost.c)

pico_enable_stdio_usb(telitHost 1)
pico_enable_stdio_uart(telitHost 0)

pico_add_extra_outputs(telitHost)

target_link_libraries(telitHost 
                        pico_stdlib 
                        pico_stdio 
                        pico_time
                        pico_runtime
                        hardware_gpio 
                        hardware_uart 
                        hardware_irq 
                        hardware_timer)