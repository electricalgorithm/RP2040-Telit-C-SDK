#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/runtime.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include "hardware/uart.h"
#include "hardware/irq.h"


// To see detailed debug messages, uncomment the following line.
#define DETAILED_PRINT
#define PICO_MALLOC_PANIC 1
#define PICO_DEBUG_MALLOC 1

// Registers
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
#define TELIT_BUFFER_SIZE 128
#define TELIT_MSG_WAIT_MS 5000

char            uart0_buffer[TELIT_BUFFER_SIZE];    // It holds the data coming from RX.
uint16_t        uart0_buffer_index = 0;             // It holds the index of the buffer of TELIT.
char            recieved_char = '\0';               // It holds the last char received.
volatile bool   is_message_finished = false;        // It is true when the message is finished with OK or ERROR.
const char      start_message[] = "AT";             // It is the start message of the TELIT.
const char      end_message[] = "\r\n";             // It is the end message of the TELIT.

// The ones on the Heap.
char* index_start;
char* index_end;
uint16_t* ip_address;
/*************************************************/

/********      BOARD BUTTON SETTINGS      ********/
#define BOARD_BUTTON_PIN 2
#define BOUNCING_DELAY 150000
#define USB_BUFFER_SIZE 256

volatile uint32_t   btn_rise_started_time = 0;          // It holds the time when the button is pressed.
volatile bool       is_board_button_clicked = false;    // It is true when the button is clicked.
char                usb_buffer[USB_BUFFER_SIZE];        // It holds the data to be sent to the USB.
uint16_t            usb_buffer_index = 0;               // It holds the index of the buffer of USB.
char                concat_buffer[USB_BUFFER_SIZE + 4]; // It holds the data to be sent to the USB with CRLF.
/*************************************************/

/**********   Function Declarations    ***********/
void reboot_pico();
/*void free_heap_usage(uint8_t);*/
/*void prepare_for_next(uint8_t);*/
void set_gpios();

// TELIT
void send_message_to_telit(char[]);
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
bool define_apn();
bool activate_pdp();
void telit_init_3g();
/*bool check_telit_ready();*/

// MQTT
bool mqtt_enable_and_configure(bool, char[], char[]);
uint8_t mqtt_login(char[], char[], char[]);
bool mqtt_logout();
bool mqtt_subscribe_topic(char[]);
bool mqtt_publish(char[], char[]);
char* mqtt_read(uint8_t);
char* mqtt_read_in_queue();
uint8_t mqtt_new_message_count();
bool process_mqtt_login(char[], char[], char[]);
bool process_mqtt_enable(bool, char[], char[]);

//-- Timers
bool repeating_timer_callback(struct repeating_timer *t);
char* _timer_msg = NULL;
bool _check_read_timer = false;

//-- Interrupts
void on_uart0_rx();
void gpio_interrupt_handler(uint, uint32_t);
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
    set_gpios();

    // Inform the GPIO and TELIT_UART is ready.
    printf("$> GPIO and UART setup completed.\n");

    // Wait to get a signal for a moment.
    sleep_ms(10000);

    // Initilization of the TELIT mode.
    telit_init_3g();

    // Enable and set the MQTT.
    if (!process_mqtt_enable(false, "mqtt3.thingspeak.com", "1883")) {
        // Login to the MQTT broker.
        process_mqtt_login("NzsNHSIHKy40Jx8AKSIfHg8", "NzsNHSIHKy40Jx8AKSIfHg8", "WIr4lWLSISAqwDL8fOuRp0Nk");

        // Subscribe to the topic.
        bool status = mqtt_subscribe_topic("channels/1708249/subscribe/fields/field1");
        if (!status)
            printf("$> Subscribed to the topic.\n");
        else {
            printf("$> Failed to subscribe to the topic.\n");
            printf("$> Rebooting the Pico in 3 seconds.\n");
            sleep_ms(3000);
            reboot_pico();
        }

        // Publish to the topic.
        mqtt_publish("channels/1708249/publish", "field1=500&status=MQTTPUBLISH");

        printf("$> Checking message queue in MQTT Broker...\n");
        uint8_t new_msg = mqtt_new_message_count();
        if (new_msg == 60) {
            printf("$> Something got wrong with the MQTT Broker.\n");
            printf("$> Rebooting the Pico in 3 seconds.");
            sleep_ms(3000);
            reboot_pico();
        } else {
            printf("$> There are %d messages in queue.\n", new_msg);
        }

        printf("$> Reading first message from MQTT Broker...\n");
        mqtt_read_in_queue();
        
        printf("$> Publishing to the topic... Value: 11\n");
        mqtt_publish("channels/1708249/publish", "field1=0&status=MQTTPUBLISH");

        printf("$> Reading second message from MQTT Broker...\n");
        mqtt_read_in_queue();

        printf("$> Publishing to the topic... Value: 250\n");
        mqtt_publish("channels/1708249/publish", "field1=26&status=MQTTPUBLISH");

        printf("$> Checking message queue in MQTT Broker...\n");
        new_msg = mqtt_new_message_count();
        printf("$> There are %d messages in queue.\n", new_msg);

        /*printf("$> Reading second message from MQTT Broker...\n");
        msg = mqtt_read(2);
        printf("$> The second message is: %s\n", msg);
        prepare_for_next(1);
        free(msg);*/

        printf("$> Reading third message from MQTT Broker...\n");
        mqtt_read_in_queue();

        // Logout from the MQTT broker.
        mqtt_logout();
    }
    
    // Create the timer for getting input every 10 seconds.
    /*
    struct repeating_timer timer;
    add_repeating_timer_ms(10000, repeating_timer_callback, NULL, &timer);
    */

    /* INFINITE LOOP */
    while (true) {
        
        // If timer runt.
        if (_check_read_timer) {
             printf("$> Timer: Getting last message if there is.");
            if (_timer_msg  != NULL) free(_timer_msg);
            
            uint8_t message_count = mqtt_new_message_count();
            printf("$> ~ MSG CNT: %d", message_count);
            
            if (message_count != 60) {
                _timer_msg = mqtt_read_in_queue();
                printf("$> ~ MSG: %s", _timer_msg);
                free(_timer_msg);
            }

            _check_read_timer = false;
        }

        if (is_board_button_clicked) {
            printf("> TELIT cmd: ");
            
            // Get the command from the PC.
            char last_char = getchar_timeout_us(5000);
            while (last_char != 0x0A && last_char != 0x0D) {

                // If it is not timeout, then add it to the buffer.
                if (last_char != 0xFF) {
                    usb_buffer[usb_buffer_index] = last_char;
                    usb_buffer_index++;
                }
                // Get new one.
                last_char = getchar_timeout_us(5000);
            }

            usb_buffer[usb_buffer_index] = '\0';

            // Send the message to TELIT.
            printf("\n>$ Send the usb buffer to TELIT. Message: %s", usb_buffer);
            send_message_to_telit(usb_buffer);
            // Clear the buffer.
            memset(usb_buffer, '\0', sizeof(usb_buffer));
            usb_buffer_index = 0;
            
            // Assign the default value of variable, to let it get new commands.
            is_board_button_clicked = false;
        }

        tight_loop_contents();
    }
}

uint8_t mqtt_new_message_count() {
    #ifdef DETAILED_PRINT
        printf("\n==== mqtt_new_message_count() ====\n");
    #endif

    char command[] = "#MQREAD?";
    char response[] = "MQREAD: 1,";
    
    send_message_to_telit(command);

    #ifdef DETAILED_PRINT
        printf("-- message count request sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);

    // Check if the returned message is belongs to our command.
    index_start = strstr(uart0_buffer, response);
    if (index_start != NULL) { 

        // Check if there is a OK signal.
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");
        
        // Save it as a variable to use it later.
        uint8_t message_count = index_start[strlen(response)] - '0';

        #ifdef DETAILED_PRINT
            printf("-- RESULT: message count %d\n", message_count);
            printf("==== mqtt_new_message_count() ====\n\n");
        #endif

        if (index_end == NULL) return 60;
        else return message_count;
    }
    
    #ifdef DETAILED_PRINT
        printf("==== mqtt_new_message_count() ====\n\n");
    #endif
    
    return 60; // 60 is the error code.
}

char* mqtt_read_in_queue() {
    #ifdef DETAILED_PRINT
        printf("\n====== mqtt_read_in_queue() ======\n");
    #endif

    char command[] = "#MQREAD=1,1";
    char response[] = "#MQREAD: 1,";

    // Send command to the server.
    send_message_to_telit(command);

    #ifdef DETAILED_PRINT
        printf("-- first message request sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);

    /*printf("-- buff %s", uart0_buffer);*/

    // Check if the returned message is belongs to our command.
    index_start = strstr(uart0_buffer, response);
    /*printf("-- index start %s", index_start);*/
    if (index_start != NULL) {
        index_start = index_start + strlen(response);
        
        // Find the data size of response.
        char* delimeter = strstr(index_start, ",");
        char* data_size_crlr = strstr(delimeter, "\r\n");
        
        // Get the data size from the response.
        char* data_size = (char *) malloc(sizeof(char) * (data_size_crlr - delimeter));
        memset(data_size, '\0', sizeof(char) * (data_size_crlr - delimeter));
        strncpy(data_size, delimeter + 1, data_size_crlr - delimeter - 1);
        
        // Conversion to int.
        uint32_t data_size_int = atoi(data_size);
        free(data_size);

        // Check if there is a OK signal.
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");
        if (index_end == NULL) return "ERROR";

        // Get the index of the message start.
        char* index_of_message = strstr(index_start, "<") + (sizeof(char) * (data_size_int + 3));
        /*printf("index_of_message %s\n", index_of_message);*/

        /*printf("message_to_send size: %d\n", sizeof(char) * (data_size_int + 1));*/
        char* message = (char *) malloc(sizeof(char) * (data_size_int + 1));
        memset(message, '\0', sizeof(char) * (data_size_int + 1));
        strncpy(message, index_of_message, data_size_int);

        #ifdef DETAILED_PRINT
            printf("-- RESULT: message databits: %d\n", data_size_int);
            printf("-- RESULT: message came: %s\n", message);
            printf("==== mqtt_read_in_queue() ====\n\n");
        #endif

        free(message);

        return "";
    }
    
    #ifdef DETAILED_PRINT
        printf("==== mqtt_read_in_queue() ====\n\n");
    #endif
}

char* mqtt_read(uint8_t order) {
    #ifdef DETAILED_PRINT
        printf("\n====== mqtt_read() ======\n");
    #endif

    uint8_t msg_count = 3/*mqtt_new_message_count()*/;
    /*prepare_for_next(1);*/

    // printf("-- message count: %d\n", msg_count);

    if (msg_count >= order) {

        char command[] = "#MQREAD=1,2";
        char response[] = "MQREAD: 1,";

        // Convert the order to string.
        //char order_num[2];
        //sprintf(order_num, "%d", order);
        
        //printf("-- order: %d\n-- order_num: %s\n", order, order_num);

        // Create the message space in the heap.
        //char* message = malloc(sizeof(char) * (strlen(command) + strlen(order_num) + 1));
        //memset(message, '\0', sizeof(char) * (strlen(command) + strlen(order_num) + 1));

        // Concat the number of the order, and the prefix of command.
        //strcat(message, command);
        //strcat(message, order_num);

        // Send command to the server.
        // send_message_to_telit(message);
        is_message_finished = false;
        if (uart_is_writable(TELIT_UART)) {
            int index = 0;
            while (index != strlen(command)) {
                uart_putc_raw(TELIT_UART, command[index]);
                index++;
            }
        }

        #ifdef DETAILED_PRINT
            //printf("-- %s. message request sent to modem.\n", order_num);
            // Wait a little bit to recieve message.
            printf("-- waiting 5 seconds.\n");
        #endif

        sleep_ms(5*TELIT_MSG_WAIT_MS);

        printf("-- buffer: %s", uart0_buffer);

        // Check if the returned message is belongs to our command.
        index_start = strstr(uart0_buffer, response);
        if (index_start != NULL) {

            index_start = index_start + strlen(response);
            
            // Find the data size of response.
            char* delimeter = strstr(index_start, ",");
            char* data_size_crlr = strstr(delimeter, "\r\n");
    
            char* data_size = malloc(sizeof(char) * (data_size_crlr - delimeter));
            memset(data_size, '\0', sizeof(char) * (data_size_crlr - delimeter));
            strncpy(data_size, delimeter + 1, data_size_crlr - delimeter - 1);
            uint32_t data_size_int = atoi(data_size);

            // Check if there is a OK signal.
            index_end = strstr(uart0_buffer, "\r\nOK\r\n");

            // Get the index of the message start.
            char* index_of_message = strstr(index_start, "<") + (sizeof(char) * (data_size_int + 3));

            char* message_to_send = (char*) malloc(sizeof(char) * (data_size_int + 1));
            memset(message_to_send, '\0', sizeof(char) * (data_size_int + 1));
            strncpy(message_to_send, index_of_message, data_size_int);

            #ifdef DETAILED_PRINT
                printf("-- RESULT: message databits: %d\n", data_size_int);
                printf("-- RESULT: message came: %s\n", message_to_send);
                printf("==== mqtt_read() ====\n\n");
            #endif

            free(data_size);
            //free(message);
            if (index_end == NULL) return "ERROR";
            else return message_to_send;
        }
        
        #ifdef DETAILED_PRINT
            printf("==== mqtt_read() ====\n\n");
        #endif

    } else {
        
        // No message to read.
        printf("$> Not enough new message.\n");
        #ifdef DETAILED_PRINT
            printf("==== mqtt_new_message_count() ====\n\n");
        #endif
        
        return NULL;
    }
}

bool mqtt_logout() {
    #ifdef DETAILED_PRINT
        // Inform the function entrance.
        printf("\n======= mqtt_logout() =======\n");
    #endif

    // Create command to send it.
    char command_message[] = "#MQDISC=1";
    send_message_to_telit(command_message);

    #ifdef DETAILED_PRINT
        printf("-- logout message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);

    // Check if the returned message is belongs to our command.
    index_start = strstr(uart0_buffer, command_message);
    if (index_start != NULL) {

        // Check if there is a OK signal.
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");
        // If there is no OK, then something got wrong.
        if (index_end == NULL) return true;
        
        #ifdef DETAILED_PRINT
            printf("-- RESULT: mqtt logged out is %s\n", (index_end != NULL) ? "performed" : "failed");
            printf("======= mqtt_logout() =======\n\n");
        #endif

        return false;
    }

    #ifdef DETAILED_PRINT
        printf("======= mqtt_logout() =======\n\n");
    #endif

    return true;
}

bool process_mqtt_enable(bool will, char server_address[], char server_port[]) {
    printf("$> MQTT is enabling, and setting up...\n");

    // Try 3 times to enable, and set the MQTT.
    for (int try = 0; try < 3; try++) {
        if (!mqtt_enable_and_configure(will, server_address, server_port)) {
            printf("$> MQTT is enabled and setted.\n");
            return false;
        } else {
            printf("$> MQTT is failed to enable.\n");
            // If it couldn't achieve on the tryings, reboot Pico.
            if (try == 2) {
                printf("$> MQTT enabling couldn't completed.\n");
                printf("$> Pico will be reboot in 3 seconds.\n");
                // Let the Pico to sleep for 3 seconds to show the information to user.
                sleep_ms(3000);
                // Reboot the Pico.
                reboot_pico();
            }

            sleep_ms(TELIT_MSG_WAIT_MS);
        }
    }

    return true;
}

bool process_mqtt_login(char client_id[], char user_name[], char password[]){
    uint8_t status_code = 0;

    // Try to connect for 3 times.
    for (uint8_t try = 0; try < 3; try++) {
        // Inform the user.
        printf("$> Logging into the MQTT broker... (%d)\n", try + 1);
        
        // Get the status code.
        status_code = mqtt_login(client_id, user_name, password);
        
        // If it is not 1, it is not connected.
        if (status_code == 1) {
            printf("$> Logged in to the MQTT broker.\n");
            break;

        } else {
            printf("$> Failed to login to the MQTT broker, trying again.\n");
            mqtt_logout();

            if (try == 2) {
                printf("$> MQTT login couldn't completed.\n");
                printf("$> Pico will be reboot in 3 seconds.\n");
                // Let the Pico to sleep for 3 seconds to show the information to user.
                sleep_ms(3000);
                // Reboot the Pico.
                reboot_pico();
            }

        }
    }
}

bool mqtt_enable_and_configure(bool last_will, char server_address[], char server_port[]) {        
    /************************** ENABLING MQTT ****************************/
    #ifdef DETAILED_PRINT
        // Inform the function entrance.
        printf("\n==== mqtt_enable_and_configure() ====\n");
    #endif

    // Create command to send it.
    char command_message_enable[] = "#MQEN=1,1";
    send_message_to_telit(command_message_enable);

    #ifdef DETAILED_PRINT
        printf("-- enable message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);

    // Check if the returned message is belongs to our command.
    index_start = strstr(uart0_buffer, command_message_enable);
    if (index_start != NULL) {

        // Check if there is a OK signal.
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");
        
        #ifdef DETAILED_PRINT
            printf("-- RESULT: mqtt has %s\n", (index_end != NULL) ? "enabled" : "error");
        #endif

        // If there is no OK, then something got wrong.
        if (index_end == NULL) return true;
    }


    /************************** LAST-WILL SET ****************************/
    // Create the message, and send it.
    char command_message_lastwill[] = "#MQWCFG=1,0";
    command_message_lastwill[10] = (last_will) ? '1' : '0';
    send_message_to_telit(command_message_lastwill);

    #ifdef DETAILED_PRINT
        printf("-- last will setting message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);

    // Check if the returned message is belongs to our command.
    index_start = strstr(uart0_buffer, command_message_lastwill);
    if (index_start != NULL) {

        // Check if there is a OK signal.
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");

        #ifdef DETAILED_PRINT
            printf("-- RESULT: last will is %s\n", (index_end != NULL) ? "setted" : "error");
        #endif

        // If there is no OK, then something got wrong.
        if (index_end == NULL) return true;
    }

    /************************** SERVER SET ****************************/
    const char prefix[] = "#MQCFG=1,";
    const char midfix[] = ",";
    const char postfix[] = ",1";

    // Create a heap memory for the message concating.
    char* concat_message = malloc(sizeof(char) * (strlen(prefix) + strlen(server_address) + strlen(midfix) + strlen(server_port) + strlen(postfix) + 1));
    memset(concat_message, '\0', sizeof(char) * (strlen(prefix) + strlen(server_address) + strlen(midfix) + strlen(server_port) + strlen(postfix) + 1));

    // Concat the message.
    strcat(concat_message, prefix);
    strcat(concat_message, server_address);
    strcat(concat_message, midfix);
    strcat(concat_message, server_port);
    strcat(concat_message, postfix);

    send_message_to_telit(concat_message);

    #ifdef DETAILED_PRINT
        printf("-- server setting message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);

    // Check if the returned message is belongs to our command.
    index_start = strstr(uart0_buffer, concat_message);
    if (index_start != NULL) {
        
        // Free the memory.
        free(concat_message);

        // Check if there is a OK signal.
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");

        #ifdef DETAILED_PRINT
            printf("-- RESULT: server settings is %s\n", (index_end != NULL) ? "setted" : "error");
        #endif

        // If there is no OK, then something got wrong.
        if (index_end == NULL) {
            return true;
        }

        #ifdef DETAILED_PRINT
            printf("==== mqtt_enable_and_configure() ====\n\n");
        #endif

        return false;
    }

    // Free the concat message, since it won't be used anymore.
    free(concat_message);
    
    #ifdef DETAILED_PRINT
        printf("\n==== mqtt_enable_and_configure() ====\n\n");
    #endif

    return true;
}

uint8_t mqtt_login(char client_id[], char user_name[], char password[]) {
    #ifdef DETAILED_PRINT
        printf("\n======= mqtt_login() =======\n");
    #endif

    /********************* SENDING USER INFO **********************/
    const char prefix[] = "#MQCONN=1,";
    const char midfix[] = ",";

    // Create a heap memory for the message concating.
    char* concat_message = (char *) malloc(sizeof(char) * (strlen(prefix) + strlen(client_id) + strlen(midfix) + strlen(user_name) + strlen(midfix) + strlen(password) + 1));
    memset(concat_message, '\0', sizeof(char) * (strlen(prefix) + strlen(client_id) + strlen(midfix) + strlen(user_name) + strlen(midfix) + strlen(password) + 1));

    // Concate the message.
    strcat(concat_message, prefix);
    strcat(concat_message, client_id);
    strcat(concat_message, midfix);
    strcat(concat_message, user_name);
    strcat(concat_message, midfix);
    strcat(concat_message, password);

    // Send it to TELIT.
    send_message_to_telit(concat_message);

    #ifdef DETAILED_PRINT
        printf("-- login details sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 10 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS * 2);

    free(concat_message);
    
    /********************* SENDING CONFIRM **********************/
    char confirm_message[] = "#MQCONN?";
    const char confirm_prefix[] = "#MQCONN: 1,";
    send_message_to_telit(confirm_message);

    #ifdef DETAILED_PRINT
        printf("-- confirmation request sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);

    // Check if the returned message is belongs to our command.
    index_start = strstr(uart0_buffer, confirm_prefix);
    if (index_start != NULL) { 

        // Check if there is a OK signal.
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");
        if (index_end == NULL) return 60;
        
        // Save it as a variable to use it later.
        uint8_t status_code = index_start[strlen(confirm_prefix)] - '0';

        #ifdef DETAILED_PRINT
            printf("-- RESULT: status code %d\n", status_code);
            printf("======= mqtt_login() =======\n\n");
        #endif

        return status_code;
    }
    
    #ifdef DETAILED_PRINT
        printf("======= mqtt_login() =======\n\n");
    #endif
    
    return 60; // 60 is the error code.
}

bool mqtt_subscribe_topic(char topic_subscribe_address[]) {
    #ifdef DETAILED_PRINT
        printf("\n==== mqtt_subscribe_topic() ====\n");
    #endif

    const char prefix[] = "#MQSUB=1,";
    const char confirm[] = "#MQSUB";

    // Create a heap memory for the message concating.
    char* concat_message = (char *) malloc(sizeof(char) * (strlen(prefix) + strlen(topic_subscribe_address) + 1));
    memset(concat_message, '\0', sizeof(char) * (strlen(prefix) + strlen(topic_subscribe_address) + 1));

    // Concate the message.
    strcat(concat_message, prefix);
    strcat(concat_message, topic_subscribe_address);

    // Send it to TELIT.
    send_message_to_telit(concat_message);
    free(concat_message);

    #ifdef DETAILED_PRINT
        printf("-- subscription request sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);

    // Check if the returned message is belongs to our command.
    index_start = strstr(uart0_buffer, confirm);
    if (index_start != NULL) { 

        // Check if there is a OK signal.
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");

        #ifdef DETAILED_PRINT
            printf("-- RESULT:  %s\n", (index_end != NULL) ? "subscribed" : "error");
            printf("==== mqtt_subscribe_topic() ====\n\n");
        #endif
        
        if (index_end == NULL) return true;
        else return false;
    }

    printf("==== mqtt_subscribe_topic() ====\n\n");
    return true;
}

bool mqtt_publish(char topic_publish_address[], char string_to_publish[]) {
    #ifdef DETAILED_PRINT
        printf("\n======= mqtt_publish() =======\n");
    #endif

    const char prefix[] = "#MQPUBS=1,";
    const char midfix[] = ",0,0,";

    // Create a heap memory for the message concating.
    char* concat_message = (char *) malloc(sizeof(char) * (strlen(prefix) + strlen(topic_publish_address) + strlen(midfix) + strlen(string_to_publish) + 1));
    memset(concat_message, '\0', sizeof(char) * (strlen(prefix) + strlen(topic_publish_address) + strlen(midfix) + strlen(string_to_publish) + 1));

    // Concate the message.
    strcat(concat_message, prefix);
    strcat(concat_message, topic_publish_address);
    strcat(concat_message, midfix);
    strcat(concat_message, string_to_publish);

    // Send it to TELIT.
    send_message_to_telit(concat_message);
    free(concat_message);

    #ifdef DETAILED_PRINT
        printf("-- publish request sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);

    // Check if the returned message is belongs to our command.
    index_start = strstr(uart0_buffer, prefix);
    if (index_start != NULL) { 

        // Check if there is a OK signal.
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");

        #ifdef DETAILED_PRINT
            printf("-- RESULT:  The message has %s\n", (index_end != NULL) ? "sent." : "not sent.");
            printf("======= mqtt_publish() =======\n\n");
        #endif
        
        if (index_end == NULL) return true;
        else return false;
    }

    printf("======= mqtt_publish() =======\n\n");
    return true;
}

/**
 * @brief The function checks and sets everything, and connect the TELIT into 3G network.
 * 
 */
void telit_init_3g() {
    // Signal Quailty Check.
    process_signal_quailty();

    // Carrier Registration Check.
    process_carrier_registration();

    // GPRS Registration Check.
    process_gprs_registration();

    // GPRS Attach Check.
    process_gprs_attach();

    // Define APN.
    printf("$> Defining APN...\n");
    bool is_apn_ready = !define_apn();
    if (is_apn_ready)
        printf("$> APN definition success.\n");
    else
        printf("$> APN definition failed.\n");

    // Activate PDP Context.
    printf("$> Activating PDP context...\n");
    bool is_pdp_ready = !activate_pdp();
    if (is_pdp_ready)
        printf("$> PDP context activated.\n");
    else
        printf("$> PDP context couldn't activated.\n");
}

/**
 * @brief This function activates Packet Domain Protocol. Returns "false" if it is activated.
 * 
 * @return true: PDP is not activated.
 * @return false PDP is activated.
 */
bool activate_pdp() {
    #ifdef DETAILED_PRINT
        // Inform the carrier registration is being checked.
        printf("\n==== activate_pdp() ====\n");
    #endif
    
    // Create command and send it.
    char command_message[] = "#SGACT=1,1";
    char return_message[] = "#SGACT: ";
    send_message_to_telit(command_message);

    #ifdef DETAILED_PRINT
        printf("-- message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 15 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS * 3);

    // Check if the returned message is belongs to our command.
    index_start = strstr(uart0_buffer, command_message);
    if (index_start != NULL) {
        
        // Select only the IP address of the returned message.
        index_start = strstr(index_start + strlen(command_message), return_message);
        
        // Check if it is OK.
        index_end = strstr(index_start, "\r\n\r\nOK\r\n");
        if (index_end == NULL) return true;

        char* substr = (char*) malloc(sizeof(char) * (index_end - index_start + 1));
        strncpy(substr, index_start + strlen(return_message), index_end - index_start + 1);

        // Save the positions of the delimeters.
        uint8_t indices_of_delimeters[4];
        uint8_t index_of_delimeter = 0;
        
        // Travel inside the sub string substr.
        for (uint16_t str_index = 0; str_index < strlen(substr); str_index++) {
            // Check if the current character is a delimeter.
            if (substr[str_index] == '.' || (substr[str_index] == '\r' && substr[str_index+1] == '\n')) {
                // If it is, save the index of the delimeter.
                indices_of_delimeters[index_of_delimeter] = str_index;
                // Increase the index of the delimeter.
                index_of_delimeter++;
            }

            // End the loop, if indices are finished.
            if (index_of_delimeter == 4) break;
        }
        
        // Save each IP address number into a char array.
        char sub_data[4][4] = {"", "", "", ""};
        strncpy(sub_data[0], substr, indices_of_delimeters[0]);
        strncpy(sub_data[1], substr + indices_of_delimeters[0] + 1, indices_of_delimeters[1] - indices_of_delimeters[0] - 1);
        strncpy(sub_data[2], substr + indices_of_delimeters[1] + 1, indices_of_delimeters[2] - indices_of_delimeters[1] - 1);
        strncpy(sub_data[3], substr + indices_of_delimeters[2] + 1, indices_of_delimeters[3] - indices_of_delimeters[2] - 1);

        // Create ip_address in heap, and give the numbers into.
        ip_address = (uint16_t*) malloc(sizeof(uint16_t) * 4);
        for (int i = 0; i < 4; i++)
            ip_address[i] = atoi(sub_data[i]);

        #ifdef DETAILED_PRINT
            printf("-- RESULT: ip addr= %d %d %d %d", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
            printf("\n==== define_apn() ====\n\n");
        #endif

        // Free the memory.
        free(substr);
        free(ip_address);

        return false;
    }

    // Any other case, return true -- means PDP is not defined.
    return true;
}

/**
 * @brief It defines APN details. If everything works correctly, it returns false.
 * 
 * @return true APN is not setted.
 * @return false APN is setted.
 */
bool define_apn() {
    #ifdef DETAILED_PRINT
        // Inform the carrier registration is being checked.
        printf("\n==== define_apn() ====\n");
    #endif
    
    /* TODO: Let user to change "super" in future. */

    // Create command to send it.
    char command_message[] = "+CGDCONT=1,\"IP\",\"super\"";
    send_message_to_telit(command_message);

    #ifdef DETAILED_PRINT
        printf("-- message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);

    // Check if the returned message is belongs to our command.
    index_start = strstr(uart0_buffer, command_message);
    if (index_start != NULL) {

        // Check if there is a OK signal.
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");

        #ifdef DETAILED_PRINT
            printf("-- RESULT: define APN=%s", (index_end != NULL) ? "ok" : "err");
            printf("\n==== define_apn() ====\n\n");
        #endif

        // If there is an OK, then the APN is defined.
        if (index_end != NULL) return false;
    }

    // Any other case, return true -- means APN is not defined.
    return true;
}

/**
 * @brief This function checks for GPRS with CGREG command. It returns the status.
 * 
 * @return uint8_t 
 */
uint8_t check_gprs_registration() {
    #ifdef DETAILED_PRINT
        // Inform the carrier registration is being checked.
        printf("\n==== check_gprs_registration() ====\n");
    #endif
    
    // Create command to send it.
    char command_message[] = "+CGREG?";
    char return_message[] = "+CGREG";
    send_message_to_telit(command_message);

    #ifdef DETAILED_PRINT
        printf("-- message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);

    index_start = strstr(uart0_buffer, command_message);

    if (index_start != NULL) {
        index_start = strstr(index_start + strlen(command_message), return_message);
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");

        char answer_look_like[] = "+CGREG: 0,5";
        char* substr = (char*) malloc(sizeof(answer_look_like));
        memset(substr, '\0', sizeof(answer_look_like));
        strncpy(substr, index_start, sizeof(answer_look_like));
        
        #ifdef DETAILED_PRINT
            printf("-- returned message: %s", substr);
        #endif

        int gprs_reg_status = atoi(substr + strlen(answer_look_like) - 1);

        // Free the memory.
        free(substr);

        #ifdef DETAILED_PRINT
            printf("\n-- RESULT: gprs registration=%d", gprs_reg_status);
            printf("\n==== check_gprs_registration() ====\n\n");
        #endif

        return gprs_reg_status;
    }

    return 0;
}

/**
 * @brief This function runs GPRS registration 20 times, and waits for return 2.
 * If it returns 2, everything is correct.
 */
void process_gprs_registration() {
    uint8_t is_gr_set;

    // Try to get GPRS registration for 20 times.
    for (int try = 0; try < 20; try++) {
        printf("$> Checking GPRS registration... (%d)\n", try+1);
        is_gr_set = check_gprs_registration();
        // If it is good, exit from the loop.
        if (is_gr_set == 0 || is_gr_set == 1 || is_gr_set == 5) break;
        else if (is_gr_set == 2) {
            printf("$> Waiting for 5 second.\n");
            sleep_ms(5000);
        }
        else if (is_gr_set == 3) {
            printf("$> ERROR: Return [3]. Not Implemented.\n");
            printf("$> GRPS registration check not completed.\n");
            return;
        }
    }

    printf("$> GRPS registration check completed.\n");
}

/**
 * @brief It checks for GPRS attachment with CGATT. Returns false if status is 1.
 * 
 * @return true It has problems.
 * @return false Everything is okay.
 */
bool check_gprs_attach() {
    #ifdef DETAILED_PRINT
        // Inform the  signal quality is being checked.
        printf("\n==== check_gprs_attach() ====\n");
    #endif

    // Create command to send it.
    char command_message[] = "+CGATT?";
    char return_message[] = "+CGATT";
    send_message_to_telit(command_message);

    #ifdef DETAILED_PRINT
        printf("-- message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);
    
    // Check if the returned message is belongs to our command.
    index_start = strstr(uart0_buffer, command_message);
    if (index_start != NULL) {

        // Select the start and the end of returned message.
        index_start = strstr(index_start + strlen(command_message), return_message);
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");

        // Extract the data we want into substr.
        char* substr = (char *) malloc(sizeof(char) * (index_end - index_start + 1));
        memset(substr, '\0', sizeof(char) * (index_end - index_start + 1));
        strncpy(substr, index_start, index_end - index_start + 1);

        #ifdef DETAILED_PRINT
            printf("-- returned message: %s", substr);
        #endif
        
        // Convert the status code into integer.
        uint8_t grps_attach_status = atoi(substr + 8);

        // Free the memory.
        free(substr);
        
        #ifdef DETAILED_PRINT
            printf("\n-- RESULT: gprs attach status=%d", grps_attach_status);
            printf("\n==== check_gprs_attach() ====\n\n");
        #endif

        // Return false if everything is okay.
        if (grps_attach_status == 1) return false;
    }
    
    return true;
}

/**
 * @brief It does the automation of GPRS attachemnt check.
 * 
 */
void process_gprs_attach() {
    uint8_t is_ga_set;

    printf("$> Checking GPRS attach...\n");
    is_ga_set = !check_gprs_attach();
    if (is_ga_set) {
        printf("$> GPRS attach check completed.\n");
    }
    else {
        printf("$> ERROR: Not Implemented.\n");
        printf("$> GPRS attach check not completed.\n");
    }
}

/**
 * @brief It does the automation of the carrier registration. 
 * It tries only 3 times, which is different from the documentation.
 * If it couldn't registered to the carrier, reboots the system.
 * 
 */
void process_carrier_registration() {
    uint8_t is_cr_set;

    // Try to get signal quality for 3 times, if it is bad, reboot it.
    for (int try = 0; try < 3; try++) {
        printf("$> Checking carrier registration... (%d)\n", try+1);
        is_cr_set = check_carrier_registration();
        // If it is good, exit from the loop.
        if (is_cr_set == 3 || is_cr_set == 5) break;
        else if (is_cr_set == 2) {
            
            if (try == 2) {
                printf("$> Carrier registration couldn't completed.\n");
                printf("$> Pico will be reboot in 3 seconds.\n");
                // Let the Pico to sleep for 3 seconds to show the information to user.
                sleep_ms(3000);
                // Reboot the Pico.
                reboot_pico();
            }

            printf("$> Waiting for 10 second.\n");
            sleep_ms(10000);
        }
        else if (is_cr_set == 0 || is_cr_set == 3) {
            printf("\n$> ERROR: Return [0 or 3]. Not Implemented.\n");
        }
    }

    printf("$> Carrier registration check completed.\n");
}

/**
 * @brief Checks carrier registration with +CREG command.
 * 
 * @return uint8_t 
 */
uint8_t check_carrier_registration() {
    #ifdef DETAILED_PRINT
        // Inform the carrier registration is being checked.
        printf("\n==== check_carrier_registration() ====\n");
    #endif
    
    // Create command to send it.
    char command_message[] = "+CREG?";
    char return_message[] = "+CREG";
    send_message_to_telit(command_message);

    #ifdef DETAILED_PRINT
        printf("-- message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);

    // Check if the returned message is belongs to our command.
    index_start = strstr(uart0_buffer, command_message);
    if (index_start != NULL) {

        // Select the start and the end of returned message.
        index_start = strstr(index_start + strlen(command_message), return_message);
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");

        // Extract the data we want into substr.
        char* substr = (char*) malloc(sizeof(char) * (index_end - index_start + 1));
        memset(substr, '\0', sizeof(char) * (index_end - index_start + 1));
        strncpy(substr, index_start, index_end - index_start + 1);
        
        #ifdef DETAILED_PRINT
            printf("-- returned message: %s", substr);
        #endif

        // Convert the status code into integer.
        int carrier_reg_status = atoi(substr + 9);

        // Free the memory.
        free(substr);

        #ifdef DETAILED_PRINT
            printf("\n-- RESULT: carrier registration=%d", carrier_reg_status);
            printf("\n==== check_carrier_registration() ====\n\n");
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
    for (int try = 0; try < 3; try++) {
        printf("$> Checking signal quality... (%d)\n", try+1);
        is_sq_good = !check_signal_quality();
        // If it is good, exit from the loop.
        if (is_sq_good) break;
        printf("$> Waiting for 5 second.\n");
        sleep_ms(5000);
    }
    
    if (!is_sq_good) {
        printf("$> Signal quality is bad.\n$> Please check the antenna.\n");
        printf("$> Pico will be reboot in 3 seconds.\n");
        // Let the Pico to sleep for 3 seconds to show the information to user.
        sleep_ms(3000);
        // Reboot the Pico.
        reboot_pico();
    } else
        printf("$> Signal quality is good.\n");
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
        printf("\n==== check_signal_quailty() ====\n");
    #endif

    // Create command to send it, and send it.
    char command_message[] = "+CSQ";
    send_message_to_telit(command_message);

    #ifdef DETAILED_PRINT
        printf("-- message sent to modem.\n");
        // Wait a little bit to recieve message.
        printf("-- waiting 5 seconds.\n");
    #endif

    sleep_ms(TELIT_MSG_WAIT_MS);
    
    // Check if the returned message is for our command.
    index_start = strstr(uart0_buffer, command_message);
    if (index_start != NULL) {
        // Select the start and the end of returned data.
        index_start = strstr(index_start + strlen(command_message), command_message);
        index_end = strstr(uart0_buffer, "\r\nOK\r\n");

        // Extract the data we want into substr.
        char* substr = (char *) malloc(sizeof(char) * (index_end - index_start + 1));
        memset(substr, '\0', sizeof(char) * (index_end - index_start + 1));
        strncpy(substr, index_start, index_end - index_start + 1);

        #ifdef DETAILED_PRINT
            printf("-- returned message: %s", substr);
        #endif
        
        // Convert this string to integer, and store it.
        int signal_quality = atoi(substr + strlen(command_message) + 1);
        
        // Free the substr.
        free(substr);

        #ifdef DETAILED_PRINT
            printf("\n-- RESULT: signal quality=%d", signal_quality);
            printf("\n==== check_signal_quailty() ====\n\n");
        #endif

        // Return if it is good.
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
    char* message_to_return = (char *) malloc(sizeof(char) * (strlen(command) + strlen(start_message) + strlen(end_message) + 1));
    memset(message_to_return, '\0', sizeof(char) * (strlen(command) + strlen(start_message) + strlen(end_message) + 1));

    strcat(message_to_return, start_message);
    strcat(message_to_return, command);
    strcat(message_to_return, end_message);

    return message_to_return;
}

/**
 * @brief It creates a message object consits of "AT" on the front, 
 * message on the middle, and "\r\n" on the end.
 * 
 * @param message The command after "AT".
 */
void send_message_to_telit(char message[]) {
    // Clear the old message's answer in the buffer.
    memset(uart0_buffer, '\0', sizeof(char) * TELIT_BUFFER_SIZE);

    // Create command to send it.
    char* message_to_send = create_message(message);

    #ifdef DETAILED_PRINT
        printf("-- message is (%d byte) %s", sizeof(char) * (strlen(message) + strlen(start_message) + strlen(end_message) + 1), message_to_send);
    #endif

    // Send the command to the TELIT.
    is_message_finished = false;
    if (uart_is_writable(TELIT_UART)) {
        int index = 0;
        while (index != strlen(message_to_send)) {
            uart_putc_raw(TELIT_UART, message_to_send[index]);
            index++;
        }
    }

    // Clear the message_to_send
    free(message_to_send);
}


/**
 * @brief Initilize the GPIOs, set their directions,
 * and assigns them IRQs.
 * 
 */
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

/**
 * @brief Frees the heap memory used in code. 
 * 
 */
/*
void free_heap_usage(uint8_t setting) {
    if (setting == 0) {
        free(substr);
        free(message_to_send);
    }
    else if (setting == 1) {
        free(message_to_send);
    }
}
*/

/**
 * @brief It frees the memory, and clear the TELIT's buffer.
 * 
 */
/*
void prepare_for_next(uint8_t setting) {
    // Clear the memory, and buffer.
    memset(uart0_buffer, '\0', TELIT_BUFFER_SIZE);
    free_heap_usage(setting);
}
*/

/**
 * @brief Reboots Pico using Watchdog's register.
 * 
 */
void reboot_pico() {
    /* TODO: Implement a real watchdog. */
    AIRCR_Register = 0x5FA0004;
}

/**********   Timer Calback Routines    **********/
/*
bool repeating_timer_callback(struct repeating_timer *t) {
    _check_read_timer = true;
    return false;
}
*/
/*************************************************/

/********   Interrupt Services Routines    ********/
void on_uart0_rx() {
    // If the UART channel is readable, read it.
    if (uart_is_readable(TELIT_UART)) {
        recieved_char = uart_getc(TELIT_UART);
        if (recieved_char != 0xff) {
            uart0_buffer[uart0_buffer_index] = recieved_char;
            uart0_buffer_index++;
        }
    }

    // If buffer has OK or ERROR, it means the message is ended.
    if (strstr(uart0_buffer, "OK\r\n") != NULL || strstr(uart0_buffer, "ERROR\r\n") != NULL) {
        is_message_finished = true;
        uart0_buffer_index = 0;
    }
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