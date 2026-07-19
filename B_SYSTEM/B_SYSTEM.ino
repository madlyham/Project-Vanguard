#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal.h>
#include <Keypad.h>

// --- TACTICAL NETWORK SETTINGS ---
const char* ssid = "VSquadB"; 
const char* password = "";    

WebServer server(80);
String latestFieldIntel = ""; 

// --- HARDWARE PINOUTS (ESP32 WROOM) ---
#define BUZZER_PIN 33
#define LORA_RX 16 // Hardware Serial 2
#define LORA_TX 17 
const String SECRET_CODE = "007"; // <--- THE OVERRIDE CODE

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
byte colPins[COLS] = {18, 5, 4, 2}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

String currentInput = ""; // Stores keypad presses

void setup() {
  Serial.begin(115200);      
  loraSerial.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);    
  
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Setup LCD
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("HQ NODE ONLINE");
  lcd.setCursor(0, 1);
  lcd.print("Radio: Listening");

  // Boot the HQ Wi-Fi Radio
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  Serial.print("\nHQ AP Active! IP: ");
  Serial.println(WiFi.softAPIP());

  // --- API ROUTE: APP FETCHING FIELD INTEL ---
  server.on("/receive", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (latestFieldIntel.length() > 0) {
      server.send(200, "text/plain", latestFieldIntel);
      latestFieldIntel = ""; 
    } else {
      server.send(204, "text/plain", "No new intel.");
    }
  });

  // --- API ROUTE: MIT APP UNLOCK / OVERRIDE ---
  server.on("/unlock", HTTP_GET, []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    if (server.hasArg("code")) {
      String passCode = server.arg("code");
      if (passCode == SECRET_CODE) { 
        triggerSystemOverride("APP OVERRIDE");
        server.send(200, "text/plain", "System Override Successful");
      } else {
        server.send(403, "text/plain", "ACCESS DENIED - Wrong Code");
      }
    } else {
      server.send(400, "text/plain", "Error: No code provided");
    }
  });

  server.begin(); 
} 

void loop() {
  server.handleClient(); // Keep App Inventor link alive

  // ==========================================
  // PHYSICAL KEYPAD LOGIC
  // ==========================================
  char key = keypad.getKey();
  
  if (key) {
    // Tactile Audio Click
    digitalWrite(BUZZER_PIN, HIGH); delay(30); digitalWrite(BUZZER_PIN, LOW);
    
    if (key == '*') {
      // Clear input
      currentInput = "";
      resetLCD();
    } 
    else if (key == '#') {
      // Submit code
      if (currentInput == SECRET_CODE) {
        triggerSystemOverride("MANUAL OVERRIDE");
      } else {
        // Access Denied Beep
        digitalWrite(BUZZER_PIN, HIGH); delay(500); digitalWrite(BUZZER_PIN, LOW);
        lcd.clear();
        lcd.print("ACCESS DENIED");
        delay(1500);
        resetLCD();
      }
      currentInput = ""; // Clear after submit
    } 
    else {
      // Add digit to passcode string and show on LCD
      if (currentInput.length() < 16) {
        currentInput += key;
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("ENTER CODE:");
        lcd.setCursor(0, 1);
        // OPSEC: Print asterisks instead of the actual numbers
        for (int i=0; i<currentInput.length(); i++) { lcd.print("*"); }
      }
    }
  }

  // ==========================================
  // LORA RADIO LOGIC
  // ==========================================
  if (loraSerial.available()) {
    String incomingPayload = loraSerial.readStringUntil('\n'); 
    incomingPayload.trim(); 
    
    if (incomingPayload.length() > 0) {
      latestFieldIntel = incomingPayload; 
      
      // Tactical Audio Alert
      digitalWrite(BUZZER_PIN, HIGH); delay(100);
      digitalWrite(BUZZER_PIN, LOW); delay(50);
      digitalWrite(BUZZER_PIN, HIGH); delay(100);
      digitalWrite(BUZZER_PIN, LOW);

      int firstPipe = incomingPayload.indexOf('|');
      String tacticalMsg = incomingPayload.substring(0, firstPipe);
      
      lcd.clear();
      lcd.print("SOS RECEIVED");
      lcd.setCursor(0, 1);
      lcd.print(tacticalMsg.substring(0, 16)); 
      
      delay(3000); // Hold message on screen
      resetLCD();
    }
  }
}

// ==========================================
// HELPER FUNCTIONS
// ==========================================
void triggerSystemOverride(String method) {
  // LCD Feedback
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("CODE ACCEPTED");
  lcd.setCursor(0,1);
  lcd.print(method);

  // Success Audio Confirmation
  digitalWrite(BUZZER_PIN, HIGH);
  delay(1000); // Hold for 1 solid second
  digitalWrite(BUZZER_PIN, LOW);
  
  delay(2000); // Hold the success screen before resetting
  resetLCD();
}

void resetLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("HQ NODE ONLINE");
  lcd.setCursor(0, 1);
  lcd.print("Radio: Listening");
}
