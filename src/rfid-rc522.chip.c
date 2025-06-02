#include "wokwi-api.h"
#include <stdint.h>
#include <stdio.h>
#include <unistd.h> // For sleep function

// Function to simulate SPI communication
void spi_transfer(uint8_t *data_out, uint8_t *data_in, uint8_t len) {
    // Simulate SPI transfer
    printf("MFRC522: Sending data: ");
    for (int i = 0; i < len; i++) {
        printf("0x%02X ", data_out[i]);
    }
    printf("\n");

    // Simulate receiving data
    for (int i = 0; i < len; i++) {
        data_in[i] = 0xAB; // Dummy data
    }
    printf("MFRC522: Received data: ");
    for (int i = 0; i < len; i++) {
        printf("0x%02X ", data_in[i]);
    }
    printf("\n");
}

// Function to send UID data to Arduino
void send_uid_to_arduino() {
    // Loop for sending 10 sets of UID data
    for (int i = 0; i < 10; i++) {
        // Simulate UID data
        uint8_t uid_data[4] = {0x12, 0x34, 0x56, 0x78};

        // Print UID data
        printf("UID %d: ", i + 1);
        for (int j = 0; j < 4; j++) {
            printf("%02X ", uid_data[j]);
        }
        printf("\n");

        // Send UID data to Arduino via SPI
        spi_transfer(uid_data, NULL, 4);

        // Wait for 1 second (for demonstration, reduce to 1 second)
        sleep(1);
    }
}


// Function to initialize the chip and SPI pins
void chip_init() {
printf("ddd");
    // Initialize MFRC522
    // Reset MFRC522
    // Additional initialization steps can be added here
     //mfrc522_write(0x0A, 0x0F); // Command register address and reset command
     
}

int main() {
    // Initialize chip
    chip_init();

    // Send UID data to Arduino
    // Send UID data to Arduino every 10 seconds
    while (1) {
        send_uid_to_arduino();
        // Wait for 10 seconds
        sleep(10);
    }

    return 0;
}
