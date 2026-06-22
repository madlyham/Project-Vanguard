#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LiquidCrystal.h>
#include <Adafruit_Fingerprint.h>
#include <SoftwareSerial.h>
#include <Servo.h>

// ==========================================
// HARDWARE SETUP
// ==========================================
LiquidCrystal lcd(0, 2, 14, 12, 13, 16);
SoftwareSerial mySerial(5, 4); 
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
Servo myServo;

const int servoPin = 15; // D8
ESP8266WebServer server(80);
bool isNetworkActive = false;

// ==========================================
// SETUP ROUTINE
// ==========================================
void setup() {
  Serial.begin(115200);
  
  // 1. Boot LCD
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SYSTEM LOCKED");
  lcd.setCursor(0, 1);
  lcd.print("Booting Bio...");

  // 2. Boot Servo (Move to 0, then detach to prevent jitter)
  myServo.attach(servoPin);
  myServo.write(0);
  delay(500); 
  myServo.detach(); 

  // 3. Keep Wi-Fi completely OFF for security
  WiFi.mode(WIFI_OFF);

  // 4. Boot Fingerprint Scanner
  finger.begin(57600);
  if (finger.verifyPassword()) {
    lcd.setCursor(0, 1);
    lcd.print("Awaiting Bio... ");
  } else {
    lcd.clear();
    lcd.print("Sensor Error!");
    while (1) { delay(1); } // Halt system
  }
}

// ==========================================
// MAIN LOOP
// ==========================================
void loop() {
  if (!isNetworkActive) {
    // Phase 1: Wait for a valid fingerprint
    checkFingerprint();
    delay(50);
  } else {
    // Phase 2: Network is live, listen to the HTML webpage
    server.handleClient();
  }
}

// ==========================================
// FINGERPRINT LOGIC
// ==========================================
void checkFingerprint() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return; 

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return;

  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    // MATCH FOUND! (ID #1)
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ACCESS GRANTED");
    lcd.setCursor(0, 1);
    lcd.print("Booting Server..");
    
    // Smooth Sweep Open (0 to 180)
    myServo.attach(servoPin);
    for (int pos = 0; pos <= 180; pos += 2) { 
      myServo.write(pos);
      delay(15); 
    }
    
    delay(2000); // Hold it open for 2 seconds
    
    // Smooth Sweep Closed (180 to 0)
    for (int pos = 180; pos >= 0; pos -= 2) { 
      myServo.write(pos);
      delay(15); 
    }
    myServo.detach(); // Sleep
    
    bootNetwork(); // Start the Wi-Fi
  } else {
    // WRONG FINGER
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ACCESS DENIED");
    delay(2000);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("SYSTEM LOCKED");
    lcd.setCursor(0, 1);
    lcd.print("Awaiting Bio... ");
  }
}

// ==========================================
// WI-FI & WEB SERVER LOGIC
// ==========================================
void bootNetwork() {
  // 1. Turn on the Wi-Fi Access Point
  WiFi.mode(WIFI_AP);
  WiFi.softAP("VSquadB"); 
  isNetworkActive = true;

  // 2. Setup the HTML /status endpoint (Polling)
  server.on("/status", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "ONLINE");
  });

  // 3. Setup the HTML /send endpoint (Action)
  server.on("/send", []() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    
    if (server.hasArg("msg")) {
      String msg = server.arg("msg");
      
      // Output 1: Print to LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("MSG RECEIVED:");
      lcd.setCursor(0, 1);
      lcd.print(msg.substring(0, 16)); 
      
      // Output 2: Smooth Sweep Open (0 to 180)
      myServo.attach(servoPin);
      for (int pos = 0; pos <= 180; pos += 2) { 
        myServo.write(pos);
        delay(15); 
      }
      
      delay(2000); // Hold it open
      
      // Smooth Sweep Closed (180 to 0)
      for (int pos = 180; pos >= 0; pos -= 2) { 
        myServo.write(pos);
        delay(15); 
      }
      myServo.detach(); // Sleep
      
      server.send(200, "text/plain", "Command Executed");
    } else {
      server.send(400, "text/plain", "Error: No Message");
    }
  });

  // 4. Start listening
  server.begin();

  // Update LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SERVER ONLINE");
  lcd.setCursor(0, 1);
  lcd.print("IP:192.168.4.1");
}
