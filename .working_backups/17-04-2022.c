#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "pico/time.h"
#include "string.h"

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

/********      BOARD BUTTON SETTINGS      ********/
#define BOARD_BUTTON_PIN 2
#define BOUNCING_DELAY 150000
#define USB_BUFFER_SIZE 128

volatile uint32_t btn_rise_started_time = 0;
volatile bool is_board_button_clicked = false;
char usb_buffer[USB_BUFFER_SIZE];
char concat_buffer[USB_BUFFER_SIZE + 4];
/*************************************************/

/********   Interrupt Services Routines    ********/
void on_uart0_rx() {
    
    while (uart_is_readable(TELIT_UART)) {
        recieved_char = uart_getc(TELIT_UART);
        uart0_buffer[buffer_index] = recieved_char;
        buffer_index++;
    }

    is_message_finished = true;
}

void gpio_interrupt_handler(uint GPIO_pin, uint32_t event) {
    switch (GPIO_pin) {

        case BOARD_BUTTON_PIN:
            
            if (event == GPIO_IRQ_EDGE_RISE) {
                btn_rise_started_time = time_us_32();
            }

            else if (event == GPIO_IRQ_EDGE_FALL) {
                if (time_us_32() - btn_rise_started_time > BOUNCING_DELAY) {
                    is_board_button_clicked = true;
                }
            }

            break;
        
        default:
            break;
    }
}
/*************************************************/

/********   Timer Calback Routines    ********/
bool timer_callback_random_send_modem(struct repeating_timer *_timer) {
    if (uart_is_writable(TELIT_UART))
        uart_puts(TELIT_UART, "AT+IMEISV\r\n");
}

int64_t send_message_alarm(alarm_id_t id, void *user_data) {
    puts("Alarm fired!");
    strncpy(concat_buffer, usb_buffer, sizeof(concat_buffer));
    strcat(concat_buffer, "\r\n");
    while (true)
        if (uart_is_writable(TELIT_UART)) {
            uart_puts(TELIT_UART, concat_buffer);
            break;
        }
    
    return 0;
}
/*************************************************/
void send_message_to_telit() {
    // Add CR+LF ending since TELIT needs it.
    strncpy(concat_buffer, usb_buffer, sizeof(concat_buffer));
    strcat(concat_buffer, "\r\n");
    // Try to send it until UART channel is writable.
    while (true)
        if (uart_is_writable(TELIT_UART)) {
            uart_puts(TELIT_UART, concat_buffer);
            break;
        }
}

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
    * Since the order of the data is important for us,
    * we need to enable FIFO. If you'll disable it, it'll
    * not show you real data in order -- or send it in order.
    */
    uart_set_fifo_enabled(TELIT_UART, true);

    // Set ISR as on_uart_rx, enable it, and tell when its triggered.    
    irq_set_exclusive_handler(TELIT_UART_IRQ, on_uart0_rx);
    irq_set_enabled(TELIT_UART_IRQ, true);
    uart_set_irq_enables(TELIT_UART, true, false);
}

void set_gpios() {
    // Create a button input, and assign a ISR into its rising.
    gpio_init(BOARD_BUTTON_PIN);
    gpio_set_dir(BOARD_BUTTON_PIN, GPIO_IN);
    gpio_set_pulls(BOARD_BUTTON_PIN, false, true);
    /*
    * IRQs are used due to removing effect of bouncing of button.
    * Important note: As the SDK ver 1.3.0, the first parameter of the function
    * gpio_set_irq_enabled_with_callback() is not working. It means that we
    * have to put a "checking" condition statments in the ISR function to look
    * for which GPIO is calling the IRQ. Also, since processor of RP2040 can
    * handle only one interrupt, we can put only rising or falling. Not both!
    * If it will change in future, please uncomment the next code line.
    */
    // gpio_set_irq_enabled_with_callback(BOARD_BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true, &on_fall_button_board);
    gpio_set_irq_enabled_with_callback(BOARD_BUTTON_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_interrupt_handler);
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

    // Set the GPIOs.
    set_gpios();
    
    // Wait for 10 seconds, to power up the modem.
    sleep_ms(10000);

    // Create a repeating timer to send commands to TELIT.
    // struct repeating_timer random_send_modem;
    // add_repeating_timer_ms(2000, timer_callback_random_send_modem, NULL, &random_send_modem);
    
    while (true) {
        // Print the TELIT Modem (uart0) Response.
        if (is_message_finished) {
            printf("%s", uart0_buffer);
            // To start from the begining of the array, and catch new data.
            memset(uart0_buffer, '\0', sizeof(uart0_buffer));
            buffer_index = 0;
            is_message_finished = false;
        }     

        // Communicate between PC and Pico.
        if (is_board_button_clicked) {
            printf("> TELIT cmd: ");
            scanf("%64s", usb_buffer);
            
            // Add alarm to fire uart_puts due to higher priority.
            // add_alarm_in_ms(500, send_message_alarm, NULL, false);
            send_message_to_telit();

            // Assign the default value of variable, to let it get new commands.
            is_board_button_clicked = false;
        }

        tight_loop_contents();
    }
}