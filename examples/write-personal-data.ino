/*
 * Write personal data of a MIFARE RFID card using a RFID-RC522 reader
 * Uses MFRC522 - Library to use ARDUINO RFID MODULE KIT 13.56 MHZ WITH TAGS SPI W AND R BY COOQROBOT. 
 * -----------------------------------------------------------------------------------------
 *             MFRC522      Arduino       Arduino   Arduino    Arduino          Arduino
 *             Reader/PCD   Uno/101       Mega      Nano v3    Leonardo/Micro   Pro Micro
 * Signal      Pin          Pin           Pin       Pin        Pin              Pin
 * -----------------------------------------------------------------------------------------
 * RST/Reset   RST          9             5         D9         RESET/ICSP-5     RST
 * SPI SS      SDA(SS)      10            53        D10        10               10
 * SPI MOSI    MOSI         11 / ICSP-4   51        D11        ICSP-4           16
 * SPI MISO    MISO         12 / ICSP-1   50        D12        ICSP-1           14
 * SPI SCK     SCK          13 / ICSP-3   52        D13        ICSP-3           15
 *
 * More pin layouts for other boards can be found here: https://github.com/miguelbalboa/rfid#pin-layout
 *
 * Hardware required:
 * Arduino
 * PCD (Proximity Coupling Device): NXP MFRC522 Contactless Reader IC
 * PICC (Proximity Integrated Circuit Card): A card or tag using the ISO 14443A interface, eg Mifare or NTAG203.
 * The reader can be found on eBay for around 5 dollars. Search for "mf-rc522" on ebay.com. 
 */

 #include <SPI.h>
 #include <MFRC522.h>
 
 #define RST_PIN         9           // Configurable, see typical pin layout above
 #define SS_PIN          10          // Configurable, see typical pin layout above
 
 MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance
 
 void setup() {
   Serial.begin(9600);        // Initialize serial communications with the PC
   SPI.begin();               // Init SPI bus
   mfrc522.PCD_Init();        // Init MFRC522 card
   Serial.println(F("Write personal data on a MIFARE PICC "));
 }
 
 void loop() {
 
   // Prepare key - all keys are set to FFFFFFFFFFFFh at chip delivery from the factory.
   MFRC522::MIFARE_Key key;
   for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;
 
   // Reset the loop if no new card present on the sensor/reader. This saves the entire process when idle.
   if ( ! mfrc522.PICC_IsNewCardPresent()) {
     return;
   }
 
   // Select one of the cards
   if ( ! mfrc522.PICC_ReadCardSerial()) {
     return;
   }
 
   Serial.print(F("Card UID:"));    //Dump UID
   for (byte i = 0; i < mfrc522.uid.size; i++) {
     Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
     Serial.print(mfrc522.uid.uidByte[i], HEX);
   }
   Serial.print(F(" PICC type: "));   // Dump PICC type
   MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
   Serial.println(mfrc522.PICC_GetTypeName(piccType));
 
   byte buffer[34]; // Этот буфер больше не будет использоваться для ввода, но может быть использован для временных операций
   byte block;
   MFRC522::StatusCode status;
   byte len;

   // Вместо чтения ввода, используем фиксированные данные
   const byte block1_data[16] = {'H', 'e', 'l', 'l', 'o', ' ', 'W', 'o', 'r', 'l', 'd', ' ', ' ', ' ', ' ', ' '};
   const byte block2_data[16] = {'T', 'h', 'i', 's', ' ', 'i', 's', ' ', 'b', 'l', 'o', 'c', 'k', ' ', '2', '.'};
   const byte block4_data[16] = {'A', 'n', 'o', 't', 'h', 'e', 'r', ' ', 'd', 'a', 't', 'a', ' ', 'b', 'l', 'k'};
   const byte block5_data[16] = {'F', 'i', 'n', 'a', 'l', ' ', 'd', 'a', 't', 'a', ' ', 'b', 'l', 'o', 'c', 'k'};
 
   block = 1;
   //Serial.println(F("Authenticating using key A..."));
   status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
   if (status != MFRC522::STATUS_OK) {
     Serial.print(F("PCD_Authenticate() failed: "));
     Serial.println(mfrc522.GetStatusCodeName(status));
     return;
   }
   else Serial.println(F("PCD_Authenticate() success: "));
 
   // Write block
   status = mfrc522.MIFARE_Write(block, (byte *)block1_data, 16);
   if (status != MFRC522::STATUS_OK) {
     Serial.print(F("MIFARE_Write() failed: "));
     Serial.println(mfrc522.GetStatusCodeName(status));
     return;
   }
   else Serial.println(F("MIFARE_Write() success: "));
   
   block = 2;
   //Serial.println(F("Authenticating using key A..."));
   status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
   if (status != MFRC522::STATUS_OK) {
     Serial.print(F("PCD_Authenticate() failed: "));
     Serial.println(mfrc522.GetStatusCodeName(status));
     return;
   }
 
   // Write block
   status = mfrc522.MIFARE_Write(block, (byte *)block2_data, 16);
   if (status != MFRC522::STATUS_OK) {
     Serial.print(F("MIFARE_Write() failed: "));
     Serial.println(mfrc522.GetStatusCodeName(status));
     return;
   }
   else Serial.println(F("MIFARE_Write() success: "));
 
   block = 4;
   //Serial.println(F("Authenticating using key A..."));
   status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
   if (status != MFRC522::STATUS_OK) {
     Serial.print(F("PCD_Authenticate() failed: "));
     Serial.println(mfrc522.GetStatusCodeName(status));
     return;
   }
 
   // Write block
   status = mfrc522.MIFARE_Write(block, (byte *)block4_data, 16);
   if (status != MFRC522::STATUS_OK) {
     Serial.print(F("MIFARE_Write() failed: "));
     Serial.println(mfrc522.GetStatusCodeName(status));
     return;
   }
   else Serial.println(F("MIFARE_Write() success: "));
 
   block = 5;
   //Serial.println(F("Authenticating using key A..."));
   status = mfrc522.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(mfrc522.uid));
   if (status != MFRC522::STATUS_OK) {
     Serial.print(F("PCD_Authenticate() failed: "));
     Serial.println(mfrc522.GetStatusCodeName(status));
     return;
   }
 
   // Write block
   status = mfrc522.MIFARE_Write(block, (byte *)block5_data, 16);
   if (status != MFRC522::STATUS_OK) {
     Serial.print(F("MIFARE_Write() failed: "));
     Serial.println(mfrc522.GetStatusCodeName(status));
     return;
   }
   else Serial.println(F("MIFARE_Write() success: "));
 
 
   Serial.println(" ");
   mfrc522.PICC_HaltA(); // Halt PICC
   mfrc522.PCD_StopCrypto1();  // Stop encryption on PCD

   delay(999999999999999);
 
 }
 