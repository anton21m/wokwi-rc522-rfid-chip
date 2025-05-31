z
#include <SPI.h>

void setup() {
  Serial.begin(9600);
  SPI.begin();
  pinMode(10, OUTPUT); // CS
}

void loop() {
  digitalWrite(10, LOW); // Выбор SPI-устройства
  byte response = SPI.transfer('H'); // Отправка символа 'H'
  digitalWrite(10, HIGH); // Снятие выбора

  Serial.print("Response: ");
  Serial.println((char)response);
  delay(1000);
}