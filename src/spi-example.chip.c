// This file implements an emulation of the MFRC522 RFID chip for Wokwi projects.
// It allows you to simulate SPI communication with the MFRC522, including basic MIFARE commands.
// For more information and source code, see: https://github.com/anton21m
// Author: Anton21m blackdark20@mail.ru

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
#define CMD_SEL_CL1 0x93
#define CMD_SEL_CL2 0x95
#define CMD_SEL_CL3 0x97
#define CMD_CT 0x88
#define CMD_AUTH_A 0x60
#define CMD_AUTH_B 0x61
#define CMD_READ 0x30
#define CMD_WRITE 0xA0
#define REG_VERSION 0x92

// Добавьте недостающие:
#define CMD_CALC_CRC      0x03
#define CMD_IDLE          0x00
#define CMD_MEM           0x01
#define CMD_GEN_RANDOM_ID 0x02
#define CMD_TRANSMIT      0x04
#define CMD_RECEIVE       0x08
#define CMD_DECREMENT     0xC0 // MIFARE Decrement
#define CMD_INCREMENT     0xC1 // MIFARE Increment
#define CMD_RESTORE       0xC2 // MIFARE Restore
#define CMD_TRANSFER      0xB0 // MIFARE Transfer
#define CMD_UL_WRITE      0xA2 // MIFARE Ultralight Write

typedef enum {
  SPI_STATE_IDLE,
  SPI_STATE_WAIT_DATA,
} spi_transaction_state_t;

typedef struct {
  pin_t cs_pin;
  uint32_t spi;

  uint8_t registers[NUM_REGISTERS];
  uint8_t fifo[FIFO_SIZE];
  uint8_t fifo_len;

  uint8_t spi_buffer[18];
  spi_transaction_state_t spi_transaction_state;
  uint8_t current_address;
  bool is_read;
  uint8_t read_count;

  // Emulated card data (MIFARE Classic 1K = 16 sectors * 4 blocks * 16 bytes)
  uint8_t card_data[16 * 4 * 16];
  uint8_t uid[4];

  // New internal data register for MIFARE Value Block operations (Restore/Transfer)
  uint8_t internal_data_register[16];

  // State variables
  bool card_selected;
  bool authenticated;

  // Internal variables for anticollision, auth, etc.
  uint8_t anticoll_step;
  bool uid_read_completed;
  uint8_t cascade_level;
  uint8_t current_level_known_bits;
  
  // New variables for proper SELECT handling
  bool select_completed;
  uint8_t select_response_sent;

  // Streaming write state: true while CS is low and we are writing consecutive bytes into FIFO
  bool stream_write_to_fifo;

  // Backdoor variables
  bool uid_backdoor_step1;
  bool uid_backdoor_open;

  // MIFARE write command state
  int8_t pending_write_block; // -1 if no pending write, otherwise block address
  uint8_t pending_write_len;  // Expected length of data for pending write

  // NEW: MIFARE two-step command state
  int8_t pending_mifare_twostep_command; // -1 if no pending, otherwise the command (CMD_DECREMENT, CMD_INCREMENT, etc.)
  uint8_t pending_mifare_twostep_block_addr; // The block address for the pending two-step command
} chip_state_t;

// Forward declarations
static void chip_pin_change(void *user_data, pin_t pin, uint32_t value);
static void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count);

// MIFARE command processing functions
static void handle_reqa_wupa_command(chip_state_t *chip);
static void handle_anticoll_command(chip_state_t *chip);
static void handle_select_command(chip_state_t *chip);

// SPI read/write functions
static void handle_spi_read_command(chip_state_t *chip);
static void handle_spi_write_command(chip_state_t *chip, uint8_t val);
static void read_version_register(chip_state_t *chip);
static void read_comirq_register(chip_state_t *chip);
static void read_fifo_level_register(chip_state_t *chip);
static void read_fifo_data_register(chip_state_t *chip);
static void write_fifo_register(chip_state_t *chip, uint8_t val);
static void write_command_register(chip_state_t *chip, uint8_t val);

// FIFO management functions
static void fifo_push(chip_state_t *chip, uint8_t val);
static void fifo_remove_bytes(chip_state_t *chip, int bytes_to_remove);
static void update_fifo_level_register(chip_state_t *chip);

// State management functions
static void reset_chip_state(chip_state_t *chip);
static void set_irq_flag(chip_state_t *chip);
static void clear_irq_flag(chip_state_t *chip, uint8_t flag);
static void set_specific_irq_flag(chip_state_t *chip, uint8_t flag);
static void log_chip_state(chip_state_t *chip, const char *context);

// CRC_A для ISO14443A (полином 0x8408, начальное значение 0x6363)
static void calc_crc_a(const uint8_t *data, size_t len, uint8_t *crc) {
    uint16_t crcval = 0x6363;
    for (size_t i = 0; i < len; i++) {
        crcval ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crcval & 0x0001)
                crcval = (crcval >> 1) ^ 0x8408;
            else
                crcval = (crcval >> 1);
        }
    }
    crc[0] = crcval & 0xFF;
    crc[1] = (crcval >> 8) & 0xFF;
}

static void perform_crc_calculation(chip_state_t *chip) {
  // Выполнить CRC_A для текущего содержимого FIFO
  if (chip->fifo_len == 0) {
    // Нечего считать - возвращаем 0
    chip->registers[0x22] = 0x00; // CRCResultRegL
    chip->registers[0x21] = 0x00; // CRCResultRegH
  } else {
    uint8_t crc[2];
    calc_crc_a(chip->fifo, chip->fifo_len, crc);
    chip->registers[0x22] = crc[0]; // CRCResultRegL
    chip->registers[0x21] = crc[1]; // CRCResultRegH
  }
  // Установить бит CRCIRq (0x04) в DivIrqReg (адрес 0x05)
  chip->registers[0x05] |= 0x04;
//   printf("CRC calculated for %d bytes -> %02X %02X, DivIrqReg: 0x%02X\n", chip->fifo_len, chip->registers[0x22], chip->registers[0x21], chip->registers[0x05]);
}

// Helper to decode a 4-byte value from a MIFARE value block
static int32_t decode_mifare_value(const uint8_t *buffer) {
    return (int32_t)(buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | (buffer[3] << 24));
}

// Helper to encode a 4-byte value into a MIFARE value block format
static void encode_mifare_value(uint8_t *buffer, int32_t value, uint8_t block_address) {
    buffer[0] = (uint8_t)(value & 0xFF);
    buffer[1] = (uint8_t)((value >> 8) & 0xFF);
    buffer[2] = (uint8_t)((value >> 16) & 0xFF);
    buffer[3] = (uint8_t)((value >> 24) & 0xFF);

    buffer[4] = ~buffer[0];
    buffer[5] = ~buffer[1];
    buffer[6] = ~buffer[2];
    buffer[7] = ~buffer[3];

    buffer[8] = buffer[0];
    buffer[9] = buffer[1];
    buffer[10] = buffer[2];
    buffer[11] = buffer[3];

    buffer[12] = block_address;
    buffer[13] = block_address;
    buffer[14] = block_address;
    buffer[15] = ~block_address;
}

void chip_init(void) {
  chip_state_t *chip = calloc(1, sizeof(chip_state_t));
  chip->cs_pin = pin_init("CS", INPUT_PULLUP);

  // Initialize registers, set version reg to typical MFRC522 version
  chip->registers[VERSION_REG] = 0x92;

  // Example UID (выбрали 50 92 9D 39, чтобы BCC был 0x66)
  chip->uid[0] = 0x50;
  chip->uid[1] = 0x92;
  chip->uid[2] = 0x9D;
  chip->uid[3] = 0x39;

  printf("INIT, UID %02X %02X %02X %02X\n",
    chip->uid[0], chip->uid[1], chip->uid[2], chip->uid[3]);

  // Initialize card data with default MIFARE Classic 1K structure
  memset(chip->card_data, 0, sizeof(chip->card_data));

  // Populate Block 0 (Manufacturer Block) with UID and BCC
  chip->card_data[0] = chip->uid[0];
  chip->card_data[1] = chip->uid[1];
  chip->card_data[2] = chip->uid[2];
  chip->card_data[3] = chip->uid[3];
  chip->card_data[4] = chip->uid[0] ^ chip->uid[1] ^ chip->uid[2] ^ chip->uid[3]; // BCC
  // The rest of block 0 is manufacturer data, can be left as 0.

  // Populate all sector trailers with default keys and access bits.
  // This mimics a fresh MIFARE Classic card.
  const uint8_t default_trailer[16] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // Key A
    0xFF, 0x07, 0x80,                   // Access Bits (default configuration)
    0x69,                               // User Data Byte (GPB)
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF  // Key B
  };

  // MIFARE Classic 1K has 16 sectors.
  for (int sector = 0; sector < 16; sector++) {
    // The trailer is the last block of the sector (block 3).
    int trailer_block_index = sector * 4 + 3;
    memcpy(&chip->card_data[trailer_block_index * 16], default_trailer, 16);
  }

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
  chip->registers[0x04] = 0x00;        // ComIrqReg - все флаги сброшены
  chip->registers[0x06] = 0x00;        // ErrorReg - нет ошибок
  chip->registers[0x0A] = 0x00;        // FIFOLevelReg
  chip->registers[0x0C] = 0x80;        // ControlReg (PowerOn=1)
  chip->registers[0x26] = 0x70;        // RFCfgReg (default to 48dB gain)
  
  // Clear FIFO
  chip->fifo_len = 0;
  memset(chip->fifo, 0, FIFO_SIZE);
  
  // Initialize state
  chip->card_selected = false;
  chip->authenticated = false;
  chip->anticoll_step = 0;
  chip->uid_read_completed = false;
  chip->cascade_level = 1;
  chip->current_level_known_bits = 0;
  chip->select_completed = false;
  chip->select_response_sent = 0;
  chip->stream_write_to_fifo = false;
  chip->spi_transaction_state = SPI_STATE_IDLE;
  chip->pending_write_block = -1;
  chip->pending_write_len = 0;
  chip->pending_mifare_twostep_command = -1; // NEW
  chip->pending_mifare_twostep_block_addr = 0; // NEW

  // Initialize internal data register to all zeros
  memset(chip->internal_data_register, 0, sizeof(chip->internal_data_register));
  
  printf("Chip initialized - ComIrqReg: 0x%02X\n", chip->registers[0x04]);
  
  // Проверяем состояние всех важных регистров
  printf("Initial register state:\n");
  printf("ComIrqReg (0x04): 0x%02X\n", chip->registers[0x04]);
  printf("FIFOLevelReg (0x0A): 0x%02X\n", chip->registers[0x0A]);
  printf("ControlReg (0x0C): 0x%02X\n", chip->registers[0x0C]);
  printf("VersionReg (0x37): 0x%02X\n", chip->registers[VERSION_REG]);
}

void chip_pin_change(void *user_data, pin_t pin, uint32_t value) {
  chip_state_t *chip = (chip_state_t *)user_data;
  if (pin == chip->cs_pin) {
    if (value == LOW) {
      chip->spi_transaction_state = SPI_STATE_IDLE;
      spi_start(chip->spi, chip->spi_buffer, 1);
    } else {
      spi_stop(chip->spi);
      // Do NOT reset anticoll_step here. It must be preserved across transactions
      // during card selection sequence. It will be reset by REQA/WUPA or SELECT completion.
      // chip->anticoll_step = 0;
      chip->stream_write_to_fifo = false;
    }
  }
}

// MIFARE command processing functions
static void handle_reqa_wupa_command(chip_state_t *chip) {
  printf("REQA/WUPA - sending ATQA\n");
  // Заменяем команду REQA на ATQA
  chip->fifo[0] = 0x04;  // ATQA
  chip->fifo[1] = 0x00;
  chip->fifo_len = 2;
  update_fifo_level_register(chip);
  // Устанавливаем только RxIRq для успешного приема данных
  set_specific_irq_flag(chip, 0x20);  // RxIRq (corrected from 0x04)
  chip->anticoll_step = 0;
  chip->registers[0x0C] &= ~0x07; // Сброс RxLastBits в 0, так как ATQA - это полные байты
//   log_chip_state(chip, "after REQA/WUPA");
}

static void handle_anticoll_command(chip_state_t *chip) {
//   printf("ANTICOLL command - step: %d, fifo_len: %d\n", chip->anticoll_step, chip->fifo_len);
//   log_chip_state(chip, "before ANTICOLL processing");
  
//   if (chip->fifo_len > 0) {
//     printf("ANTICOLL fifo content: ");
//     for (int i = 0; i < chip->fifo_len && i < 4; i++) {
//       printf("%02X ", chip->fifo[i]);
//     }
//     printf("\n");
//   }
  
  // Обработка ANTICOLLISION для Cascade Level 1
  if (chip->anticoll_step == 0 && chip->fifo_len >= 1 && chip->fifo[0] == CMD_SEL_CL1) {
    // Очищаем FIFO перед формированием ответа
    chip->fifo_len = 0;
    printf("ANTICOLL - responding with UID\n");
    chip->fifo[chip->fifo_len++] = chip->uid[0];
    chip->fifo[chip->fifo_len++] = chip->uid[1];
    chip->fifo[chip->fifo_len++] = chip->uid[2];
    chip->fifo[chip->fifo_len++] = chip->uid[3];
    uint8_t bcc = chip->uid[0] ^ chip->uid[1] ^ chip->uid[2] ^ chip->uid[3];
    chip->fifo[chip->fifo_len++] = bcc;  // UID + BCC
    update_fifo_level_register(chip);
    set_specific_irq_flag(chip, 0x20);  // RxIRq (corrected from 0x04)
    chip->anticoll_step = 1;
    chip->current_level_known_bits = 32;  // Все 32 бита известны
    chip->registers[0x0C] &= ~0x07; // Сброс RxLastBits в 0, так как UID - это полные байты
//     printf("ANTICOLL processed - UID and BCC in FIFO: ");
//     for (int i = 0; i < chip->fifo_len; i++) {
//       printf("%02X ", chip->fifo[i]);
//     }
//     printf("\n");
  }
  
//   log_chip_state(chip, "after ANTICOLL processing");
}

static void handle_select_command(chip_state_t *chip) {
//   printf("SELECT command detected - processing full UID selection\n");
//   printf("SELECT command content: ");
//   for (int i = 0; i < chip->fifo_len; i++) {
//     printf("%02X ", chip->fifo[i]);
//   }
//   printf("\n");

  // Check if this is the correct SELECT command for our UID
  if (chip->fifo[2] == chip->uid[0] && chip->fifo[3] == chip->uid[1] &&
      chip->fifo[4] == chip->uid[2] && chip->fifo[5] == chip->uid[3]) {
    printf("SELECT - UID match, sending SAK\n");

    // Clear FIFO before sending SAK
      chip->fifo_len = 0;

    // Send SAK with CRC
    chip->fifo[0] = 0x08;  // SAK for MIFARE Classic 1K
    uint8_t crc[2];
    calc_crc_a(chip->fifo, 1, crc);
    chip->fifo[1] = crc[0];
    chip->fifo[2] = crc[1];
    chip->fifo_len = 3;

      update_fifo_level_register(chip);
    set_specific_irq_flag(chip, 0x20);  // RxIRq (corrected from 0x04)

      chip->card_selected = true;
    chip->authenticated = false; // Reset authentication state on new selection
    chip->select_completed = true;
    chip->registers[0x0C] &= ~0x07; // Сброс RxLastBits в 0, так как SAK - это полный байт
//     printf("SELECT completed - SAK+CRC sent: %02X %02X %02X\n", chip->fifo[0], chip->fifo[1], chip->fifo[2]);
    } else {
    printf("SELECT failed - UID mismatch\n");
    printf("Expected UID: %02X %02X %02X %02X\n",
           chip->uid[0], chip->uid[1], chip->uid[2], chip->uid[3]);
    printf("Received UID: %02X %02X %02X %02X\n",
           chip->fifo[2], chip->fifo[3], chip->fifo[4], chip->fifo[5]);
      chip->fifo_len = 0;
      update_fifo_level_register(chip);
    }
  // Reset anticoll_step after SELECT is done
      chip->anticoll_step = 0;
}

void process_mifare_command(chip_state_t *chip) {
  if (chip->fifo_len == 0) return;

  // Обработка второй фазы MIFARE WRITE, если она ожидается
  if (chip->pending_write_block != -1 && chip->fifo_len == 18) {
    printf("Processing MIFARE WRITE Phase 2 (block 0x%02X) - received 18 bytes (16 data + 2 CRC)\n", chip->pending_write_block);
    // Проверяем, авторизован ли доступ к сектору
    bool allow_write = chip->authenticated;
    if (chip->pending_write_block == 0) { // UID block
      allow_write = true; // Always allow write to block 0 for now (can be restricted later by authentication or backdoor)
    }

    if (allow_write) {
      // Копируем только 16 байт данных, игнорируя последние 2 байта CRC
      memcpy(&chip->card_data[chip->pending_write_block * 16], chip->fifo, 16);
      if (chip->pending_write_block == 0) {
        // Update UID from block 0
        memcpy(chip->uid, &chip->card_data[0], 4);
      }
      
      // Отправляем 4-битный ACK
      chip->fifo_len = 0; // Clear FIFO before putting ACK
      chip->fifo[0] = 0x0A; // MF_ACK
      chip->fifo_len = 1;
      update_fifo_level_register(chip);
      set_specific_irq_flag(chip, 0x20); // RxIRq
      chip->registers[0x0C] = (chip->registers[0x0C] & ~0x07) | 0x04; // Set RxLastBits to 4
      printf("Sent ACK (0x0A) for WRITE Phase 2. RxLastBits set to 4. FIFO len: %d\n", chip->fifo_len);
    } else {
      printf("WRITE Phase 2 failed: not authenticated for this sector.\n");
      // Отправляем NACK или ничего не отправляем, позволяя таймауту произойти
      chip->fifo_len = 0; // Clear FIFO
      update_fifo_level_register(chip);
    }

    // Сбрасываем состояние записи
    chip->pending_write_block = -1;
    chip->pending_write_len = 0;
    return; // Завершаем обработку команды
  }
  
  // NEW: Обработка второй фазы двухступенчатых MIFARE команд (Decrement, Increment, Restore, Transfer)
  // These commands are split into two PCD_MIFARE_Transceive calls.
  // First call: command + block address
  // Second call: 4 bytes of data (for Increment/Decrement) or 0 (for Restore/Transfer)
  if (chip->pending_mifare_twostep_command != -1 && chip->fifo_len == 4) { // Expecting 4 bytes of data
      uint8_t command = chip->pending_mifare_twostep_command;
      uint8_t blockAddr = chip->pending_mifare_twostep_block_addr;
      
      if (chip->authenticated) {
          switch (command) {
              case CMD_DECREMENT: {
                  int32_t delta = decode_mifare_value(chip->fifo);
                  memcpy(chip->internal_data_register, &chip->card_data[blockAddr * 16], 16);
                  int32_t currentValue = decode_mifare_value(chip->internal_data_register);
                  currentValue -= delta;
                  encode_mifare_value(chip->internal_data_register, currentValue, blockAddr);
                  memcpy(&chip->card_data[blockAddr * 16], chip->internal_data_register, 16);
                  printf("MIFARE DECREMENT executed on block 0x%02X with delta %d. New value: %d\n", blockAddr, delta, currentValue);
                  break;
              }
              case CMD_INCREMENT: {
                  int32_t delta = decode_mifare_value(chip->fifo);
                  memcpy(chip->internal_data_register, &chip->card_data[blockAddr * 16], 16);
                  int32_t currentValue = decode_mifare_value(chip->internal_data_register);
                  currentValue += delta;
                  encode_mifare_value(chip->internal_data_register, currentValue, blockAddr);
                  memcpy(&chip->card_data[blockAddr * 16], chip->internal_data_register, 16);
                  printf("MIFARE INCREMENT executed on block 0x%02X with delta %d. New value: %d\n", blockAddr, delta, currentValue);
                  break;
              }
              case CMD_RESTORE:
                  // For RESTORE, the action (copying to internal_data_register) happens in Phase 1.
                  // This Phase 2 just needs to send ACK.
                  printf("MIFARE RESTORE Phase 2 (data) received, data ignored. Command for block 0x%02X\n", blockAddr);
                  break;
              case CMD_TRANSFER:
                  // For TRANSFER, the action (copying from internal_data_register to block) happens in Phase 1.
                  // This Phase 2 just needs to send ACK.
                  printf("MIFARE TRANSFER Phase 2 (data) received, data ignored. Command for block 0x%02X\n", blockAddr);
                  break;
          }
          // Send 4-bit ACK
          chip->fifo_len = 0;
          chip->fifo[0] = 0x0A;
          chip->fifo_len = 1;
          update_fifo_level_register(chip);
          set_specific_irq_flag(chip, 0x20); // RxIRq
          chip->registers[0x0C] = (chip->registers[0x0C] & ~0x07) | 0x04; // Set RxLastBits to 4
          printf("Sent ACK (0x0A) for two-step command Phase 2 (cmd 0x%02X, block 0x%02X). FIFO len: %d\n", command, blockAddr, chip->fifo_len);
      } else {
          printf("Two-step command (0x%02X) Phase 2 failed: not authenticated for block 0x%02X.\n", command, blockAddr);
          chip->fifo_len = 0;
          update_fifo_level_register(chip);
      }
      chip->pending_mifare_twostep_command = -1;
      chip->pending_mifare_twostep_block_addr = 0;
      return;
  }


  uint8_t cmd = chip->fifo[0];
  printf("Processing MIFARE command: 0x%02X (fifo_len=%d, anticoll_step=%d)\n", cmd, chip->fifo_len, chip->anticoll_step);

  switch (cmd) {
    case CMD_REQA:
    case CMD_WUPA:
      // printf("Handling REQA/WUPA command\n");
      handle_reqa_wupa_command(chip);
      break;

    case CMD_SEL_CL1:
    case CMD_SEL_CL2:
    case CMD_SEL_CL3:
      if (chip->anticoll_step == 1 && chip->fifo_len >= 9) {
        handle_select_command(chip);
      } else {
      handle_anticoll_command(chip);
      }
      break;

    case CMD_READ:
      // printf("Handling READ command (block 0x%02X)\n", chip->fifo[1]);
      if (chip->authenticated) {
        if (chip->fifo_len >= 2) {
          uint8_t blockAddr = chip->fifo[1];
          if (blockAddr < 64) { // 16 sectors * 4 blocks/sector
            printf("Reading block %d\n", blockAddr);
            // Copy 16 bytes from emulated card memory
            chip->fifo_len = 0; // Clear FIFO before filling
            memcpy(chip->fifo, &chip->card_data[blockAddr * 16], 16);
            
            // Append CRC
            uint8_t crc[2];
            calc_crc_a(chip->fifo, 16, crc);
            chip->fifo[16] = crc[0];
            chip->fifo[17] = crc[1];
            chip->fifo_len = 18;
            update_fifo_level_register(chip);
            set_specific_irq_flag(chip, 0x20); // RxIRq
            chip->registers[0x0C] &= ~0x07; // Clear RxLastBits to 0 (8 valid bits)
          } else {
            printf("READ failed: block address %d is out of bounds.\n", blockAddr);
            chip->fifo_len = 0;
            update_fifo_level_register(chip);
          }
        } else {
            printf("READ failed: command too short.\n");
            chip->fifo_len = 0;
            update_fifo_level_register(chip);
        }
      } else {
        printf("READ failed: not authenticated for this sector.\n");
        // Don't respond, let it time out.
        chip->fifo_len = 0;
        update_fifo_level_register(chip);
      }
      break;

    case CMD_WRITE:
      printf("Handling WRITE command (block 0x%02X)\n", chip->fifo[1]);
      uint8_t blockAddr = chip->fifo[1];
      bool allow_write = false;
      if (chip->authenticated) {
        allow_write = true;
      }
      // Разрешить запись в блок 0, если открыт backdoor
      if (blockAddr == 0 && chip->uid_backdoor_open) {
        allow_write = true;
        printf("Backdoor open: allowing write to block 0 without authentication!\n");
        chip->uid_backdoor_open = false; // Сбросить после успешной записи
      }
      if (allow_write) {
        if (chip->fifo_len >= 2) { // CMD_WRITE + block_addr + CRC
          // First phase of MIFARE WRITE: send ACK and set up for next 16 bytes
          chip->pending_write_block = blockAddr;
          chip->pending_write_len = 16;

          // Send 4-bit ACK
          chip->fifo_len = 0; // Clear FIFO before putting ACK
          chip->fifo[0] = 0x0A; // MF_ACK
          chip->fifo_len = 1;
          update_fifo_level_register(chip);
          set_specific_irq_flag(chip, 0x20); // RxIRq
          chip->registers[0x0C] = (chip->registers[0x0C] & ~0x07) | 0x04; // Set RxLastBits to 4
          printf("Sent ACK (0x0A) for WRITE Phase 1. RxLastBits set to 4. FIFO len: %d\n", chip->fifo_len);
        } else {
          printf("WRITE failed: command too short for phase 1.\n");
          chip->fifo_len = 0;
          update_fifo_level_register(chip);
        }
      }
      else {
        printf("WRITE failed: not authenticated for this sector.\n");
        chip->fifo_len = 0;
        update_fifo_level_register(chip);
      }
      break;

    case CMD_DECREMENT: // 0xC0
    case CMD_INCREMENT: // 0xC1
    case CMD_RESTORE:   // 0xC2
    case CMD_TRANSFER:  // 0xB0
      // Phase 1 of MIFARE Two-Step Commands (command + block address)
      if (chip->fifo_len >= 2) {
          uint8_t blockAddr = chip->fifo[1];
          if (blockAddr < 64) {
              chip->pending_mifare_twostep_command = cmd;
              chip->pending_mifare_twostep_block_addr = blockAddr;
              
              if (cmd == CMD_RESTORE) {
                  if (chip->authenticated) {
                      memcpy(chip->internal_data_register, &chip->card_data[blockAddr * 16], 16);
                      printf("MIFARE RESTORE executed: block 0x%02X restored to internal register.\n", blockAddr);
                  } else {
                      printf("MIFARE RESTORE failed: not authenticated for block 0x%02X.\n", blockAddr);
                      chip->fifo_len = 0;
                      update_fifo_level_register(chip);
                      return;
                  }
              } else if (cmd == CMD_TRANSFER) {
                  if (chip->authenticated) {
                      memcpy(&chip->card_data[blockAddr * 16], chip->internal_data_register, 16);
                      printf("MIFARE TRANSFER executed: internal register transferred to block 0x%02X.\n", blockAddr);
                  } else {
                      printf("MIFARE TRANSFER failed: not authenticated for block 0x%02X.\n", blockAddr);
                      chip->fifo_len = 0;
                      update_fifo_level_register(chip);
                      return;
                  }
              }

              // Send 4-bit ACK for the first phase
              chip->fifo_len = 0;
              chip->fifo[0] = 0x0A;
              chip->fifo_len = 1;
              update_fifo_level_register(chip);
              set_specific_irq_flag(chip, 0x20); // RxIRq
              chip->registers[0x0C] = (chip->registers[0x0C] & ~0x07) | 0x04; // Set RxLastBits to 4
              printf("Sent ACK (0x0A) for two-step command Phase 1 (cmd 0x%02X, block 0x%02X). FIFO len: %d\n", cmd, blockAddr, chip->fifo_len);
          } else {
              printf("Two-step command (0x%02X) failed: block address out of bounds.\n", cmd);
              chip->fifo_len = 0;
              update_fifo_level_register(chip);
          }
      } else {
          printf("Two-step command (0x%02X) failed: command too short for phase 1.\n", cmd);
          chip->fifo_len = 0;
          update_fifo_level_register(chip);
      }
      break;

    case CMD_UL_WRITE:
      printf("Handling MIFARE ULTRALIGHT WRITE command (page 0x%02X)\n", chip->fifo[1]);
      if (chip->fifo_len >= 6) { // CMD + pageAddr + data (4 bytes) + CRC (2 bytes)
        uint8_t pageAddr = chip->fifo[1];
        // MIFARE Ultralight имеет 16 страниц (0-15), каждая по 4 байта.
        // Проверяем, что адрес страницы допустим.
        // Page 0 is R/O UID, Page 1 is R/O internal, Page 2 is R/W
        // The lib tests write to page 4.
        if (pageAddr >= 2 && pageAddr < 16) {
          memcpy(&chip->card_data[pageAddr * 16], &chip->fifo[2], 4); // Copy only 4 bytes
          chip->fifo_len = 0; // Clear FIFO
          chip->fifo[0] = 0x0A; // MF_ACK
          chip->fifo_len = 1;
          update_fifo_level_register(chip);
          set_specific_irq_flag(chip, 0x20); // RxIRq
          chip->registers[0x0C] = (chip->registers[0x0C] & ~0x07) | 0x04; // Set RxLastBits to 4
        } else {
          printf("MIFARE ULTRALIGHT WRITE failed: page address %d out of bounds or read-only.\n", pageAddr);
          chip->fifo_len = 0;
          update_fifo_level_register(chip);
        }
      } else {
        printf("MIFARE ULTRALIGHT WRITE failed: invalid command length.\n");
        chip->fifo_len = 0;
        update_fifo_level_register(chip);
      }
      break;

    case 0x50: // HALT
      reset_chip_state(chip); // Сброс состояния для переподключения
      chip->fifo_len = 0;
      chip->uid_backdoor_step1 = true; // Установить для следующей команды 0x40
      // set_specific_irq_flag(chip, 0x10); // IdleIRq - Удалена эта строка
      printf("HALT command received. Card state reset for re-discovery. No response will be sent.\n");
      break;
    case 0x40:
      if (chip->uid_backdoor_step1) {
        chip->fifo[0] = 0x0A;
        chip->fifo_len = 1;
        set_specific_irq_flag(chip, 0x20); // RxIRq
        chip->registers[0x0C] = (chip->registers[0x0C] & ~0x07) | 0x04; // Set RxLastBits to 4
        chip->uid_backdoor_step1 = false;
        chip->uid_backdoor_open = true; // разрешаем следующий шаг
      } else {
        chip->fifo_len = 0;
      }
      break;
    case 0x43:
      if (chip->uid_backdoor_open) {
        chip->fifo[0] = 0x0A;
        chip->fifo_len = 1;
        set_specific_irq_flag(chip, 0x20); // RxIRq
        chip->registers[0x0C] = (chip->registers[0x0C] & ~0x07) | 0x04; // Set RxLastBits to 4
        // Теперь разрешить запись в сектор 0
        chip->uid_backdoor_open = true;
      } else {
        chip->fifo_len = 0;
      }
      break;

    case CMD_AUTH_A:
    case CMD_AUTH_B:
      // printf("Handling AUTHENTICATE command\n");
      // Simplified authentication: we just check the command in FIFO.
      // A real implementation would check the key against the sector trailer.
      if (chip->fifo_len >= 1 && (chip->fifo[0] == CMD_AUTH_A || chip->fifo[0] == CMD_AUTH_B)) {
          printf("Authentication successful (simulated)\n");
          chip->authenticated = true;
          // The command completes when IdleIRq is set.
          set_specific_irq_flag(chip, 0x10); // IdleIRq
      } else {
          printf("Authentication failed: incorrect command in FIFO (len=%d)\n", chip->fifo_len);
          // Maybe set an error flag? For now, do nothing and let it time out.
      }
      chip->fifo_len = 0; // Clear FIFO after auth attempt
      update_fifo_level_register(chip);
      chip->registers[0x01] = 0; // Go to Idle
      break;

    case CMD_CT:
      // printf("Handling CT command (Transceive)\n");
      // This command is typically used for Transmit and Receive.
      // For self-test, we just acknowledge it.
      set_specific_irq_flag(chip, 0x20); // TxIRq
      chip->registers[0x01] = 0; // Go to Idle
      break;

    case CMD_CALC_CRC:
      // printf("Handling CALC_CRC command\n");
      perform_crc_calculation(chip);
      chip->registers[0x01] = 0; // Go to Idle
      break;

    case CMD_IDLE:
      // printf("Handling IDLE command\n");
      chip->registers[0x01] = 0; // Go to Idle
      break;

    case CMD_MEM:
      // printf("Handling MEM command (Transfer FIFO to internal buffer)\n");
      // This command transfers FIFO data to internal buffer for self-test
      // In our emulation, we just acknowledge the command
      set_specific_irq_flag(chip, 0x10); // IdleIRq
      chip->registers[0x01] = 0; // Go to Idle
      break;

    case CMD_GEN_RANDOM_ID:
      // printf("Handling GEN_RANDOM_ID command\n");
      // This command generates a random ID for self-test
      // In our emulation, we just acknowledge the command
      set_specific_irq_flag(chip, 0x10); // IdleIRq
      chip->registers[0x01] = 0; // Go to Idle
      break;

    case CMD_TRANSMIT:
      // printf("Handling TRANSMIT command\n");
      // This command transmits data from FIFO for self-test
      // In our emulation, we just acknowledge the command
      set_specific_irq_flag(chip, 0x20); // TxIRq
      chip->registers[0x01] = 0; // Go to Idle
      break;

    case CMD_RECEIVE:
      // printf("Handling RECEIVE command\n");
      // This command activates the receiver for self-test
      // In our emulation, we just acknowledge the command
      set_specific_irq_flag(chip, 0x10); // IdleIRq
      chip->registers[0x01] = 0; // Go to Idle
      break;

    default:
      printf("Unknown MIFARE command: 0x%02X\n", cmd);
      break;
  }
}

// SPI read/write functions
static void read_version_register(chip_state_t *chip) {
  chip->spi_buffer[0] = 0x92;
  chip->read_count = 1;
}

static void read_comirq_register(chip_state_t *chip) {
  chip->spi_buffer[0] = chip->registers[0x04];
  chip->read_count = 1;
//   printf("ComIrqReg read: 0x%02X (IRQ flags: RxIRq=%d, IdleIRq=%d, LoAlertIRq=%d, ErrIRq=%d, TimerIRq=%d, TxIRq=%d)\n", 
//          chip->registers[0x04],
//          (chip->registers[0x04] & 0x20) ? 1 : 0,  // RxIRq
//          (chip->registers[0x04] & 0x10) ? 1 : 0,  // IdleIRq
//          (chip->registers[0x04] & 0x02) ? 1 : 0,  // LoAlertIRq
//          (chip->registers[0x04] & 0x08) ? 1 : 0,  // ErrIRq
//          (chip->registers[0x04] & 0x10) ? 1 : 0,  // TimerIRq (corrected from 0x01 to 0x10 previously)
//          (chip->registers[0x04] & 0x40) ? 1 : 0); // TxIRq (corrected from 0x20 to 0x40 previously)
}

static void read_fifo_level_register(chip_state_t *chip) {
  chip->spi_buffer[0] = chip->fifo_len;
  chip->read_count = 1;
//   printf("FIFOLevelReg read: %d bytes\n", chip->fifo_len);
}

static void read_fifo_data_register(chip_state_t *chip) {
  if (chip->fifo_len > 0) {
    uint8_t bytes_to_read = chip->fifo_len;
    if (bytes_to_read > 18) bytes_to_read = 18;
//     printf("FIFO READ: fifo_len=%d, bytes_to_read=%d, first_byte=0x%02X\n",
//            chip->fifo_len, bytes_to_read, chip->fifo[0]);
    
    memcpy(chip->spi_buffer, chip->fifo, bytes_to_read);
    chip->read_count = bytes_to_read;
    
    // Удаляем прочитанные байты из FIFO, но не очищаем полностью
      fifo_remove_bytes(chip, bytes_to_read);
    
    // Устанавливаем RxIRq только если в FIFO еще есть данные
    if (chip->fifo_len > 0) {
      set_specific_irq_flag(chip, 0x20); // RxIRq
//       printf("RxIRq (0x20) set because FIFO still has %d bytes\n", chip->fifo_len);
    } else {
      clear_irq_flag(chip, 0x20); // Сбросить RxIRq (0x20)
//       printf("RxIRq (0x20) cleared because FIFO is now empty\n");
    }
    
//     printf("FIFO READ complete: buffer prepared with %d bytes, fifo now has %d bytes\n", bytes_to_read, chip->fifo_len);
  } else {
    chip->spi_buffer[0] = 0;
    chip->read_count = 1;
    // printf("FIFO READ: empty FIFO, returning 0\n", chip->fifo_len);
  }
}

static void handle_spi_read_command(chip_state_t *chip) {
  uint8_t val = chip->registers[chip->current_address];
  
  if (chip->current_address == VERSION_REG) {
    read_version_register(chip);
  } else if (chip->current_address == 0x04) {
    read_comirq_register(chip);
  } else if (chip->current_address == 0x0A) {
    read_fifo_level_register(chip);
  } else if (chip->current_address == 0x09) {
    read_fifo_data_register(chip);
  } else if (chip->current_address == 0x0C) {
    // Чтение ControlReg - важно для библиотеки MFRC522
    chip->spi_buffer[0] = val;
    chip->read_count = 1;
//     printf("ControlReg read: 0x%02X (RxLastBits=%d)\n", val, val & 0x07);
  } else if (chip->current_address == 0x06) {
    // Чтение ErrorReg - важно для библиотеки MFRC522
    chip->spi_buffer[0] = val;
    chip->read_count = 1;
//     printf("ErrorReg read: 0x%02X\n", val);
  } else if (chip->current_address == 0x36) {
    // Чтение AutoTestReg - важно для self-test
    chip->spi_buffer[0] = val;
    chip->read_count = 1;
//     printf("AutoTestReg read: 0x%02X\n", val);
  } else {
    chip->spi_buffer[0] = val;
    chip->read_count = 1;
  }
}

static void write_fifo_register(chip_state_t *chip, uint8_t val) {
  if (chip->fifo_len < FIFO_SIZE) {
    fifo_push(chip, val);
    
    // Отладка для всех входящих байтов
//     printf("FIFO push: 0x%02X (len %d)", val, chip->fifo_len);
    
    // Если это начало SELECT команды
//     if (val == 0x93 && chip->fifo_len == 1) {
//       printf(" - SELECT command start\n");
//     }
//     // Если это второй байт SELECT (0x70)
//     else if (chip->fifo_len == 2 && chip->fifo[0] == 0x93 && val == 0x70) {
//       printf(" - SELECT second byte\n");
//     }
//     // Если это байты UID в SELECT команде
//     else if (chip->fifo_len >= 3 && chip->fifo_len <= 6 && chip->fifo[0] == 0x93 && chip->fifo[1] == 0x70) {
//       printf(" - SELECT UID byte %d\n", chip->fifo_len - 2);
//     }
//     // Если это байты CRC в SELECT команде
//     else if (chip->fifo_len >= 7 && chip->fifo_len <= 9 && chip->fifo[0] == 0x93 && chip->fifo[1] == 0x70) {
//       printf(" - SELECT CRC byte %d\n", chip->fifo_len - 7);
//     } else {
//       printf("\n");
//     }

    // Проверяем, собираем ли мы SELECT команду
    if (chip->fifo[0] == CMD_SEL_CL1 && chip->anticoll_step == 1) {
//       printf("Building SELECT command: %d bytes received: ", chip->fifo_len);
//       for (int i = 0; i < chip->fifo_len; i++) {
//         printf("%02X ", chip->fifo[i]);
//       }
//       printf("\n");
      
      // Если получили все 9 байт SELECT команды
      if (chip->fifo_len == 9) {
//         printf("Full SELECT command received, processing...\n");
        process_mifare_command(chip);
      }
    }
  } else {
    printf("FIFO full, ignoring: 0x%02X\n", val);
  }
}

static void write_command_register(chip_state_t *chip, uint8_t val) {
//   printf("CommandReg = 0x%02X\n", val);
  switch (val) {
    case CMD_IDLE: // 0x00
      // printf("Command 0x00 - PCD_Idle (Idle)\n");
      // Clear command related IRQ flags: IdleIRq, RxIRq, TxIRq, ErrIRq
      chip->registers[0x04] &= ~(0x10 | 0x20 | 0x40 | 0x08);
      chip->registers[0x01] = 0x00; // Явно устанавливаем CommandReg в Idle
      break;

    case CMD_MEM: // 0x01
      // printf("Command 0x01 - PCD_Mem (Transfer FIFO to internal buffer)\n");
      set_specific_irq_flag(chip, 0x10); // IdleIRq
      chip->registers[0x01] = 0x00; // Go to Idle
      break;

    case CMD_GEN_RANDOM_ID: // 0x02
      // printf("Command 0x02 - PCD_GenerateRandomID (Generate random ID)\n");
      set_specific_irq_flag(chip, 0x10); // IdleIRq
      chip->registers[0x01] = 0x00; // Go to Idle
      break;

    case CMD_CALC_CRC: // 0x03
      // printf("Command 0x03 - PCD_CalcCRC (Calculate CRC)\n");
      if (chip->registers[0x36] == 0x09) { // Self-test mode
        // printf("Self-test mode detected - generating 64 bytes of test data\n");
        const uint8_t self_test_data[64] = {
          0x00, 0xEB, 0x66, 0xBA, 0x57, 0xBF, 0x23, 0x95,
          0xD0, 0xE3, 0x0D, 0x3D, 0x27, 0x89, 0x5C, 0xDE,
          0x9D, 0x3B, 0xA7, 0x00, 0x21, 0x5B, 0x89, 0x82,
          0x51, 0x3A, 0xEB, 0x02, 0x0C, 0xA5, 0x00, 0x49,
          0x7C, 0x84, 0x4D, 0xB3, 0xCC, 0xD2, 0x1B, 0x81,
          0x5D, 0x48, 0x76, 0xD5, 0x71, 0x61, 0x21, 0xA9,
          0x86, 0x96, 0x83, 0x38, 0xCF, 0x9D, 0x5B, 0x6D,
          0xDC, 0x15, 0xBA, 0x3E, 0x7D, 0x95, 0x3B, 0x2F
        };
        chip->fifo_len = 0;
        memcpy(chip->fifo, self_test_data, 64);
        chip->fifo_len = 64;
        update_fifo_level_register(chip);
        // printf("Self-test data generated: 64 bytes in FIFO\n");
      } else {
        perform_crc_calculation(chip);
      }
      chip->registers[0x01] = 0x00; // Go to Idle
      break;

    case CMD_TRANSMIT: // 0x04
      // printf("Command 0x04 - PCD_Transmit (Transmit data from FIFO)\n");
      set_specific_irq_flag(chip, 0x20); // TxIRq
      chip->registers[0x01] = 0x00; // Go to Idle
      break;

    case CMD_RECEIVE: // 0x08
      // printf("Command 0x08 - PCD_Receive (Activate receiver)\n");
      set_specific_irq_flag(chip, 0x10); // IdleIRq
      chip->registers[0x01] = 0x00; // Go to Idle
      break;

    case 0x0C: // PCD_Transceive
      // printf("Command 0x0C - PCD_Transceive (Transmit and receive)\n");
      if (chip->fifo_len > 0) {
    process_mifare_command(chip);
      }
      chip->registers[0x01] = 0x00; // Go to Idle
      break;

    case 0x0E: // PCD_MFAuthent
      // printf("Command 0x0E - PCD_MFAuthent (MIFARE Authenticate)\n");
      if (chip->fifo_len >= 1 && (chip->fifo[0] == CMD_AUTH_A || chip->fifo[0] == CMD_AUTH_B)) {
          printf("Authentication successful (simulated)\n");
          chip->authenticated = true;
          set_specific_irq_flag(chip, 0x10); // IdleIRq
    } else {
          printf("Authentication failed: incorrect command in FIFO (len=%d)\n", chip->fifo_len);
      }
      chip->fifo_len = 0;
      update_fifo_level_register(chip);
      chip->registers[0x01] = 0x00; // Go to Idle
      break;

    case 0x0F: { // PCD_SoftReset
      // printf("Command 0x0F - PCD_SoftReset (Soft Reset)\n");
      reset_chip_state(chip);
      chip->registers[0x01] = 0x00; // Явно сбрасываем CommandReg в Idle
      break;
    }

    case 0x10: { // Special handling for PowerDown bit (bit 4 of CommandReg)
      // This case handles writing 0x10 to CommandReg, which implies PCD_SoftPowerDown.
      // printf("Command 0x10 - PCD_SoftPowerDown (Power Down)\n");
      chip->registers[0x01] = val; // Set the command to PowerDown
      chip->registers[0x04] |= 0x10; // Set IdleIRq (from datasheet, or observation)
      chip->registers[0x04] &= ~(0x20 | 0x40); // Clear RxIRq and TxIRq
      break;
    }

    default:
      // Other commands may just set the CommandReg and let other logic handle it.
      // If it's a command that transitions from PowerDown to Active (PCD_SoftPowerUp)
      // The library does this by writing any non-0x10 value to CommandReg
      if ((chip->registers[0x01] & 0x10) && !(val & 0x10)) {
        // printf("Transition from PowerDown to Active (PCD_SoftPowerUp) by writing 0x%02X\n", val);
        chip->registers[0x01] = val; // Store the new command
        chip->registers[0x04] &= ~0x10; // Clear IdleIRq (indicating wake-up)
        // LoAlertIRq and HiAlertIRq might be set here depending on specific conditions
        // For simplicity, we can set them if the library expects them for a successful power up.
        // As per MFRC522.cpp, PCD_SoftPowerUp expects PowerDown bit to be cleared.
        // It does not explicitly check HiAlertIRq/LoAlertIRq.
      } else {
        chip->registers[0x01] = val; // Default: just store the value
      }
      break;
  }
}

static void handle_spi_write_command(chip_state_t *chip, uint8_t val) {
  uint8_t reg = chip->current_address;

  if (reg == 0x09) {
    write_fifo_register(chip, val);
  } else if (reg == 0x0A) {
    // FIFOLevelReg — возможен флаг FlushBuffer (бит 7)
    if (val & 0x80) {
//       printf("FIFO flush requested (write 0x%02X to FIFOLevelReg). Clearing FIFO (was %d bytes)\n", val, chip->fifo_len);
      chip->fifo_len = 0;
      update_fifo_level_register(chip);
    }
    chip->registers[reg] = val & 0x7F; // сохраняем без бита FlushBuffer
  } else if (reg == 0x01) {
    write_command_register(chip, val);
  }
  else if (reg == 0x04) {
    // Специальная обработка ComIrqReg
    {
//       printf("Direct write to ComIrqReg: 0x%02X (before: 0x%02X)\n", val, chip->registers[reg]);
      // If the library attempts to clear flags by writing 0x7F, clear all flags in our emulator.
      if (val == 0x7F) {
          chip->registers[reg] = 0x00; // Correctly clear all IRQ flags
          // printf("ComIrqReg cleared to 0x00 after 0x7F write.\n");
  } else {
          // For other values, just update the register. Specific flags will be set/cleared by other handlers.
          chip->registers[reg] = val;
//           printf("ComIrqReg after write: 0x%02X\n", chip->registers[reg]);
      }
      return; // Заменено break; на return;
    }
  } else if (reg == 0x08) { // Status2Reg
//     printf("Write to Status2Reg: 0x%02X\n", val);
    // If MFCrypto1On (bit 3) is being cleared, we should exit authenticated state.
    if ((chip->registers[reg] & 0x08) && !(val & 0x08)) {
//         printf("Exiting authenticated state.\n");
        chip->authenticated = false;
    }
    chip->registers[reg] = val;
  } else if (reg == 0x36) { // AutoTestReg
//     printf("Write to AutoTestReg: 0x%02X\n", val);
    chip->registers[reg] = val;
  }
  else {
    chip->registers[reg] = val;
  }
  chip->spi_buffer[0] = 0;
}

// FIFO management functions
static void fifo_push(chip_state_t *chip, uint8_t val) {
  if (chip->fifo_len < FIFO_SIZE) {
    chip->fifo[chip->fifo_len] = val;
    chip->fifo_len++;
    update_fifo_level_register(chip);
  }
}

static void fifo_remove_bytes(chip_state_t *chip, int bytes_to_remove) {
  if (bytes_to_remove > 0 && bytes_to_remove <= chip->fifo_len) {
//     printf("FIFO REMOVE: before memmove, len=%d, content: ", chip->fifo_len);
//     for(int i = 0; i < chip->fifo_len; ++i) printf("%02X ", chip->fifo[i]);
//     printf("\n");

    memmove(chip->fifo, chip->fifo + bytes_to_remove, 
            (chip->fifo_len - bytes_to_remove) * sizeof(chip->fifo[0]));
    memset(chip->fifo + (chip->fifo_len - bytes_to_remove), 0, bytes_to_remove); // Explicitly zero out removed part

    chip->fifo_len -= bytes_to_remove;
    update_fifo_level_register(chip);
    
    // MFRC522 сбрасывает RxIRq, когда FIFO пуст после удаления байтов
    if (chip->fifo_len == 0) {
        clear_irq_flag(chip, 0x20); // Сбросить RxIRq (0x20)
//         printf("RxIRq (0x20) cleared because FIFO is empty after remove.\n");
    }
//     printf("FIFO REMOVE: after memmove and zeroing, len=%d, content: ", chip->fifo_len);
//     for(int i = 0; i < chip->fifo_len; ++i) printf("%02X ", chip->fifo[i]);
//     printf("\n");
  }
}

static void update_fifo_level_register(chip_state_t *chip) {
  chip->registers[0x0A] = chip->fifo_len;
}

// State management functions
static void reset_chip_state(chip_state_t *chip) {
  chip->authenticated = false;
  chip->card_selected = false;
  chip->anticoll_step = 0;
  chip->uid_read_completed = false;
  chip->cascade_level = 1;
  chip->current_level_known_bits = 0;
  chip->select_completed = false;
  chip->select_response_sent = 0;
  chip->registers[0x04] = 0;  // Сбрасываем все флаги IRQ
//   printf("Chip state reset - ComIrqReg cleared to 0x00\n");
}

static void set_irq_flag(chip_state_t *chip) {
//   printf("set_irq_flag called - before: ComIrqReg: 0x%02X\n", chip->registers[0x04]);
  chip->registers[0x04] |= (0x10 | 0x20);  // IdleIRq (0x10) + RxIRq (0x20)
  chip->registers[0x0D] &= ~0x80;
//   printf("set_irq_flag called - after: ComIrqReg: 0x%02X\n", chip->registers[0x04]);
}

static void clear_irq_flag(chip_state_t *chip, uint8_t flag) {
  chip->registers[0x04] &= ~flag;
//   printf("IRQ flag 0x%02X cleared - ComIrqReg: 0x%02X\n", flag, chip->registers[0x04]);
}

static void set_specific_irq_flag(chip_state_t *chip, uint8_t flag) {
//   printf("Before setting flag 0x%02X - ComIrqReg: 0x%02X\n", flag, chip->registers[0x04]);
  chip->registers[0x04] |= flag;
//   printf("After setting flag 0x%02X - ComIrqReg: 0x%02X\n", flag, chip->registers[0x04]);
}

static void log_chip_state(chip_state_t *chip, const char *context) {
//   printf("=== CHIP STATE [%s] ===\n", context);
//   printf("ComIrqReg: 0x%02X, FIFOLevel: %d, ControlReg: 0x%02X\n", 
//          chip->registers[0x04], chip->registers[0x0A], chip->registers[0x0C]);
//   printf("FIFO content: ");
//   for (int i = 0; i < chip->fifo_len && i < 8; i++) {
//     printf("%02X ", chip->fifo[i]);
//   }
//   printf("(len=%d)\n", chip->fifo_len);
//   printf("anticoll_step: %d, card_selected: %s, uid_read_completed: %s, select_completed: %s\n", 
//          chip->anticoll_step, chip->card_selected ? "true" : "false", 
//          chip->uid_read_completed ? "true" : "false", chip->select_completed ? "true" : "false");
//   printf("====================\n");
}

void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count) {
  chip_state_t *chip = (chip_state_t *)user_data;

  if (pin_read(chip->cs_pin) == HIGH) {
    return; // CS is high, transaction is over.
  }

  switch (chip->spi_transaction_state) {
    case SPI_STATE_IDLE: {
      uint8_t cmd_byte = buffer[0];
      chip->current_address = (cmd_byte >> 1) & 0x3F;
      chip->is_read = (cmd_byte & 0x80) != 0;
      
      if (chip->stream_write_to_fifo && (chip->current_address != 0x09 || chip->is_read)) {
        chip->stream_write_to_fifo = false;
      }

    // if (chip->current_address == 0x09 || chip->current_address == 0x01) {
    //       printf("SPI cmd: 0x%02X, addr: 0x%02X, read: %s\n", cmd_byte, chip->current_address, chip->is_read ? "yes" : "no");
    // }

    if (chip->is_read) {
      handle_spi_read_command(chip);
        if (chip->read_count > 0) {
      spi_start(chip->spi, chip->spi_buffer, chip->read_count);
        }
        chip->spi_transaction_state = SPI_STATE_IDLE; // Stay idle, ready for next command
      } else { // It's a write command
        if (chip->current_address == 0x09) {
            chip->stream_write_to_fifo = true;
        }
        chip->spi_transaction_state = SPI_STATE_WAIT_DATA;
        spi_start(chip->spi, chip->spi_buffer, 1); // Wait for the data byte
      }
      break;
    }

    case SPI_STATE_WAIT_DATA: {
      uint8_t data_byte = buffer[0];
      handle_spi_write_command(chip, data_byte);

      if (chip->stream_write_to_fifo) {
        chip->spi_transaction_state = SPI_STATE_WAIT_DATA;
    spi_start(chip->spi, chip->spi_buffer, 1);
  } else {
        chip->spi_transaction_state = SPI_STATE_IDLE;
      }
      break;
    }
  }
}