#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ctype.h>
#include <ArduinoJson.h>

// --- LA TEVA LLISTA DEFINITIVA DE PINS S3 receptor---
#define LORA_NSS  10
#define LORA_NRST 48
#define LORA_BUSY 47
#define LORA_DIO1 42

#define LORA_SCK  12
#define LORA_MISO 13
#define LORA_MOSI 11

// Pins OLED I2C (ESP32-S3 per defecte)
#define OLED_SDA 8
#define OLED_SCL 9
#define OLED_RESET -1
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Creem el mòdul amb els pins de control
SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_NRST, LORA_BUSY);

// OLED display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Clau alfabètica de 16 caràcters per desencriptació Vigenère
const String key = "ABCDEFGHIJKLMNOP";

// Funció per desencriptar amb Vigenère
String decryptVigenere(String text, String key) {
    String result = "";
    int keyLen = key.length();
    for (int i = 0, j = 0; i < text.length(); ++i) {
        char c = text[i];
        if (isalpha(c)) {
            char base = isupper(c) ? 'A' : 'a';
            int shift = toupper(key[j % keyLen]) - 'A';
            char decryptedChar = (char)((c - base - shift + 26) % 26 + base);
            result += decryptedChar;
            j++;
        } else {
            result += c;
        }
    }
    return result;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("--- RECEPTOR LORA S3 ---"));

  // Inicialitzem OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println(F("Iniciant..."));
  display.display();

  // 1. Iniciem els pins SPI exactes de la teva S3
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);

  // 2. Iniciem la ràdio a 868.0 MHz
  Serial.print(F("[SX1262] Inicialitzant... "));
  int state = radio.begin(868.0);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("èxit! Escoltant el comptador dels blocs..."));
  } else {
    Serial.print(F("Error de hardware, codi: "));
    Serial.println(state);
    while (true); // S'atura si falla
  }
}

void loop() {
  // Variable per guardar el número del comptador que ve dels blocs
  String dadaRebuda;
  
  // Intentem rebre el paquet de l'aire
  int state = radio.receive(dadaRebuda);

  if (state == RADIOLIB_ERR_NONE) {
    // Desencriptem el missatge rebut
    dadaRebuda = decryptVigenere(dadaRebuda, key);
    
    // Parsegem el JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, dadaRebuda);

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);

    if (!error) {
      int id = doc["id"];
      float temp = doc["temp"];
      float hum = doc["hum"];
      float power_W = doc["power_W"]; // Ara rebem la potència en Watts

      // Mostrem per Serial
      Serial.println(F("====================================="));
      Serial.printf("ID: %d | Temp: %.1f C | Hum: %.1f %% | Power: %.2f W\n", id, temp, hum, power_W);
      Serial.print(F("RSSI: "));
      Serial.println(radio.getRSSI());
      
      // Mostrem a la pantalla OLED
      display.printf("ID: %d\n", id);
      display.printf("Temp: %.1f C\n", temp);
      display.printf("Hum: %.1f %%\n", hum); // S'ha de canviar el format per mostrar 2 decimals
      display.printf("Power: %.2f W\n", power_W); // S'ha de canviar el format per mostrar 2 decimals
      display.printf("RSSI: %.0f dBm", radio.getRSSI());
    } else {
      Serial.println(F("Error parsejant JSON o missatge no JSON"));
      display.println(F("Format invàlid:"));
      display.println(dadaRebuda.substring(0, 20));
    }
    
    display.display();
    
  } else if (state == RADIOLIB_ERR_RX_TIMEOUT) {
    // No ha arribat res encara, continua esperant en silenci
  } else {
    // Si hi ha hagut alguna interferència
    Serial.print(F("Error en rebre, codi: "));
    Serial.println(state);
  }
}