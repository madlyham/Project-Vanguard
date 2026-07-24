// ================================================================
// HQ NODE (NODE B) — PRODUCTION MASTER BUILD
// ================================================================
// WIRING:
//  - E32 LoRa module: GND + M0 + M1 tied together to ESP32 GND
//  - E32 VCC -> ESP32 VIN (5V)
//  - E32 AUX -> ESP32 GPIO 34 (Input Only)
//  - LCD Enable moved to GPIO32
// ================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal.h>
#include <Keypad.h>
#include <math.h>

// --- NETWORK & SYSTEM STATE ---
const char* ssid = "VSquadB"; 
const char* password = "";     
WebServer server(80);

String latestMessage = ""; 
String latestFieldIntel = "";
String currentInput = ""; 

// --- TACTICAL ARQ / CSMA PROTOCOL VARIABLES ---
uint8_t txSequenceNumber = 0;        // Rolling packet ID (0-255)
int pendingAckId = -1;               // -1 means no packet is awaiting ACK
String pendingPayload = "";          // Stores payload for retransmission
unsigned long lastTxTime = 0;        // Timestamp of last transmission attempt
uint8_t retryCount = 0;              // Current retry attempt
const uint8_t MAX_RETRIES = 3;       // Max retransmissions before giving up
const unsigned long ACK_TIMEOUT = 2000; // 2 seconds to wait for an ACK

// --- THE ONLY 4 LOCATION VARIABLES YOU NEED (DOUBLE PRECISION) ---
double currentLat = 10.841500; // This Node (Node B / HQ)
double currentLon = 106.809900;
double friendLat  = 0.000000;  // Target Node (Node A / Vanguard)
double friendLon  = 0.000000;

// --- HARDWARE PINOUTS (ESP32 WROOM) ---
#define BUZZER_PIN 33
#define LORA_RX 16 // Hardware Serial 2
#define LORA_TX 17
#define AUX_PIN 34 // E32 Hardware Busy/Idle Pin (Input Only)
const String SECRET_CODE = "007"; 

unsigned long previousBuzzerTime = 0; 
int beepCount = 0;                    
bool isAlarmActive = false;           
bool isBuzzerOn = false; 

// 1. LCD Screen
LiquidCrystal lcd(13, 12, 14, 27, 26, 25); 

// 2. Radio Serial Port
HardwareSerial loraSerial(2); 

// 3. Matrix Keypad Setup
const byte ROWS = 4; 
const byte COLS = 4; 
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {23, 22, 21, 19}; 
byte colPins[COLS] = {18, 5, 4, 32}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// --- FUNCTION PROTOTYPES ---
void resetLCD();
void updateLCD(String line1, String line2);
void triggerSystemOverride(String method);
double calculateDistance(double lat1, double lon1, double lat2, double lon2);
void executeCSMATransmission(String payload);
void sendRadioPacket(String command);

// ================================================================
// 1. SETUP & BOOT
// ================================================================
void setup() {
  delay(2000);
  Serial.begin(115200);     
  randomSeed(analogRead(36)); // Seed RNG using floating noise on VP pin
  loraSerial.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);     
  
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(AUX_PIN, INPUT); // Note: GPIO 34 is input-only!

  lcd.begin(16, 2);
  resetLCD();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  WiFi.setTxPower(WIFI_POWER_15dBm); 
  Serial.print("\n[SYSTEM] HQ AP Active! IP: ");
  Serial.println(WiFi.softAPIP());

  setupAPIRoutes();
  server.begin(); 
} 

// ================================================================
// 2. MAIN LOOP
// ================================================================
void loop() {
  server.handleClient(); 
  handleKeypad();
  handleIncomingRadio();
  handleARQLoop();       // ARQ Retransmission engine
}

// ================================================================
// 3. CORE FUNCTIONS
// ================================================================
void handleKeypad() {
  char key = keypad.getKey();
  if (!key) return;

  digitalWrite(BUZZER_PIN, HIGH); delay(30); digitalWrite(BUZZER_PIN, LOW);
  
  if (key == '*') {
    currentInput = "";
    resetLCD();
  } 
  else if (key == '#') {
    if (currentInput == SECRET_CODE) {
      triggerSystemOverride("MANUAL OVERRIDE");
    } else {
      digitalWrite(BUZZER_PIN, HIGH); delay(500); digitalWrite(BUZZER_PIN, LOW);
      lcd.clear();
      lcd.print("ACCESS DENIED");
      delay(1500);
      resetLCD();
    }
    currentInput = ""; 
  } 
  else {
    if (currentInput.length() < 16) {
      currentInput += key;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("ENTER CODE:");
      lcd.setCursor(0, 1);
      for (int i = 0; i < currentInput.length(); i++) { lcd.print("*"); }
    }
  }
}

// ----------------------------------------------------------------
// NON-BLOCKING CSMA/CA TRANSMISSION ENGINE
// ----------------------------------------------------------------
void executeCSMATransmission(String payload) {
  // Step 1: Carrier Sense (Wait for hardware radio to finish current task)
  unsigned long startSense = millis();
  while (digitalRead(AUX_PIN) == LOW && (millis() - startSense < 1000)) {
    delay(5); 
  }

  // Step 2: Collision Avoidance (Random backoff between 50ms and 200ms)
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
// QUEUE A NEW PACKET WITH ARQ
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
// ARQ RETRY LOOP
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
// UPGRADED INCOMING RECEIVER WITH SEQUENCE MATCHING
// ----------------------------------------------------------------
void handleIncomingRadio() {
  if (loraSerial.available()) {
    String incomingPayload = loraSerial.readStringUntil('\n');
    incomingPayload.trim();

    if (incomingPayload.length() > 0 && incomingPayload.startsWith("VSQUAD:")) {
      String cleanPayload = incomingPayload.substring(7);
      
      int firstPipe  = cleanPayload.indexOf('|');
      int secondPipe = cleanPayload.indexOf('|', firstPipe + 1);
      int thirdPipe  = cleanPayload.indexOf('|', secondPipe + 1);

      if (firstPipe != -1 && secondPipe != -1 && thirdPipe != -1) {
        int rxId       = cleanPayload.substring(0, firstPipe).toInt();
        String rxMsg   = cleanPayload.substring(firstPipe + 1, secondPipe);
        double rxLat   = cleanPayload.substring(secondPipe + 1, thirdPipe).toDouble();
        double rxLon   = cleanPayload.substring(thirdPipe + 1).toDouble();

        // -- CASE A: INCOMING ACKNOWLEDGEMENT FOR OUR PENDING PACKET --
        if (rxMsg == "ACK") {
          if (rxId == pendingAckId) {
            Serial.println("<<< [ARQ SUCCESS] Received ACK for packet #" + String(rxId));
            pendingAckId = -1; // Clear pending state, transmission complete!
          }
          return; 
        }

        // -- CASE B: INCOMING TACTICAL COMMAND FROM NODE A --
// For Node B's handleIncomingRadio():
        Serial.println("\n<<< [INTEL RX #" + String(rxId) + "] From Node A: " + rxMsg + "|" + String(friendLat, 6) + "|" + String(friendLon, 6));        latestFieldIntel = rxMsg;
        friendLat = rxLat;
        friendLon = rxLon;

        double distanceMeters = calculateDistance(currentLat, currentLon, rxLat, rxLon);

        // SWAPPED BUZZER: Non-blocking alarm state trigger (formerly on Node A)
        isAlarmActive = true; 
        beepCount = 0;        
        isBuzzerOn = false;   

        lcd.clear();
        lcd.print(rxMsg.substring(0, 16));
        lcd.setCursor(0, 1);
        if (distanceMeters > 9999) {
          lcd.print("DIST: "); lcd.print(distanceMeters / 1000.0, 1); lcd.print("km   ");
        } else {
          lcd.print("DIST: "); lcd.print((int)distanceMeters); lcd.print("m    ");
        }

        // FIRE BACK AN ACK PACKET MATCHING THE SENDER'S ID VIA CSMA
        String ackPacket = "VSQUAD:" + String(rxId) + "|ACK|" + String(currentLat, 6) + "|" + String(currentLon, 6);
        executeCSMATransmission(ackPacket);
      }
    }
  }
}

// --- LCD & OVERRIDE HELPERS ---
void updateLCD(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line1);
  lcd.setCursor(0, 1); lcd.print(line2);
}

void triggerSystemOverride(String method) {
  updateLCD("CODE ACCEPTED", method);
  digitalWrite(BUZZER_PIN, HIGH); delay(1000); digitalWrite(BUZZER_PIN, LOW);
  delay(1500); 
  resetLCD();
}

void resetLCD() {
  updateLCD("HQ NODE ONLINE", "Radio: Listening");
}

// --- DISTANCE CALCULATOR ---
double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
  double R = 6371000.0; // Earth radius in meters
  double phi1 = lat1 * PI / 180.0;
  double phi2 = lat2 * PI / 180.0; 
  double deltaPhi = (lat2 - lat1) * PI / 180.0;
  double deltaLambda = (lon2 - lon1) * PI / 180.0;

  double a = sin(deltaPhi / 2.0) * sin(deltaPhi / 2.0) +
             cos(phi1) * cos(phi2) *
             sin(deltaLambda / 2.0) * sin(deltaLambda / 2.0);
  double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
  return R * c; 
}

// ================================================================
// 4. STANDARDIZED API ROUTES
// ================================================================
void setupAPIRoutes() {
  server.on("/receive", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (server.hasArg("lat") && server.hasArg("lon")) {
      currentLat = server.arg("lat").toDouble();
      currentLon = server.arg("lon").toDouble();
    }
    
    // 1. Grab the latest radio command, or default to PING if quiet
    String msgText = latestFieldIntel.length() > 0 ? latestFieldIntel : "PING:NODE_B";
    
    // 2. Format strictly as "message|lat|lon" using Node A's last known coordinates!
    String appPayload = msgText + "|" + String(friendLat, 6) + "|" + String(friendLon, 6);
    
    server.send(200, "text/plain", appPayload);
    latestFieldIntel = ""; // Clear command after reading so it doesn't spam the app
  });

  server.on("/unlock", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (server.hasArg("code")) {
      if (server.arg("code") == SECRET_CODE) { 
        triggerSystemOverride("APP OVERRIDE");
        server.send(200, "text/plain", "System Override Successful");
      } else {
        server.send(403, "text/plain", "ACCESS DENIED - Wrong Code");
      }
    } else {
      server.send(400, "text/plain", "Error: No code provided");
    }
  });

  server.on("/status", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*"); 
    server.send(200, "text/plain", "OK");
  });

  server.on("/send", HTTP_GET, []() {
    String msg = server.arg("msg");
    if (server.hasArg("lat") && server.hasArg("lon")) {
      currentLat = server.arg("lat").toDouble();
      currentLon = server.arg("lon").toDouble();
    }

    // Let your ARQ engine handle ID generation, headers, and retransmissions!
    sendRadioPacket(msg);

    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "TraFnsmission Sent");
  });
}
