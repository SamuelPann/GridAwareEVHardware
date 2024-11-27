// something very simple to get stuff on lcd screen... works!

// GND (LCD) → Connect to the GND row (e.g., row 16).
// VCC (LCD) → Connect to the 3.3V row (e.g., row 17).
// SCL (LCD) → Connect to the row corresponding to GPIO 22 (SCL).
// SDA (LCD) → Connect to the row corresponding to GPIO 21 (SDA).

#include <Arduino.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <Wire.h>

// unique MAC address and api_key
String mac_address;
String test_string = "Testing the OLED!";
String api_key = "secret";

// Initialize the display with the I2C connection (SCL -> GPIO 22, SDA -> GPIO 21)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ 22, /* data=*/ 21); 

void setup() {
  Serial.begin(115200);

  // get device MAC address
  uint64_t get_mac_address = ESP.getEfuseMac();
  mac_address = String((uint16_t)(get_mac_address >> 32), HEX) + String((uint32_t)get_mac_address, HEX);
  Serial.println("(SETUP) MAC Address of device: " + mac_address);

  // Start communication with the display
  u8g2.begin();
}

void loop() {
  // Clear the display
  u8g2.clearBuffer();

  // other options:
  // u8g2_font_6x10_tr (slightly larger)
  // u8g2_font_4x6_tr (even smaller, very compact)

  // 5 pixels wide, 8 pixels high
  u8g2.setFont(u8g2_font_5x8_tr);

  u8g2.drawStr(0, 10, mac_address.c_str());
  u8g2.drawStr(0, 20, test_string.c_str());

  // Send the buffer to the display
  u8g2.sendBuffer();
  
  delay(5000);
}