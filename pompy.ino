#include <ESP8266WiFi.h>
#include <Wire.h>
#include <EEPROM.h>
#include <RTClib.h>
#include <TimeLib.h>
#include <PCF8574.h>

RTC_DS3231 rtc;
PCF8574 pcf(0x20);  // Adres I2C: 0x20. Jeśli inny, zmień odpowiednio (0x20-0x27)

const byte NUM_PUMPS = 8;

// Struktura przechowująca ustawienia dla jednej pompy
struct PumpSettings {
    byte status;     // 0 - wyłączona, 1 - włączona
    byte hour;       // godzina dozowania (0-23)
    byte minute;     // minuta dozowania (0-59)
    byte flow;       // przepływ w ml/min (wartość całkowita)
    byte flowDec;    // część dziesiętna przepływu (0-9)
    byte volume;     // objętość w ml (wartość całkowita)
    byte volumeDec;  // część dziesiętna objętości (0-9)
    byte days;       // bity dni tygodnia (bit 0 = niedziela, bit 6 = sobota)
};

PumpSettings pumps[NUM_PUMPS];
byte pumpStates = 0;  // Stan wszystkich pomp w jednym bajcie

// Sprawdza czy pompa powinna działać w dany dzień tygodnia
bool isDayEnabled(byte days, byte currentDay) {
    return (days & (1 << currentDay)) != 0;
}

// Włączanie/wyłączanie pojedynczej pompy
void setPump(byte pumpIndex, bool state) {
    if (state) {
        pumpStates |= (1 << pumpIndex);  // Ustaw bit
    } else {
        pumpStates &= ~(1 << pumpIndex); // Wyczyść bit
    }
    for (int i = 0; i < 8; i++) {
        pcf.digitalWrite(i, !(pumpStates & (1 << i))); // Zanegowane, bo PCF8574 ma odwróconą logikę
    }
}

// Dozowanie dla danej pompy
void dosePump(byte pumpIndex) {
    float volume = pumps[pumpIndex].volume + (pumps[pumpIndex].volumeDec * 0.1);
    float flow = pumps[pumpIndex].flow + (pumps[pumpIndex].flowDec * 0.1);
    
    // Oblicz czas dozowania w sekundach
    float dosingTime = (volume / flow) * 60;
    
    // Włącz pompę
    setPump(pumpIndex, true);
    
    // Czekaj określony czas
    delay(dosingTime * 1000);
    
    // Wyłącz pompę
    setPump(pumpIndex, false);
}

// Zapisywanie ustawień do EEPROM
void saveSettings() {
    int addr = 0;
    for (byte i = 0; i < NUM_PUMPS; i++) {
        EEPROM.write(addr++, pumps[i].status);
        EEPROM.write(addr++, pumps[i].hour);
        EEPROM.write(addr++, pumps[i].minute);
        EEPROM.write(addr++, pumps[i].flow);
        EEPROM.write(addr++, pumps[i].flowDec);
        EEPROM.write(addr++, pumps[i].volume);
        EEPROM.write(addr++, pumps[i].volumeDec);
        EEPROM.write(addr++, pumps[i].days);
    }
    EEPROM.commit();
}

// Wczytywanie ustawień z EEPROM
void loadSettings() {
    int addr = 0;
    for (byte i = 0; i < NUM_PUMPS; i++) {
        pumps[i].status = EEPROM.read(addr++);
        pumps[i].hour = EEPROM.read(addr++);
        pumps[i].minute = EEPROM.read(addr++);
        pumps[i].flow = EEPROM.read(addr++);
        pumps[i].flowDec = EEPROM.read(addr++);
        pumps[i].volume = EEPROM.read(addr++);
        pumps[i].volumeDec = EEPROM.read(addr++);
        pumps[i].days = EEPROM.read(addr++);
    }
}

// ... poprzednie funkcje ...

// Funkcja kalibracji
void calibratePump(byte pumpIndex, int calibrationTime) {
    Serial.print("Rozpoczynam kalibrację pompy ");
    Serial.println(pumpIndex);
    Serial.print("Czas kalibracji: ");
    Serial.print(calibrationTime);
    Serial.println(" sekund");
    Serial.println("Przygotuj menzurkę i naciśnij ENTER aby rozpocząć");
    
    while(Serial.read() != '\n') delay(100);
    
    // Włącz pompę na zadany czas
    setPump(pumpIndex, true);
    delay(calibrationTime * 1000);
    setPump(pumpIndex, false);
    
    Serial.println("Podaj zmierzoną objętość w ml (format: XX.X):");
    
    while(!Serial.available()) delay(100);
    float measuredVolume = Serial.parseFloat();
    
    // Oblicz przepływ w ml/min
    float flowRate = (measuredVolume * 60.0) / calibrationTime;
    
    // Zapisz obliczony przepływ
    pumps[pumpIndex].flow = (byte)flowRate;
    pumps[pumpIndex].flowDec = (byte)((flowRate - (byte)flowRate) * 10);
    
    Serial.print("Obliczony przepływ: ");
    Serial.print(flowRate, 1);
    Serial.println(" ml/min");
    
    saveSettings();
}

// Obsługa komend przez port szeregowy
void handleSerialCommands() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
    
        // Format komendy: "P,index,status,hour,minute,flow,flowDec,volume,volumeDec,days"
        if (command.startsWith("P,")) {
            byte values[9];
            int index = 0;
            int startPos = 2;
      
            // Parsowanie wartości
            while (index < 9 && startPos < command.length()) {
                int commaPos = command.indexOf(',', startPos);
                if (commaPos == -1) {
                    values[index] = command.substring(startPos).toInt();
                    break;
                }
                    values[index] = command.substring(startPos, commaPos).toInt();
                    startPos = commaPos + 1;
                    index++;
            }
      
            // Aktualizacja ustawień pompy
            byte pumpIndex = values[0];

            if (pumpIndex < NUM_PUMPS) {
                pumps[pumpIndex].status = values[1];
                pumps[pumpIndex].hour = values[2];
                pumps[pumpIndex].minute = values[3];
                pumps[pumpIndex].flow = values[4];
                pumps[pumpIndex].flowDec = values[5];
                pumps[pumpIndex].volume = values[6];
                pumps[pumpIndex].volumeDec = values[7];
                pumps[pumpIndex].days = values[8];
                
                saveSettings();
                Serial.println("OK");
            }
        }    
        // Komenda do ręcznego sterowania pompą: "M,index,state"
        else if (command.startsWith("M,")) {
            int index = command.substring(2, command.indexOf(',', 2)).toInt();
            int state = command.substring(command.lastIndexOf(',') + 1).toInt();
      
            if (index < NUM_PUMPS) {
                setPump(index, state == 1);
                Serial.println("OK");
            }
        }    
        // Komenda do odczytu ustawień
        else if (command == "R") {
            for (byte i = 0; i < NUM_PUMPS; i++) {
                Serial.print("P");
                Serial.print(i);
                Serial.print(":");
                Serial.print(pumps[i].status);
                Serial.print(",");
                Serial.print(pumps[i].hour);
                Serial.print(",");
                Serial.print(pumps[i].minute);
                Serial.print(",");
                Serial.print(pumps[i].flow);
                Serial.print(".");
                Serial.print(pumps[i].flowDec);
                Serial.print(",");
                Serial.print(pumps[i].volume);
                Serial.print(".");
                Serial.print(pumps[i].volumeDec);
                Serial.print(",");
                Serial.println(pumps[i].days, BIN);
            }
        }
        // Komenda kalibracji: "C,index,seconds"
        else if (command.startsWith("C,")) {
            int firstComma = command.indexOf(',');
            int secondComma = command.indexOf(',', firstComma + 1);
        
            if (firstComma != -1 && secondComma != -1) {
                byte pumpIndex = command.substring(firstComma + 1, secondComma).toInt();
                int calibrationTime = command.substring(secondComma + 1).toInt();
            
                if (pumpIndex < NUM_PUMPS && calibrationTime > 0) {
                    calibratePump(pumpIndex, calibrationTime);
                }
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    Wire.begin(); // Inicjalizacja I2C
  
    // Inicjalizacja PCF8574
    if (!pcf.begin()) {
        Serial.println("Nie znaleziono PCF8574");
        while(1);
    }
  
    // Ustaw wszystkie piny PCF8574 jako wyjścia
    for (byte i = 0; i < 8; i++) {
        pcf.pinMode(i, OUTPUT);
        pcf.digitalWrite(i, LOW);
    }
  
    // Inicjalizacja EEPROM
    EEPROM.begin(512);
  
    // Inicjalizacja RTC
    if (!rtc.begin()) {
        Serial.println("Nie znaleziono RTC");
        while(1);
    }

    // Wczytaj ustawienia z EEPROM
    loadSettings();
}

void loop() {
    DateTime now = rtc.now();
  
    // Sprawdź każdą pompę
    for (byte i = 0; i < NUM_PUMPS; i++) {
        if (pumps[i].status && isDayEnabled(pumps[i].days, now.dayOfTheWeek())) {
            if (now.hour() == pumps[i].hour && now.minute() == pumps[i].minute) {
                dosePump(i);
            }
        }
    }

    handleSerialCommands();
    delay(1000);
}
