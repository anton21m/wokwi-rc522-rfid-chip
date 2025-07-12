#include "wokwi-api.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define VERSION_REG 0x37
#define NUM_REGISTERS 64
#define FIFO_SIZE 64

// MIFARE commands
#define CMD_REQA 0x26
#define CMD_WUPA 0x52
#define CMD_ANTICOLL 0x93
#define CMD_AUTH_A 0x60
#define CMD_AUTH_B 0x61
#define CMD_READ 0x30
#define CMD_WRITE 0xA0
#define REG_VERSION 0x92

typedef struct {
  pin_t cs_pin;
  uint32_t spi;

  uint8_t registers[NUM_REGISTERS];
  uint8_t fifo[FIFO_SIZE];
  uint8_t fifo_len;

  uint8_t spi_buffer[18];
  uint8_t spi_state;
  uint8_t current_address;
  bool is_read;
  uint8_t read_count;

  // Emulated card data (MIFARE Classic 1K = 16 sectors * 4 blocks * 16 bytes)
  uint8_t card_data[16 * 4 * 16];
  uint8_t uid[4];

  // State variables
  bool card_selected;
  bool authenticated;

  // Internal variables for anticollision, auth, etc.
  uint8_t anticoll_step;
} chip_state_t;

static void chip_pin_change(void *user_data, pin_t pin, uint32_t value);
static void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count);

void chip_init(void) {
  chip_state_t *chip = calloc(1, sizeof(chip_state_t));
  chip->cs_pin = pin_init("CS", INPUT_PULLUP);

  // Initialize registers, set version reg to typical MFRC522 version
  chip->registers[VERSION_REG] = 0x92;

  // Example UID
  chip->uid[0] = 0x50;
  chip->uid[1] = 0x9D;
  chip->uid[2] = 0x39;
  chip->uid[3] = 0x23;

  printf("INIT, UID %02X %02X %02X %02X\n",
    chip->uid[0], chip->uid[1], chip->uid[2], chip->uid[3]);

  // Example: put UID in sector 0 block 0 (manufacturer block)
  memcpy(&chip->card_data[0], chip->uid, 4);

  // Setup pin watching and SPI
  pin_watch_config_t watch_cfg = {
    .edge = BOTH,
    .pin_change = chip_pin_change,
    .user_data = chip,
  };
  pin_watch(chip->cs_pin, &watch_cfg);

  spi_config_t spi_cfg = {
    .sck = pin_init("SCK", INPUT),
    .miso = pin_init("MISO", INPUT),
    .mosi = pin_init("MOSI", INPUT),
    .done = chip_spi_done,
    .user_data = chip,
  };
  chip->spi = spi_init(&spi_cfg);

  // Initialize important registers
  memset(chip->registers, 0, NUM_REGISTERS);
  chip->registers[VERSION_REG] = 0x92;  // Version
  chip->registers[0x04] = 0x00;        // ComIrqReg
  chip->registers[0x0A] = 0x00;        // FIFOLevelReg
  chip->registers[0x0C] = 0x80;        // ControlReg (PowerOn=1)
  
  // Clear FIFO
  chip->fifo_len = 0;
  memset(chip->fifo, 0, FIFO_SIZE);
  
  // Initialize state
  chip->card_selected = false;
  chip->authenticated = false;
  chip->anticoll_step = 0;
}

void chip_pin_change(void *user_data, pin_t pin, uint32_t value) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (pin == chip->cs_pin) {
    if (value == LOW) {
      chip->spi_state = 0;
      spi_start(chip->spi, chip->spi_buffer, 1);
    } else {
      spi_stop(chip->spi);
      chip->spi_state = 0;
      chip->anticoll_step = 0;
    }
  }
}

void process_mifare_command(chip_state_t *chip) {
  if (chip->fifo_len == 0) return;

  uint8_t cmd = chip->fifo[0];
  int bytes_to_process = 0;

  switch (cmd) {
    case CMD_REQA:
    case CMD_WUPA:
      printf("REQA/WUPA - sending ATQA\n");
      chip->fifo_len = 0;
      chip->fifo[0] = 0x04;  // ATQA
      chip->fifo[1] = 0x00;
      chip->fifo_len = 2;
      chip->registers[0x04] |= 0x01;  // Set IRQ
      chip->registers[0x0A] = chip->fifo_len;
      chip->anticoll_step = 0;
      return;

    case CMD_ANTICOLL:
      if (chip->anticoll_step == 0 && chip->fifo_len == 2 && chip->fifo[0] == 0x93 && (chip->fifo[1] == 0x20 || chip->fifo[1] == 0x26)) {
        printf("ANTICOLL - sending UID and BCC\n");
        chip->fifo[0] = chip->uid[0];
        chip->fifo[1] = chip->uid[1];
        chip->fifo[2] = chip->uid[2];
        chip->fifo[3] = chip->uid[3];
        uint8_t bcc = chip->uid[0] ^ chip->uid[1] ^ chip->uid[2] ^ chip->uid[3];
        chip->fifo[4] = bcc;
        chip->fifo_len = 5;
        chip->registers[0x04] |= 0x01;
        chip->registers[0x0A] = chip->fifo_len;
        chip->anticoll_step = 1;
        bytes_to_process = 2;
        printf("ANTICOLL processed - UID and BCC in FIFO: %02X %02X %02X %02X %02X\n",
               chip->fifo[0], chip->fifo[1], chip->fifo[2], chip->fifo[3], chip->fifo[4]);
      } else if (chip->anticoll_step == 1 && chip->fifo_len >= 9 && chip->fifo[0] == 0x93 && chip->fifo[1] == 0x94) {
        uint8_t received_uid[4] = {chip->fifo[2], chip->fifo[3], chip->fifo[4], chip->fifo[5]};
        uint8_t received_bcc = chip->fifo[6];
        uint8_t calculated_bcc = received_uid[0] ^ received_uid[1] ^ received_uid[2] ^ received_uid[3];
        if (received_bcc == calculated_bcc) {
          chip->fifo_len = 0;
          chip->fifo[0] = 0x08;  // SAK
          chip->fifo_len = 1;
          chip->registers[0x04] |= 0x01;
          chip->registers[0x0A] = chip->fifo_len;
          chip->card_selected = true;
          chip->anticoll_step = 0;
          bytes_to_process = 9;
          printf("SELECT processed - card selected, SAK sent\n");
        } else {
          printf("SELECT failed - BCC mismatch\n");
          chip->fifo_len = 0;
          chip->registers[0x0A] = chip->fifo_len;
        }
      } else {
        printf("ANTICOLL/SELECT incomplete - fifo_len=%d\n", chip->fifo_len);
        return;
      }
      break;
  }

  if (bytes_to_process > 0 && bytes_to_process <= chip->fifo_len) {
    memmove(chip->fifo, chip->fifo + bytes_to_process, (chip->fifo_len - bytes_to_process) * sizeof(chip->fifo[0]));
    chip->fifo_len -= bytes_to_process;
  }
  chip->registers[0x04] |= 0x01;
  chip->registers[0x0D] &= ~0x80;
}

void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count) {
  chip_state_t *chip = (chip_state_t *)user_data;

  if (chip->spi_state == 0) {
    uint8_t cmd = buffer[0];
    chip->current_address = (cmd >> 1) & 0x3F;
    chip->is_read = (cmd & 0x80) != 0;

    if (chip->current_address == 0x09 || chip->current_address == 0x01) {
      printf("SPI cmd: 0x%02X, addr: 0x%02X, read: %s\n", cmd, chip->current_address, chip->is_read ? "yes" : "no");
    }

    if (chip->is_read) {
      uint8_t val = chip->registers[chip->current_address];
      if (chip->current_address == VERSION_REG) {
        val = 0x92;
        chip->spi_buffer[0] = val;
        chip->read_count = 1;
      } else if (chip->current_address == 0x04) {
        chip->spi_buffer[0] = chip->registers[0x04];
        chip->read_count = 1;
        printf("ComIrqReg read: 0x%02X\n", chip->registers[0x04]);
      } else if (chip->current_address == 0x0A) {
        chip->spi_buffer[0] = chip->fifo_len;
        chip->read_count = 1;
        printf("FIFOLevelReg read: %d bytes\n", chip->fifo_len);
      } else if (chip->current_address == 0x09) {
        if (chip->fifo_len > 0) {
          uint8_t bytes_to_read = chip->fifo_len;
          if (bytes_to_read > 18) bytes_to_read = 18;
          printf("FIFO READ: fifo_len=%d, bytes_to_read=%d, first_byte=0x%02X\n",
                 chip->fifo_len, bytes_to_read, chip->fifo[0]);
          for (int i = 0; i < bytes_to_read; i++) {
            chip->spi_buffer[i] = chip->fifo[i];
          }
          chip->read_count = bytes_to_read;
          if (bytes_to_read < chip->fifo_len) {
            memmove(chip->fifo, chip->fifo + bytes_to_read, chip->fifo_len - bytes_to_read);
            chip->fifo_len -= bytes_to_read;
          } else {
            chip->fifo_len = 0;
          }
          chip->registers[0x0A] = chip->fifo_len;
          printf("FIFO READ complete: %d bytes read, remaining fifo_len=%d\n", bytes_to_read, chip->fifo_len);
        } else {
          chip->spi_buffer[0] = 0;
          chip->read_count = 1;
          printf("FIFO READ: empty FIFO, returning 0\n");
        }
      } else {
        chip->spi_buffer[0] = val;
        chip->read_count = 1;
      }
    } else {
      chip->spi_buffer[0] = 0;
      chip->read_count = 0;
    }

    chip->spi_state = 1;
  } else {
    if (chip->is_read && chip->read_count > 1) {
      spi_start(chip->spi, chip->spi_buffer, chip->read_count);
      chip->spi_state = 0;
      return;
    }
    if (!chip->is_read) {
      uint8_t val = buffer[0];
      uint8_t reg = chip->current_address;

      if (reg == 0x09) {
        if (chip->fifo_len < FIFO_SIZE) {
          chip->fifo[chip->fifo_len] = val;
          chip->fifo_len++;
          chip->registers[0x0A] = chip->fifo_len;
          printf("FIFO push: 0x%02X (len %d)\n", val, chip->fifo_len);
        }
      } else if (reg == 0x01) {
        printf("CommandReg = 0x%02X\n", val);
        if (val == 0x0E) {
          chip->authenticated = false;
          chip->card_selected = false;
          chip->anticoll_step = 0;
          chip->registers[0x04] = 0;
          printf("Reset command - state cleared\n");
        } else if (val == 0x0C) {
          process_mifare_command(chip);
          chip->registers[0x01] = 0;
        } else if (val == 0x00) {
          chip->registers[0x01] = 0;
        }
      } else {
        chip->registers[reg] = val;
      }
      chip->spi_buffer[0] = 0;
    }
    chip->spi_state = 0;
  }

  if (pin_read(chip->cs_pin) == LOW) {
    spi_start(chip->spi, chip->spi_buffer, 1);
  }
}