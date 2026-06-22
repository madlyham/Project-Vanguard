#include <SoftwareSerial.h>

// ESP RX là D1 (GPIO 5) nối với LoRa TXD
// ESP TX là D2 (GPIO 4) nối với LoRa RXD
SoftwareSerial loraSerial(5, 4); 

void setup() {
  Serial.begin(115200);
  // Khi M0 và M1 ở mức CAO (3V3), module LoRa luôn giao tiếp ở tốc độ 9600 8N1
  loraSerial.begin(9600); 
  
  delay(2000); // Đợi mạch ổn định
  Serial.println("\n--- BẮT ĐẦU RESET LORA E32 ---");

  // Tập lệnh Hex: C4 C4 C4 dùng để khôi phục cài đặt gốc cho module hãng Ebyte
  uint8_t resetCmd[] = {0xC4, 0xC4, 0xC4};
  loraSerial.write(resetCmd, 3);
  
  delay(500);
  Serial.println("Đã bắn lệnh Reset! Chờ phản hồi từ LoRa...");
  
  // Đọc tín hiệu phản hồi từ mạch
  if (loraSerial.available()) {
    Serial.print("LoRa phản hồi (Hex): ");
    while (loraSerial.available()) {
      Serial.print(loraSerial.read(), HEX);
      Serial.print(" ");
    }
    Serial.println("\n=> THÀNH CÔNG! LoRa đã khôi phục về cấu hình gốc.");
  } else {
    Serial.println("\n=> LỖI: KHÔNG THẤY PHẢN HỒI.");
    Serial.println("Hãy kiểm tra lại xem M0, M1 đã cắm chặt vào 3V3 chưa, và dây RX/TX có bị lỏng không?");
  }
}

void loop() {
  // Reset xong rồi thì nằm im không làm gì cả
}
