#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

/********   UART0 TELIT MODEM SETTINGS    ********/
#define TELIT_UART uart0
#define TELIT_UART_BAUDRATE 115200
#define TELIT_UART_DATABITS 8
#define TELIT_UART_STOPBITS 1
#define TELIT_UART_PARITY UART_PARITY_NONE
#define TELIT_UART_TX_PIN 0
#define TELIT_UART_RX_PIN 1
#define TELIT_UART_IRQ (TELIT_UART == uart0 ? UART0_IRQ : UART1_IRQ)
#define TELIT_BUFFER_SIZE 5000

char uart0_buffer[TELIT_BUFFER_SIZE];    // It holds the data coming from RX.
uint16_t buffer_index = 0;               // It holds the index of the buffer.
char recieved_char = '\0';
volatile bool is_message_finished = false;
/*************************************************/

/********   Interrupt Services Routines    ********/
void on_uart0_rx() {
    
    if (uart_is_readable(TELIT_UART)) {
        recieved_char = uart_getc(TELIT_UART);
        uart0_buffer[buffer_index] = recieved_char;

        if (recieved_char == '\0') {
            buffer_index = 0;
            is_message_finished = true;
        }

        else {
            buffer_index++;
            is_message_finished = false;
        }
    }
}

/*************************************************/

/********   Timer Calback Routines    ********/
bool timer_callback_random_send_modem(struct repeating_timer *_timer) {
    printf("\n> Command sent.\n");
    uart_puts(TELIT_UART, "AT+CCID\r\n");
    printf("\n\n> Current State: %s\n\n", uart0_buffer);
}
/*************************************************/

void set_telit_uart_ready() {
    // Open the UART channel with given baudrate.
    uart_init(TELIT_UART, TELIT_UART_BAUDRATE);
 
    // Give the UART neccecary pins which we'll use.
    gpio_set_function(TELIT_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(TELIT_UART_RX_PIN, GPIO_FUNC_UART);
    
    // UART channel won't use CTS and RTS.
    uart_set_hw_flow(TELIT_UART, false, false);

    // Set format for UART0 -- 8bit data + 1bit stop, without parity.
    uart_set_format(TELIT_UART, TELIT_UART_DATABITS, TELIT_UART_STOPBITS, TELIT_UART_PARITY);
    
    /*
    * PUT SOME COMMENT IN HERE.
    */
    uart_set_fifo_enabled(TELIT_UART, true);

    // Set ISR as on_uart_rx, enable it, and tell when its triggered.    
    irq_set_exclusive_handler(TELIT_UART_IRQ, on_uart0_rx);
    irq_set_enabled(TELIT_UART_IRQ, true);
    uart_set_irq_enables(TELIT_UART, true, false);
}

int main(){
    /* 
    * Initialize the stdio from the USB/UART0.
    * Note that, CMakeLists file has to have
    * "pico_enable_stdio_uart(main 0)" to
    * not use UART channel for communication
    * between PC and Pico.
    */
    stdio_init_all(); 

    // Set the UART0 ready for TELIT modem.
    set_telit_uart_ready();
    
    // Wait for 10 seconds, to power up the modem.
    sleep_ms(10000);

    // Create a repeating timer to send commands to TELIT.
    struct repeating_timer random_send_modem;
    add_repeating_timer_ms(10000, timer_callback_random_send_modem, NULL, &random_send_modem);
    
    while (true) {
        if (is_message_finished) printf("%s", uart0_buffer);

        tight_loop_contents();
    }
}