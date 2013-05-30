#include "atcommander.h"

#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define RETRY_DELAY_MS 50
#define RESPONSE_DELAY_MS 100
#define MAX_RESPONSE_LENGTH 8
#define MAX_RETRIES 3

#define debug(config, ...) \
    if(config->log_function != NULL) { \
        config->log_function(__VA_ARGS__); \
        config->log_function("\r\n"); \
    }

/** Private: Send an array of bytes to the AT device.
 */
void write(AtCommanderConfig* config, const char* bytes, int size) {
    int i;
    if(config->write_function != NULL) {
        for(i = 0; i < size; i++) {
            config->write_function(bytes[i]);
        }
    }
}

/** Private: If a delay function is available, delay the given time, otherwise
 * just continue.
 */
void delay_ms(AtCommanderConfig* config, int ms) {
    if(config->delay_function != NULL) {
        config->delay_function(ms);
    }
}

/** Private: Read multiple bytes from Serial into the buffer.
 *
 * Continues to try and read each byte from Serial until a maximum number of
 * retries.
 *
 * Returns the number of bytes actually read - may be less than size.
 */
int read(AtCommanderConfig* config, char* buffer, int size,
        int max_retries) {
    int bytes_read = 0;
    int retries = 0;
    while(bytes_read < size && retries < max_retries) {
        int byte = config->read_function();
        if(byte == -1) {
            delay_ms(config, RETRY_DELAY_MS);
            retries++;
        } else {
            buffer[bytes_read++] = byte;
        }
    }
    return bytes_read;
}

/** Private: Compare a response received from a device with some expected
 *      output.
 *
 * Returns true if there reponse matches content and length, otherwise false.
 */
bool check_response(AtCommanderConfig* config, char* response,
        int response_length, char* expected, int expected_length) {
    if(response_length == expected_length && !strncmp(response, expected,
                expected_length)) {
        return true;
    }

    if(response_length != expected_length) {
        debug(config, "Expected %d bytes in response but received %d",
                expected_length, response_length);
    }

    if(response_length > 0) {
        debug(config, "Expected %s response but got %s", expected, response);
    }
    return false;
}

/** Private: Send an AT command, read a response, and verify it matches the
 * expected value.
 *
 * Returns true if the response matches the expected.
 */
bool command_request(AtCommanderConfig* config, char* command,
        char* expected_response) {
    write(config, command, strlen(command));
    delay_ms(config, RESPONSE_DELAY_MS);

    char response[MAX_RESPONSE_LENGTH];
    int bytes_read = read(config, response, strlen(expected_response),
            MAX_RETRIES);

    return check_response(config, response, bytes_read, expected_response,
            strlen(expected_response));
}

/** Private: Change the baud rate of the UART interface and update the config
 * accordingly.
 *
 * This function does *not* attempt to change anything on the AT-command set
 * supporting device, it just changes the host interface.
 */
bool initialize_baud(AtCommanderConfig* config, int baud) {
    if(config->baud_rate_initializer != NULL) {
        debug(config, "Initializing at baud %d", baud);
        config->baud_rate_initializer(baud);
        config->baud = baud;
        return true;
    }
    debug(config, "No baud rate initializer set, can't change baud - "
            "trying anyway");
    return false;
}

bool at_commander_enter_command_mode(AtCommanderConfig* config) {
    int baud_index;
    if(!config->connected) {
        for(baud_index = 0; baud_index < sizeof(VALID_BAUD_RATES) /
                sizeof(int); baud_index++) {
            initialize_baud(config, VALID_BAUD_RATES[baud_index]);
            debug(config, "Attempting to enter command mode");

            if(command_request(config, "$$$", "CMD\r\n")) {
                config->connected = true;
                break;
            }
        }

        if(config->connected) {
            debug(config, "Initialized UART and entered command mode at "
                    "baud %d", config->baud);
        } else {
            debug(config, "Unable to enter command mode at any baud rate");
        }
    } else {
        debug(config, "Already in command mode");
    }
    return config->connected;
}

bool at_commander_exit_command_mode(AtCommanderConfig* config) {
    if(config->connected) {
        if(command_request(config, "---", "END\r\n")) {
            debug(config, "Switched back to data mode");
            config->connected = false;
        } else {
            debug(config, "Unable to exit command mode");
        }
    } else {
        debug(config, "Not in command mode");
    }
}

bool at_commander_reboot(AtCommanderConfig* config) {
    if(at_commander_enter_command_mode(config)) {
        write(config, "R,1\r\n", 5);
        debug(config, "Rebooting RN-42");
    } else {
        debug(config, "Unable to enter command mode, can't reboot");
    }
}

bool at_commander_set_baud(AtCommanderConfig* config, int baud) {
    if(at_commander_enter_command_mode(config)) {
        char command[5];
        sprintf(command, "SU,%d\r\n", baud);
        if(command_request(config, command, "AOK\r\n")) {
            debug(config, "Changed device baud rate to %d", baud);
            config->device_baud = baud;
            return true;
        } else {
            debug(config, "Unable to change device baud rate");
            return false;
        }
    } else {
        debug(config, "Unable to enter command mode, can't set baud rate");
        return false;
    }
}
