/*
  Wokwi Custom SPI Chip example

  The chip implements a simple ROT13 letter substitution cipher:
  https://en.wikipedia.org/wiki/ROT13

  See https://docs.wokwi.com/chips-api/getting-started for more info about custom chips
*/

#include <SPI.h>

#define CS 10

void setup() {
  char buffer[] = "Uryyb, FCV! ";

  Serial.begin(115200);
  pinMode(CS, OUTPUT);

  // SPI Transaction: sends the contents of buffer, and overwrites it with the received data.
  digitalWrite(CS, LOW);
  SPI.begin();
  SPI.transfer(buffer, strlen(buffer));
  SPI.end();
  digitalWrite(CS, HIGH);

  Serial.println("Data received from SPI device:");
  Serial.println(buffer);
}

void loop() {
}
