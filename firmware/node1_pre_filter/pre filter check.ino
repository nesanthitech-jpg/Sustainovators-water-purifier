#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// --- Pin Definitions ---
#define PIN_TDS_PRE   34  // Analog pin for raw water TDS
#define PIN_PH_PRE    35  // Analog pin for raw water pH
#define PIN_TEMP_PRE  4   // Digital OneWire pin for DS18B20 Temp Probe

// --- UART Communication (To Node 3) ---
#define TXD2 17  // Transmit pin to Node 3 RX2 (GPIO 16)
#define RXD2 16  // Unused, but allocated for hardware serial

// --- Sensor Objects ---
OneWire oneWire(PIN_TEMP_PRE);
DallasTemperature tempSensor(&oneWire);

// --- Timers ---
unsigned long last_send_time = 0;
const unsigned long SEND_INTERVAL = 2000; // Broadcast every 2 seconds

// --- 1. Pre-TDS Reading Algorithm ---
int readPreTDS() {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(PIN_TDS_PRE);
    delay(5);
  }
  float voltage = (sum / 10.0) * (3.3 / 4095.0);
  float compVolt = voltage / 1.0; // Normalized to 25°C standard
  float tdsValue = (133.42 * pow(compVolt, 3) - 255.86 * pow(compVolt, 2) + 857.39 * compVolt) * 0.5;
  return constrain((int)tdsValue, 0, 2000);
}

// --- 2. Pre-pH Reading Algorithm ---
float readPrePH() {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(PIN_PH_PRE);
    delay(5);
  }
  float voltage = (sum / 10.0) * (3.3 / 4095.0);
  
  // Calibrated linear pH conversion formula
  float phValue = (3.5 * voltage) + 2.4; 
  return constrain(phValue, 0.0, 14.0);
}

// --- 3. Pre-Temperature Reading ---
float readPreTemp() {
  tempSensor.requestTemperatures();
  float tempC = tempSensor.getTempCByIndex(0);
  
  // Fallback check if the DS18B20 probe is disconnected or floating
  if (tempC == DEVICE_DISCONNECTED_C || tempC < -10.0 || tempC > 85.0) {
    return 25.0; // Default nominal water temp (25°C) if sensor unhooked
  }
  return tempC;

  /* --- ALTERNATIVE: If using an Analog LM35 Temperature Sensor ---
  int raw = analogRead(PIN_TEMP_PRE);
  float voltage = raw * (3.3 / 4095.0);
  return voltage * 100.0; // 10mV per degree Celsius
  */
}

void setup() {
  // Debug serial monitor for local logging
  Serial.begin(115200);

  // Initialize UART2 for transmitting to Node 3
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  // Initialize temperature sensor bus
  tempSensor.begin();

  // Configure ADC resolution
  analogReadResolution(12);

  Serial.println("\n--- NODE 1: PRE-FILTER SENSOR NODE BOOTED ---");
  Serial.println("Broadcasting format: PRE:<pH>,<TDS>,<Temp>");
}

void loop() {
  if (millis() - last_send_time >= SEND_INTERVAL) {
    // 1. Acquire raw sensor readings
    float pre_ph = readPrePH();
    int pre_tds = readPreTDS();
    float pre_temp = readPreTemp();

    // 2. Construct the exact CSV payload expected by Node 3
    // Payload pattern: "PRE:pH,TDS,Temp\n"
    String payload = "PRE:" + String(pre_ph, 2) + "," + String(pre_tds) + "," + String(pre_temp, 2);

    // 3. Transmit packet over UART to Node 3
    Serial2.println(payload);

    // 4. Mirror to local Serial Monitor for testing and verification
    Serial.print("Transmitted to Node 3 -> ");
    Serial.println(payload);

    last_send_time = millis();
  }
}