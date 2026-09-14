#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESP_Mail_Client.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "secrets.h"   // <-- create this from secrets_template.h, keep it out of git

// --- Dynamic Evaluator Email ---
String current_recipient_email = DEFAULT_RECIPIENT_EMAIL; // Default starting email

// --- Pin Definitions (ADC1 safe for Wi-Fi) ---
#define PIN_TDS_POST 35
#define PIN_PH_POST 32  
#define RXD2 16 // Node 1 Data
#define TXD2 17 
#define RXD1 26 // Node 2 Status
#define TXD1 27 

// --- System Objects ---
WebServer server(80);
LiquidCrystal_I2C lcd(0x27, 16, 2); 
SMTPSession smtp;

// --- Global State Variables ---
int pre_tds = 0; 
float pre_ph = 0.0;
float pre_temp = 0.0; 
int post_tds = 0; 
float post_ph = 0.0;
String system_status = "UNKNOWN";

// --- Flags & Trackers ---
bool backwash_email_sent = false;
bool warning_email_sent = false;
bool ip_printed = false; 
int lcd_page = 0; 

// --- Timers ---
unsigned long last_lcd_update = 0;
unsigned long last_sensor_read = 0;

int readPostTDS() {
  long sum = 0;
  for (int i = 0; i < 10; i++) { 
    sum += analogRead(PIN_TDS_POST); 
    delay(5); 
  }
  float voltage = (sum / 10.0) * (3.3 / 4095.0);
  float compVolt = voltage / 1.0; 
  float tdsValue = (133.42 * pow(compVolt, 3) - 255.86 * pow(compVolt, 2) + 857.39 * compVolt) * 0.5;
  return constrain((int)tdsValue, 0, 2000);
}

float readPostPH() {
  long sum = 0;
  for (int i = 0; i < 10; i++) { 
    sum += analogRead(PIN_PH_POST); 
    delay(5); 
  }
  float voltage = (sum / 10.0) * (3.3 / 4095.0);
  return (3.5 * voltage) + 2.4; 
}

void listenToNetwork() {
  if (Serial2.available()) {
    String incoming = Serial2.readStringUntil('\n');
    incoming.trim();
    if (incoming.startsWith("PRE:")) {
      incoming.remove(0, 4); 
      int firstComma = incoming.indexOf(',');
      int secondComma = incoming.indexOf(',', firstComma + 1); 
      if (firstComma > 0 && secondComma > 0) {
        pre_ph = incoming.substring(0, firstComma).toFloat();
        pre_tds = incoming.substring(firstComma + 1, secondComma).toInt();
        pre_temp = incoming.substring(secondComma + 1).toFloat(); 
      }
    }
  }

  if (Serial1.available()) {
    String incoming = Serial1.readStringUntil('\n');
    incoming.trim();
    if (incoming.startsWith("STAT:N")) system_status = "NORMAL";
    else if (incoming.startsWith("STAT:B")) system_status = "BACKWASH";
    else if (incoming.startsWith("STAT:S")) system_status = "STOPPED";
  }
}

void sendEmailAlert(String subject, String message) {
  if (WiFi.status() != WL_CONNECTED) return; 

  ESP_Mail_Session session;
  session.server.host_name = SMTP_HOST;
  session.server.port = SMTP_PORT;
  session.login.email = SENDER_EMAIL;
  session.login.password = SENDER_PASSWORD;
  session.login.user_domain = "";

  SMTP_Message msg;
  msg.sender.name = "RO System Node 3";
  msg.sender.email = SENDER_EMAIL;
  msg.subject = subject;
  
  msg.addRecipient("Evaluator", current_recipient_email); 
  msg.text.content = message;

  if (smtp.connect(&session)) {
    MailClient.sendMail(&smtp, &msg);
    Serial.println("Email successfully sent to: " + current_recipient_email);
  }
}

void evaluateAlarms() {
  if (post_tds > 50) {
    if (!warning_email_sent) {
      sendEmailAlert("URGENT: Filter Change Needed", "Post-filtration TDS has exceeded safe limits. Current Post-TDS: " + String(post_tds) + " ppm. Water is unsafe to drink.");
      warning_email_sent = true;
    }
  } else { 
    warning_email_sent = false;
  }

  if (system_status == "BACKWASH") {
    if (!backwash_email_sent) {
      sendEmailAlert("SYSTEM NOTICE: Backwash Initiated", "The automated backwash cycle has begun to clean the pre-filters.");
      backwash_email_sent = true;
    }
  } else { 
    backwash_email_sent = false;
  }
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta http-equiv='refresh' content='10'><meta name='viewport' content='width=device-width, initial-scale=1'><title>RO Monitor</title>";
  html += "<style>body{font-family:Arial;text-align:center;background:#121212;color:white;} .card{background:#1e1e1e;padding:20px;margin:20px;border-radius:10px;} h1{color:#00adb5;} input[type=email]{padding:10px; width:80%; border-radius:5px; border:none; margin-bottom:10px;} input[type=submit]{padding:10px 20px; background:#00adb5; color:white; border:none; border-radius:5px; font-weight:bold; cursor:pointer;}</style></head><body>";
  html += "<h1>Live RO Plant Dashboard</h1>";
  
  html += "<div class='card'><h2>Evaluator Setup</h2>";
  html += "<form action='/setemail' method='POST'>";
  html += "<input type='email' name='eval_email' placeholder='Enter your email address' required>";
  html += "<br><input type='submit' value='Set Alert Destination'>";
  html += "</form>";
  html += "<p style='font-size:0.9em; color:#888;'>Current Target: <b>" + current_recipient_email + "</b></p></div>";

  html += "<div class='card'><h2>Status: <span style='color:#00ff00'>" + system_status + "</span></h2></div>";
  html += "<div class='card'><h2>Raw Water (Pre)</h2><p>TDS: " + String(pre_tds) + " ppm</p><p>pH: " + String(pre_ph) + "</p><p>Temp: " + String(pre_temp) + " &deg;C</p></div>";
  html += "<div class='card'><h2>Clean Water (Post)</h2><p>TDS: " + String(post_tds) + " ppm</p><p>pH: " + String(post_ph) + "</p></div>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleSetEmail() {
  if (server.hasArg("eval_email")) {
    current_recipient_email = server.arg("eval_email");
    Serial.println("\n>>> EVALUATOR EMAIL UPDATED TO: " + current_recipient_email + " <<<");
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2); 
  Serial1.begin(9600, SERIAL_8N1, RXD1, TXD1); 
  Serial2.setTimeout(50);
  Serial1.setTimeout(50);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("System Booting..");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("\nConnecting to Wi-Fi in background...");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/setemail", HTTP_POST, handleSetEmail); 
  server.begin();
}

void loop() {
  listenToNetwork();
  
  if (WiFi.status() == WL_CONNECTED) {
    if (!ip_printed) {
      Serial.println("\n=================================");
      Serial.println("       WI-FI CONNECTED!          ");
      Serial.print("   WEB DASHBOARD IP: ");
      Serial.println(WiFi.localIP());
      Serial.println("=================================\n");
      ip_printed = true; 
    }
    server.handleClient(); 
  } else { ip_printed = false; }

  if (millis() - last_sensor_read > 2000) {
    post_tds = readPostTDS();
    post_ph = readPostPH();
    evaluateAlarms(); 
    
    Serial.println("-----------------------------------");
    Serial.printf("STATUS    : %s\n", system_status.c_str());
    Serial.printf("PRE-FILTER: TDS %d ppm | pH %.2f | Temp %.2f C\n", pre_tds, pre_ph, pre_temp);
    Serial.printf("POST-RO   : TDS %d ppm | pH %.2f\n", post_tds, post_ph);
    Serial.println("-----------------------------------");

    last_sensor_read = millis();
  }

  if (millis() - last_lcd_update > 2500) {
    lcd.clear();
    
    if (lcd_page == 0) {
      lcd.setCursor(0, 0);
      lcd.print("Sys: " + system_status);
      lcd.setCursor(0, 1);
      lcd.printf("Temp: %.1f C", pre_temp);
    } 
    else if (lcd_page == 1) {
      lcd.setCursor(0, 0);
      lcd.printf("Pre-TDS: %d", pre_tds);
      lcd.setCursor(0, 1);
      lcd.printf("Pre-pH : %.2f", pre_ph);
    }
    else if (lcd_page == 2) {
      lcd.setCursor(0, 0);
      lcd.printf("Pst-TDS: %d", post_tds);
      lcd.setCursor(0, 1);
      lcd.printf("Pst-pH : %.2f", post_ph);
    }

    lcd_page++; 
    if (lcd_page > 2) lcd_page = 0; 
    last_lcd_update = millis();
  }
}
