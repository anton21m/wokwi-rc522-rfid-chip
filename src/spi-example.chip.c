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
  printf("CRC calculated for %d bytes -> %02X %02X, DivIrqReg: 0x%02X\n", chip->fifo_len, chip->registers[0x22], chip->registers[0x21], chip->registers[0x05]);
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
  chip->registers[0x04] = 0x00;        // ComIrqReg - все флаги сброшены
  chip->registers[0x06] = 0x00;        // ErrorReg - нет ошибок
  chip->registers[0x0A] = 0x00;        // FIFOLevelReg
  chip->registers[0x0C] = 0x80;        // ControlReg (PowerOn=1)
  
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
  set_specific_irq_flag(chip, 0x04);  // RxIRq
  chip->anticoll_step = 0;
  log_chip_state(chip, "after REQA/WUPA");
}

static void handle_anticoll_command(chip_state_t *chip) {
  printf("ANTICOLL command - step: %d, fifo_len: %d\n", chip->anticoll_step, chip->fifo_len);
  log_chip_state(chip, "before ANTICOLL processing");
  
  if (chip->fifo_len > 0) {
    printf("ANTICOLL fifo content: ");
    for (int i = 0; i < chip->fifo_len && i < 4; i++) {
      printf("%02X ", chip->fifo[i]);
    }
    printf("\n");
  }
  
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
    set_specific_irq_flag(chip, 0x04);  // RxIRq
    chip->anticoll_step = 1;
    chip->current_level_known_bits = 32;  // Все 32 бита известны
    printf("ANTICOLL processed - UID and BCC in FIFO: %02X %02X %02X %02X %02X\n",
           chip->fifo[0], chip->fifo[1], chip->fifo[2], chip->fifo[3], chip->fifo[4]);
  }
  
  log_chip_state(chip, "after ANTICOLL processing");
}

static void handle_select_command(chip_state_t *chip) {
  printf("SELECT command detected - processing full UID selection\n");
  printf("SELECT command content: ");
  for (int i = 0; i < chip->fifo_len; i++) {
    printf("%02X ", chip->fifo[i]);
  }
  printf("\n");

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
    set_specific_irq_flag(chip, 0x04);  // RxIRq

    chip->card_selected = true;
    chip->select_completed = true;

    printf("SELECT completed - SAK+CRC sent: %02X %02X %02X\n",
           chip->fifo[0], chip->fifo[1], chip->fifo[2]);
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

  uint8_t cmd = chip->fifo[0];
  printf("Processing MIFARE command: 0x%02X (fifo_len=%d, anticoll_step=%d)\n", cmd, chip->fifo_len, chip->anticoll_step);

  switch (cmd) {
    case CMD_REQA:
    case CMD_WUPA:
      printf("Handling REQA/WUPA command\n");
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
  printf("ComIrqReg read: 0x%02X (IRQ flags: RxIRq=%d, IdleIRq=%d, LoAlertIRq=%d, ErrIRq=%d, TimerIRq=%d, TxIRq=%d, IdleIRq=%d)\n", 
         chip->registers[0x04],
         (chip->registers[0x04] & 0x04) ? 1 : 0,  // RxIRq
         (chip->registers[0x04] & 0x01) ? 1 : 0,  // IdleIRq
         (chip->registers[0x04] & 0x02) ? 1 : 0,  // LoAlertIRq
         (chip->registers[0x04] & 0x08) ? 1 : 0,  // ErrIRq
         (chip->registers[0x04] & 0x10) ? 1 : 0,  // TimerIRq
         (chip->registers[0x04] & 0x20) ? 1 : 0,  // TxIRq
         (chip->registers[0x04] & 0x40) ? 1 : 0); // IdleIRq
}

static void read_fifo_level_register(chip_state_t *chip) {
  chip->spi_buffer[0] = chip->fifo_len;
  chip->read_count = 1;
  printf("FIFOLevelReg read: %d bytes\n", chip->fifo_len);
}

static void read_fifo_data_register(chip_state_t *chip) {
  if (chip->fifo_len > 0) {
    uint8_t bytes_to_read = chip->fifo_len;
    if (bytes_to_read > 18) bytes_to_read = 18;
    printf("FIFO READ: fifo_len=%d, bytes_to_read=%d, first_byte=0x%02X\n",
           chip->fifo_len, bytes_to_read, chip->fifo[0]);
    
    memcpy(chip->spi_buffer, chip->fifo, bytes_to_read);
    chip->read_count = bytes_to_read;
    
    chip->fifo_len = 0;
    update_fifo_level_register(chip);
    
    clear_irq_flag(chip, 0x04); // Сбросить RxIRq (0x04)
    printf("RxIRq (0x04) cleared because FIFO is being read.\n");
    
    printf("FIFO READ complete: buffer prepared with %d bytes, fifo now empty\n", bytes_to_read);
  } else {
    chip->spi_buffer[0] = 0;
    chip->read_count = 1;
    printf("FIFO READ: empty FIFO, returning 0\n");
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
    printf("ControlReg read: 0x%02X (RxLastBits=%d)\n", val, val & 0x07);
  } else if (chip->current_address == 0x06) {
    // Чтение ErrorReg - важно для библиотеки MFRC522
    chip->spi_buffer[0] = val;
    chip->read_count = 1;
    printf("ErrorReg read: 0x%02X\n", val);
  } else {
    chip->spi_buffer[0] = val;
    chip->read_count = 1;
  }
}

static void write_fifo_register(chip_state_t *chip, uint8_t val) {
  if (chip->fifo_len < FIFO_SIZE) {
    fifo_push(chip, val);
    
    // Отладка для всех входящих байтов
    printf("FIFO push: 0x%02X (len %d)", val, chip->fifo_len);
    
    // Если это начало SELECT команды
    if (val == 0x93 && chip->fifo_len == 1) {
      printf(" - SELECT command start\n");
    }
    // Если это второй байт SELECT (0x70)
    else if (chip->fifo_len == 2 && chip->fifo[0] == 0x93 && val == 0x70) {
      printf(" - SELECT second byte\n");
    }
    // Если это байты UID в SELECT команде
    else if (chip->fifo_len >= 3 && chip->fifo_len <= 6 && chip->fifo[0] == 0x93 && chip->fifo[1] == 0x70) {
      printf(" - SELECT UID byte %d\n", chip->fifo_len - 2);
    }
    // Если это байты CRC в SELECT команде
    else if (chip->fifo_len >= 7 && chip->fifo_len <= 9 && chip->fifo[0] == 0x93 && chip->fifo[1] == 0x70) {
      printf(" - SELECT CRC byte %d\n", chip->fifo_len - 7);
    } else {
      printf("\n");
    }

    // Проверяем, собираем ли мы SELECT команду
    if (chip->fifo[0] == CMD_SEL_CL1 && chip->anticoll_step == 1) {
      printf("Building SELECT command: %d bytes received: ", chip->fifo_len);
      for (int i = 0; i < chip->fifo_len; i++) {
        printf("%02X ", chip->fifo[i]);
      }
      printf("\n");
      
      // Если получили все 9 байт SELECT команды
      if (chip->fifo_len == 9) {
        printf("Full SELECT command received, processing...\n");
        process_mifare_command(chip);
      }
    }
  } else {
    printf("FIFO full, ignoring: 0x%02X\n", val);
  }
}

static void write_command_register(chip_state_t *chip, uint8_t val) {
  printf("CommandReg = 0x%02X\n", val);
  if (val == 0x0F) { // PCD_SoftReset
    reset_chip_state(chip);
    printf("Reset command - state cleared\n");
  } else if (val == 0x0C) {
    printf("Command 0x0C - PCD_Transceive (Transmit and receive)\n");
    process_mifare_command(chip);
    chip->registers[0x01] = 0;
  } else if (val == 0x00) {
    printf("Command 0x00 - PCD_Idle (Idle)\n");
    chip->registers[0x01] = 0;
  } else if (val == 0x03) {
    // Команда 0x03 - PCD_CalcCRC (Calculate CRC)
    printf("Command 0x03 - PCD_CalcCRC (Calculate CRC)\n");
    perform_crc_calculation(chip);
    // В реальном чипе после завершения CRC пользователь обычно устанавливает Idle, 
    // но мы оставим в CommandReg текущее значение (0x03) — библиотека потом сбросит.
  }
}

static void handle_spi_write_command(chip_state_t *chip, uint8_t val) {
  uint8_t reg = chip->current_address;

  if (reg == 0x09) {
    write_fifo_register(chip, val);
  } else if (reg == 0x0A) {
    // FIFOLevelReg — возможен флаг FlushBuffer (бит 7)
    if (val & 0x80) {
      printf("FIFO flush requested (write 0x%02X to FIFOLevelReg). Clearing FIFO (was %d bytes)\n", val, chip->fifo_len);
      chip->fifo_len = 0;
      update_fifo_level_register(chip);
    }
    chip->registers[reg] = val & 0x7F; // сохраняем без бита FlushBuffer
  } else if (reg == 0x01) {
    write_command_register(chip, val);
  } else if (reg == 0x04) {
    // Специальная обработка ComIrqReg
    printf("Direct write to ComIrqReg: 0x%02X (before: 0x%02X)\n", val, chip->registers[0x04]);
    chip->registers[reg] = val;
    printf("ComIrqReg after write: 0x%02X\n", chip->registers[0x04]);
  } else {
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
    memmove(chip->fifo, chip->fifo + bytes_to_remove, 
            (chip->fifo_len - bytes_to_remove) * sizeof(chip->fifo[0]));
    chip->fifo_len -= bytes_to_remove;
    update_fifo_level_register(chip);
    
    // MFRC522 сбрасывает RxIRq, когда FIFO пуст после удаления байтов
    if (chip->fifo_len == 0) {
        clear_irq_flag(chip, 0x04); // Сбросить RxIRq (0x04)
        printf("RxIRq (0x04) cleared because FIFO is empty after remove.\n");
    }
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
  printf("Chip state reset - ComIrqReg cleared to 0x00\n");
}

static void set_irq_flag(chip_state_t *chip) {
  printf("set_irq_flag called - before: ComIrqReg: 0x%02X\n", chip->registers[0x04]);
  chip->registers[0x04] |= 0x05;  // IdleIRq (0x01) + RxIRq (0x04)
  chip->registers[0x0D] &= ~0x80;
  printf("set_irq_flag called - after: ComIrqReg: 0x%02X\n", chip->registers[0x04]);
}

static void clear_irq_flag(chip_state_t *chip, uint8_t flag) {
  chip->registers[0x04] &= ~flag;
  printf("IRQ flag 0x%02X cleared - ComIrqReg: 0x%02X\n", flag, chip->registers[0x04]);
}

static void set_specific_irq_flag(chip_state_t *chip, uint8_t flag) {
  printf("Before setting flag 0x%02X - ComIrqReg: 0x%02X\n", flag, chip->registers[0x04]);
  chip->registers[0x04] |= flag;
  printf("After setting flag 0x%02X - ComIrqReg: 0x%02X\n", flag, chip->registers[0x04]);
}

static void log_chip_state(chip_state_t *chip, const char *context) {
  printf("=== CHIP STATE [%s] ===\n", context);
  printf("ComIrqReg: 0x%02X, FIFOLevel: %d, ControlReg: 0x%02X\n", 
         chip->registers[0x04], chip->registers[0x0A], chip->registers[0x0C]);
  printf("FIFO content: ");
  for (int i = 0; i < chip->fifo_len && i < 8; i++) {
    printf("%02X ", chip->fifo[i]);
  }
  printf("(len=%d)\n", chip->fifo_len);
  printf("anticoll_step: %d, card_selected: %s, uid_read_completed: %s, select_completed: %s\n", 
         chip->anticoll_step, chip->card_selected ? "true" : "false", 
         chip->uid_read_completed ? "true" : "false", chip->select_completed ? "true" : "false");
  printf("====================\n");
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
      
      if (chip->current_address == 0x09 || chip->current_address == 0x01) {
          printf("SPI cmd: 0x%02X, addr: 0x%02X, read: %s\n", cmd_byte, chip->current_address, chip->is_read ? "yes" : "no");
      }

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
        spi_start(chip->spi, chip->spi_buffer, 1);
      }
      break;
    }
  }
}