// code for esp32 intended for WPA2 networks

// bluetooth, simulating algorithm, tiny display, sending data to server on WPA2 network,
// battery_percentage, AES encryption

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <stdlib.h>
#include <esp_wpa2.h>
#include <U8g2lib.h>
#include <Wire.h>
#include "mbedtls/aes.h"
#include "mbedtls/base64.h"

// bluetooth UUID (insert your custom service and characteristic as mobile)
#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "87654321-4321-4321-4321-cba987654321"

// encryption bluetooth (insert your custom key and iv as mobile)
unsigned char key[17] = "0000000000000000";  // 16-byte key
unsigned char iv[17] = "abcdefghijklmnop";   // 16-byte IV

// initialize display with I2C connection
// GND (LCD) → Connect to the GND row
// VCC (LCD) → Connect to the 3.3V row
// SCL (LCD) → Connect to the row corresponding to GPIO 22 (SCL)
// SDA (LCD) → Connect to the row corresponding to GPIO 21 (SDA)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ 22, /* data=*/ 21);
String line_one = "";
String line_two = "";
String line_three = "";
String line_four = "";
String line_five = "";
String line_six = "";

// wpa2 specific constant
String ssid = "UCF_WPA2";

// stored on non-volatile memory
Preferences preferences;
String stored_wifi = "";
String stored_password = "";
String stored_jwt = "";
String stored_exist_state = "";
float battery_percentage = 0.00;

// bluetooth connection
BLEServer* pServer = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool deviceConnected = false;

// communicate to lightsail server endpoints
HTTPClient http;
const char* lightsail_store = "https://gridawarecharging.com/api/store_controller_reading";
const char* lightsail_register = "https://gridawarecharging.com/api/register_device";
const char* lightsail_check = "https://gridawarecharging.com/api/check_exists";
bool wifi_working = false;
bool bleConnectedWait = false;

// NTP server for time synchronization
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -18000; // EST is UTC-5 hours
const int   daylightOffset_sec = 3600; // 1 hour DST

// unique MAC address and api_key (insert your custom API key from server)
String mac_address;
String api_key = "abcdefg1234567";

// simulation constants
const float nominal_freq = 60.00;
const float freq_lower_bound = 59.95;
const float freq_upper_bound = 60.05;
const float spike_low_min = 58.50;
const float spike_low_max = 59.00;
const float spike_high_min = 61.00;
const float spike_high_max = 61.50;
const int how_often = 333; // calculations 3 times per second = 333ms (another ex. 0 times per second = 33ms)
const int spike_time = 10000; // spike every 20 seconds
const int recovery_time = 2000; // recover from spike within 2 seconds
const int send_data = 333; // send data to server 3 times per second
const float power = 7200.00;  // power usage when charging (7.2 kW)
const int battery_time = 20000; // show battery percentage every 20 seconds

// simulation trackers
unsigned long last_update = 0;
unsigned long last_spike = 0;
unsigned long last_data = 0;
unsigned long last_battery = 0;

float current_freq = nominal_freq;
float target_freq = nominal_freq;
float ratio_SC_on = 0.50; // assume 0.50 is default for this scenario
float voltage = 0.00;
float current = 0.00;

bool in_recovery = false;
bool spike_up = false;
bool is_charging = true;
bool show_battery = false;

//////////

// Base64-decoding function to convert encrypted string to byte array
int base64_decode(unsigned char* output, size_t* output_len, const char* input) {
  return mbedtls_base64_decode(output, *output_len, output_len, (const unsigned char*)input, strlen(input));
}

// manually apply PKCS7 padding
size_t remove_padding(unsigned char* buffer, size_t length) {
  size_t padding_length = buffer[length - 1];
  return length - padding_length;
}

// connects to wi-fi wpa2 network
void connectToWiFi() {
  Serial.println("(WIFI WPA2) Connecting to Wi-Fi...");

  WiFi.disconnect(true);
  delay(1000);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // WPA2 Enterprise connection requires specific EAP credentials setup
  esp_err_t eap_err;

  eap_err = esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)stored_wifi.c_str(), stored_wifi.length());
  if (eap_err != ESP_OK) {
    Serial.println("(WIFI WPA2) failed to set identity");
    return;
  }

  eap_err = esp_wifi_sta_wpa2_ent_set_username((uint8_t *)stored_wifi.c_str(), stored_wifi.length());
  if (eap_err != ESP_OK) {
    Serial.println("(WIFI WPA2) failed to set username");
    return;
  }

  eap_err = esp_wifi_sta_wpa2_ent_set_password((uint8_t *)stored_password.c_str(), stored_password.length());
  if (eap_err != ESP_OK) {
    Serial.println("(WIFI WPA2) failed to set password");
    return;
  }

  eap_err = esp_wifi_sta_wpa2_ent_enable();
  if (eap_err != ESP_OK) {
    Serial.println("(WIFI WPA2) failed to enable WPA2 Enterprise");
    return;
  }

  WiFi.begin(ssid);

  unsigned long startAttemptTime = millis();

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("(WIFI WPA2) failed to connect to Wi-Fi");
  } else {
    Serial.println("(WIFI WPA2) connected to Wi-Fi");
    Serial.print("(WIFI WPA2) IP address: ");
    Serial.println(WiFi.localIP());
  }
}

// gets current timstamp, gives precision to milliseconds
String getISO8601Time() {
  struct timeval tv;
  gettimeofday(&tv, NULL); // get current time with microseconds
  struct tm* timeinfo = localtime(&tv.tv_sec); // convert to tm structure
  
  char timeStringBuff[40];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", timeinfo);

  // append microseconds to the time string (convert microseconds to milliseconds)
  char msString[8]; // 6 digits for microseconds, 1 dot, 1 for 'Z'
  snprintf(msString, sizeof(msString), ".%03lu", tv.tv_usec / 1000); // convert microseconds to milliseconds

  return String(timeStringBuff) + String(msString); // combine seconds with milliseconds
}

// Function to update voltage and current based on charging state
void updateVoltageAndCurrent() {
  if (is_charging) {
    // Generate random voltage between 239.98V and 240.02V
    voltage = 239.98 + (float(random(5)) * 0.01); // random(5) gives values 0 to 4, multiply by 0.01 to get 0.00 to 0.04
    
    // calculate the current based on power (P = V * I => I = P / V)
    current = power / voltage;
  } else {
    voltage = 0.00; // no voltage when not charging
    current = 0.00; // no current when not charging
  }
}

// function to update the current battery charge value
void updateCharge() {
  // reset battery to 0% once it reaches max capacity
  if (battery_percentage >= 100.00) {
    battery_percentage = 0.00;
  }

  battery_percentage += 0.1;

  // store battery state in NVS
  preferences.begin("my-app", false);
  preferences.putFloat("battery", battery_percentage);
  preferences.end();
}

// Function to display the battery status on the screen
void displayBattery() {
  u8g2.clearBuffer();

  // Draw battery outline
  int batteryWidth = 100;
  int batteryHeight = 30;
  int batteryX = 14;  // Centered
  int batteryY = 20;  // Middle of the screen
  int capWidth = 6;   // Battery cap width

  // draw outer rectangle
  u8g2.drawFrame(batteryX, batteryY, batteryWidth, batteryHeight);
  
  // draw the cap on the battery
  u8g2.drawBox(batteryX + batteryWidth, batteryY + batteryHeight / 4, capWidth, batteryHeight / 2);

  // calculate width of the filled area
  int fillWidth = (batteryWidth - 2) * (battery_percentage / 100.0);

  // fill the area based on fill width
  u8g2.drawBox(batteryX + 1, batteryY + 1, fillWidth, batteryHeight - 2);

  // display the percentage in the top-left corner
  u8g2.setFont(u8g2_font_5x8_tr); // 5 pixels wide, 8 pixels high
  u8g2.setCursor(0, 10);
  u8g2.printf("Battery: %.2f%%", battery_percentage);

  u8g2.sendBuffer();  // Send buffer to display
}


// generate random frequency within normal bounds
float generateNormalFreq() {
  float mean = nominal_freq;
  float stddev = (freq_upper_bound - freq_lower_bound) / 6.0; // rough approximation for distribution
  return mean + stddev * (((float)random(1000) / 500.0) - 1.0); // random value near 60.00Hz
}

// simulate spikes
float generateSpikeFreq() {
  if (random(2) == 0) {
    // spike down
    spike_up = false;
    return random(spike_low_min * 100, spike_low_max * 100) / 100.0;
  } else {
    // spike up
    spike_up = true;
    return random(spike_high_min * 100, spike_high_max * 100) / 100.0;
  }
}

// calculate ratio of SCs that are charging based on current frequency
float calculateScRatio(float freq) {
  if (freq >= freq_lower_bound && freq <= freq_upper_bound) {
    is_charging = true;
    // nominal ratio
    return 0.50;
  } else if (freq < nominal_freq) {
    is_charging = false;
    // scale ratio between 0.10 and 0.45 as freq ranges from 58.50 to 59.94
    float min_freq = 58.50;
    float max_freq = 59.94;
    float min_ratio = 0.10;
    float max_ratio = 0.45;

    // linear interpolation formula: y = y1 + (y2 - y1) * ((x - x1) / (x2 - x1))
    return min_ratio + (max_ratio - min_ratio) * ((freq - min_freq) / (max_freq - min_freq));
  } else {
    is_charging = true;
    // scale ratio between 0.55 and 0.90 as freq ranges from 60.06 to 61.50
    float min_freq = 60.06;
    float max_freq = 61.50;
    float min_ratio = 0.55;
    float max_ratio = 0.90;

    // linear interpolation formula: y = y1 + (y2 - y1) * ((x - x1) / (x2 - x1))
    return min_ratio + (max_ratio - min_ratio) * ((freq - min_freq) / (max_freq - min_freq));
  }
}

// decrypts string recieved over bluetooth
String decryptAES(String encryptedData) {
  const char* encryptedString = encryptedData.c_str();

  // Prepare buffers for Base64-decoded data
  unsigned char encryptedBytes[512];
  size_t encryptedStringLen = sizeof(encryptedBytes);

  // Base64-decode the encrypted JWT string into bytes
  if (base64_decode(encryptedBytes, &encryptedStringLen, encryptedString) != 0) {
    Serial.println("Base64 decoding of encryptedString failed");
    String errorString = "{\"wifi\":\"\",\"password\":\"\"}";
    return(errorString);
  }

  Serial.println("Encrypted String (Base64 decoded):");
  for (size_t i = 0; i < encryptedStringLen; i++) {
    Serial.print(encryptedBytes[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // Prepare the buffer for decryption
  unsigned char decryptedString[512];  // Decrypted JWT output buffer

  // Initialize AES context
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  // Set up AES decryption with the key and IV
  mbedtls_aes_setkey_dec(&aes, key, 128); // 128-bit decryption

  // Decrypt the encrypted JWT
  unsigned char iv_copy[16];
  memcpy(iv_copy, iv, 16);  // Copy the IV for decryption
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, encryptedStringLen, iv_copy, encryptedBytes, decryptedString);

  // Remove PKCS7 padding from the decrypted JWT data
  size_t decryptedStringLen = remove_padding(decryptedString, encryptedStringLen);

  // Null-terminate the decrypted strings
  decryptedString[decryptedStringLen] = '\0';

  // Print the decrypted JWT
  Serial.println("\nDecrypted JWT:");
  Serial.println((char*)decryptedString);

  // Free AES context
  mbedtls_aes_free(&aes);

  return((char*)decryptedString);
}

// keeps track if device is connected or not
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
  };

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
  }
};

// process to recieve and store jwt + wifi credentials
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      String data = String(rxValue.c_str());
      Serial.println("I recieved the following: " + data); 
      data = decryptAES(data);
      Serial.println("I decrypted into: " + data);

      // parse for "jwt" given by mobile
      int jwtIndex = data.indexOf("\"jwt\"");
      if (jwtIndex != -1) {
        int startIndex = data.indexOf(":", jwtIndex) + 2;
        int endIndex = data.indexOf("\"", startIndex);
        stored_jwt = data.substring(startIndex, endIndex);
        Serial.println("(BLUETOOTH) JWT stored: " + stored_jwt);

        // store jwt in NVS
        preferences.begin("my-app", false);
        preferences.putString("jwt", stored_jwt);
        preferences.end();
      }

      // parse for "wifi" and "password" give by mobile
      int wifiIndex = data.indexOf("\"wifi\"");
      int passwordIndex = data.indexOf("\"password\"");
      if (wifiIndex != -1 && passwordIndex != -1) {
        int wifiStartIndex = data.indexOf(":", wifiIndex) + 2;
        int wifiEndIndex = data.indexOf("\"", wifiStartIndex);
        stored_wifi = data.substring(wifiStartIndex, wifiEndIndex);

        // Fix for double backslash issue:
        stored_wifi.replace("\\\\", "\\");  // This will replace \\ with \

        int passwordStartIndex = data.indexOf(":", passwordIndex) + 2;
        int passwordEndIndex = data.indexOf("\"", passwordStartIndex);
        stored_password = data.substring(passwordStartIndex, passwordEndIndex);

        Serial.println("(BLUETOOTH) Wi-Fi: " + stored_wifi + ", Password: " + stored_password);

        // store Wi-Fi and password in NVS
        preferences.begin("my-app", false);
        preferences.putString("wifi", stored_wifi);
        preferences.putString("password", stored_password);
        preferences.end();
    }
    }
  }
};

void startBluetooth() {
  // begin bluetooth advertising (runs in background)
  BLEDevice::init("ESP32 BLE");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );

  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("(BLE) waiting for a client connection to notify...");

  u8g2.clearBuffer();
  line_one = "Initializing: " + mac_address;
  u8g2.drawStr(0, 10, line_one.c_str());
  line_two = "Waiting for bluetooth...";
  u8g2.drawStr(0, 20, line_two.c_str());
  line_three = "v1.1";
  u8g2.drawStr(0, 30, line_three.c_str());
  u8g2.sendBuffer();
}

void checkIfDeviceExists() {
  http.begin(lightsail_check);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");

  String jsonCheckPayload = "{";
  jsonCheckPayload += "\"api_key\":\"" + api_key + "\",";
  jsonCheckPayload += "\"device_mac_address\":\"" + mac_address + "\"";
  jsonCheckPayload += "}";

  int httpResponseCode = http.POST(jsonCheckPayload);

  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("(CHECK) response from server: " + response + ".");

    int existsIndex = response.indexOf("\"exists\":");
    if (existsIndex != -1) {
      // extract the value after the "exists" key
      int valueStart = response.indexOf(':', existsIndex) + 1;
      int valueEnd = response.indexOf('}', valueStart);

      // Trim any extra spaces and store the result
      stored_exist_state = response.substring(valueStart, valueEnd);
      stored_exist_state.trim();
      Serial.print("(CHECK) device exists: " + stored_exist_state + ".");

      // store updated exist state in NVS
      preferences.begin("my-app", false);
      preferences.putString("exists", stored_exist_state);
      preferences.end();

    } else {
      Serial.println("(CHECK) could not find 'exists' in the response.");
    }
  } else {
    Serial.print("(CHECK) error in POST request: " + httpResponseCode);
  }

  http.end();
}

//////////

void setup() {
  // begin connection to serial monitor and display
  Serial.begin(115200);
  u8g2.begin(); //
  u8g2.setFont(u8g2_font_5x8_tr); // 5 pixels wide, 8 pixels high //

  // uncomment this portion to reset fields stored in NVS memory
  // preferences.begin("my-app", false); // Open NVS
  // preferences.clear(); // Clear all data in the namespace
  // preferences.end(); // Close NVS

  // get device MAC address
  uint64_t get_mac_address = ESP.getEfuseMac();
  mac_address = String((uint16_t)(get_mac_address >> 32), HEX) + String((uint32_t)get_mac_address, HEX);
  Serial.println("(SETUP) MAC Address of device: " + mac_address);

  u8g2.clearBuffer();
  line_one = "Initializing: " + mac_address;
  u8g2.drawStr(0, 10, line_one.c_str());
  u8g2.sendBuffer();

  // load stored data from NVS namespace (read-only mode)
  preferences.begin("my-app", true);
  stored_wifi = preferences.getString("wifi", "");
  stored_password = preferences.getString("password", "");
  stored_jwt = preferences.getString("jwt", "");
  stored_exist_state = preferences.getString("exists", "");
  battery_percentage = preferences.getFloat("battery", 0.00);
  preferences.end();

  // print stored data (may not be present)
  Serial.println("(SETUP) initial stored Wi-Fi: " + stored_wifi);
  Serial.println("(SETUP) initial stored Password: " + stored_password);
  Serial.println("(SETUP) initial stored JWT: " + stored_jwt);
  Serial.println("(SETUP) initial stored device exist state: " + stored_exist_state);
  //Serial.println("(SETUP) initial stored battery percentage: " + battery_percentage);

  // wifi and password information already stored in memory, connect to wifi right away
  if (stored_wifi != "" && stored_password != "" && stored_jwt != "") {
    Serial.println("(SETUP) credentials already exist in memory, connecting to wi-fi now");
    
    connectToWiFi();
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("(SETUP) stopping setup due to wi-fi failure");
      return;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("(SETUP) successful wi-fi initialization");

      // but first, check to see if device still exists in database
      checkIfDeviceExists();

      // if the device is not linked to a user, reset all fields to prompt the clean initiation process
      if (stored_exist_state == "false") {
        u8g2.clearBuffer();
        line_one = "Initializing: " + mac_address;
        u8g2.drawStr(0, 10, line_one.c_str());
        line_two = "Device not in database.";
        u8g2.drawStr(0, 20, line_two.c_str());
        line_three = "Resetting device...";
        u8g2.drawStr(0, 30, line_three.c_str());
        u8g2.sendBuffer();

        preferences.begin("my-app", false);
        preferences.putString("jwt", "");
        preferences.putString("wifi", "");
        preferences.putString("password", "");
        preferences.putFloat("battery", 0.00);
        preferences.end();

        stored_wifi = "";
        stored_password = "";
        stored_jwt = "";
      }

      if (stored_exist_state == "true") {
        u8g2.clearBuffer();
        line_one = "Initializing: " + mac_address;
        u8g2.drawStr(0, 10, line_one.c_str());
        line_two = "Device in the database!";
        u8g2.drawStr(0, 20, line_two.c_str());
        line_three = "Connected to Wi-Fi!";
        u8g2.drawStr(0, 30, line_three.c_str());
        u8g2.sendBuffer();

        wifi_working = true;
        http.begin(lightsail_store);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Connection", "keep-alive");
      }
    }
  }

  // initialize random seed generator and counters
  randomSeed(analogRead(0));
  last_spike = millis();
  last_data = millis();
  last_battery = millis();
}

//////////

void loop() {
  // loop in here if wi-fi connection is not set up yet. this is for FIRST TIME setup OR invalid wi-fi credentials
  if (wifi_working == false) {
    delay(1000);
    // first, open the bluetooth connection
    if (bleConnectedWait == false) {
      bleConnectedWait = true;
      Serial.println("(PRE-LOOP) starting BLE connection");
      startBluetooth();
    }

    // for FIRST TIME setup, wait until credentials are recieved
    // if credentials are invalid (wi-fi isn't established), then stays in loop until valid credentials are used
    if (stored_wifi != "" && stored_password != "" && stored_jwt != "") {
      bleConnectedWait = false;
      BLEDevice::deinit(true); // stop and release resources used by BLE
      Serial.println("(PRE-LOOP) BLE stopped");

      Serial.println("(PRE-LOOP) stored Wi-Fi: " + stored_wifi);
      Serial.println("(PRE-LOOP) stored Password: " + stored_password);
      Serial.println("(PRE-LOOP) stored JWT: " + stored_jwt);

      connectToWiFi();
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("(PRE-LOOP) re-entering loop due to Wi-Fi failure");
        return;
      }

      // fresh initialization. establishes wi-fi, initializes storing, and registers device
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("(PRE-LOOP) successful wi-fi initialization");
        wifi_working = true;

        u8g2.clearBuffer();
        line_one = "Initializing: " + mac_address;
        u8g2.drawStr(0, 10, line_one.c_str());
        line_two = "Connected to Wi-Fi!";
        u8g2.drawStr(0, 20, line_two.c_str());
        line_three = "Registering device...";
        u8g2.drawStr(0, 30, line_three.c_str());
        u8g2.sendBuffer();

        http.begin(lightsail_register);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Connection", "close");

        String jsonRegisterPayload = "{";
        jsonRegisterPayload += "\"api_key\":\"" + api_key + "\",";
        jsonRegisterPayload += "\"user_jwt\":\"" + stored_jwt + "\",";
        jsonRegisterPayload += "\"device_mac_address\":\"" + mac_address + "\"";
        jsonRegisterPayload += "}";

        http.POST(jsonRegisterPayload);
        http.end();
        Serial.println("(PRE-LOOP) sent register_device payload: " + jsonRegisterPayload);

        http.begin(lightsail_store);
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Connection", "keep-alive");
      }
      else {
        Serial.println("(PRE-LOOP) WiFi Disconnected");
        http.end();
      }
    }
  }

  // send data if wi-fi connection is properly set up
  if (wifi_working == true) {
    unsigned long current_time = millis();

    // 'how_often' controls how often calculations occur
    if (current_time - last_update >= how_often) {
      last_update = current_time;

      if (!in_recovery) {
        // normal frequency update with noise
        current_freq = generateNormalFreq();
      } else {
        // if a frequency drop, positive steps
        if (spike_up == false) {
          float recovery_step = (nominal_freq - target_freq) / ((float)recovery_time / how_often);
          current_freq += recovery_step;
        }
        
        // if a frequency rise, negative steps
        if (spike_up == true) {
          float recovery_step = (target_freq - nominal_freq) / ((float)recovery_time / how_often);
          current_freq -= recovery_step;
        }

        // check if recovery is done
        if (current_freq >= freq_lower_bound && current_freq <= freq_upper_bound) {
          in_recovery = false;
          current_freq = generateNormalFreq(); // return to normal behavior
        }
      }

      // calculate the ratio of smart controllers ON based on frequency
      ratio_SC_on = calculateScRatio(current_freq);

      if (is_charging == true) {
        updateCharge();
      }

      updateVoltageAndCurrent();
    }

    // spike_time controls how often there is spike
    if (current_time - last_spike >= spike_time) {
      last_spike = current_time;
      target_freq = generateSpikeFreq(); // generate a spike (up or down)
      current_freq = target_freq;
      in_recovery = true; // start recovery process
    }
    
    // send_data controls how often data is sent to server
    if (current_time - last_data >= send_data) {
      last_data = current_time;

      // check if connected to WiFi
      if (WiFi.status() == WL_CONNECTED) {
        String timestamp = getISO8601Time();

        String jsonPayload = "{";
        jsonPayload += "\"api_key\":\"" + api_key + "\",";
        jsonPayload += "\"timestamp\":\"" + timestamp + "Z" + "\",";
        jsonPayload += "\"mac_address\":\"" + mac_address + "\",";
        jsonPayload += "\"is_charging\":" + String(is_charging ? "true" : "false") + ",";
        jsonPayload += "\"frequency\":" + String(current_freq, 2) + ",";
        jsonPayload += "\"voltage\":" + String(voltage, 2) + ",";
        jsonPayload += "\"current\":" + String(current, 2) + ",";
        jsonPayload += "\"battery_percentage\":" + String(battery_percentage, 2);
        jsonPayload += "}";

        // battery_time controls how often the battery is displayed
        if (current_time - last_battery >= battery_time) {
          last_battery = current_time;
          show_battery = true;
        }

        // controls duration of how long battery figure is shown
        if ((show_battery == true) && (current_time - last_battery >= 3000)) {
          show_battery = false;
        }

        // display all information on OLED
        if (show_battery == false) {
          u8g2.clearBuffer();
          line_one = "Device: " + mac_address;
          u8g2.drawStr(0, 10, line_one.c_str());
          line_two = "Frequency: " + String(current_freq, 2);
          u8g2.drawStr(0, 20, line_two.c_str());
          line_three = "Voltage: " + String(voltage, 2);
          u8g2.drawStr(0, 30, line_three.c_str());
          line_four = "Current: " + String(current, 2);
          u8g2.drawStr(0, 40, line_four.c_str());
          line_five = "Charging: " + String(is_charging ? "true" : "false");
          u8g2.drawStr(0, 50, line_five.c_str());
          line_six = "Battery: " + String(battery_percentage, 2) + "%";
          u8g2.drawStr(0, 60, line_six.c_str());
          u8g2.sendBuffer();
        }
        
        if (show_battery == true) {
          displayBattery();
        }

        http.POST(jsonPayload);
        Serial.println("(MAIN-LOOP) sent store_controller_reading payload: " + jsonPayload);
      } else {
        Serial.println("(MAIN-LOOP) WiFi Disconnected");
        //http.end();
      }
    }
  }

  //delay(1000);
}