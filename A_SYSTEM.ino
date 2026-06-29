#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <Adafruit_Fingerprint.h> 

// --- TACTICAL NETWORK SETTINGS ---
const char* ssid = "VSquadA"; 
const char* password = "";    

ESP8266WebServer server(80);
String latestMessage = ""; 
bool systemUnlocked = false; 

// --- NON-BLOCKING ALARM VARIABLES ---
unsigned long previousBuzzerTime = 0; 
int beepCount = 0;                    
bool isAlarmActive = false;           
bool isBuzzerOn = false;              

// --- HARDWARE CONFIGURATION ---
#define ONBOARD_LED 2 // Stealth blue LED on D4 (GPIO 2)
#define AUX_PIN 14    // D5 - LoRa Busy pin (E32 specific)
#define BUZZER_PIN 16 // D0 - Tactical Audio Alarm

// 1. Radio Serial Port (E32 LoRa)
SoftwareSerial loraSerial(5, 4); // ESP D1 (RX) to LoRa TX, ESP D2 (TX) to LoRa RX

// 2. Fingerprint Serial Port
SoftwareSerial fingerSerial(12, 13); // ESP D6 (RX) to AS608 TX, ESP D7 (TX) to AS608 RX
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

void setup() {
  Serial.begin(115200);      
  loraSerial.begin(9600);    
  
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, HIGH); // Turn off onboard LED (Active LOW)
  
  pinMode(AUX_PIN, INPUT_PULLUP);
  
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // Ensure buzzer is quiet on boot
   
  Serial.println("\n--- VANGUARD NODE A (FIELD TERMINAL) BOOTING ---");

  // --- HARDWARE LOCKDOWN ---
  // Ensure the Wi-Fi radio emits absolutely zero RF signature until unlocked
  WiFi.mode(WIFI_OFF);
  Serial.println("RF Signature: DARK (Wi-Fi Disabled).");

  // --- BOOT THE FINGERPRINT SCANNER ---
  finger.begin(57600); 
  if (finger.verifyPassword()) {
    Serial.println("Biometric Scanner Online. System LOCKED.");
  } else {
    Serial.println("ERROR: Scanner not found! Check D6/D7 wiring.");
    while (1) { delay(1); } // Brick the system if hardware is tampered with
  }

  // --- API ROUTE: SENDING DATA ---
  server.on("/send", HTTP_GET, []() {
    String msg = server.arg("msg");
    String lat = server.arg("lat");
    String lon = server.arg("lon");
    
    String payload = msg + "|" + lat + "|" + lon + "\n"; 
    Serial.println(">>> BROADCASTING: " + payload);
    
    // E32 Module strict timing: Wait until AUX is HIGH (module is idle)
    int timeoutCounter = 0;
    while(digitalRead(AUX_PIN) == LOW && timeoutCounter < 100) { 
      delay(10); 
      timeoutCounter++;
    }
    
    loraSerial.print(payload);
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Transmission Sent");
  });

  // --- API ROUTE: RECEIVING DATA ---
  server.on("/receive", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    
    if (latestMessage.length() > 0) {
      server.send(200, "text/plain", latestMessage);
      latestMessage = ""; 
    } else {
      server.send(204, "text/plain", "No new intel.");
    }
  });
} 

void loop() {
  
  // ========================================================
  // STATE 1: LOCKED (Listening to Scanner ONLY)
  // ========================================================
  if (!systemUnlocked) {
    fingerSerial.listen(); // Force ESP to only listen to the scanner
    int fingerStatus = getFingerprintIDez(); 
    
    if (fingerStatus >= 0) { // WE HAVE A MATCH!
      systemUnlocked = true;
      Serial.println("AUTHORIZATION GRANTED. Booting Network...");
      
      // 1. Give tactical audio feedback (Double Beep)
      digitalWrite(BUZZER_PIN, HIGH); delay(100);
      digitalWrite(BUZZER_PIN, LOW); delay(50);
      digitalWrite(BUZZER_PIN, HIGH); delay(100);
      digitalWrite(BUZZER_PIN, LOW);

      // 2. Boot the Wi-Fi Radio
      WiFi.mode(WIFI_AP);
      WiFi.softAP(ssid, password);
      Serial.print("Tactical AP Active! IP: ");
      Serial.println(WiFi.softAPIP());
      
      // 3. Boot the API Endpoints
      server.begin(); 
      
      // 4. Permanently switch the ESP's ears to the LoRa Radio
      loraSerial.listen(); 
    }
  } 
  
  // ========================================================
  // STATE 2: UNLOCKED (Handling Radio & App)
  // ========================================================
  else {
    server.handleClient(); // Keep the App Inventor link alive

    // 1. Check for incoming radio transmissions
    if (loraSerial.available()) {
      String incomingPayload = loraSerial.readStringUntil('\n'); 
      incomingPayload.trim(); 
      
      if (incomingPayload.length() > 0) {
        Serial.println("<<< INCOMING INTERCEPT: " + incomingPayload);
        latestMessage = incomingPayload; 
        
        // SOS DETECTED - Trigger the physical alarm!
        if (incomingPayload.indexOf("SOS") >= 0) {
          Serial.println("!!! SOS DETECTED - ALARM TRIGGERED !!!");
          isAlarmActive = true; 
          beepCount = 0;
          isBuzzerOn = false;
        }
      }
    }

    // 2. THE NON-BLOCKING ALARM HANDLER
    if (isAlarmActive) {
      unsigned long currentMillis = millis();
      
      // Decide how long to wait based on whether the buzzer is currently ON or OFF
      int waitTime = isBuzzerOn ? 50 : 100; 

      if (currentMillis - previousBuzzerTime >= waitTime) {
        previousBuzzerTime = currentMillis; // Reset stopwatch

        if (isBuzzerOn) {
          digitalWrite(BUZZER_PIN, LOW); // Turn it off
          isBuzzerOn = false;
          beepCount++; // That completes one full beep
          
          if (beepCount >= 5) {
            isAlarmActive = false; // Turn off the alarm after 5 beeps
          }
        } else {
          digitalWrite(BUZZER_PIN, HIGH); // Turn it on
          isBuzzerOn = true;
        }
      }
    }
  }
}

// --- HELPER FUNCTION: QUICK FINGERPRINT SCAN ---
int getFingerprintIDez() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK) return -1;
  
  return finger.fingerID; 
}
