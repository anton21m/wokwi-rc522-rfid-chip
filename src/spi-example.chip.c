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

  // Emulated card data (MIFARE Classic 1K = 16 sectors * 4 blocks * 16 bytes)
  uint8_t card_data[16 * 4 * 16];
  uint8_t uid[4];

  // State variables
  bool card_selected;
  bool authenticated;

  // Internal variables for anticollision, auth, etc.
  uint8_t anticoll_step;
} chip_state_t;


static void process_mifare_command(chip_state_t *chip);  // <- ДО вызова!


static void chip_pin_change(void *user_data, pin_t pin, uint32_t value);
static void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count);

void chip_init(void) {
  chip_state_t *chip = calloc(1, sizeof(chip_state_t));
  chip->cs_pin = pin_init("CS", INPUT_PULLUP);

  // Initialize registers, set version reg to typical MFRC522 version
  chip->registers[VERSION_REG] = 0x92;

  // Example UID
  chip->uid[0] = 0xDE;
  chip->uid[1] = 0xAD;
  chip->uid[2] = 0xBE;
  chip->uid[3] = 0xEF;

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

  printf("Emulation initialized, UID %02X %02X %02X %02X\n",
    chip->uid[0], chip->uid[1], chip->uid[2], chip->uid[3]);
}


void chip_pin_change(void *user_data, pin_t pin, uint32_t value) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (pin == chip->cs_pin) {
    printf("CS pin changed: %s\n", value == LOW ? "LOW" : "HIGH");
    if (value == LOW) {
      printf("SPI chip selected\n");
      chip->spi_state = 0;
      spi_start(chip->spi, chip->spi_buffer, 1);
    } else {
      printf("SPI chip deselected\n");
      spi_stop(chip->spi);
      // Не сбрасывайте chip->fifo_len здесь!
      chip->authenticated = false;
      chip->card_selected = false;
      chip->anticoll_step = 0;
    }
  }
}

static void process_mifare_command(chip_state_t *chip);

void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count) {
  chip_state_t *chip = (chip_state_t *)user_data;

  printf("SPI buffer received (count %d):", count);
  for (uint32_t i=0; i<count; i++) {
      printf(" %02X", buffer[i]);
  }
  printf("\n");

  if (chip->spi_state == 0) {
    uint8_t cmd = buffer[0];

    printf("SPI cmd received: 0x%02X\n", cmd);

    chip->current_address = (cmd >> 1) & 0x3F;
    chip->is_read = (cmd & 0x80) != 0;

    if (chip->is_read) {
      // Read register
      uint8_t val = chip->registers[chip->current_address];
      if (chip->current_address == VERSION_REG) {
        val = 0x92; // Version reg fixed value
      }
      chip->spi_buffer[0] = val;
      printf("READ reg 0x%02X = 0x%02X\n", chip->current_address, val);
    } else {
      // For write commands, reply with dummy byte
      chip->spi_buffer[0] = 0;
      printf("WRITE to reg 0x%02X\n", chip->current_address);
    }

    chip->spi_state = 1;
  } else {
    if (!chip->is_read) {
      // Write data to register or FIFO
      uint8_t val = buffer[0];
      uint8_t reg = chip->current_address;

      if (reg == 0x09) { // FIFODataReg
        if (chip->fifo_len < FIFO_SIZE) {
          chip->fifo[chip->fifo_len++] = val;
          printf("FIFO push: 0x%02X (len %d)\n", val, chip->fifo_len);
        }
      } else if (reg == 0x01) { // CommandReg
        printf("CommandReg write: 0x%02X\n", val);
        if (val == 0x0E) { // Soft reset
          chip->fifo_len = 0;
          chip->authenticated = false;
          chip->card_selected = false;
          chip->anticoll_step = 0;
          printf("Soft reset\n");
        } else if (val == 0x0C) { // Transceive command
          // Process the command in FIFO
          process_mifare_command(chip);
          // Сброс FIFO только после обработки!
          chip->fifo_len = 0;
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


void process_mifare_command(chip_state_t *chip) {
  printf("process_mifare_command: fifo_len=%d, fifo=", chip->fifo_len);
  for(int i=0; i < chip->fifo_len; i++) {
      printf("%02X ", chip->fifo[i]);
  }
  printf("\n");

  if (chip->fifo_len == 0) return;

  uint8_t cmd = chip->fifo[0];

  switch (cmd) {
    case CMD_REQA:
      // Ответ на REQA — 0x04 0x00 (ATQA)
      chip->spi_buffer[0] = 0x04;
      chip->spi_buffer[1] = 0x00;
      printf("REQA detected, sending ATQA\n");
      break;

      case CMD_ANTICOLL:
      if (chip->fifo_len == 2 && chip->fifo[1] == 0x20) {
        // Anticollision
        uint8_t bcc = 0;
        for (int i = 0; i < 4; i++) {
          chip->spi_buffer[i] = chip->uid[i];
          bcc ^= chip->uid[i];
        }
        chip->spi_buffer[4] = bcc;
        printf("ANTICOLL, sending UID\n");
      } else if (chip->fifo_len >= 9 && chip->fifo[1] == 0x70) {
          chip->card_selected = true;
          printf("SELECT command received\n");
        }
        break;
    
    case CMD_AUTH_A:
    case CMD_AUTH_B:
      chip->authenticated = true; // Упрощение: всегда аутентифицировано
      printf("AUTH command received, authenticated\n");
      break;

    case CMD_READ:
      if (chip->authenticated && chip->fifo_len >= 2) {
        uint8_t block = chip->fifo[1];
        if (block < (16*4)) {
          memcpy(chip->spi_buffer, &chip->card_data[block * 16], 16);
          printf("READ block %d\n", block);
        }
      }
      break;

      case CMD_WRITE:
      if (chip->authenticated && chip->fifo_len >= 2) {
        uint8_t block = chip->fifo[1];
        // Ожидаем, что дальше придут 16 байт данных
        // Для простоты опустим реализацию по частям
        printf("WRITE command block %d (data not implemented)\n", block);
      }
      break;

    default:
      
      printf("Unknown MIFARE command 0x%02X\n", cmd);
      break;
  }
}

