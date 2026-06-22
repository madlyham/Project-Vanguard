#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <LiquidCrystal.h> // --- STANDARD 6-PIN LCD LIBRARY ---

// --- TACTICAL NETWORK SETTINGS ---
const char* ssid = "VSquadB"; // OVERWATCH BASE STATION
const char* password = "";    

ESP8266WebServer server(80);
String latestMessage = ""; 

// --- HARDWARE CONFIGURATION ---
#define AUX_PIN 14    // D5 - LoRa Busy pin
#define BUZZER_PIN 15 // D8 - Tactical Audio Alarm (Must be D8 for safe boot)

// 1. Radio Serial Port
SoftwareSerial loraSerial(5, 4); // ESP D1 (RX) to LoRa TX, ESP D2 (TX) to LoRa RX

// 2. LCD Screen Configuration
// We map the 6 LCD pins: RS, EN, D4, D5, D6, D7
// Mapped to ESP pins: D3(0), D4(2), D6(12), D7(13), D0(16), RX(3)
LiquidCrystal lcd(0, 2, 12, 13, 16, 3); 

// --- NON-BLOCKING ALARM VARIABLES ---
unsigned long previousBuzzerTime = 0; 
int beepCount = 0;                    
bool isAlarmActive = false;           
bool isBuzzerOn = false;              

void setup() {
  Serial.begin(115200);      
  loraSerial.begin(9600);    
  
  pinMode(AUX_PIN, INPUT_PULLUP);
  
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // Keep it silent on boot

  // Boot the LCD Screen
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("VANGUARD BASE");
  lcd.setCursor(0, 1);
  lcd.print("SYSTEM ONLINE");

  Serial.println("\n--- VANGUARD NODE B (OVERWATCH) BOOTING ---");

  // 1. BOOT THE TACTICAL WIFI
  WiFi.softAP(ssid, password);
  Serial.print("Tactical AP Active! IP: ");
  Serial.println(WiFi.softAPIP());

  // 2. API ROUTE: SENDING DATA (Base App -> Radio)
  server.on("/send", HTTP_GET, []() {
    String msg = server.arg("msg");
    String lat = server.arg("lat");
    String lon = server.arg("lon");
    
    String payload = msg + "|" + lat + "|" + lon + "\n"; 
    
    // Update LCD to show we are transmitting
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("TRANSMITTING...");
    
    int timeoutCounter = 0;
    while(digitalRead(AUX_PIN) == LOW && timeoutCounter < 100) { 
      delay(10); 
      timeoutCounter++;
    }
    
    loraSerial.print(payload);
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Transmission Sent");
  });

  // 3. API ROUTE: RECEIVING DATA (Radio -> Base App)
  server.on("/receive", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    
    if (latestMessage.length() > 0) {
      server.send(200, "text/plain", latestMessage);
      latestMessage = ""; 
    } else {
      server.send(204, "text/plain", "No new intel.");
    }
  });
  
  // Start the Engine
  server.begin(); 
  Serial.println("Base Station Web Server Locked & Loaded!");
} 

void loop() {
  server.handleClient(); // Keep the App polling alive

  // --- RADIO INTERCEPT SCANNER ---
  if (loraSerial.available()) {
    String incomingPayload = loraSerial.readStringUntil('\n'); 
    incomingPayload.trim(); 
    
    if (incomingPayload.length() > 0) {
      Serial.println("<<< INCOMING INTERCEPT: " + incomingPayload);
      latestMessage = incomingPayload; 

      // Update the Physical LCD Screen with the new message
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("INCOMING INTEL:");
      lcd.setCursor(0, 1);
      // Print up to 16 characters of the message so it doesn't break the screen
      lcd.print(incomingPayload.substring(0, 16)); 

      // --- EMERGENCY SOS OVERRIDE ---
      if (incomingPayload.indexOf("SOS") >= 0) {
        Serial.println("!!! EMERGENCY SOS DETECTED - SOUNDING ALARM !!!");
        
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("!!! SOS ALERT !!!");
        lcd.setCursor(0, 1);
        lcd.print("SQUAD IN DANGER");

        isAlarmActive = true; 
        beepCount = 0;
        isBuzzerOn = false;
      }
    }
  }

  // --- THE NON-BLOCKING ALARM HANDLER ---
  if (isAlarmActive) {
    unsigned long currentMillis = millis();
    int waitTime = isBuzzerOn ? 50 : 100; // Fast, aggressive beeps

    if (currentMillis - previousBuzzerTime >= waitTime) {
      previousBuzzerTime = currentMillis; 

      if (isBuzzerOn) {
        digitalWrite(BUZZER_PIN, LOW); 
        isBuzzerOn = false;
        beepCount++; 
        
        if (beepCount >= 5) {
          isAlarmActive = false; // Silence the alarm after 5 bursts
        }
      } else {
        digitalWrite(BUZZER_PIN, HIGH); 
        isBuzzerOn = true;
      }
    }
  }
}
