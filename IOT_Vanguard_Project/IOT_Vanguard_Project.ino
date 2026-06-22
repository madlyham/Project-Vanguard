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
unsigned long previousBuzzerTime = 0; // The stopwatch memory
int beepCount = 0;                    // How many times has it beeped?
bool isAlarmActive = false;           // Is the SOS currently happening?
bool isBuzzerOn = false;              // Is the physical pin currently HIGH or LOW?

// --- HARDWARE CONFIGURATION ---
#define ONBOARD_LED 2 // Stealth blue LED on D4 (GPIO 2)
#define AUX_PIN 14    // D5 - LoRa Busy pin
#define BUZZER_PIN 16 // D0 - Tactical Audio Alarm

// 1. Radio Serial Port
SoftwareSerial loraSerial(5, 4); // ESP D1 (RX) to LoRa TX, ESP D2 (TX) to LoRa RX

// 2. Fingerprint Serial Port
SoftwareSerial fingerSerial(12, 13); // ESP D6 (RX) to AS608 TX, ESP D7 (TX) to AS608 RX
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

void setup() {
  Serial.begin(115200);      
  loraSerial.begin(9600);    
  
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, HIGH); 
  
  pinMode(AUX_PIN, INPUT_PULLUP);
  
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);   

  Serial.println("\n--- VANGUARD NODE A (FIELD TERMINAL) BOOTING ---");

  // --- BOOT THE FINGERPRINT SCANNER ---
  finger.begin(57600); 
  if (finger.verifyPassword()) {
    Serial.println("Biometric Scanner Online. System LOCKED.");
  } else {
    Serial.println("ERROR: Scanner not found! Check D6/D7 wiring.");
  }

  // 1. BOOT THE TACTICAL WIFI (Server is dead until unlocked)
  WiFi.softAP(ssid, password);
  Serial.print("Tactical AP Active! IP: ");
  Serial.println(WiFi.softAPIP());

  // 2. API ROUTE: SENDING DATA
  server.on("/send", HTTP_GET, []() {
    String msg = server.arg("msg");
    String lat = server.arg("lat");
    String lon = server.arg("lon");
    
    String payload = msg + "|" + lat + "|" + lon + "\n"; 
    Serial.println(">>> BROADCASTING: " + payload);
    
    int timeoutCounter = 0;
    while(digitalRead(AUX_PIN) == LOW && timeoutCounter < 100) { 
      delay(10); 
      timeoutCounter++;
    }
    
    loraSerial.print(payload);
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Transmission Sent");
  });

  // 3. API ROUTE: RECEIVING DATA
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
  
  // --- STATE 1: LOCKED (Listening to Scanner) ---
  if (!systemUnlocked) {
    fingerSerial.listen(); 
    int fingerStatus = getFingerprintIDez(); 
    
    if (fingerStatus >= 0) { // WE HAVE A MATCH!
      systemUnlocked = true;
      Serial.println("AUTHORIZATION GRANTED. Booting Web Server...");
      
      server.begin(); 
      
      // We can leave this short double-beep as delay() because it only happens once during boot
      digitalWrite(BUZZER_PIN, HIGH); delay(100);
      digitalWrite(BUZZER_PIN, LOW); delay(50);
      digitalWrite(BUZZER_PIN, HIGH); delay(100);
      digitalWrite(BUZZER_PIN, LOW);
      
      loraSerial.listen(); // Switch ears to LoRa Radio
    }
  } 
  // --- STATE 2: UNLOCKED (Listening to Radio & App) ---
  else {
    server.handleClient(); // MIT App polling never gets blocked!

    // 1. Check for new radio messages
    if (loraSerial.available()) {
      String incomingPayload = loraSerial.readStringUntil('\n'); 
      incomingPayload.trim(); 
      
      if (incomingPayload.length() > 0) {
        Serial.println("<<< INCOMING INTERCEPT: " + incomingPayload);
        latestMessage = incomingPayload; 
        
        // SOS DETECTED: Just flip the switch, don't freeze the code!
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
