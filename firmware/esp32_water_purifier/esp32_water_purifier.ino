/*
  =====================================================================
  Smart Household Water Purification & Monitoring System
  ESP32 Firmware — Base + Optional Modules, Rule-Based Contaminant Flag,
  Graduated Risk Scoring (Safety-Margin Buffer), Backwash Cycle
  =====================================================================

  See README.md sections 5, 5.1, 9, 10, 11, 12 for the full design
  rationale. This file is intentionally heavily commented so it can be
  understood, defended, and recalibrated without re-deriving the logic
  from scratch.

  IMPORTANT: All HARD_*, MARGIN_*, and RISK_WEIGHT_* constants below
  are PLACEHOLDERS. They MUST be recalibrated against your physical
  sensor unit before any real demo or deployment. See README §10.
*/

#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_AS726x.h>

// =====================================================================
// PIN DEFINITIONS
// =====================================================================
#define PIN_PH_SENSOR         34   // ADC1
#define PIN_TURBIDITY_SENSOR  35   // ADC1
#define PIN_TDS_SENSOR        32   // ADC1
#define PIN_TEMP_ONEWIRE      4    // DS18B20, needs 4.7k pull-up

// I2C bus (AS7262 spectral sensor + 16x2 LCD share this bus)
#define PIN_I2C_SDA           21
#define PIN_I2C_SCL           22

#define PIN_RELAY_TAP_LOCK      25
#define PIN_RELAY_SENSOR_DIVERTER 26
#define PIN_RELAY_BACKWASH      27
#define PIN_MOSFET_UVC           14
#define PIN_BUZZER                13

// GSM module (SIM800L) — optional, HardwareSerial2
#define PIN_GSM_TX  17
#define PIN_GSM_RX  16

// Relay logic: modules used here are typically active-LOW opto relays.
// Flip these if your relay board is active-HIGH.
#define RELAY_ON   LOW
#define RELAY_OFF  HIGH

// =====================================================================
// TIMING CONSTANTS
// =====================================================================
#define BASELINE_SETTLE_MS      3000   // let water settle in chamber before reading
#define FILTER_FLOW_MS         20000   // time allotted for water to pass through filter stack
#define VERIFY_SETTLE_MS        3000
#define BACKWASH_DURATION_MS   15000
#define BACKWASH_INTERVAL_MS   (24UL * 60UL * 60UL * 1000UL)  // 24 hours
#define SENSOR_SAMPLE_COUNT        10   // averaging window per reading

// =====================================================================
// HARD THRESHOLDS — BIS 10500:2012 acceptable limits (placeholders —
// replace with the exact values you calibrate/verify against BIS 10500
// for your target parameters before deployment)
// =====================================================================
#define HARD_PH_MIN            6.5
#define HARD_PH_MAX            8.5
#define HARD_TURBIDITY_NTU     1.0
#define HARD_TDS_PPM         500.0

// AS7262 6-channel spectral "absorbance-proxy" hard thresholds.
// These are placeholders — see README §10 calibration procedure.
#define HARD_CH_450  300.0   // violet — iron signature
#define HARD_CH_500  300.0
#define HARD_CH_550  300.0
#define HARD_CH_570  300.0
#define HARD_CH_600  300.0   // orange — copper signature
#define HARD_CH_650  300.0

// =====================================================================
// MARGIN THRESHOLDS — start of the "caution zone" below each hard
// limit (README §5.1). Set at ~85-90% of the HARD value initially;
// tune during calibration. A reading between MARGIN and HARD does NOT
// fail outright, but contributes to the composite risk score.
// =====================================================================
#define MARGIN_PH_LOW_BAND      0.3    // ph caution band width from either hard edge
#define MARGIN_TURBIDITY_NTU    0.85
#define MARGIN_TDS_PPM         425.0

#define MARGIN_CH_450  255.0
#define MARGIN_CH_500  255.0
#define MARGIN_CH_550  255.0
#define MARGIN_CH_570  255.0
#define MARGIN_CH_600  255.0
#define MARGIN_CH_650  255.0

// =====================================================================
// RISK WEIGHTS — relative importance of each parameter in the
// composite risk score. Spectral/heavy-metal-proxy channels weighted
// higher than general turbidity, since they map more directly to
// toxicity rather than aesthetics. Tune during calibration (README §10
// step 6). Weights need not sum to 1 — only their ratios matter.
// =====================================================================
#define RISK_WEIGHT_PH        1.0
#define RISK_WEIGHT_TURBIDITY 0.6
#define RISK_WEIGHT_TDS       1.0
#define RISK_WEIGHT_CH_450    1.4   // iron
#define RISK_WEIGHT_CH_500    1.0
#define RISK_WEIGHT_CH_550    1.0
#define RISK_WEIGHT_CH_570    1.0
#define RISK_WEIGHT_CH_600    1.4   // copper
#define RISK_WEIGHT_CH_650    1.0

// Composite score (0..sum_of_weights, effectively 0..1 after
// normalization) above which a "borderline" lock is triggered even
// though no single hard threshold was crossed.
#define COMPOSITE_RISK_CAUTION_THRESHOLD  0.40

// =====================================================================
// GLOBAL OBJECTS
// =====================================================================
OneWire oneWire(PIN_TEMP_ONEWIRE);
DallasTemperature tempSensor(&oneWire);
LiquidCrystal_I2C lcd(0x27, 16, 2);   // confirm LCD I2C address (0x27 or 0x3F)
Adafruit_AS726x spectralSensor;       // default addr 0x49 — verify no clash with LCD
HardwareSerial gsmSerial(2);

// =====================================================================
// DATA STRUCTURES
// =====================================================================
struct SensorReading {
  float ph;
  float turbidityNTU;
  float tdsPPM;
  float tempC;
  float ch450, ch500, ch550, ch570, ch600, ch650;
};

enum SystemState {
  STATE_IDLE,
  STATE_BASELINE_READ,
  STATE_FILTERING,
  STATE_VERIFY_READ,
  STATE_DECISION,
  STATE_DISPENSE,
  STATE_LOCKED,
  STATE_BACKWASH
};

enum FailReason {
  FAIL_NONE,
  FAIL_HARD_LIMIT,     // one or more parameters exceeded the absolute BIS limit
  FAIL_BORDERLINE       // no single hard breach, but composite risk score too high
};

SystemState currentState = STATE_IDLE;
SensorReading baselineReading;
SensorReading verifyReading;
FailReason lastFailReason = FAIL_NONE;
String lastFlaggedContaminant = "None";
unsigned long stateEnteredAt = 0;
unsigned long lastBackwashAt = 0;

// =====================================================================
// SETUP
// =====================================================================
void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  pinMode(PIN_RELAY_TAP_LOCK, OUTPUT);
  pinMode(PIN_RELAY_SENSOR_DIVERTER, OUTPUT);
  pinMode(PIN_RELAY_BACKWASH, OUTPUT);
  pinMode(PIN_MOSFET_UVC, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  // Fail-safe defaults: tap locked, all solenoids off, UV-C off.
  digitalWrite(PIN_RELAY_TAP_LOCK, RELAY_OFF);
  digitalWrite(PIN_RELAY_SENSOR_DIVERTER, RELAY_OFF);
  digitalWrite(PIN_RELAY_BACKWASH, RELAY_OFF);
  digitalWrite(PIN_MOSFET_UVC, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  tempSensor.begin();

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Water Purifier");
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");

  if (!spectralSensor.begin()) {
    Serial.println("ERROR: AS7262 spectral sensor not found. Check I2C wiring.");
    lcd.clear();
    lcd.print("Sensor Error!");
  }

  gsmSerial.begin(9600, SERIAL_8N1, PIN_GSM_RX, PIN_GSM_TX);

  lastBackwashAt = millis();
  delay(1500);
  lcd.clear();

  Serial.println("System ready. Awaiting dispense request.");
  currentState = STATE_IDLE;
  stateEnteredAt = millis();
}

// =====================================================================
// MAIN LOOP — simple non-blocking state machine
// =====================================================================
void loop() {
  switch (currentState) {

    case STATE_IDLE:
      handleIdle();
      break;

    case STATE_BASELINE_READ:
      handleBaselineRead();
      break;

    case STATE_FILTERING:
      handleFiltering();
      break;

    case STATE_VERIFY_READ:
      handleVerifyRead();
      break;

    case STATE_DECISION:
      handleDecision();
      break;

    case STATE_DISPENSE:
      handleDispense();
      break;

    case STATE_LOCKED:
      handleLocked();
      break;

    case STATE_BACKWASH:
      handleBackwash();
      break;
  }

  // Backwash trigger check is evaluated regardless of current state,
  // but only actually enters STATE_BACKWASH from IDLE to avoid
  // interrupting an in-progress dispense cycle.
  if (currentState == STATE_IDLE &&
      (millis() - lastBackwashAt >= BACKWASH_INTERVAL_MS)) {
    Serial.println("Scheduled 24h backwash triggered.");
    changeState(STATE_BACKWASH);
  }
}

// =====================================================================
// STATE HANDLERS
// =====================================================================

void handleIdle() {
  lcd.setCursor(0, 0);
  lcd.print("Ready. Press to ");
  lcd.setCursor(0, 1);
  lcd.print("dispense water  ");

  // Demo mode: auto-trigger a cycle. Replace with a real button/GPIO
  // interrupt read for a production build.
  if (dispenseRequested()) {
    Serial.println("Dispense requested -> BASELINE_READ");
    changeState(STATE_BASELINE_READ);
  }
}

void handleBaselineRead() {
  // Route raw water through the shared sensor chamber (README §5).
  setSensorDiverter(true);

  if (millis() - stateEnteredAt < BASELINE_SETTLE_MS) return; // let water settle

  baselineReading = readAllSensors();
  logReading("BASELINE", baselineReading);

  changeState(STATE_FILTERING);
}

void handleFiltering() {
  // Divert sensor chamber out of the loop while water flows through
  // the filter stack (sediment -> carbon -> UV-C -> optional modules).
  setSensorDiverter(false);
  digitalWrite(PIN_MOSFET_UVC, HIGH);

  if (millis() - stateEnteredAt < FILTER_FLOW_MS) return;

  digitalWrite(PIN_MOSFET_UVC, LOW);
  changeState(STATE_VERIFY_READ);
}

void handleVerifyRead() {
  // Route filtered output back through the SAME sensor chamber.
  setSensorDiverter(true);

  if (millis() - stateEnteredAt < VERIFY_SETTLE_MS) return;

  verifyReading = readAllSensors();
  logReading("VERIFY", verifyReading);

  changeState(STATE_DECISION);
}

void handleDecision() {
  // Pass 1: hard BIS 10500 threshold check. Any single breach is an
  // automatic, non-negotiable fail — no composite scoring needed.
  bool hardFail = false;
  String hardFailContaminant = "Unknown";

  if (verifyReading.ph < HARD_PH_MIN || verifyReading.ph > HARD_PH_MAX) {
    hardFail = true;
    hardFailContaminant = "Acidic/Alkaline (pH)";
  }
  if (verifyReading.turbidityNTU > HARD_TURBIDITY_NTU) {
    hardFail = true;
    hardFailContaminant = "Silt/Sediment";
  }
  if (verifyReading.tdsPPM > HARD_TDS_PPM) {
    hardFail = true;
    hardFailContaminant = "High TDS";
  }
  if (verifyReading.ch450 > HARD_CH_450) {
    hardFail = true;
    hardFailContaminant = "Iron";
  }
  if (verifyReading.ch600 > HARD_CH_600) {
    hardFail = true;
    hardFailContaminant = "Copper/Lead";
  }
  // (Remaining spectral channels checked the same way — omitted
  // duplicate branches here for brevity; extend as needed per
  // contaminant signature during calibration.)

  if (hardFail) {
    lastFailReason = FAIL_HARD_LIMIT;
    lastFlaggedContaminant = hardFailContaminant;
    changeState(STATE_LOCKED);
    return;
  }

  // Pass 2: no hard breach — compute composite risk score across the
  // margin/caution zone (README §5.1). This is what catches a case
  // like "299 vs a 300 limit" combined with other near-limit readings
  // that would otherwise silently pass a naive per-parameter check.
  float composite = computeCompositeRiskScore(verifyReading);
  Serial.print("Composite risk score: ");
  Serial.println(composite, 3);

  if (composite >= COMPOSITE_RISK_CAUTION_THRESHOLD) {
    lastFailReason = FAIL_BORDERLINE;
    lastFlaggedContaminant = topRiskContributor(verifyReading);
    changeState(STATE_LOCKED);
    return;
  }

  lastFailReason = FAIL_NONE;
  changeState(STATE_DISPENSE);
}

void handleDispense() {
  digitalWrite(PIN_RELAY_TAP_LOCK, RELAY_ON);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Water Safe");
  lcd.setCursor(0, 1);
  lcd.print("Tap Open");

  // In a real build this would wait for a flow sensor / timed dispense
  // volume. Demo: hold open for a fixed window, then return to idle.
  if (millis() - stateEnteredAt > 8000) {
    digitalWrite(PIN_RELAY_TAP_LOCK, RELAY_OFF);
    changeState(STATE_IDLE);
  }
}

void handleLocked() {
  digitalWrite(PIN_RELAY_TAP_LOCK, RELAY_OFF); // redundant, but explicit fail-safe
  digitalWrite(PIN_BUZZER, HIGH);

  lcd.clear();
  lcd.setCursor(0, 0);
  if (lastFailReason == FAIL_HARD_LIMIT) {
    lcd.print("UNSAFE: ");
  } else {
    lcd.print("Borderline: ");
  }
  lcd.setCursor(0, 1);
  lcd.print(lastFlaggedContaminant);

  Serial.print("LOCKED. Reason: ");
  Serial.print(lastFailReason == FAIL_HARD_LIMIT ? "HARD_LIMIT" : "BORDERLINE_COMPOSITE");
  Serial.print(" | Likely factor: ");
  Serial.println(lastFlaggedContaminant);

  sendSmsAlert(lastFlaggedContaminant, lastFailReason);

  if (millis() - stateEnteredAt > 6000) {
    digitalWrite(PIN_BUZZER, LOW);
    changeState(STATE_IDLE);
  }
}

void handleBackwash() {
  digitalWrite(PIN_RELAY_BACKWASH, RELAY_ON);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Backwash cycle");
  lcd.setCursor(0, 1);
  lcd.print("in progress...");

  if (millis() - stateEnteredAt >= BACKWASH_DURATION_MS) {
    digitalWrite(PIN_RELAY_BACKWASH, RELAY_OFF);
    lastBackwashAt = millis();
    Serial.println("Backwash complete.");
    changeState(STATE_IDLE);
  }
}

// =====================================================================
// GRADUATED RISK SCORING (README §5.1)
// =====================================================================

// Clamp a value's position between margin (0.0) and hard (1.0) limits.
// Returns 0 if reading is below margin (no risk contribution), and
// clamps at 1.0 if it's already at/above hard (hard-fail path handles
// that case separately, but clamping keeps this function safe to call
// standalone too).
float marginRisk(float reading, float marginLimit, float hardLimit) {
  if (hardLimit <= marginLimit) return 0.0; // misconfigured constants guard
  if (reading <= marginLimit) return 0.0;
  float r = (reading - marginLimit) / (hardLimit - marginLimit);
  if (r > 1.0) r = 1.0;
  if (r < 0.0) r = 0.0;
  return r;
}

// pH is two-sided (too acidic OR too alkaline), so its margin risk is
// computed against whichever edge the reading is closer to failing.
float phMarginRisk(float ph) {
  float lowMarginEdge = HARD_PH_MIN + MARGIN_PH_LOW_BAND;
  float highMarginEdge = HARD_PH_MAX - MARGIN_PH_LOW_BAND;
  if (ph <= lowMarginEdge && ph >= HARD_PH_MIN) {
    return marginRisk(lowMarginEdge - ph, 0.0, MARGIN_PH_LOW_BAND);
  }
  if (ph >= highMarginEdge && ph <= HARD_PH_MAX) {
    return marginRisk(ph - highMarginEdge, 0.0, MARGIN_PH_LOW_BAND);
  }
  return 0.0;
}

float computeCompositeRiskScore(const SensorReading &r) {
  float totalWeight = RISK_WEIGHT_PH + RISK_WEIGHT_TURBIDITY + RISK_WEIGHT_TDS +
                       RISK_WEIGHT_CH_450 + RISK_WEIGHT_CH_500 + RISK_WEIGHT_CH_550 +
                       RISK_WEIGHT_CH_570 + RISK_WEIGHT_CH_600 + RISK_WEIGHT_CH_650;

  float weightedSum =
      RISK_WEIGHT_PH        * phMarginRisk(r.ph) +
      RISK_WEIGHT_TURBIDITY * marginRisk(r.turbidityNTU, MARGIN_TURBIDITY_NTU, HARD_TURBIDITY_NTU) +
      RISK_WEIGHT_TDS       * marginRisk(r.tdsPPM,       MARGIN_TDS_PPM,       HARD_TDS_PPM) +
      RISK_WEIGHT_CH_450    * marginRisk(r.ch450, MARGIN_CH_450, HARD_CH_450) +
      RISK_WEIGHT_CH_500    * marginRisk(r.ch500, MARGIN_CH_500, HARD_CH_500) +
      RISK_WEIGHT_CH_550    * marginRisk(r.ch550, MARGIN_CH_550, HARD_CH_550) +
      RISK_WEIGHT_CH_570    * marginRisk(r.ch570, MARGIN_CH_570, HARD_CH_570) +
      RISK_WEIGHT_CH_600    * marginRisk(r.ch600, MARGIN_CH_600, HARD_CH_600) +
      RISK_WEIGHT_CH_650    * marginRisk(r.ch650, MARGIN_CH_650, HARD_CH_650);

  return weightedSum / totalWeight; // normalized to roughly 0..1
}

// For LCD/SMS messaging: identify which parameter contributed most to
// a borderline (composite) fail, so the alert is still actionable
// rather than a vague "risk score exceeded" message.
String topRiskContributor(const SensorReading &r) {
  struct { const char* name; float risk; } candidates[] = {
    {"pH (borderline)",        phMarginRisk(r.ph)},
    {"Turbidity (borderline)", marginRisk(r.turbidityNTU, MARGIN_TURBIDITY_NTU, HARD_TURBIDITY_NTU)},
    {"TDS (borderline)",       marginRisk(r.tdsPPM, MARGIN_TDS_PPM, HARD_TDS_PPM)},
    {"Iron (borderline)",      marginRisk(r.ch450, MARGIN_CH_450, HARD_CH_450)},
    {"Copper/Lead (borderline)", marginRisk(r.ch600, MARGIN_CH_600, HARD_CH_600)},
  };

  int bestIdx = 0;
  for (int i = 1; i < 5; i++) {
    if (candidates[i].risk > candidates[bestIdx].risk) bestIdx = i;
  }
  return String(candidates[bestIdx].name);
}

// =====================================================================
// SENSOR READING HELPERS
// =====================================================================

SensorReading readAllSensors() {
  SensorReading r;

  r.ph = readPhAveraged();
  r.turbidityNTU = readTurbidityAveraged();
  r.tdsPPM = readTdsAveraged(r.tempC); // temp compensation applied below once known

  tempSensor.requestTemperatures();
  r.tempC = tempSensor.getTempCByIndex(0);
  // Re-derive TDS with proper temp compensation now that tempC is known.
  r.tdsPPM = readTdsAveraged(r.tempC);

  spectralSensor.startMeasurement();
  while (!spectralSensor.dataReady()) {
    delay(10);
  }
  spectralSensor.readCalibratedValues(
      &r.ch450, &r.ch500, &r.ch550, &r.ch570, &r.ch600, &r.ch650);

  return r;
}

float readPhAveraged() {
  long sum = 0;
  for (int i = 0; i < SENSOR_SAMPLE_COUNT; i++) {
    sum += analogRead(PIN_PH_SENSOR);
    delay(5);
  }
  float avgRaw = sum / (float)SENSOR_SAMPLE_COUNT;
  // Placeholder linear conversion — replace with your probe's actual
  // calibration curve (2-point or 3-point buffer solution calibration).
  float voltage = avgRaw * (3.3 / 4095.0);
  float ph = 7.0 + ((2.5 - voltage) / 0.18);
  return ph;
}

float readTurbidityAveraged() {
  long sum = 0;
  for (int i = 0; i < SENSOR_SAMPLE_COUNT; i++) {
    sum += analogRead(PIN_TURBIDITY_SENSOR);
    delay(5);
  }
  float avgRaw = sum / (float)SENSOR_SAMPLE_COUNT;
  float voltage = avgRaw * (3.3 / 4095.0);
  // Placeholder curve — replace with your sensor's datasheet curve /
  // your own clear-water vs. known-NTU calibration samples.
  float ntu = -1120.4 * voltage * voltage + 5742.3 * voltage - 4352.9;
  if (ntu < 0) ntu = 0;
  return ntu;
}

float readTdsAveraged(float tempC) {
  long sum = 0;
  for (int i = 0; i < SENSOR_SAMPLE_COUNT; i++) {
    sum += analogRead(PIN_TDS_SENSOR);
    delay(5);
  }
  float avgRaw = sum / (float)SENSOR_SAMPLE_COUNT;
  float voltage = avgRaw * (3.3 / 4095.0);

  // Standard temperature-compensated TDS conversion (Gravity TDS
  // module reference formula) — placeholder, verify against your
  // module's datasheet.
  float compensationCoeff = 1.0 + 0.02 * (tempC - 25.0);
  float compensatedVoltage = voltage / compensationCoeff;
  float tds = (133.42 * pow(compensatedVoltage, 3)
             - 255.86 * pow(compensatedVoltage, 2)
             + 857.39 * compensatedVoltage) * 0.5;
  if (tds < 0) tds = 0;
  return tds;
}

// =====================================================================
// I/O HELPERS
// =====================================================================

void setSensorDiverter(bool routeThroughChamber) {
  digitalWrite(PIN_RELAY_SENSOR_DIVERTER, routeThroughChamber ? RELAY_ON : RELAY_OFF);
}

bool dispenseRequested() {
  // TODO: replace with real button GPIO read (with debounce) for a
  // production build. Demo stub: auto-trigger once every idle period.
  static bool triggered = false;
  if (!triggered) {
    triggered = true;
    return true;
  }
  return false;
}

void sendSmsAlert(const String &contaminant, FailReason reason) {
  // Optional — only meaningful if SIM800L is actually fitted.
  String msg = (reason == FAIL_HARD_LIMIT)
      ? "ALERT: Water UNSAFE. Cause: " + contaminant
      : "CAUTION: Water borderline/near limits. Factor: " + contaminant;

  gsmSerial.println("AT+CMGF=1");
  delay(200);
  gsmSerial.println("AT+CMGS=\"+91XXXXXXXXXX\""); // set recipient number
  delay(200);
  gsmSerial.print(msg);
  gsmSerial.write(26); // Ctrl+Z to send
}

void logReading(const char* label, const SensorReading &r) {
  Serial.print("[");
  Serial.print(label);
  Serial.print("] pH=");
  Serial.print(r.ph, 2);
  Serial.print(" NTU=");
  Serial.print(r.turbidityNTU, 1);
  Serial.print(" TDS=");
  Serial.print(r.tdsPPM, 1);
  Serial.print(" Temp=");
  Serial.print(r.tempC, 1);
  Serial.print(" CH[450,500,550,570,600,650]=");
  Serial.print(r.ch450, 1); Serial.print(",");
  Serial.print(r.ch500, 1); Serial.print(",");
  Serial.print(r.ch550, 1); Serial.print(",");
  Serial.print(r.ch570, 1); Serial.print(",");
  Serial.print(r.ch600, 1); Serial.print(",");
  Serial.println(r.ch650, 1);
}

void changeState(SystemState newState) {
  currentState = newState;
  stateEnteredAt = millis();
}
