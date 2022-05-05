#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/time.h"

#include "hardware/uart.h"
#include "hardware/irq.h"


// To see detailed debug messages, uncomment the following line.
//#define DETAILED_PRINT

#define AIRCR_Register (*((volatile uint32_t*)(PPB_BASE + 0x0ED0C)))

/********   UART0 TELIT MODEM SETTINGS    ********/
#define TELIT_UART uart0
#define TELIT_UART_BAUDRATE 115200
#define TELIT_UART_DATABITS 8
#define TELIT_UART_STOPBITS 1
#define TELIT_UART_PARITY UART_PARITY_NONE
#define TELIT_UART_TX_PIN 0
#define TELIT_UART_RX_PIN 1
#define TELIT_UART_IRQ (TELIT_UART == uart0 ? UART0_IRQ : UART1_IRQ)
#define TELIT_BUFFER_SIZE 64

char            uart0_buffer[TELIT_BUFFER_SIZE];    // It holds the data coming from RX.
uint8_t         uart0_buffer_index = 0;             // It holds the index of the buffer of TELIT.
char            recieved_char = '\0';               // It holds the last char received.
volatile bool   is_message_finished = false;        // It is true when the message is finished with OK or ERROR.
const char      start_message[] = "AT";             // It is the start message of the TELIT.
const char      end_message[] = "\r\n";             // It is the end message of the TELIT.

// The ones on the Heap.
char* index_start;
char* index_end;
char* substr;
char* message_to_send;
/*************************************************/

/********      BOARD BUTTON SETTINGS      ********/
/*
#define BOARD_BUTTON_PIN 2
#define BOUNCING_DELAY 150000
#define USB_BUFFER_SIZE 256

volatile uint32_t   btn_rise_started_time = 0;          // It holds the time when the button is pressed.
volatile bool       is_board_button_clicked = false;    // It is true when the button is clicked.
char                usb_buffer[USB_BUFFER_SIZE];        // It holds the data to be sent to the USB.
uint16_t            usb_buffer_index = 0;               // It holds the index of the buffer of USB.
char                concat_buffer[USB_BUFFER_SIZE + 4]; // It holds the data to be sent to the USB with CRLF.
*/
/*************************************************/

/**********   Function Declarations    ***********/
void free_heap_usage();
/*void set_gpios();*/
/*void send_message_to_telit(char[]);*/
void set_telit_uart_ready();
char* create_message(char*);
bool check_signal_quality();
void process_signal_quailty();
uint8_t check_carrier_registration();
void process_carrier_registration();
uint8_t check_gprs_registration();
void process_gprs_registration();
bool check_gprs_attach();
void process_gprs_attach();
/*bool check_telit_ready();*/

//-- Timers

//-- Interrupts
void on_uart0_rx();
/*void gpio_interrupt_handler(uint, uint32_t);*/
/*************************************************/

int main(){
    /* 
    * Initialize the stdio from the USB/UART0.
    * Note that, CMakeLists file has to have
    * "pico_enable_stdio_uart(main 0)" to
    * not use UART channel for communication
    * between PC and Pico.
    */
    stdio_init_all();
    
    // Wait for the USB to be ready.
    sleep_ms(5000);

    // Inform the USB that the Pico is ready.
    printf("$> Welcome to the Pico!\n");
    
    // Set the UART0 ready for TELIT modem.
    set_telit_uart_ready();

    // Set the GPIOs.
    /*set_gpios();*/

    // Inform the GPIO and TELIT_UART is ready.
    printf("$> GPIO and UART setup completed.\n");

    // Wait to get a signal for a moment.
    sleep_ms(10000);

    // Signal Quailty Check.
    process_signal_quailty();

    sleep_ms(5000);

    // Clear the memory, and buffer.
    memset(uart0_buffer, 0, TELIT_BUFFER_SIZE);
    free_heap_usage();

    // Carrier Registration Check.
    process_carrier_registration();

    // Clear the memory, and buffer.
    memset(uart0_buffer, 0, TELIT_BUFFER_SIZE);
    free_heap_usage();
    
    // GPRS Registration Check.
    process_gprs_registration();

    // Clear the memory, and buffer.
    memset(uart0_buffer, 0, TELIT_BUFFER_SIZE);
    free_heap_usage();

    // GPRS Attach Check.
    process_gprs_attach();

    /* INFINITE LOOP */
    while (true) tight_loop_contents();
}

uint8_t check_gprs_registration() {
    #ifdef DETAILED_PRINT
        // Inform the carrier registration is being checked.
        printf("\n====START check_gprs_registration() START====\n");
    #endif
    
    // Create command to send it.
    char command_message[] = "+CGREG?";
    char return_message[] = "+CGREG";
    message_to_send = create_message(command_message);

    // Send the command to the TELIT.
    is_message_finished = false;
    if (uart_is_writable(TELIT_UART)) {
        int index = 0;
        while (index != strlen(message_to_send)) {
            uart_putc_raw(TELIT_UART, message_to_send[index]);
            index++;
        }
    }

    #ifdef DETAILED_PRINT
        printf("-- message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(5000);

    index_start = strstr(uart0_buffer, command_message);

    if (index_start != NULL) {
        index_start = strstr(index_start + strlen(command_message), return_message);
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");

        char answer_look_like[] = "+CGREG: 0,5";
        substr = malloc(sizeof(answer_look_like));
        memset(substr, '\0', sizeof(answer_look_like));
        
        strncpy(substr, index_start, sizeof(answer_look_like));
        
        #ifdef DETAILED_PRINT
            printf("-- returned message: %s", substr);
        #endif

        int gprs_reg_status = atoi(substr + strlen(answer_look_like) - 1);

        #ifdef DETAILED_PRINT
            printf("\n-- RESULT: gprs registration=%d", gprs_reg_status);
            printf("\n====END check_gprs_registration() END====\n");
        #endif

        return gprs_reg_status;
    }

    return 0;
}

void process_gprs_registration() {
    uint8_t is_gr_set;

    // Try to get GPRS registration for 20 times.
    for (int try = 0; try < 20; try++) {
        printf("$> Checking GPRS registration... (%d)\n", try+1);
        is_gr_set = check_gprs_registration();
        // If it is good, exit from the loop.
        if (is_gr_set == 0 || is_gr_set == 1 || is_gr_set == 5) break;
        else if (is_gr_set == 2) {
            printf("\n$> Waiting for 5 second.\n");
            sleep_ms(5000);
        }
        else if (is_gr_set == 3) {
            printf("\n$> ERROR: Return [3]. Not Implemented.\n");
            printf("\n$> GRPS registration check not completed.\n");
            return;
        }
    }

    printf("\n$> GRPS registration check completed.\n");
}

bool check_gprs_attach() {
    #ifdef DETAILED_PRINT
        // Inform the  signal quality is being checked.
        printf("\n====START check_gprs_attach() START====\n");
    #endif

    // Create command to send it.
    char command_message[] = "+CGATT?";
    char return_message[] = "+CGATT";
    message_to_send = create_message(command_message);

    // Send the command to the TELIT.
    is_message_finished = false;
    if (uart_is_writable(TELIT_UART)) {
        int index = 0;
        while (index != strlen(message_to_send)) {
            uart_putc_raw(TELIT_UART, message_to_send[index]);
            index++;
        }
    }

    #ifdef DETAILED_PRINT
        printf("-- message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(5000);
    
    index_start = strstr(uart0_buffer, command_message);

    if (index_start != NULL) {
        index_start = strstr(index_start + strlen(command_message), return_message);
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");

        char answer_look_like[] = "+CGATT: 1";
        substr = malloc(sizeof(answer_look_like));
        memset(substr, '\0', sizeof(answer_look_like));
        
        strncpy(substr, index_start, sizeof(answer_look_like));

        #ifdef DETAILED_PRINT
            printf("-- returned message: %s", substr);
        #endif
        
        uint8_t grps_attach_status = atoi(substr + strlen(answer_look_like) - 1);
        
        #ifdef DETAILED_PRINT
            printf("\n-- RESULT: gprs attach status=%d", grps_attach_status);
            printf("\n====END check_gprs_attach() END====\n");
        #endif

        if (grps_attach_status == 1) return false;
    }
    
    return true;
}

void process_gprs_attach() {
    uint8_t is_ga_set;

    printf("$> Checking GPRS attach...\n");
    is_ga_set = !check_gprs_attach();
    if (is_ga_set) {
        printf("\n$> GPRS attach check completed.\n");
    }
    else {
        printf("\n$> ERROR: Not Implemented.\n");
        printf("\n$> GPRS attach check not completed.\n");
    }
}

void process_carrier_registration() {
    uint8_t is_cr_set;

    // Try to get signal quality for 30 times, if it is bad, reboot it.
    for (int try = 0; try < 30; try++) {
        printf("$> Checking carrier registration... (%d)\n", try+1);
        is_cr_set = check_carrier_registration();
        // If it is good, exit from the loop.
        if (is_cr_set == 3 || is_cr_set == 5) break;
        else if (is_cr_set == 2) {
            printf("\n$> Waiting for 10 second.\n");
            sleep_ms(10000);
        }
        else if (is_cr_set == 0 || is_cr_set == 3) {
            printf("\n$> ERROR: Return [0 or 3]. Not Implemented.\n");
        }
    }

    printf("\n$> Carrier registration check completed.\n");
}

uint8_t check_carrier_registration() {
    #ifdef DETAILED_PRINT
        // Inform the carrier registration is being checked.
        printf("\n====START check_carrier_registration() START====\n");
    #endif
    
    // Create command to send it.
    char command_message[] = "+CREG?";
    char return_message[] = "+CREG";
    message_to_send = create_message(command_message);

    // Send the command to the TELIT.
    is_message_finished = false;
    if (uart_is_writable(TELIT_UART)) {
        int index = 0;
        while (index != strlen(message_to_send)) {
            uart_putc_raw(TELIT_UART, message_to_send[index]);
            index++;
        }
    }

    #ifdef DETAILED_PRINT
        printf("-- message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(5000);

    index_start = strstr(uart0_buffer, command_message);

    if (index_start != NULL) {
        index_start = strstr(index_start + strlen(command_message), return_message);
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");

        char answer_look_like[] = "+CREG: 0,5";
        substr = malloc(sizeof(answer_look_like));
        memset(substr, '\0', sizeof(answer_look_like));
        
        strncpy(substr, index_start, sizeof(answer_look_like));
        
        #ifdef DETAILED_PRINT
            printf("-- returned message: %s", substr);
        #endif

        int carrier_reg_status = atoi(substr + strlen(answer_look_like) - 1);

        #ifdef DETAILED_PRINT
            printf("\n-- RESULT: signal quality=%d", carrier_reg_status);
            printf("\n====END check_carrier_registration() END====\n");
        #endif

        return carrier_reg_status;
    }

    return 0;
}

/**
 * @brief This function does the automation of 
 * checking signal quality. It tries for 10 times,
 * and if nothing, it reboots the Pico.
 */
void process_signal_quailty() {
    bool is_sq_good = false;

    // Try to get signal quality for 5 times, if it is bad, reboot it.
    for (int try = 0; try < 5; try++) {
        printf("$> Checking signal quality... (%d)\n", try+1);
        is_sq_good = !check_signal_quality();
        // If it is good, exit from the loop.
        if (is_sq_good) break;
        printf("\n$> Waiting for 10 second.\n");
        sleep_ms(5000);
    }
    
    if (!is_sq_good) {
        printf("\n$> Signal quality is bad.\n$> Please check the antenna.\n");
        printf("\n$> Pico will be reboot in 5 seconds.\n");
        // Let the Pico to sleep for 5 seconds to show the information to user.
        sleep_ms(5000);
        // Reboot the Pico.
        AIRCR_Register = 0x5FA0004;
    } else
        printf("\n$> Signal quality is good.\n");
}

/**
 * @brief Function checks the signal quality of the TELIT modem. If it is below than 70,
 * returns False. Otherwise, returns True.
 * 
 * @return true
 * @return false 
 */
bool check_signal_quality() {
    #ifdef DETAILED_PRINT
        // Inform the  signal quality is being checked.
        printf("\n====START check_signal_quailty() START====\n");
    #endif

    // Create command to send it.
    char command_message[] = "+CSQ";
    message_to_send = create_message(command_message);

    // Send the command to the TELIT.
    is_message_finished = false;
    if (uart_is_writable(TELIT_UART)) {
        int index = 0;
        while (index != strlen(message_to_send)) {
            uart_putc_raw(TELIT_UART, message_to_send[index]);
            index++;
        }
    }

    #ifdef DETAILED_PRINT
        printf("-- message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(5000);
    
    index_start = strstr(uart0_buffer, command_message);

    if (index_start != NULL) {
        index_start = strstr(index_start + strlen(command_message), command_message);
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");

        char answer_look_like[] = "+CSQ: 76,8";
        substr = malloc(sizeof(answer_look_like));
        memset(substr, '\0', sizeof(answer_look_like));
        
        strncpy(substr, index_start, sizeof(answer_look_like));

        #ifdef DETAILED_PRINT
            printf("-- returned message: %s", substr);
        #endif
        
        int signal_quality = atoi(substr + strlen(command_message) + 1);
        
        #ifdef DETAILED_PRINT
            printf("\n-- RESULT: signal quality=%d", signal_quality);
            printf("\n====END check_signal_quailty() END====\n");
        #endif

        if (signal_quality < 70 && signal_quality > 0) return false;
    }
    
    return true;
}

/**
 * @brief Create a message object which includes "AT" on the front,
 * CR+LF on the end.
 * 
 * @param command: The command to be sent to the TELIT modem. (After AT part.) 
 * @return char* 
 */
char* create_message(char* command) {
    char* message_to_return = malloc(sizeof(char) * (strlen(command) + strlen(start_message) + strlen(end_message) + 1));
    memset(message_to_return, '\0', sizeof(char) * (strlen(command) + strlen(start_message) + strlen(end_message) + 1));

    int message_index = 0;
    int index = 0;
    while (start_message[index] != '\0'){
        message_to_return[message_index] = start_message[index];
        message_index++;
        index++;
    }
    index = 0;
    while (command[index] != '\0'){
        message_to_return[message_index] = command[index];
        message_index++;
        index++;
    }
    index = 0;
    while (end_message[index] != '\0'){
        message_to_return[message_index] = end_message[index];
        message_index++;
        index++;
    }
    
    return message_to_return;
}



// /**
//  * @brief Initilize the GPIOs, set their directions,
//  * and assigns them IRQs.
//  * 
//  */
// void set_gpios() {
//     // Create a button input, and assign a ISR into its rising.
//     gpio_init(BOARD_BUTTON_PIN);
//     gpio_set_dir(BOARD_BUTTON_PIN, GPIO_IN);
//     gpio_set_pulls(BOARD_BUTTON_PIN, false, true);
//     /*
//     * IRQs are used due to removing effect of bouncing of button.
//     * Important note: As the SDK ver 1.3.0, the first parameter of the function
//     * gpio_set_irq_enabled_with_callback() is not working. It means that we
//     * have to put a "checking" condition statments in the ISR function to look
//     * for which GPIO is calling the IRQ. Also, since processor of RP2040 can
//     * handle only one interrupt, we can put only rising or falling. Not both!
//     * If it will change in future, please uncomment the next code line.
//     */
//     // gpio_set_irq_enabled_with_callback(BOARD_BUTTON_PIN, GPIO_IRQ_EDGE_FALL, true, &on_fall_button_board);
//     gpio_set_irq_enabled_with_callback(BOARD_BUTTON_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_interrupt_handler);
// }


/**
 * @brief Initilization of the TELIT modem's UART.
 */
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
    // irq_set_priority(TELIT_UART_IRQ, 0);
    uart_set_irq_enables(TELIT_UART, true, false);
}

void free_heap_usage() {
    free(substr);
    free(message_to_send);
}

/**********   Timer Calback Routines    **********/
/*************************************************/

/********   Interrupt Services Routines    ********/
void on_uart0_rx() {
    // If the UART channel is readable, read it.
    /*printf("$> Recieved data: ");*/
    if (uart_is_readable(TELIT_UART)) {
        recieved_char = uart_getc(TELIT_UART);
        if (recieved_char != 0xff) {
            uart0_buffer[uart0_buffer_index] = recieved_char;
            /*printf("%x (%c) \n", recieved_char, recieved_char);*/
            uart0_buffer_index++;
        }
    }

    if (strstr(uart0_buffer, "\r\nOK\r\n") != NULL || strstr(uart0_buffer, "\r\nERROR\r\n") != NULL) {
        /*printf("$> Message finished.\n");*/
        is_message_finished = true;
        uart0_buffer_index = 0;
    }

    //printf("\n$> Buffer: %s\n", uart0_buffer);
}

/*
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
*/
/*************************************************/