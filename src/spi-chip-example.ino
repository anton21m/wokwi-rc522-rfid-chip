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


String tagID = "";


void setup() {
  Serial.begin(115200);
  while (!Serial);  // Ожидание открытия порта (для некоторых плат)
  Serial.println("Starting RC522 test...");

  SPI.begin();      
  mfrc522.PCD_Init(); // Инициализация чипа
  byte version = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.print("RC522 Version: 0x");
  Serial.println(version, HEX);
  
  for (uint8_t i = 0; i < 4; i++) {
      tagID.concat(String(mfrc522.uid.uidByte[i], HEX));
  }
  Serial.println(tagID);

  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String tagID = "";
    for (uint8_t i = 0; i < mfrc522.uid.size; i++) {
        if (mfrc522.uid.uidByte[i] < 0x10) tagID += "0";
        tagID += String(mfrc522.uid.uidByte[i], HEX);
    }
    Serial.println("Tag UID: " + tagID);
} else {
    Serial.println("No card present");
}


  // char buffer[] = "Uryyb, FCV! ";

  // Serial.begin(115200);
  // pinMode(CS, OUTPUT);

  // // SPI Transaction: sends the contents of buffer, and overwrites it with the received data.
  // digitalWrite(CS, LOW);
  // SPI.begin();
  // Serial.println("Sending data to SPI device...");
  // SPI.transfer(buffer, strlen(buffer));
  // SPI.end();
  // digitalWrite(CS, HIGH);

  // Serial.println("Data received from SPI device:");
  // Serial.println(buffer);
}

void loop() {
}
