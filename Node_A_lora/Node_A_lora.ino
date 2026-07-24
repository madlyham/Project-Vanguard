// ================================================================
// VANGUARD NODE A — FIELD TERMINAL — STANDARDIZED BUILD
// ================================================================

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>
#include <Adafruit_Fingerprint.h> 

// --- NETWORK & SYSTEM STATE ---
const char* ssid = "VSquadA"; 
const char* password = "";     
ESP8266WebServer server(80);

// --- TACTICAL ARQ / CSMA PROTOCOL VARIABLES ---
uint8_t txSequenceNumber = 0;        // Rolling packet ID (0-255)
int pendingAckId = -1;               // -1 means no packet is awaiting ACK
String pendingPayload = "";          // Stores payload for retransmission
unsigned long lastTxTime = 0;        // Timestamp of last transmission attempt
uint8_t retryCount = 0;              // Current retry attempt
const uint8_t MAX_RETRIES = 3;       // Max retransmissions before giving up
const unsigned long ACK_TIMEOUT = 2000; // 2 seconds to wait for an ACK

String latestMessage = ""; 
bool systemUnlocked = false; 

// --- THE ONLY 4 LOCATION VARIABLES YOU NEED (DOUBLE PRECISION) ---
double currentLat = 10.841970; // This Node (Node A)
double currentLon = 106.838440;
double friendLat  = 0.000000;  // Target Node (Node B)
double friendLon  = 0.000000;

// --- ALARM TIMING ---
unsigned long previousBuzzerTime = 0; 
int beepCount = 0;                    
bool isAlarmActive = false;           
bool isBuzzerOn = false;              

// --- HARDWARE PINOUTS ---
#define ONBOARD_LED 2 // Active LOW
#define AUX_PIN 14    // D5 - E32 Busy/Idle pin
#define BUZZER_PIN 16 // D0 - Alarm Buzzer

SoftwareSerial loraSerial(5, 4);     // RX, TX for E32 Radio (D1, D2)
SoftwareSerial fingerSerial(12, 13); // RX, TX for Biometric Scanner (D6, D7)
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

// ================================================================
// 1. SETUP & BOOT
// ================================================================
void setup() {
  delay(2000);
  Serial.begin(115200);     
  loraSerial.begin(9600); 
  randomSeed(analogRead(A0)); // Seeds the random number generator    
  
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, HIGH); // Turn off LED
  pinMode(AUX_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);   // Silence buzzer
    
  Serial.println("\n--- VANGUARD NODE A BOOTING ---");
  WiFi.mode(WIFI_OFF);

  finger.begin(57600); 
  if (finger.verifyPassword()) {
    Serial.println("[SYSTEM] Scanner Online. Status: LOCKED.");
  } else {
    Serial.println("[ERROR] Scanner not detected on D6/D7!");
    while (1) { delay(10); } 
  }

  setupAPIRoutes();
} 

// ================================================================
// 2. MAIN LOOP
// ================================================================
void loop() {
  if (!systemUnlocked) {
    handleBiometricLock();
  } else {
    server.handleClient(); 
    handleIncomingRadio();
    handleARQLoop();       // <-- ADD THIS LINE HERE
    handleAlarmBuzzer();
  }
}

// ================================================================
// 3. CORE FUNCTIONS
// ================================================================
void handleBiometricLock() {
  fingerSerial.listen(); 
  int status = getFingerprintID(); 
  
  if (status >= 0) { 
    systemUnlocked = true;
    Serial.println("[SYSTEM] ACCESS GRANTED. Booting Network...");
    
    digitalWrite(BUZZER_PIN, HIGH); delay(100);
    digitalWrite(BUZZER_PIN, LOW); delay(50);
    digitalWrite(BUZZER_PIN, HIGH); delay(100);
    digitalWrite(BUZZER_PIN, LOW);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    WiFi.setOutputPower(15); 
    Serial.print("[SYSTEM] Tactical AP IP: ");
    Serial.println(WiFi.softAPIP());
    
    server.begin(); 
    loraSerial.listen(); // Lock serial exclusively to LoRa
    while (loraSerial.available()) { loraSerial.read(); } 
  } 
  else if (status == -2) { 
    digitalWrite(BUZZER_PIN, HIGH); delay(300); digitalWrite(BUZZER_PIN, LOW);
  }
}

void executeCSMATransmission(String payload) {
  // Step 1: Carrier Sense (Wait for hardware radio to finish current task)
  unsigned long startSense = millis();
  while (digitalRead(AUX_PIN) == LOW && (millis() - startSense < 1000)) {
    delay(5); 
  }

  // Step 2: Collision Avoidance (Random backoff to prevent sync-collisions)
  // Random delay between 50ms and 200ms
  unsigned long backoff = random(50, 200);
  unsigned long startBackoff = millis();
  unsigned long hardDeadline = millis() + 500;
  while (millis() - startBackoff < backoff && millis() < hardDeadline) {
    if (digitalRead(AUX_PIN) == LOW) {
      startBackoff = millis();
    }
    delay(2);
  }

  // Step 3: Transmit
  Serial.println(">>> [TX ON AIR]: " + payload);
  loraSerial.println(payload);
}

// ----------------------------------------------------------------
// 2. QUEUE A NEW PACKET WITH ARQ (Replaces old sendRadioPacket)
// ----------------------------------------------------------------
void sendRadioPacket(String command) {
  txSequenceNumber = (txSequenceNumber + 1) % 256; // Roll over at 255
  
  // Format: VSQUAD:<ID>|<COMMAND>|<LAT>|<LON>
  pendingAckId = txSequenceNumber;
  pendingPayload = "VSQUAD:" + String(pendingAckId) + "|" + command + "|" + 
                   String(currentLat, 6) + "|" + String(currentLon, 6);
  
  retryCount = 0;
  executeCSMATransmission(pendingPayload);
  lastTxTime = millis();
}

// ----------------------------------------------------------------
// 3. ARQ RETRY LOOP (Must be called inside loop())
// ----------------------------------------------------------------
void handleARQLoop() {
  if (pendingAckId != -1) {
    if (millis() - lastTxTime >= ACK_TIMEOUT) {
      if (retryCount < MAX_RETRIES) {
        retryCount++;
        Serial.println("[ARQ] ACK Timeout! Retransmitting pkt #" + String(pendingAckId) + 
                       " (Attempt " + String(retryCount) + "/" + String(MAX_RETRIES) + ")");
        executeCSMATransmission(pendingPayload);
        lastTxTime = millis();
      } else {
        Serial.println("[ARQ ERROR] Transmission Failed! Target Node Unreachable (Pkt #" + String(pendingAckId) + ")");
        pendingAckId = -1; // Give up to prevent endless loop
      }
    }
  }
}

// ----------------------------------------------------------------
// 4. UPGRADED INCOMING RECEIVER (Prints exact App Payload format)
// ----------------------------------------------------------------
void handleIncomingRadio() {
  if (loraSerial.available()) {
    String rawPayload = loraSerial.readStringUntil('\n'); 
    rawPayload.trim(); 

    if (rawPayload.length() == 0 || !rawPayload.startsWith("VSQUAD:")) return;

    String cleanPayload = rawPayload.substring(7); // Strip "VSQUAD:"
    
    int firstPipe  = cleanPayload.indexOf('|');
    int secondPipe = cleanPayload.indexOf('|', firstPipe + 1);
    int thirdPipe  = cleanPayload.indexOf('|', secondPipe + 1);

    if (firstPipe != -1 && secondPipe != -1 && thirdPipe != -1) {
      int rxId       = cleanPayload.substring(0, firstPipe).toInt();
      String rxMsg   = cleanPayload.substring(firstPipe + 1, secondPipe);
      friendLat      = cleanPayload.substring(secondPipe + 1, thirdPipe).toDouble();
      friendLon      = cleanPayload.substring(thirdPipe + 1).toDouble();

      // -- CASE A: INCOMING ACKNOWLEDGEMENT FOR OUR PENDING PACKET --
      if (rxMsg == "ACK") {
        if (rxId == pendingAckId) {
          Serial.println("<<< [ARQ SUCCESS] Received ACK for packet #" + String(rxId));
          pendingAckId = -1; // Clear pending state, transmission complete!
        }
        return; 
      }

      // -- CASE B: INCOMING TACTICAL COMMAND FROM FRIEND --
      // Format immediately as "msg|lat|lon" exactly as the MIT App expects it!
      latestMessage = rxMsg + "|" + String(friendLat, 6) + "|" + String(friendLon, 6);
      
      // Print the exact string that will be sent to the App:
      Serial.println("\n<<< [INTEL RX #" + String(rxId) + "] App Payload Buffered: " + latestMessage);

      // SWAPPED BUZZER: Quick double-beep pattern
      digitalWrite(BUZZER_PIN, HIGH); delay(100);
      digitalWrite(BUZZER_PIN, LOW); delay(50);
      digitalWrite(BUZZER_PIN, HIGH); delay(100);
      digitalWrite(BUZZER_PIN, LOW);
      
      // SEND IMMEDIATE ACKNOWLEDGEMENT BACK TO NODE B
      String ackPacket = "VSQUAD:" + String(rxId) + "|ACK|" + String(currentLat, 6) + "|" + String(currentLon, 6);
      executeCSMATransmission(ackPacket);
    }
  }
}

void handleAlarmBuzzer() {
  if (!isAlarmActive) return;
  unsigned long currentMillis = millis();
  int waitTime = isBuzzerOn ? 50 : 100; 

  if (currentMillis - previousBuzzerTime >= waitTime) {
    previousBuzzerTime = currentMillis; 
    if (isBuzzerOn) {
      digitalWrite(BUZZER_PIN, LOW); 
      isBuzzerOn = false;
      beepCount++; 
      if (beepCount >= 5) isAlarmActive = false; 
    } else {
      digitalWrite(BUZZER_PIN, HIGH); 
      isBuzzerOn = true;
    }
  }
}

int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;
  p = finger.fingerFastSearch();
  if (p != FINGERPRINT_OK) return -2; 
  return finger.fingerID; 
}
// ================================================================
// 4. STANDARDIZED API ROUTES (Fixed /receive logic)
// ================================================================
void setupAPIRoutes() {
  server.on("/send", HTTP_GET, []() {
    String msg = server.arg("msg");
    
    if (server.hasArg("lat") && server.hasArg("lon")) {
      currentLat = server.arg("lat").toDouble();
      currentLon = server.arg("lon").toDouble();
    }
    
    String checkMsg = msg;
    checkMsg.toUpperCase(); 
    if (checkMsg.indexOf("UTE") >= 0)      { currentLat = 10.850000; currentLon = 106.770000; } 
    else if (checkMsg.indexOf("VAA") >= 0) { currentLat = 10.810000; currentLon = 106.680000; }
    else if (checkMsg.indexOf("UTF") >= 0) { currentLat = 10.870000; currentLon = 106.800000; }

    sendRadioPacket(msg);
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Transmission Sent");
  });

  server.on("/receive", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (server.hasArg("lat") && server.hasArg("lon")) {
      currentLat = server.arg("lat").toDouble();
      currentLon = server.arg("lon").toDouble();
    }
    
    if (latestMessage.length() > 0) {
      Serial.println(">>> [HTTP /receive] Sending to App: " + latestMessage);
      server.send(200, "text/plain", latestMessage);
      latestMessage = ""; // Clear buffer after sending
    } else {
      // FIX: Never send 0.000000! Always send the true last-known coordinates!
      String pingPayload = "PING:NODE_A|" + String(friendLat, 6) + "|" + String(friendLon, 6);
      server.send(200, "text/plain", pingPayload);
    }    
  });
}
