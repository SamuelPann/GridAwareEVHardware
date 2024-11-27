#include <Arduino.h>
#include "BluetoothSerial.h"  // Include the Bluetooth Serial library

BluetoothSerial SerialBT;      // Create an instance of the BluetoothSerial class
String mac_address;

void setup() {
    Serial.begin(115200);      // Start the hardware serial communication with the PC
    SerialBT.begin("ESP32Test"); // Start the Bluetooth Serial with the name "ESP32Test"
    Serial.println("Bluetooth device is ready to pair");

    uint64_t get_mac_address = ESP.getEfuseMac();
    mac_address = String((uint16_t)(get_mac_address >> 32), HEX) + String((uint32_t)get_mac_address, HEX);
}

void loop() {
    if (SerialBT.hasClient()) {  // Check if there is an active Bluetooth connection
        SerialBT.println(mac_address);  // Transmit the number via Bluetooth
        Serial.println(mac_address + " sent");  // Optional: Print to Serial Monitor for debugging
        delay(3000);  // Wait for 3 seconds
    }
}