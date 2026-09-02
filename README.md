# Smart Household Water Purification \& Monitoring System

A solar-powered, point-of-use water purifier for households in mining-affected and rural districts of Jharkhand. It filters water through a modular multi-stage system, checks its quality before and after filtration using one shared sensor set, identifies the likely contaminant present, self-cleans via periodic backwashing, and only opens the tap when the water is verified safe against BIS 10500 standards.

\---

## Table of Contents

1. [Problem Statement](#1-problem-statement)
2. [Solution Overview](#2-solution-overview)
3. [System Architecture](#3-system-architecture)
4. [Hardware — Full Bill of Materials](#4-hardware--full-bill-of-materials)
5. [Sensing System — How Detection Works](#5-sensing-system--how-detection-works)
6. [Filtration System — Base Unit + Optional Modules](#6-filtration-system--base-unit--optional-modules)
7. [Power System](#7-power-system)
8. [Wiring / Pin Mapping](#8-wiring--pin-mapping)
9. [Firmware — Logic Walkthrough](#9-firmware--logic-walkthrough)
10. [Calibration Procedure](#10-calibration-procedure)
11. [Safety Mechanism](#11-safety-mechanism)
12. [Backwash Cycle](#12-backwash-cycle)
13. [Deployment Model](#13-deployment-model)
14. [Cost Summary](#14-cost-summary)
15. [How to Build \& Flash](#15-how-to-build--flash)
16. [Repository Structure](#16-repository-structure)
17. [References](#17-references)
18. [License](#18-license)

\---

## 1\. Problem Statement

Households in mining-affected districts of Jharkhand (e.g. Ramgarh, Dhanbad, West Singhbhum) draw drinking water directly from wells, hand pumps, and borewells — with no treatment at the point of use. This water commonly carries:

* Suspended silt and coal dust from mining runoff
* Acidic pH from acid mine drainage
* Dissolved heavy metals (iron, lead, copper) from mining activity
* Fluoride/arsenic, a separate but co-located issue in parts of Jharkhand's groundwater

There is no household-level way to check, in real time, what is actually in the water before it's consumed. Existing purifiers are generic (fixed configuration regardless of the actual contaminant) and typically depend on continuous grid electricity.

## 2\. Solution Overview

A single, self-contained unit installed at a household's water outlet:

* Cleans water through a **base filtration stack**, plus **optional add-on modules** fitted only where a local water test shows they're needed.
* Uses **one shared set of sensors** to read water quality both before and after filtration — ensuring an accurate, apples-to-apples comparison instead of relying on two separately-calibrated sensor sets.
* Runs a **rule-based (non-ML) contamination classifier** on an ESP32, using a 6-channel spectral sensor plus pH/TDS/turbidity/temperature, to flag the likely contaminant type (iron, lead/copper, general silt).
* **Physically locks the tap** — via a solenoid valve — unless the post-filtration reading passes BIS 10500 safety thresholds.
* **Self-cleans** via a periodic backwash cycle that reverse-flushes trapped debris out of the filters, extending their working life.
* Runs fully **off-grid** on solar + Li-ion battery power.

## 3\. System Architecture

```
Raw water in
     │
     ▼
\[Sensor chamber — baseline reading]  (pH, turbidity, TDS, temp, spectral scan)
     │
     ▼
\[Base filtration]  Sediment → Carbon/resin → UV-C
     │
     ▼
\[Optional add-on modules, if fitted]  Fluoride/arsenic alumina | Heavy-metal resin | RO
     │
     ▼
\[Sensor chamber — verify reading]  (same sensors, same chamber)
     │
     ▼
\[Decision: Safe? — BIS 10500 check]
     │                  │
    YES                 NO
     │                  │
     ▼                  ▼
\[Tap opens]      \[Tap stays locked + buzzer/SMS alert]

(Periodically, or when readings show reduced filter efficiency:
 → Backwash cycle reverse-flushes the filter stack via a drain line)
```

## 4\. Hardware — Full Bill of Materials

### Base Unit

|Component|Specs|Qty|Approx. Cost (₹)|
|-|-|-|-|
|ESP32 DevKit v1|WROOM-32, 30-pin|1|450|
|pH Sensor Module|Analog, BNC probe|1|900|
|Turbidity Sensor|Analog infrared|1|350|
|TDS Sensor Module|Analog (Gravity TDS)|1|400|
|Waterproof Temp Sensor|DS18B20, steel probe|1|120|
|AS7262 Spectral Sensor|6-channel, I2C|1|750|
|16x2 LCD Display|I2C module|1|250|
|PP Sediment Cartridge + Housing|5 micron, 10"|1|450|
|Activated Carbon Cartridge + Housing|Coconut shell blend, 10"|1|650|
|UV-C LED Module (inline)|12V DC, quartz sleeve|1|1,100|
|Solenoid Valve — Tap Lock|12V DC, normally closed|1|350|
|Solenoid Valve — Sensor Diverter|12V DC, 3-way|1|450|
|Solenoid Valve — Backwash Reverse|12V DC, normally closed|1|350|
|Relay Modules|1-channel, 5V, opto-isolated|3|180|
|MOSFET Driver Module|IRF520, UV-LED switching|1|70|
|Buzzer Module|5V active|1|30|
|GSM Module (optional)|SIM800L + antenna|1|550|
|Li-ion Cells|3.7V 2200mAh|4|700|
|4S BMS Module|4S, 10A+ rated|1|200|
|Buck Converter — 12V stage|Adjustable|1|120|
|Buck Converters — 5V/3.3V|LM2596|2|100|
|Solar Panel|12V 20W polycrystalline|1|850|
|Lithium Solar Charge Module|4S/14.8V rated|1|300|
|Enclosure Box|IP54 plastic project box|1|300|
|Raw Water Container|Small, food-grade plastic|1|250|
|Clean Water Container|Small, food-grade plastic|1|250|
|Backwash Drain Bucket|Small collection container|1|100|
|Silicone Tubing|8mm OD, food-grade, 4m|4 m|200|
|Push-fit Connectors|Elbows/tees, 1/4"–8mm|\~12|300|
|Jumper Wires|M-M, M-F, F-F, 40 pcs/pack|3 packs|180|
|Hookup Wire|22 AWG, 5m|5 m|100|
|Perfboard|For sensor/relay interconnect|1|80|
|Misc: screws, cable ties, solder, heat-shrink|—|—|250|
|**Base Unit Total**|||**≈ ₹11,880**|

### Optional Add-On Modules (fitted per household, based on local water test)

|Module|Specs|Cost (₹)|
|-|-|-|
|Fluoride/Arsenic Module|Activated alumina cartridge + housing|650|
|Heavy Metal Module|KDF-55/chelating resin cartridge + housing|750|
|RO Module|RO membrane + small 12V booster pump|1,900|

## 5\. Sensing System — How Detection Works

Three general-purpose sensors plus one spectral sensor, all read through a **single shared sensor chamber**:

* **pH probe** — flags acidic water (acid mine drainage signature).
* **Turbidity sensor** — measures cloudiness/silt content.
* **TDS/conductivity probe** — measures dissolved solids; distinguishes non-conductive silt from conductive dissolved metal ions.
* **Temperature sensor (DS18B20)** — affects microbial growth rate and other readings' interpretation.
* **AS7262 6-channel spectral sensor** — shines white light through the water and measures absorbance at 6 wavelengths (450/500/550/570/600/650 nm). Different dissolved metals absorb light differently at different wavelengths (e.g. iron absorbs strongly in violet/blue, copper in orange/red).

**Classification approach:** this project uses **calibrated rule-based threshold logic**, not a trained machine learning model. Reference readings are collected in advance using known test solutions (e.g. iron sulfate, copper sulfate, clean tap water baseline), and thresholds are set based on those readings. The firmware then applies simple `if/else` comparisons against these thresholds to flag a likely contaminant type. This is explicitly **not** a neural network — the architecture is designed so that a trained TensorFlow Lite Micro model could be substituted in later, but the current, honestly-scoped implementation is rule-based.

**Shared sensor chamber:** rather than using two separate sensor sets (one at the inlet, one at the outlet), a single sensor chamber is used for both readings. A diverter valve routes raw water through the chamber first (baseline reading), then routes it into the filter stack, and finally routes the filtered output back through the *same* chamber for the verification reading. This removes sensor-to-sensor calibration drift as a source of error and roughly halves sensor cost.

### 5.1 Graduated Risk Scoring (Safety Margin Buffer)

A plain hard-threshold check (`if (reading > LIMIT) unsafe`) has a blind spot: a reading of, say, 299 against a limit of 300 passes cleanly, even though it's effectively at the edge of the limit — and even more so if *several* parameters are simultaneously sitting just under their own limits at once. A single hard cutoff per parameter can't see that combined pattern.

To close this gap without claiming to be a trained ML model, each parameter gets **two** thresholds instead of one:

* **HARD limit** — the actual BIS 10500 limit. Crossing this on any parameter is an automatic, non-negotiable fail (tap stays locked, no exceptions).
* **MARGIN limit** — a configurable buffer below the hard limit (e.g., \~85–90% of it). Readings between the margin and the hard limit don't fail on their own, but contribute a proportional **risk score**:

  `risk\_i = clamp((reading − margin\_i) / (hard\_i − margin\_i), 0, 1)`

  These per-parameter risk scores are combined into a single **composite risk score** using per-parameter weights (spectral/heavy-metal channels weighted higher than turbidity, for example, since they're a more direct proxy for toxicity). If the composite score crosses a **caution threshold**, the system locks the tap and reports *"borderline — near contamination limits"*, even though no single parameter individually crossed its hard limit.

  This means a case like pH fine, turbidity fine, but TDS at 299/300 *and* the iron spectral channel mildly elevated *and* temperature slightly off — each individually "passing" — can still correctly trip a lock, the way a trained classifier picking up on a correlated pattern would, while staying fully deterministic, inspectable, and explainable (every number in the decision can be logged and shown on the LCD/serial monitor). It's still rule-based — just rules that look at the *pattern* across parameters and *distance from the limit*, not only a single yes/no line per parameter.

  Both the HARD and MARGIN constants (and the per-parameter weights) are placeholders in firmware and must be calibrated per Section 10.

  ## 6\. Filtration System — Base Unit + Optional Modules

  **Base unit (every household):**

1. Sediment filter (5 micron) — removes mud, silt, suspended particles
2. Activated carbon/resin — removes odor, chlorine, organic matter
3. UV-C disinfection chamber — kills bacteria and pathogens

   **Optional modules (fitted only where the local water test shows a need):**

* Activated alumina — for fluoride/arsenic-affected groundwater
* KDF-55/chelating resin — for heavy-metal-affected water near mining runoff
* RO membrane + small booster pump — for water with genuinely high TDS

  This avoids the cost and waste of installing every possible filter stage in every home, regardless of whether that home's water actually has that specific problem.

  ## 7\. Power System

* 4x 3.7V 2200mAh Li-ion cells wired in series (4S1P) → 14.8V nominal, \~32.5Wh total
* **4S BMS is mandatory** — protects against overcharge/over-discharge/short-circuit and balances the 4 cells
* 12V buck converter regulates the pack's fluctuating voltage (12.4–16.8V) down to a stable 12V for solenoids/UV-C/pump
* 5V/3.3V buck converters power the ESP32 and sensors
* Solar panel (12V 20W) + a **lithium-compatible** solar charge module (not a lead-acid controller) recharges the pack daily

  ## 8\. Wiring / Pin Mapping

  See `firmware/esp32\_water\_purifier/esp32\_water\_purifier.ino` for the exact pin definitions (top of file, `PIN DEFINITIONS` section). Summary:

|Signal|ESP32 Pin|Notes|
|-|-|-|
|pH sensor (analog)|GPIO 34|ADC1|
|Turbidity sensor (analog)|GPIO 35|ADC1|
|TDS sensor (analog)|GPIO 32|ADC1|
|DS18B20 temp sensor (OneWire)|GPIO 4|Needs 4.7kΩ pull-up|
|AS7262 spectral sensor|GPIO 21 (SDA), GPIO 22 (SCL)|I2C|
|16x2 LCD|GPIO 21 (SDA), GPIO 22 (SCL)|I2C, shared bus|
|Relay — Tap lock solenoid|GPIO 25||
|Relay — Sensor diverter solenoid|GPIO 26||
|Relay — Backwash solenoid|GPIO 27||
|MOSFET — UV-C LED|GPIO 14||
|Buzzer|GPIO 13||
|GSM module (SIM800L) TX/RX|GPIO 16, GPIO 17|Optional, via HardwareSerial2|

## 9\. Firmware — Logic Walkthrough

The firmware runs as a simple state machine:

1. **IDLE** — waiting for a dispense request (button press, or continuous loop for demo).
2. **BASELINE\_READ** — diverter routes raw water through the sensor chamber; all 5 sensors are read and logged.
3. **FILTERING** — water flows through the base filters + any fitted modules.
4. **VERIFY\_READ** — diverter routes filtered water back through the *same* sensor chamber; all 5 sensors read again.
5. **DECISION** — verify-read values are checked in two passes: (a) hard BIS 10500 threshold check per parameter — any single breach is an automatic fail; (b) if all hard checks pass, a composite risk score is computed across all parameters using the margin-buffer logic in [§5.1](#51-graduated-risk-scoring-safety-margin-buffer) — if this score crosses the caution threshold, the result is also treated as unsafe ("borderline"), even with no individual hard breach.
6. **DISPENSE** (if safe) — tap-lock solenoid opens, LCD shows "Water Safe", water flows.
7. **LOCKED** (if unsafe or borderline) — tap-lock solenoid stays closed, buzzer sounds, LCD shows the likely contaminant (or "Borderline — near limits" with the top contributing parameter), optional SMS alert sent. The LCD/serial log distinguishes a hard-limit fail from a borderline/composite-risk fail so the cause is always inspectable.
8. **BACKWASH** — triggered on a timer (default: every 24 hours of operation) or when consecutive VERIFY\_READ cycles show declining filtration efficiency; reverses clean water briefly through the filter stack via the backwash solenoid to a drain line.

## 10\. Calibration Procedure

The AS7262 thresholds in the firmware (`IRON\_450\_THRESHOLD`, `COPPER\_600\_THRESHOLD`, etc.) are **placeholder values** and must be recalibrated with your specific sensor unit before demo/deployment. This now includes both the HARD limit and the MARGIN limit for every parameter (see [§5.1](#51-graduated-risk-scoring-safety-margin-buffer)):

1. Prepare a clean tap-water baseline sample.
2. Prepare a low-concentration iron sulfate (FeSO₄) solution and a low-concentration copper sulfate (CuSO₄) solution.
3. With the sensor powered and the LCD/Serial Monitor active, dip the sensor in each sample and record the 6-channel readings.
4. Set each **HARD** threshold constant roughly halfway between the clean-water reading and the contaminated-water reading for that channel (same as before).
5. Set each **MARGIN** threshold constant at \~85–90% of the HARD value for that channel — this defines the start of the "caution zone" used by the composite risk score. Tighten this percentage (closer to 100%) if the demo shows too many false-positive borderline locks; loosen it (further from 100%) if it's missing real near-limit cases.
6. Adjust per-parameter **weights** (`RISK\_WEIGHT\_\*` constants) if one parameter is dominating the composite score disproportionately — heavy-metal/spectral channels should generally carry more weight than turbidity alone.
7. Re-flash the firmware with updated constants and confirm via Serial Monitor that a deliberately near-limit test sample (e.g., 90–95% of a hard limit) now correctly triggers a "borderline" lock instead of silently passing.

## 11\. Safety Mechanism

The tap-lock solenoid is **normally closed** and only receives an "open" signal from the ESP32 after the VERIFY\_READ passes every BIS 10500 threshold. There is no code path that opens the tap without a passing verify-read — this is a deliberate hardware-enforced safety design, not just a software warning.

## 12\. Backwash Cycle

Periodically (default every 24 hours, configurable via `BACKWASH\_INTERVAL\_MS`), the firmware briefly opens the backwash solenoid, reversing clean water flow backward through the sediment filter and UF stage to flush out accumulated debris through a separate drain line. This extends filter cartridge life and maintains flow rate over time.

## 13\. Deployment Model

Designed for **Point-of-Use (POU)** installation — at the household's own water outlet, not a shared community station. This matches how Indian rural water schemes already deploy POU units in confirmed contamination-hotspot villages. Suggested funding path: District Mineral Foundation (DMF) funds, which are already earmarked for mining-affected community welfare in the relevant districts.

## 14\. Cost Summary

* Base unit: **≈ ₹11,880** per household
* Optional modules added only as needed: Fluoride/Arsenic ₹650, Heavy Metal ₹750, RO ₹1,900

## 15\. How to Build \& Flash

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) (or PlatformIO).
2. Add ESP32 board support via Boards Manager (`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package\_esp32\_index.json`).
3. Install libraries via Library Manager:

   * `Adafruit AS726x`
   * `OneWire`
   * `DallasTemperature`
   * `LiquidCrystal I2C`
4. Open `firmware/esp32\_water\_purifier/esp32\_water\_purifier.ino`.
5. Update the calibration constants (see [Calibration Procedure](#10-calibration-procedure)).
6. Select **Board: ESP32 Dev Module**, select the correct COM port, and click Upload.
7. Open Serial Monitor at **115200 baud** to view live readings during testing.

## 16\. Repository Structure

```
.
├── README.md
└── firmware/
    └── esp32\_water\_purifier/
        └── esp32\_water\_purifier.ino
```

## 17\. References

* Removal of heavy metal ions from wastewater using ion exchange resin in a batch process with kinetic isotherm — ScienceDirect, 2024. https://www.sciencedirect.com/science/article/pii/S1026918524000490
* Selective removal of contaminants from water sources using inorganic media — US Patent. https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/5078889
* Mahato, Singh, Tiwari \& Singh, "Risk Assessment Due to Intake of Metals in Groundwater of East Bokaro Coalfield, Jharkhand, India," Exposure and Health, 2016. https://link.springer.com/article/10.1007/s12403-016-0201-2
* "Evaluation of Hydrogeochemical Characteristics of Groundwater Resources in the Kuju Coal Mining Area of Ramgarh District, Jharkhand," Water, Air, \& Soil Pollution. https://link.springer.com/article/10.1007/s11270-025-08864-5
* "Evaluation of the Surface Water Quality Index of Jharia Coal Mining Region and Its Management of Surface Water Resources," Springer. https://link.springer.com/chapter/10.1007/978-981-10-5792-2\_34
* BIS 10500:2012 — Indian Standard for Drinking Water Specification



