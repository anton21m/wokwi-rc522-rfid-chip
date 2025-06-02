#include <SPI.h>
#include <MFRC522.h>

MFRC522 rfid(9, 10);

void setup(){
  Serial.begin(9600);
  SPI.begin();
  rfid.PCD_Init();
}
void loop(){
  //Serial.println("tests");
  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()  ) return;

  Serial.print("UID tag :");
  String content= "";

  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(rfid.uid.uidByte[i], HEX);
    content.concat(String(rfid.uid.uidByte[i] < 0x10 ? " 0" : " "));
    content.concat(String(rfid.uid.uidByte[i], HEX));
  }
  Serial.print("Message : ");
  content.toUpperCase();
  if (content.substring(1) == "BD 31 15 2B") {
    Serial.println("Authorized access");
  } else {
    Serial.println(" Access denied");
  }
  delay(3000);
} 