#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>

// --- TACTICAL NETWORK SETTINGS ---
const char* ssid = "VSquadB"; 
const char* password = ""; // Leave blank for instant field connection

ESP8266WebServer server(80);
String latestMessage = ""; 

// --- HARDWARE CONFIGURATION ---
#define ONBOARD_LED 2 // Stealth blue LED on D4 (GPIO 2)
#define AUX_PIN 14    // D5 - LoRa Busy pin

// LoRa E32 Serial Connection (RX, TX)
// ESP RX is D7 (GPIO 13) connected to LoRa TXD
// ESP TX is D6 (GPIO 12) connected to LoRa RXD
SoftwareSerial loraSerial(13, 12); 

void setup() {
  Serial.begin(115200);      // Connection to your Laptop
  loraSerial.begin(9600);    // Connection to LoRa E32 (Default baud is 9600)
  
  pinMode(ONBOARD_LED, OUTPUT);
  digitalWrite(ONBOARD_LED, HIGH); // Turn LED OFF
  
  pinMode(AUX_PIN, INPUT);

  Serial.println("\n--- VANGUARD SYSTEM BOOTING ---");

  // 1. BOOT THE TACTICAL WIFI
  WiFi.softAP(ssid, password);
  Serial.print("Tactical AP Active! IP: ");
  Serial.println(WiFi.softAPIP());

  // 2. API ROUTE: SENDING DATA (From App -> Radio)
  server.on("/send", HTTP_GET, []() {
    String msg = server.arg("msg");
    String lat = server.arg("lat");
    String lon = server.arg("lon");
    
    String payload = msg + "|" + lat + "|" + lon + "\n"; 
    Serial.println(">>> BROADCASTING: " + payload);
    
    while(digitalRead(AUX_PIN) == LOW) { delay(10); }
    
    loraSerial.print(payload);
    
    // --- THE NEW VIP PASS ---
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "text/plain", "Transmission Sent");
  });

  // 3. API ROUTE: RECEIVING DATA (From Radio -> App)
  server.on("/receive", HTTP_GET, []() {
    // --- THE NEW VIP PASS ---
    server.sendHeader("Access-Control-Allow-Origin", "*");
    
    if (latestMessage.length() > 0) {
      server.send(200, "text/plain", latestMessage);
      
      digitalWrite(ONBOARD_LED, LOW); 
      delay(50);
      digitalWrite(ONBOARD_LED, HIGH); 
      
      latestMessage = ""; 
    } else {
      server.send(204, "text/plain", "No new intel.");
    }
  });
}

void loop() {
  // 1. Keep the WiFi server alive for the App
  server.handleClient();

  // 2. Scan the sky for incoming LoRa radio waves
  if (loraSerial.available()) {
    // Read the incoming radio data until the newline character
    String incomingPayload = loraSerial.readStringUntil('\n'); 
    
    incomingPayload.trim(); // Clean up invisible spacing characters
    
    if (incomingPayload.length() > 0) {
      Serial.println("<<< INCOMING INTERCEPT: " + incomingPayload);
      latestMessage = incomingPayload; 
    }
  }
}
