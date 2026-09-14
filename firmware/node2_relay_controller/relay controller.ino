#include <Arduino.h>

// --- Communication Pins (To Node 3) ---
#define RXD2 16 
#define TXD2 17 // TX2 connects to Node 3 RX1 (GPIO 26) to send the status

// --- Physical Pin Mapping ---
#define PIN_R1          13 // IN 1: Solenoid 1
#define PIN_R2          14 // IN 2: Solenoid 2 (Drain)
#define PIN_R3          15 // IN 3: Solenoid 3 
#define PIN_R5          19 // IN 4: Solenoid 5
#define PIN_R6          21 // IN 5: Solenoid 6 (Brine Route)
#define PIN_IN_PUMP     22 // IN 6: Input Pump
#define PIN_BW_PUMP     23 // IN 7: Backwash Pump
#define PIN_R4          18 // IN 8: Solenoid 4

// --- Relay Logic State ---
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

void triggerStop() {
  // Turn OFF pumps FIRST to kill pressure instantly, followed by valves
  digitalWrite(PIN_IN_PUMP, RELAY_OFF);
  digitalWrite(PIN_BW_PUMP, RELAY_OFF);
  
  digitalWrite(PIN_R1, RELAY_OFF);
  digitalWrite(PIN_R2, RELAY_OFF);
  digitalWrite(PIN_R3, RELAY_OFF);
  digitalWrite(PIN_R5, RELAY_OFF); 
  digitalWrite(PIN_R6, RELAY_OFF);
  digitalWrite(PIN_R4, RELAY_OFF);
  
  Serial.println(">> STATE CHANGE: [S] - STOP (Pumps and Relays OFF)");
  
  // Transmit status to Node 3
  Serial2.println("STAT:S"); 
}

void triggerNormal() {
  Serial.println(">> STATE CHANGE: [N] - Initiating NORMAL OPERATION...");
  
  // 1. Ensure backwash path is closed
  digitalWrite(PIN_R2, RELAY_OFF);
  digitalWrite(PIN_R6, RELAY_OFF); 
  digitalWrite(PIN_BW_PUMP, RELAY_OFF);
  
  // 2. Open Forward Solenoids FIRST
  digitalWrite(PIN_R1, RELAY_OFF);
  digitalWrite(PIN_R3, RELAY_ON);
  digitalWrite(PIN_R5, RELAY_ON);
  digitalWrite(PIN_R4, RELAY_ON); 
  Serial.println("   -> Solenoids open. Waiting 2 seconds for pressure equalization...");
  
  // 3. Wait 2 seconds
  delay(2000); 
  
  // 4. Turn ON the Pump
  digitalWrite(PIN_IN_PUMP, RELAY_ON);
  Serial.println("   -> INPUT PUMP ON. Filtration running.");
  
  // Transmit status to Node 3
  Serial2.println("STAT:N"); 
}

void triggerBackwash() {
  Serial.println(">> STATE CHANGE: [B] - Initiating BACKWASH...");
  
  // 1. Ensure forward path is closed
  digitalWrite(PIN_R1, RELAY_ON);
  digitalWrite(PIN_R3, RELAY_OFF);
  digitalWrite(PIN_R5, RELAY_OFF);
  digitalWrite(PIN_R4, RELAY_OFF); 
  digitalWrite(PIN_IN_PUMP, RELAY_OFF);
  
  // 2. Open Reverse Solenoids FIRST
  digitalWrite(PIN_R6, RELAY_ON); 
  digitalWrite(PIN_R2, RELAY_ON);
  Serial.println("   -> Solenoids open. Waiting 2 seconds for brine routing...");
  
  // 3. Wait 2 seconds
  delay(2000);
  
  // 4. Turn ON the Pump
  digitalWrite(PIN_BW_PUMP, RELAY_ON);
  Serial.println("   -> BACKWASH PUMP ON. Reverse flush running.");
  
  // Transmit status to Node 3
  Serial2.println("STAT:B"); 
}

void setup() {
  Serial.begin(115200);
  
  // Initialize UART2 for Node 3 communication
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2); 
  
  pinMode(PIN_R1, OUTPUT);
  pinMode(PIN_R2, OUTPUT);
  pinMode(PIN_R3, OUTPUT);
  pinMode(PIN_R5, OUTPUT);
  pinMode(PIN_R6, OUTPUT);
  pinMode(PIN_IN_PUMP, OUTPUT);
  pinMode(PIN_BW_PUMP, OUTPUT);
  pinMode(PIN_R4, OUTPUT);

  triggerStop(); 

  Serial.println("\n--- PROTOTYPE RELAY TESTER BOOTED ---");
  Serial.println("Type 'N', 'B', or 'S' in the Serial Monitor and press Enter:");
}

void loop() {
  if (Serial.available()) {
    char incomingChar = Serial.read();
    incomingChar = toupper(incomingChar);
    
    if (incomingChar == 'N') {
      triggerNormal();
    } 
    else if (incomingChar == 'B') {
      triggerBackwash();
    }
    else if (incomingChar == 'S') {
      triggerStop();
    }
  }
}