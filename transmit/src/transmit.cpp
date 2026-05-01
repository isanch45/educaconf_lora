#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>
#include <ctype.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/adc.h" // Necessari per adc1_get_raw i configuració ADC
#include "esp_adc_cal.h" // Incloem la llibreria per calibrar l'ADC

// Configuració OLED (per a pantalles de 0.96" 128x64 I2C)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
bool oled_found = false;

// Configuració DHT22
#define DHTPIN 16       // Pin on està connectat el DHT22
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// Defineix aquesta línia per habilitar els missatges de depuració de l'ACS712
#define ACS712_DEBUG_PRINTS
// Clau alfabètica de 16 caràcters per encriptació Vigenère
const String key = "ABCDEFGHIJKLMNOP";

// Funció per encriptar amb Vigenère
String encryptVigenere(String text, String key) {
    String result = "";
    int keyLen = key.length();
    for (int i = 0, j = 0; i < text.length(); ++i) {
        char c = text[i];
        if (isalpha(c)) {
            char base = isupper(c) ? 'A' : 'a';
            int shift = toupper(key[j % keyLen]) - 'A';
            char encryptedChar = (char)((c - base + shift) % 26 + base);
            result += encryptedChar;
            j++;
        } else {
            result += c;
        }
    }
    return result;
}

// Pins per a la Keyestudio ESP32 (Emissor)
SX1262 radio = new Module(5, 27, 13, 14);

// Configuració ADC per mesurar la potència
#define ADC_PIN 36 // Pin ADC1_CHANNEL_0 (VP) per mesurar, pots canviar-lo
#define ADC_VREF 1100 // Default Vref per ESP32 en mV (valor típic, pot requerir ajust)
#define ADC_ATTENUATION ADC_ATTEN_DB_12 // Atenció de 12dB per un rang de 0-3.9V (ADC_ATTEN_DB_11 està deprecated)
#define ADC_WIDTH ADC_WIDTH_BIT_12 // Resolució de 12 bits

esp_adc_cal_characteristics_t *adc_chars;

double acs712_v_offset = 0.0;
double acs712_izero_offset = 0.0;

// Funció per llegir l'ADC i retornar mil·livolts
double fnc_esp32_ADC_mV(int pin) {
  adc1_channel_t channel;
  if (pin == 36) channel = ADC1_CHANNEL_0;
  else if (pin == 39) channel = ADC1_CHANNEL_3;
  else return 0;

  uint32_t adc_reading = adc1_get_raw(channel);
  uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
  return (double)voltage;
}

double fnc_esp32_ACS712(int _t){
	double acs_samples = 500;
	double sens5v=0.185;
	//v
	double volts = 0;
	for (int count = 0; count < acs_samples; count++) {
		volts += ((2.0 * fnc_esp32_ADC_mV(39) ) / 1000.0);
	}
	volts = (volts / acs_samples);
	#ifdef ACS712_DEBUG_PRINTS
    Serial.printf("ACS712 Debug: Volts (abans offset): %.3f V\n", volts);
	#endif
	volts += acs712_v_offset;
	if(_t==0)return volts;
	//calibration I=0
	if(_t==4){
		double vical = 0;
		for (int count = 0; count < (acs_samples*2); count++) {
			vical += (fnc_esp32_ADC_mV(36)/1000.0);
		}
		#ifdef ACS712_DEBUG_PRINTS
        Serial.printf("ACS712 Debug: Calibració - vical: %.3f V\n", vical);
		#endif
		vical = (vical / (acs_samples*2));
		acs712_izero_offset=(vical-(volts/2.0));
		return 0;
	}
	//a
	double vIout = 0;
	for (int count = 0; count < acs_samples; count++) {
		vIout += (fnc_esp32_ADC_mV(36)/1000.0);
	}
	vIout = (vIout / acs_samples);
	double vZero = ((volts/2.0)+acs712_izero_offset);
	double vSens = (vIout-vZero);
	#ifdef ACS712_DEBUG_PRINTS
    Serial.printf("ACS712 Debug: vIout: %.3f V, vZero: %.3f V, vSens: %.3f V\n", vIout, vZero, vSens);
	#endif
	double sensVcc = ((sens5v*volts)/5.0);
	double amps = (vSens/sensVcc);
	#ifdef ACS712_DEBUG_PRINTS
    Serial.printf("ACS712 Debug: sensVcc: %.3f V/A, Amps (raw): %.3f A\n", sensVcc, amps);
	#endif
	if (abs(amps) < 0.005) { // Reduïm el llindar a 5mA per filtrar només soroll molt petit
		amps=0.0;
	}
	if(_t==1)return amps;
	//w
	double wats=(volts*amps);
	if(_t==2)return wats;
	return 0.0;
}

void displayMessage(String line1, String line2 = "", String line3 = "", String line4 = "") {
  if (!oled_found) {
    return; // No faig res si la pantalla no està connectada
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  display.println(line2);
  display.println(line3);
  display.println(line4);
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println(F("--- EMISSOR LORA SX1262 (S3) ---"));

  if(display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64
    oled_found = true;
    displayMessage("Iniciant...", "Emissor LoRa");
  }

  // Inicialitzem el DHT22
  dht.begin();

  // Configurem i calibrem l'ADC per la mesura de potència
  adc1_config_width(ADC_WIDTH);
  adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTENUATION);
  adc1_config_channel_atten(ADC1_CHANNEL_3, ADC_ATTENUATION); // Configurem també el canal per al pin 39
  adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTENUATION, ADC_WIDTH, ADC_VREF, adc_chars);

  // Inicialitzem el bus SPI
  SPI.begin(18, 19, 23, 5);

  // Inicialització del mòdul LoRa
  // Inicialització del mòdul
  Serial.print(F("[SX1262] Inicialitzant ... "));
  int state = radio.begin(868.0);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("exit!"));
    displayMessage("LoRa OK", "868.0 MHz");
  } else {
    Serial.print(F("error, codi "));
    Serial.println(state);
    displayMessage("Error LoRa", "Codi: " + String(state));
    while (true); 
  }

  // Potència legal a Europa (per sota de 14 dBm sol ser el límit de la banda)
  int power = 14; 
  radio.setOutputPower(power);
  
  Serial.print(F("Potència configurada a: "));
  Serial.print(power);
  Serial.println(F(" dBm"));
  displayMessage("LoRa OK", "Potencia: " + String(power) + " dBm", "Calibrant...");
  
  // Calibració del sensor ACS712
  fnc_esp32_ACS712(4); // Calibrem el punt zero del corrent
  
}

int comptador = 0;

void loop() {
  Serial.print(F("[SX1262] Enviant paquet ... "));
  displayMessage("Enviant paquet...", "ID: " + String(comptador));

  // Llegim dades reals del DHT22
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  // Comprovem si la lectura és vàlida
  if (isnan(h) || isnan(t)) {
    Serial.println(F("Error llegint el DHT22!"));
    displayMessage("Error DHT22!", "Reintentant...");
    t = 0.0;
    h = 0.0;
  }

  // Llegim la potència en Watts del sensor ACS712
  double power_W = fnc_esp32_ACS712(2); // Obtenim la potència en Watts

  // Creem l'objecte JSON
  JsonDocument doc;
  doc["id"] = comptador;
  doc["temp"] = t;
  doc["hum"] = h;
  doc["power_W"] = power_W; // Enviem la potència en Watts

  String jsonString;
  serializeJson(doc, jsonString);

  Serial.print(F("Dades JSON: "));
  Serial.println(jsonString);

  // Encriptem el JSON complet abans d'enviar
  String missatgeXifrat = encryptVigenere(jsonString, key);

  // 1. Enviem el missatge
  int state = radio.transmit(missatgeXifrat);

  if (state == RADIOLIB_ERR_NONE) {
    displayMessage("Paquet enviat!", "ID: " + String(comptador), "Esperant...");
    Serial.println(F("Enviat correctament!"));

    // 2. CÀLCUL DINÀMIC DEL TEMPS A L'AIRE
    // Necessitem passar-li la mida del missatge real que acabem d'enviar
    float toa_ms = radio.getTimeOnAir(missatgeXifrat.length()) / 1000.0;

    // 3. CÀLCUL DE LA REGLE DE L'1% (Silenci = 99 cops el temps d'emissió)
    float silence_needed_ms = toa_ms * 99.0;

    Serial.print(F("Time on Air: "));
    Serial.print(toa_ms);
    Serial.println(F(" ms"));

    Serial.print(F("Silenci obligatori: "));
    Serial.print(silence_needed_ms / 1000.0);
    Serial.println(F(" segons"));

    Serial.println(F("---------------------------------"));

    // 4. Esperem el temps de silenci calculat per ser legals
    delay((unsigned long)silence_needed_ms); 

  } else {
    Serial.print(F("error al enviar, codi "));
    displayMessage("Error enviant", "Codi: " + String(state));
    Serial.println(state);
    delay(5000); // Si falla, esperem 5 segons abans de reintentar
  }

  comptador++;
}