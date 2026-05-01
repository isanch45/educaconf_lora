#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>

// Pins per a la Keyestudio ESP32-S3 (Emissor)
// NSS: 10, DIO1: 42, NRST: 48, BUSY: 47
SX1262 radio = new Module(10, 42, 48, 47);

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("--- EMISSOR LORA SX1262 (S3) ---"));

  // Inicialitzem el bus SPI amb els pins de la teva placa S3
  // SCK: 12, MISO: 13, MOSI: 11, NSS: 10
  SPI.begin(12, 13, 11, 10);

  // Inicialització del mòdul a 868.0 MHz
  Serial.print(F("[SX1262] Inicialitzant ... "));
  int state = radio.begin(868.0);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("èxit!"));
  } else {
    Serial.print(F("error, codi "));
    Serial.println(state);
    while (true); // Atura si hi ha error de hardware
  }
}

int comptador = 0;

void loop() {
  Serial.print(F("[SX1262] Enviant paquet ... "));

  // Creem el missatge que volem enviar
  String missatge = "Hola des de S3! Paquet: " + String(comptador);

  // Enviem el missatge
  int state = radio.transmit(missatge);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("Enviat correctament!"));
    
    // Mostrem dades del que hem enviat
    Serial.print(F("Missatge: "));
    Serial.println(missatge);
    
    // Potència de transmissió (opcional saber-ho)
    Serial.print(F("Datarate: "));
    Serial.print(radio.getDataRate());
    Serial.println(F(" bps"));

  } else {
    Serial.print(F("error al enviar, codi "));
    Serial.println(state);
  }

  comptador++;
  
  // Esperem 5 segons abans de tornar a enviar
  delay(5000); 
}