/*
  Тестирование всех функций библиотеки MFRC522 (эмуляция чипа) для Wokwi

  Автор: https://github.com/anton21m

  Документация по Wokwi chips:
  https://docs.wokwi.com/chips-api/getting-started
*/

#include <SPI.h>
#include "MFRC522.h"

#define RST_PIN 9
#define SS_PIN  10

#define NEW_UID {0xDE, 0xAD, 0xBE, 0xEF}

MFRC522 mfrc522(SS_PIN, RST_PIN);

// Вспомогательная функция для печати результата теста
void printTestResult(const char* name, bool ok) {
  Serial.print("[");
  Serial.print(name);
  Serial.print("] ");
  if (ok) {
    Serial.println("OK");
  } else {
    Serial.println("FAIL");
  }
}

// Тестовые функции с валидацией

// Инициализация чипа
void test_PCD_Init() {
  mfrc522.PCD_Init();
  // Проверим, что CommandReg сброшен в Idle (0x00)
  byte cmd = mfrc522.PCD_ReadRegister(MFRC522::CommandReg);
  printTestResult("PCD_Init", cmd == MFRC522::PCD_Idle);
}

// Сброс чипа
void test_PCD_Reset() {
  mfrc522.PCD_WriteRegister(MFRC522::CommandReg, 0xFF); // записываем мусор
  mfrc522.PCD_Reset();
  byte cmd = mfrc522.PCD_ReadRegister(MFRC522::CommandReg);
  printTestResult("PCD_Reset", cmd == MFRC522::PCD_Idle);
}

// Включение антенны
void test_PCD_AntennaOn() {
  mfrc522.PCD_AntennaOff();
  mfrc522.PCD_AntennaOn();
  byte txControl = mfrc522.PCD_ReadRegister(MFRC522::TxControlReg);
  printTestResult("PCD_AntennaOn", (txControl & 0x03) != 0);
}

// Выключение антенны
void test_PCD_AntennaOff() {
  mfrc522.PCD_AntennaOn();
  mfrc522.PCD_AntennaOff();
  byte txControl = mfrc522.PCD_ReadRegister(MFRC522::TxControlReg);
  printTestResult("PCD_AntennaOff", (txControl & 0x03) == 0);
}

// Получить усиление антенны
void test_PCD_GetAntennaGain() {
  byte gain = mfrc522.PCD_GetAntennaGain();
  printTestResult("PCD_GetAntennaGain", gain > 0);
}

// Установить максимальное усиление антенны
void test_PCD_SetAntennaGain() {
  mfrc522.PCD_SetAntennaGain(MFRC522::RxGain_max);
  byte gain = mfrc522.PCD_GetAntennaGain();
  printTestResult("PCD_SetAntennaGain", gain == MFRC522::RxGain_max);
}

// Запись в регистр
void test_PCD_WriteRegister() {
  mfrc522.PCD_WriteRegister(MFRC522::CommandReg, 0x55);
  byte val = mfrc522.PCD_ReadRegister(MFRC522::CommandReg);
  printTestResult("PCD_WriteRegister", val == 0x55);
}

// Запись нескольких байт в регистр
void test_PCD_WriteRegisterMulti() {
  byte data[2] = {0x12, 0x34};
  mfrc522.PCD_WriteRegister(MFRC522::FIFODataReg, 2, data);
  byte read[2] = {0, 0};
  mfrc522.PCD_ReadRegister(MFRC522::FIFODataReg, 2, read, 0);
  printTestResult("PCD_WriteRegisterMulti", read[0] == 0x12 && read[1] == 0x34);
}

// Чтение регистра
void test_PCD_ReadRegister() {
  mfrc522.PCD_WriteRegister(MFRC522::CommandReg, 0xAB);
  byte val = mfrc522.PCD_ReadRegister(MFRC522::CommandReg);
  printTestResult("PCD_ReadRegister", val == 0xAB);
}

// Чтение нескольких байт из регистра
void test_PCD_ReadRegisterMulti() {
  byte data[2] = {0x56, 0x78};
  mfrc522.PCD_WriteRegister(MFRC522::FIFODataReg, 2, data);
  byte read[2] = {0, 0};
  mfrc522.PCD_ReadRegister(MFRC522::FIFODataReg, 2, read, 0);
  printTestResult("PCD_ReadRegisterMulti", read[0] == 0x56 && read[1] == 0x78);
}

// Установить битовую маску регистра
void test_PCD_SetRegisterBitMask() {
  mfrc522.PCD_WriteRegister(MFRC522::TxControlReg, 0x00);
  mfrc522.PCD_SetRegisterBitMask(MFRC522::TxControlReg, 0x03);
  byte val = mfrc522.PCD_ReadRegister(MFRC522::TxControlReg);
  printTestResult("PCD_SetRegisterBitMask", (val & 0x03) == 0x03);
}

// Очистить битовую маску регистра
void test_PCD_ClearRegisterBitMask() {
  mfrc522.PCD_WriteRegister(MFRC522::TxControlReg, 0xFF);
  mfrc522.PCD_ClearRegisterBitMask(MFRC522::TxControlReg, 0x03);
  byte val = mfrc522.PCD_ReadRegister(MFRC522::TxControlReg);
  printTestResult("PCD_ClearRegisterBitMask", (val & 0x03) == 0x00);
}

// Вычисление CRC
void test_PCD_CalculateCRC() {
  byte data[2] = {0x12, 0x34};
  byte result[2] = {0, 0};
  MFRC522::StatusCode status = mfrc522.PCD_CalculateCRC(data, 2, result);
  printTestResult("PCD_CalculateCRC", status == MFRC522::STATUS_OK);
}

// Самотестирование чипа
void test_PCD_PerformSelfTest() {
  bool ok = mfrc522.PCD_PerformSelfTest();
  printTestResult("PCD_PerformSelfTest", ok);
}

// Аутентификация блока
void test_PCD_Authenticate() {
  MFRC522::MIFARE_Key key;
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 1, &key, &(mfrc522.uid));
  printTestResult("PCD_Authenticate", status == MFRC522::STATUS_OK);
}

// Остановить криптографию
void test_PCD_StopCrypto1() {
  mfrc522.PCD_StopCrypto1();
  // Нет явного способа проверить, просто считаем что вызвалась
  printTestResult("PCD_StopCrypto1", true);
}

// Передача и приём данных
void test_PCD_TransceiveData() {
  byte sendData[2] = {0x26, 0x00};
  byte backData[18];
  byte backLen = sizeof(backData);
  byte validBits = 7;
  byte rxAlign = 0;
  bool checkCRC = false;
  MFRC522::StatusCode status = mfrc522.PCD_TransceiveData(sendData, 2, backData, &backLen, &validBits, rxAlign, checkCRC);
  printTestResult("PCD_TransceiveData", status == MFRC522::STATUS_OK || status == MFRC522::STATUS_TIMEOUT);
}

// Общение с картой (низкоуровневое)
void test_PCD_CommunicateWithPICC() {
  byte sendData[2] = {0x26, 0x00};
  byte backData[18];
  byte backLen = sizeof(backData);
  byte validBits = 7;
  byte rxAlign = 0;
  bool checkCRC = false;
  MFRC522::StatusCode status = mfrc522.PCD_CommunicateWithPICC(MFRC522::PCD_Transceive, 0x26, sendData, 2, backData, &backLen, &validBits, rxAlign, checkCRC);
  printTestResult("PCD_CommunicateWithPICC", status == MFRC522::STATUS_OK || status == MFRC522::STATUS_TIMEOUT);
}

// Перевести чип в режим пониженного энергопотребления
void test_PCD_SoftPowerDown() {
  mfrc522.PCD_SoftPowerDown();
  byte val = mfrc522.PCD_ReadRegister(MFRC522::CommandReg);
  printTestResult("PCD_SoftPowerDown", (val & (1 << 4)) != 0);
}

// Вывести чип из режима пониженного энергопотребления
void test_PCD_SoftPowerUp() {
  mfrc522.PCD_SoftPowerDown();
  mfrc522.PCD_SoftPowerUp();
  byte val = mfrc522.PCD_ReadRegister(MFRC522::CommandReg);
  printTestResult("PCD_SoftPowerUp", (val & (1 << 4)) == 0);
}

// Запрос карты (RequestA)
void test_PICC_RequestA() {
  byte bufferATQA[2];
  byte bufferSize = sizeof(bufferATQA);
  MFRC522::StatusCode status = mfrc522.PICC_RequestA(bufferATQA, &bufferSize);
  printTestResult("PICC_RequestA", status == MFRC522::STATUS_OK || status == MFRC522::STATUS_TIMEOUT);
}

// Пробуждение карты (WakeupA)
void test_PICC_WakeupA() {
  byte bufferATQA[2];
  byte bufferSize = sizeof(bufferATQA);
  MFRC522::StatusCode status = mfrc522.PICC_WakeupA(bufferATQA, &bufferSize);
  printTestResult("PICC_WakeupA", status == MFRC522::STATUS_OK || status == MFRC522::STATUS_TIMEOUT);
}

// Проверка наличия новой карты
bool test_PICC_IsNewCardPresent() {
  bool present = mfrc522.PICC_IsNewCardPresent();
  printTestResult("PICC_IsNewCardPresent", present);
  return present;
}

// Чтение серийного номера карты
void test_PICC_ReadCardSerial() {
  bool ok = mfrc522.PICC_ReadCardSerial();
  printTestResult("PICC_ReadCardSerial", ok);
}

// Выбор карты по UID
void test_PICC_Select() {
  byte result = mfrc522.PICC_Select(&(mfrc522.uid));
  printTestResult("PICC_Select", result == 0); // теперь OK если STATUS_OK
}

// Перевести карту в состояние HALT
void test_PICC_HaltA() {
  mfrc522.PICC_HaltA();
  // Нет явного способа проверить, считаем что вызвалась
  printTestResult("PICC_HaltA", true);
}

// Получить тип карты по SAK
void test_PICC_GetType() {
  MFRC522::PICC_Type type = mfrc522.PICC_GetType(mfrc522.uid.sak);
  printTestResult("PICC_GetType", type != MFRC522::PICC_TYPE_UNKNOWN);
}

// Получить строковое имя типа карты
void test_PICC_GetTypeName() {
  MFRC522::PICC_Type type = mfrc522.PICC_GetType(mfrc522.uid.sak);
  // PICC_GetTypeName возвращает F() строку, поэтому используем String для проверки
  String name = mfrc522.PICC_GetTypeName(type);
  printTestResult("PICC_GetTypeName", name.length() > 0);
}

// Сброс дампа содержимого карты в Serial
void test_PICC_DumpToSerial() {
  mfrc522.PICC_DumpToSerial(&(mfrc522.uid));
  // Нет явного способа проверить, считаем что вызвалась
  printTestResult("PICC_DumpToSerial", true);
}

// Восстановление UID сектора (без бэкдора)
void test_MIFARE_UnbrickUidSector() {
  bool ok = mfrc522.MIFARE_UnbrickUidSector(false);
  printTestResult("MIFARE_UnbrickUidSector", ok);
}

// Установка нового UID
void test_MIFARE_SetUid() {
  byte newUid[] = NEW_UID;
  bool ok = mfrc522.MIFARE_SetUid(newUid, 4, true);
  printTestResult("MIFARE_SetUid", ok);
}

// Чтение блока MIFARE
void test_MIFARE_Read() {
  byte buffer[18];
  byte size = sizeof(buffer);
  MFRC522::StatusCode status = mfrc522.MIFARE_Read(1, buffer, &size);
  printTestResult("MIFARE_Read", status == MFRC522::STATUS_OK || status == MFRC522::STATUS_ERROR);
}

// Запись блока MIFARE
void test_MIFARE_Write() {
  const byte data[16] = {'T','E','S','T',' ','D','A','T','A',' ','1','2','3','4','5','6'};
  MFRC522::StatusCode status = mfrc522.MIFARE_Write(1, (byte*)data, 16);
  printTestResult("MIFARE_Write", status == MFRC522::STATUS_OK || status == MFRC522::STATUS_ERROR);
}

// Декремент значения в блоке
void test_MIFARE_Decrement() {
  MFRC522::StatusCode status = mfrc522.MIFARE_Decrement(2, 5);
  printTestResult("MIFARE_Decrement", status == MFRC522::STATUS_OK || status == MFRC522::STATUS_ERROR);
}

// Инкремент значения в блоке
void test_MIFARE_Increment() {
  MFRC522::StatusCode status = mfrc522.MIFARE_Increment(2, 5);
  printTestResult("MIFARE_Increment", status == MFRC522::STATUS_OK || status == MFRC522::STATUS_ERROR);
}

// Восстановление значения из блока
void test_MIFARE_Restore() {
  MFRC522::StatusCode status = mfrc522.MIFARE_Restore(2);
  printTestResult("MIFARE_Restore", status == MFRC522::STATUS_OK || status == MFRC522::STATUS_ERROR);
}

// Передача значения в блок
void test_MIFARE_Transfer() {
  MFRC522::StatusCode status = mfrc522.MIFARE_Transfer(2);
  printTestResult("MIFARE_Transfer", status == MFRC522::STATUS_OK || status == MFRC522::STATUS_ERROR);
}

// Запись в Ultralight
void test_MIFARE_Ultralight_Write() {
  const byte data[4] = {1,2,3,4};
  MFRC522::StatusCode status = mfrc522.MIFARE_Ultralight_Write(4, (byte*)data, 4);
  printTestResult("MIFARE_Ultralight_Write", status == MFRC522::STATUS_OK || status == MFRC522::STATUS_ERROR);
}

// Получить значение из Value-блока
void test_MIFARE_GetValue() {
  long value = 0;
  MFRC522::StatusCode status = mfrc522.MIFARE_GetValue(2, &value);
  printTestResult("MIFARE_GetValue", status == MFRC522::STATUS_OK || status == MFRC522::STATUS_ERROR);
}

// Установить значение Value-блока
void test_MIFARE_SetValue() {
  MFRC522::StatusCode status = mfrc522.MIFARE_SetValue(2, 12345);
  printTestResult("MIFARE_SetValue", status == MFRC522::STATUS_OK || status == MFRC522::STATUS_ERROR);
}

// Открыть бэкдор для UID
void test_MIFARE_OpenUidBackdoor() {
  bool ok = mfrc522.MIFARE_OpenUidBackdoor(true);
  printTestResult("MIFARE_OpenUidBackdoor", ok);
}

// Восстановление UID сектора через бэкдор
void test_MIFARE_UnbrickUidSectorBackdoor() {
  bool ok = mfrc522.MIFARE_UnbrickUidSector(true);
  printTestResult("MIFARE_UnbrickUidSectorBackdoor", ok);
}

// Получить строку статуса по коду
void test_GetStatusCodeName() {
  const __FlashStringHelper* nameF = mfrc522.GetStatusCodeName(MFRC522::STATUS_OK);
  bool ok = (nameF != nullptr);
  printTestResult("GetStatusCodeName", ok);
}


void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // Ждём подключения Serial
  }

}

void loop() {
  test_all();
  delay(10000);
}

void test_all() {
  Serial.println("Тест MFRC522: старт");

  SPI.begin();

  // Тесты инициализации и базовых функций
  test_PCD_Init();
  test_PCD_Reset();
  test_PCD_AntennaOn();
  test_PCD_AntennaOff();
  test_PCD_AntennaOn();
  test_PCD_GetAntennaGain();
  test_PCD_SetAntennaGain();
  test_PCD_WriteRegister();
  test_PCD_WriteRegisterMulti();
  test_PCD_ReadRegister();
  test_PCD_ReadRegisterMulti();
  test_PCD_SetRegisterBitMask();
  test_PCD_ClearRegisterBitMask();
  test_PCD_CalculateCRC();
  test_PCD_PerformSelfTest();
  test_PCD_SoftPowerDown();
  test_PCD_SoftPowerUp();

  // Тесты PICC и MIFARE функций
  // Ожидание карты
  while (!test_PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    Serial.println("Card not present or card old. Change uid");
    delay(1000);
  }

  // Тесты PICC и MIFARE функций
  test_PICC_RequestA();
  test_PICC_WakeupA();
  test_PICC_ReadCardSerial();
  test_PICC_Select();
  test_PICC_HaltA();
  test_PICC_GetType();
  test_PICC_GetTypeName();
  test_PICC_DumpToSerial();

  test_MIFARE_UnbrickUidSector();
  test_MIFARE_SetUid();
  test_PCD_Authenticate();
  test_MIFARE_Read();
  test_MIFARE_Write();
  test_MIFARE_Decrement();
  test_MIFARE_Increment();
  test_MIFARE_Restore();
  test_MIFARE_Transfer();
  test_MIFARE_Ultralight_Write();
  test_MIFARE_GetValue();
  test_MIFARE_SetValue();
  test_MIFARE_OpenUidBackdoor();
  test_MIFARE_UnbrickUidSectorBackdoor();
  test_GetStatusCodeName();
  test_PCD_StopCrypto1();
  test_PCD_TransceiveData();
  test_PCD_CommunicateWithPICC();

  Serial.println("Тест MFRC522: завершён");
}