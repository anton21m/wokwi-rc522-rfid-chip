# Wokwi project simulation chip rc522

This is a Wokwi project. Please edit this README file and add a description of your project.


# Wokwi demo projects
- https://wokwi.com/projects/432456562791073793 - test all function MRFC522
- https://wokwi.com/projects/436744920653539329 - multi rfid reader
- https://wokwi.com/projects/436746939600791553 - export chip


If you want to say thank you, you can buy me a tea or coffee by sending a small donation:
https://www.patreon.com/c/anton21m/membership  
https://boosty.to/anton21m

Greetings from Uzbekistan, Tashkent!


## Usage

1. Add parts by clicking the blue "+" button in the diagram editor
2. Connect parts by dragging wires between them
3. Click the green play button to start the simulation

### Setup wokwi token

```
curl -L https://wokwi.com/ci/install.sh | sh
wokwi-cli --version
export WOKWI_CLI_TOKEN=wok_*****
```

# Fast Start
1. Export the ready project to the `src` folder as is.
2. Edit the names in the Makefile and wokwi.toml.
3. Output of custom chips in VSCode is displayed in the tab `Output -> Wokwi chips`.


# Completed simulation steps
- Ability to select from 5 different card UIDs
- Firmware Version: 0x92 = v2.0
- Card recognized as MIFARE 1KB
- Output of UID, SAK
- Correct PICC_DumpToSerial()
- Passing the PCD_PerformSelfTest() test
- MIFARE_UnbrickUidSector(false): card repair, UID change
- Emulation of PCD_Authenticate(), MIFARE_Write

lib/MFRC522 - the original MRFC522 library with extended debug messages.
To build with it, change the Makefile target to: all: clean compile-chip compile-debug-arduino

ChangeLog: 
22.07.2025 Fix Change simulation PICC_IsNewCardPresent. Second Call is will "FAIL" (real chip)

```
arduino-cli compile --fqbn arduino:avr:uno mrfc-chip-example.ino --output-dir build
```


# Passing the MRFC522 Library Test

```
Тест MFRC522: старт
[PCD_Init] OK
[PCD_Reset] OK
[PCD_AntennaOn] OK
[PCD_AntennaOff] OK
[PCD_AntennaOn] OK
[PCD_GetAntennaGain] OK
[PCD_SetAntennaGain] OK
[PCD_WriteRegister] OK
[PCD_WriteRegisterMulti] OK
[PCD_ReadRegister] OK
[PCD_ReadRegisterMulti] OK
[PCD_SetRegisterBitMask] OK
[PCD_ClearRegisterBitMask] OK
[PCD_CalculateCRC] OK
[PCD_PerformSelfTest] OK
[PCD_SoftPowerDown] OK
[PCD_SoftPowerUp] OK
[PICC_RequestA] OK
[PICC_WakeupA] OK
[PICC_IsNewCardPresent] OK
[PICC_ReadCardSerial] OK
[PICC_Select] OK
[PICC_HaltA] OK
[PICC_GetType] OK
[PICC_GetTypeName] OK
Card UID: 50 9D 39 23
Card SAK: 08
PICC type: MIFARE 1KB
Sector Block   0  1  2  3   4  5  6  7   8  9 10 11  12 13 14 15  AccessBits
  15     63   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         62   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         61   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         60   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
  14     59   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         58   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         57   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         56   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
  13     55   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         54   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         53   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         52   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
  12     51   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         50   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         49   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         48   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
  11     47   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         46   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         45   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         44   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
  10     43   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         42   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         41   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         40   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
   9     39   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         38   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         37   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         36   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
   8     35   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         34   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         33   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         32   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
   7     31   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         30   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         29   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         28   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
   6     27   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         26   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         25   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         24   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
   5     23   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         22   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         21   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         20   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
   4     19   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         18   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         17   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         16   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
   3     15   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         14   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         13   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
         12   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
   2     11   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
         10   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
          9   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
          8   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
   1      7   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
          6   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
          5   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
          4   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
   0      3   FF FF FF FF  FF FF FF 07  80 69 FF FF  FF FF FF FF  [ 0 0 1 ] 
          2   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
          1   00 00 00 00  00 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 
          0   50 9D 39 23  D7 00 00 00  00 00 00 00  00 00 00 00  [ 0 0 0 ] 

[PICC_DumpToSerial] OK
[MIFARE_UnbrickUidSector] OK
[MIFARE_SetUid] OK
[PCD_Authenticate] OK
[MIFARE_Read] OK
[MIFARE_Write] OK
[MIFARE_Decrement] OK
[MIFARE_Increment] OK
[MIFARE_Restore] OK
[MIFARE_Transfer] OK
[MIFARE_Ultralight_Write] OK
[MIFARE_GetValue] OK
[MIFARE_SetValue] OK
[MIFARE_OpenUidBackdoor] OK
[MIFARE_UnbrickUidSectorBackdoor] OK
[GetStatusCodeName] OK
[PCD_StopCrypto1] OK
[PCD_TransceiveData] OK
[PCD_CommunicateWithPICC] OK
Тест MFRC522: завершён
```