/*
  Wokwi Custom SPI Chip example

  The chip implements a simple ROT13 letter substitution cipher:
  https://en.wikipedia.org/wiki/ROT13

  See https://docs.wokwi.com/chips-api/getting-started for more info about custom chips
*/

#include <SPI.h>
#include "MFRC522.h"

#define CS 10

#define RST_PIN 9 // RES pin
#define SS_PIN  10 // SDA (SS) pin

MFRC522 mfrc522(SS_PIN, RST_PIN); // создание объекта mfrc522

void setup() {
  Serial.begin(115200);
  while (!Serial);  // Ожидание открытия порта (для некоторых плат)
  Serial.println("Starting RC522 test...");

  SPI.begin();      
  mfrc522.PCD_Init(); // Инициализация чипа
  byte version = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print("RC522 Version: 0x");
  Serial.println(version, HEX);
  
  Serial.println("Waiting for card...");
}

void loop() {
  // Проверяем, есть ли новая карта
  Serial.println("Checking for card...");
  bool cardPresent = mfrc522.PICC_IsNewCardPresent();
  Serial.print("Card present: ");
  Serial.println(cardPresent ? "YES" : "NO");
  
  if (cardPresent) {
    Serial.println("Card detected, trying to read serial...");
    
    // Добавляем подробное логирование
    Serial.println("=== Starting UID read ===");
    bool readSuccess = mfrc522.PICC_ReadCardSerial();
    Serial.print("Read success: ");
    Serial.println(readSuccess ? "YES" : "NO");
    
    if (readSuccess) {
      Serial.println("Card detected!");
      
      // Выводим UID карты
      String tagID = "";
      Serial.print("UID size: ");
      Serial.println(mfrc522.uid.size);
      Serial.print("UID bytes: ");
      for (uint8_t i = 0; i < mfrc522.uid.size; i++) {
          if (mfrc522.uid.uidByte[i] < 0x10) tagID += "0";
          tagID += String(mfrc522.uid.uidByte[i], HEX);
          Serial.print("0x");
          Serial.print(mfrc522.uid.uidByte[i], HEX);
          Serial.print(" ");
      }
      Serial.println();
      Serial.println("Card UID: " + tagID);
      
      // Выводим SAK
      Serial.print("Card SAK: ");
      Serial.println(mfrc522.uid.sak, HEX);
      
      // Определяем тип карты
      MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
      Serial.print("PICC type: ");
      Serial.println(mfrc522.PICC_GetTypeName(piccType));
      
      // Небольшая задержка перед следующей проверкой
      delay(1000);
    } else {
      Serial.println("Failed to read card serial!");
      Serial.println("=== UID read failed ===");
    }
  }
  
  delay(1000); // Небольшая задержка для стабильности
}
