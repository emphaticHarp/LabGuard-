// LabGuard+ ESP32 - Modern Dashboard with Live Data
// --------------------------------------------------
// Author: Soumy - IoT Developer & Embedded Systems Engineer
// Features:
// - Serves beautiful custom HTML dashboard
// - REST API endpoints for live sensor and relay data
// - JS in HTML fetches and displays real values
// - All automation, relay, and settings logic preserved

#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// I2C LCD Display (0x27 is the default I2C address for most LCD displays)
LiquidCrystal_I2C lcd(0x27, 16, 2); // 16x2 LCD display

// Wi-Fi Credentials
const char* ssid = "apple";
const char* password = "12345678";

// ESP8266 Connection Settings (stored in EEPROM)
String esp8266_ip = "192.168.4.1";
uint16_t esp8266_port = 8080;
bool esp8266_connected = false;
String esp8266_actual_ip = ""; // Store the actual IP of connected ESP8266

// EEPROM Addresses for ESP8266 settings
#define EEPROM_SIZE 50  // Increased EEPROM size
#define ADDR_ESP8266_IP 10
#define ADDR_ESP8266_PORT 30
#define ADDR_ESP8266_IP_LEN 40

// Telegram Bot Credentials
String botToken = "7458354053:AAE5ooTnc0R3WQJi32xrR2KeWCHdgpw4R5c";
String chatID = "1351262356";

// TCP + Web Server
WiFiServer tcpServer(8080);
WiFiClient client;
WebServer server(80);

// Relay Pins
#define RELAY1 14   // Relay 1: Exhaust Fan (GPIO 14 / D5)
#define RELAY2 27   // Relay 2: Room Light (GPIO 27 / D4)
#define RELAY3 26   // Relay 3: Cooling Fan (GPIO 26 / D3)
#define RELAY4 25   // Relay 4: Buzzer (GPIO 25 / D2)

// LEDs
#define LED_RED 32      // Red LED: Wi-Fi failure (GPIO 32 / D13)
#define LED_WHITE 33    // White LED: Wi-Fi connected (GPIO 33 / D12)
#define LED_GREEN 12    // Green LED: ESP8266 comm OK (GPIO 12 / D14)

// Reset Button
#define RESET_BUTTON 15 // Manual Reset Button (GPIO 15 / D15)

// EEPROM Addresses
#define ADDR_MODE 4
#define ADDR_THRESH_TEMP 5
#define ADDR_THRESH_GAS 6
#define ADDR_THRESH_SOUND 7

// Other Variables
bool autoMode = true;
String logBuffer = "";
unsigned long lastUptimeLog = 0;
int systemUptimeMinutes = 0;
unsigned long lastSensorUpdate = 0;
bool isOnline = false;
#define SENSOR_TIMEOUT 10000 // 10 seconds

// Sensor Data Storage
struct SensorData {
  float temperature = 0.0;
  int gasLevel = 0;
  int soundLevel = 0;
  bool motionDetected = false;
  bool irTriggered = false;
  int lightLevel = 0;
  long distance = 0;
} sensorData;

// Threshold Defaults
int TEMP_THRESHOLD = 40;
int GAS_THRESHOLD = 350;
int SOUND_THRESHOLD = 80;

// Sensor Stats
struct SensorStats {
  float current = NAN;
  float average = NAN;
  float maximum = NAN;
  float minimum = NAN;
  int count = 0;
  float sum = 0;
};
SensorStats tempStats, gasStats, soundStats, lightStats, distStats;

// Active Alerts
std::vector<String> activeAlerts;

// Add at the top:
unsigned long dataPoints = 0;

// --- Sensor History Circular Buffers ---
#define HISTORY_SIZE 100
struct SensorHistoryEntry {
  float value;
  unsigned long timestamp;
};
SensorHistoryEntry tempHistory[HISTORY_SIZE];
SensorHistoryEntry gasHistory[HISTORY_SIZE];
SensorHistoryEntry soundHistory[HISTORY_SIZE];
SensorHistoryEntry lightHistory[HISTORY_SIZE];
SensorHistoryEntry distHistory[HISTORY_SIZE];
int tempHistIdx = 0, gasHistIdx = 0, soundHistIdx = 0, lightHistIdx = 0, distHistIdx = 0;

// ------------------------ Utility Functions --------------------------
void setRelay(int pin, bool state) {
  digitalWrite(pin, state ? LOW : HIGH);
}

bool getRelayState(int pin) {
  return digitalRead(pin) == LOW;
}

void saveRelayStates() {
  EEPROM.write(0, getRelayState(RELAY1));
  EEPROM.write(1, getRelayState(RELAY2));
  EEPROM.write(2, getRelayState(RELAY3));
  EEPROM.write(3, getRelayState(RELAY4));
  EEPROM.write(ADDR_MODE, autoMode);
  EEPROM.write(ADDR_THRESH_TEMP, TEMP_THRESHOLD);
  EEPROM.write(ADDR_THRESH_GAS, GAS_THRESHOLD);
  EEPROM.write(ADDR_THRESH_SOUND, SOUND_THRESHOLD);
  EEPROM.commit();
}

void loadRelayStates() {
  setRelay(RELAY1, EEPROM.read(0));
  setRelay(RELAY2, EEPROM.read(1));
  setRelay(RELAY3, EEPROM.read(2));
  setRelay(RELAY4, EEPROM.read(3));
  autoMode = EEPROM.read(ADDR_MODE);
  TEMP_THRESHOLD = EEPROM.read(ADDR_THRESH_TEMP);
  GAS_THRESHOLD = EEPROM.read(ADDR_THRESH_GAS);
  SOUND_THRESHOLD = EEPROM.read(ADDR_THRESH_SOUND);
}

// Save ESP8266 connection settings to EEPROM
void saveESP8266Settings() {
  // Save IP address length first
  EEPROM.write(ADDR_ESP8266_IP_LEN, esp8266_ip.length());
  
  // Save IP address as bytes
  for (int i = 0; i < esp8266_ip.length(); i++) {
    EEPROM.write(ADDR_ESP8266_IP + i, esp8266_ip.charAt(i));
  }
  
  // Save port (2 bytes)
  EEPROM.write(ADDR_ESP8266_PORT, esp8266_port & 0xFF);
  EEPROM.write(ADDR_ESP8266_PORT + 1, (esp8266_port >> 8) & 0xFF);
  
  EEPROM.commit();
  Serial.println("ESP8266 settings saved: " + esp8266_ip + ":" + String(esp8266_port));
}

// Load ESP8266 connection settings from EEPROM
void loadESP8266Settings() {
  // Load IP address length
  int ipLen = EEPROM.read(ADDR_ESP8266_IP_LEN);
  
  // Load IP address
  esp8266_ip = "";
  for (int i = 0; i < ipLen; i++) {
    esp8266_ip += (char)EEPROM.read(ADDR_ESP8266_IP + i);
  }
  
  // Load port (2 bytes)
  esp8266_port = EEPROM.read(ADDR_ESP8266_PORT) | (EEPROM.read(ADDR_ESP8266_PORT + 1) << 8);
  
  // If no valid settings found, use defaults
  if (esp8266_ip.length() == 0 || esp8266_port == 0) {
    esp8266_ip = "192.168.4.1";
    esp8266_port = 8080;
    saveESP8266Settings();
  }
  
  Serial.println("ESP8266 settings loaded: " + esp8266_ip + ":" + String(esp8266_port));
}

// Add a struct for log entries
struct LogEntry {
  unsigned long timestamp;
  String message;
};
std::vector<LogEntry> logEntries;

void logEvent(String msg) {
  Serial.println(msg);
  unsigned long now = millis() / 1000; // seconds since boot
  logEntries.push_back({now, msg});
  if (logEntries.size() > 100) logEntries.erase(logEntries.begin());
}

void notifyTelegram(String msg) {
  HTTPClient http;
  String url = "https://api.telegram.org/bot" + botToken + "/sendMessage?chat_id=" + chatID + "&text=" + msg;
  http.begin(url);
  http.GET();
  http.end();
}

void setLEDs(bool red, bool white, bool green) {
  digitalWrite(LED_RED, red ? HIGH : LOW);
  digitalWrite(LED_WHITE, white ? HIGH : LOW);
  digitalWrite(LED_GREEN, green ? HIGH : LOW);
}

void blinkBuzzer(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    setRelay(RELAY4, true);
    delay(delayMs);
    setRelay(RELAY4, false);
    delay(delayMs);
  }
}

// LCD Display Functions
void initLCD() {
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("LabGuard+");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  delay(2000);
}

void updateLCD() {
  lcd.clear();
  
  // Line 1: ESP32 IP and status
  lcd.setCursor(0, 0);
  lcd.print("ESP32: ");
  lcd.print(WiFi.localIP().toString());
  
  // Line 2: ESP8266 connection status
  lcd.setCursor(0, 1);
  if (esp8266_connected) {
    lcd.print("ESP8266: ");
    if (esp8266_actual_ip.length() > 0) {
      lcd.print(esp8266_actual_ip);
    } else {
      lcd.print(esp8266_ip);
    }
  } else {
    lcd.print("ESP8266: Disconnected");
  }
}

void showLCDMessage(String line1, String line2, int duration = 3000) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
  delay(duration);
  updateLCD();
}

// Parse sensor data from ESP8266
void parseSensorData(String data) {
  if (data.startsWith("DATA:")) {
    data = data.substring(5); // Remove "DATA:" prefix
    int tempIndex = data.indexOf("TEMP=");
    if (tempIndex != -1) {
      int commaIndex = data.indexOf(",", tempIndex);
      if (commaIndex != -1) {
        sensorData.temperature = data.substring(tempIndex + 5, commaIndex).toFloat();
        tempStats.current = sensorData.temperature;
        tempStats.sum += sensorData.temperature;
        tempStats.count++;
        if (isnan(tempStats.maximum) || sensorData.temperature > tempStats.maximum) tempStats.maximum = sensorData.temperature;
        if (isnan(tempStats.minimum) || sensorData.temperature < tempStats.minimum) tempStats.minimum = sensorData.temperature;
        tempStats.average = tempStats.sum / tempStats.count;
      }
    }
    int gasIndex = data.indexOf("GAS=");
    if (gasIndex != -1) {
      int commaIndex = data.indexOf(",", gasIndex);
      if (commaIndex != -1) {
        sensorData.gasLevel = data.substring(gasIndex + 4, commaIndex).toInt();
        gasStats.current = sensorData.gasLevel;
        gasStats.sum += sensorData.gasLevel;
        gasStats.count++;
        if (isnan(gasStats.maximum) || sensorData.gasLevel > gasStats.maximum) gasStats.maximum = sensorData.gasLevel;
        if (isnan(gasStats.minimum) || sensorData.gasLevel < gasStats.minimum) gasStats.minimum = sensorData.gasLevel;
        gasStats.average = gasStats.sum / gasStats.count;
      }
    }
    int soundIndex = data.indexOf("SOUND=");
    if (soundIndex != -1) {
      int commaIndex = data.indexOf(",", soundIndex);
      if (commaIndex != -1) {
        sensorData.soundLevel = data.substring(soundIndex + 6, commaIndex).toInt();
        soundStats.current = sensorData.soundLevel;
        soundStats.sum += sensorData.soundLevel;
        soundStats.count++;
        if (isnan(soundStats.maximum) || sensorData.soundLevel > soundStats.maximum) soundStats.maximum = sensorData.soundLevel;
        if (isnan(soundStats.minimum) || sensorData.soundLevel < soundStats.minimum) soundStats.minimum = sensorData.soundLevel;
        soundStats.average = soundStats.sum / soundStats.count;
      }
    }
    int pirIndex = data.indexOf("PIR=");
    if (pirIndex != -1) {
      int commaIndex = data.indexOf(",", pirIndex);
      if (commaIndex != -1) sensorData.motionDetected = (data.substring(pirIndex + 4, commaIndex).toInt() == 1);
    }
    int irIndex = data.indexOf("IR=");
    if (irIndex != -1) {
      int commaIndex = data.indexOf(",", irIndex);
      if (commaIndex != -1) sensorData.irTriggered = (data.substring(irIndex + 3, commaIndex).toInt() == 0);
    }
    int lightIndex = data.indexOf("LIGHT=");
    if (lightIndex != -1) {
      int commaIndex = data.indexOf(",", lightIndex);
      if (commaIndex != -1) {
        sensorData.lightLevel = data.substring(lightIndex + 6, commaIndex).toInt();
        lightStats.current = sensorData.lightLevel;
        lightStats.sum += sensorData.lightLevel;
        lightStats.count++;
        if (isnan(lightStats.maximum) || sensorData.lightLevel > lightStats.maximum) lightStats.maximum = sensorData.lightLevel;
        if (isnan(lightStats.minimum) || sensorData.lightLevel < lightStats.minimum) lightStats.minimum = sensorData.lightLevel;
        lightStats.average = lightStats.sum / lightStats.count;
      }
    }
    int distIndex = data.indexOf("DIST=");
    if (distIndex != -1) {
      sensorData.distance = data.substring(distIndex + 5).toInt();
      distStats.current = sensorData.distance;
      distStats.sum += sensorData.distance;
      distStats.count++;
      if (isnan(distStats.maximum) || sensorData.distance > distStats.maximum) distStats.maximum = sensorData.distance;
      if (isnan(distStats.minimum) || sensorData.distance < distStats.minimum) distStats.minimum = sensorData.distance;
      distStats.average = distStats.sum / distStats.count;
    }
    lastSensorUpdate = millis();
    isOnline = true;
    dataPoints++;
  }
  // Alerts
  activeAlerts.clear();
  if (sensorData.temperature > TEMP_THRESHOLD) activeAlerts.push_back("TEMP_HIGH");
  if (sensorData.gasLevel > GAS_THRESHOLD) activeAlerts.push_back("GAS_LEAK");
  if (sensorData.soundLevel == SOUND_THRESHOLD) activeAlerts.push_back("SOUND_EVENT");
  if (sensorData.motionDetected) activeAlerts.push_back("MOTION_PIR");
  if (sensorData.irTriggered) activeAlerts.push_back("IR_TRIGGERED");
  if (sensorData.distance > 0 && sensorData.distance < 200) activeAlerts.push_back("PRESENCE_DETECTED");
  if (sensorData.lightLevel < 100) activeAlerts.push_back("ROOM_DARK");
}

// ------------------------ API Endpoints --------------------------
void handleApiSensors() {
  DynamicJsonDocument doc(1024);
  doc["temperature"] = isnan(sensorData.temperature) ? NAN : sensorData.temperature;
  doc["gasLevel"] = isnan(sensorData.gasLevel) ? -1 : sensorData.gasLevel;
  doc["soundLevel"] = isnan(sensorData.soundLevel) ? -1 : sensorData.soundLevel;
  doc["motionDetected"] = sensorData.motionDetected;
  doc["irTriggered"] = sensorData.irTriggered;
  doc["lightLevel"] = isnan(sensorData.lightLevel) ? -1 : sensorData.lightLevel;
  doc["distance"] = isnan(sensorData.distance) ? -1 : sensorData.distance;
  doc["isOnline"] = isOnline;
  doc["dataPoints"] = dataPoints;
  JsonArray alerts = doc.createNestedArray("activeAlerts");
  for (auto &a : activeAlerts) alerts.add(a);
  // Stats
  JsonObject stats = doc.createNestedObject("stats");
  stats["temperature"] = tempStats.count ? tempStats.current : NAN;
  stats["temperature_avg"] = tempStats.count ? tempStats.average : NAN;
  stats["temperature_max"] = tempStats.count ? tempStats.maximum : NAN;
  stats["temperature_min"] = tempStats.count ? tempStats.minimum : NAN;
  stats["gas"] = gasStats.count ? gasStats.current : NAN;
  stats["gas_avg"] = gasStats.count ? gasStats.average : NAN;
  stats["gas_max"] = gasStats.count ? gasStats.maximum : NAN;
  stats["gas_min"] = gasStats.count ? gasStats.minimum : NAN;
  stats["sound"] = soundStats.count ? soundStats.current : NAN;
  stats["sound_avg"] = soundStats.count ? soundStats.average : NAN;
  stats["sound_max"] = soundStats.count ? soundStats.maximum : NAN;
  stats["sound_min"] = soundStats.count ? soundStats.minimum : NAN;
  stats["light"] = lightStats.count ? lightStats.current : NAN;
  stats["light_avg"] = lightStats.count ? lightStats.average : NAN;
  stats["light_max"] = lightStats.count ? lightStats.maximum : NAN;
  stats["light_min"] = lightStats.count ? lightStats.minimum : NAN;
  stats["distance"] = distStats.count ? distStats.current : NAN;
  stats["distance_avg"] = distStats.count ? distStats.average : NAN;
  stats["distance_max"] = distStats.count ? distStats.maximum : NAN;
  stats["distance_min"] = distStats.count ? distStats.minimum : NAN;
  // System status
  String systemStatus = "Normal";
  if (!isOnline) systemStatus = "Offline";
  else if (activeAlerts.size() > 0) systemStatus = "Alert";
  doc["systemStatus"] = systemStatus;
  // Uptime as human readable
  int mins = systemUptimeMinutes;
  String uptimeStr = String(mins) + " minute" + (mins == 1 ? "" : "s");
  if (mins >= 60) {
    int hours = mins / 60;
    int rem = mins % 60;
    uptimeStr = String(hours) + " hour" + (hours == 1 ? "" : "s");
    if (rem > 0) uptimeStr += ", " + String(rem) + " min";
  }
  doc["uptimeStr"] = uptimeStr;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleApiRelays() {
  DynamicJsonDocument doc(128);
  doc["relay1"] = getRelayState(RELAY1);
  doc["relay2"] = getRelayState(RELAY2);
  doc["relay3"] = getRelayState(RELAY3);
  doc["relay4"] = getRelayState(RELAY4);
  doc["autoMode"] = autoMode;
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleApiLog() {
  // Return log data as JSON array
  DynamicJsonDocument doc(1024);
  JsonArray logs = doc.createNestedArray("logs");
  for (auto &entry : logEntries) {
    JsonObject log = logs.createNestedObject();
    unsigned long mins = entry.timestamp / 60;
    unsigned long secs = entry.timestamp % 60;
    char timeStr[8];
    sprintf(timeStr, "%02lu:%02lu", mins, secs);
    log["time"] = String(timeStr);
    log["message"] = entry.message;
    log["type"] = entry.message.indexOf("ALERT") != -1 ? "alert" : "info";
  }
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleApiUptime() {
  DynamicJsonDocument doc(128);
  doc["uptime"] = String(systemUptimeMinutes) + " minutes";
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// Handle relay toggle via API
void handleApiRelayToggle() {
  String uri = server.uri();
  int relayNumber = 0;
  if (uri.indexOf("/api/relay/1/toggle") != -1) relayNumber = 1;
  else if (uri.indexOf("/api/relay/2/toggle") != -1) relayNumber = 2;
  else if (uri.indexOf("/api/relay/3/toggle") != -1) relayNumber = 3;
  else if (uri.indexOf("/api/relay/4/toggle") != -1) relayNumber = 4;
  
  if (relayNumber > 0 && !autoMode) {
    int pin = (relayNumber == 1) ? RELAY1 : (relayNumber == 2) ? RELAY2 : (relayNumber == 3) ? RELAY3 : RELAY4;
    bool newState = !getRelayState(pin);
    setRelay(pin, newState);
    saveRelayStates();
    logEvent("Relay " + String(relayNumber) + " toggled via API");
    
    DynamicJsonDocument doc(128);
    doc["state"] = newState;
    doc["relay"] = relayNumber;
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
  } else {
    server.send(400, "text/plain", "Invalid request or auto mode active");
  }
}

// Handle mode toggle via API
void handleApiModeToggle() {
  autoMode = !autoMode;
  EEPROM.write(ADDR_MODE, autoMode);
  EEPROM.commit();
  logEvent("Mode changed to " + String(autoMode ? "Auto" : "Manual"));
  
  DynamicJsonDocument doc(128);
  doc["mode"] = autoMode ? "Auto" : "Manual";
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// Handle all relays ON via API
void handleApiAllOn() {
  if (!autoMode) {
    setRelay(RELAY1, true);
    setRelay(RELAY2, true);
    setRelay(RELAY3, true);
    setRelay(RELAY4, true);
    saveRelayStates();
    logEvent("All relays turned ON via API");
  }
  server.send(200, "text/plain", "OK");
}

// Handle all relays OFF via API
void handleApiAllOff() {
  if (!autoMode) {
    setRelay(RELAY1, false);
    setRelay(RELAY2, false);
    setRelay(RELAY3, false);
    setRelay(RELAY4, false);
    saveRelayStates();
    logEvent("All relays turned OFF via API");
  }
  server.send(200, "text/plain", "OK");
}

// Handle settings save via API
void handleApiSettings() {
  if (server.hasArg("plain")) {
    String body = server.arg("plain");
    DynamicJsonDocument doc(512);
    deserializeJson(doc, body);
    
    if (doc.containsKey("tempThreshold")) TEMP_THRESHOLD = doc["tempThreshold"];
    if (doc.containsKey("gasThreshold")) GAS_THRESHOLD = doc["gasThreshold"];
    if (doc.containsKey("soundThreshold")) SOUND_THRESHOLD = doc["soundThreshold"];
    
    EEPROM.write(ADDR_THRESH_TEMP, TEMP_THRESHOLD);
    EEPROM.write(ADDR_THRESH_GAS, GAS_THRESHOLD);
    EEPROM.write(ADDR_THRESH_SOUND, SOUND_THRESHOLD);
    EEPROM.commit();
    
    logEvent("Settings updated via API");
    // Send new thresholds to ESP8266 if connected
    if (client && client.connected()) {
      String threshMsg = "THRESHOLDS:TEMP=" + String(TEMP_THRESHOLD) + ",GAS=" + String(GAS_THRESHOLD) + ",SOUND=" + String(SOUND_THRESHOLD);
      client.println(threshMsg);
    }
    server.send(200, "application/json", "{\"status\":\"saved\"}");
  } else {
    server.send(400, "text/plain", "Invalid request");
  }
}

// Handle chart data requests (placeholder responses)
void handleApiChart() {
  String uri = server.uri();
  String sensorType = "";
  String timeRange = "1h";
  
  if (uri.indexOf("/temperature") != -1) sensorType = "temperature";
  else if (uri.indexOf("/gas") != -1) sensorType = "gas";
  else if (uri.indexOf("/light") != -1) sensorType = "light";
  else if (uri.indexOf("/sound") != -1) sensorType = "sound";
  else if (uri.indexOf("/distance") != -1) sensorType = "distance";
  
  if (uri.indexOf("/1h") != -1) timeRange = "1h";
  else if (uri.indexOf("/6h") != -1) timeRange = "6h";
  else if (uri.indexOf("/24h") != -1) timeRange = "24h";
  else if (uri.indexOf("/7d") != -1) timeRange = "7d";
  
  // Return sample chart data
  DynamicJsonDocument doc(2048);
  JsonArray labels = doc.createNestedArray("labels");
  JsonArray values = doc.createNestedArray("values");
  unsigned long now = millis()/1000;
  SensorHistoryEntry* hist = nullptr;
  int idx = 0;
  if (sensorType == "temperature") { hist = tempHistory; idx = tempHistIdx; doc["unit"] = "°C"; doc["color"] = "#ef4444"; }
  else if (sensorType == "gas") { hist = gasHistory; idx = gasHistIdx; doc["unit"] = "ppm"; doc["color"] = "#f59e0b"; }
  else if (sensorType == "light") { hist = lightHistory; idx = lightHistIdx; doc["unit"] = "lux"; doc["color"] = "#f97316"; }
  else if (sensorType == "sound") { hist = soundHistory; idx = soundHistIdx; doc["unit"] = "dB"; doc["color"] = "#8b5cf6"; }
  else if (sensorType == "distance") { hist = distHistory; idx = distHistIdx; doc["unit"] = "cm"; doc["color"] = "#6366f1"; }
  doc["label"] = sensorType;
  if (hist) {
    for (int i = 0; i < HISTORY_SIZE; i++) {
      int realIdx = (idx + i) % HISTORY_SIZE;
      if (!isnan(hist[realIdx].value) && hist[realIdx].timestamp > 0) {
        labels.add(String(hist[realIdx].timestamp));
        values.add(hist[realIdx].value);
      }
    }
  }
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// Handle trend data requests (placeholder responses)
void handleApiTrend() {
  DynamicJsonDocument doc(1024);
  doc["labels"] = JsonArray();
  doc["datasets"] = JsonArray();
  
  JsonObject dataset = doc["datasets"].createNestedObject();
  dataset["label"] = "Current Values";
  dataset["data"] = JsonArray();
  dataset["backgroundColor"] = JsonArray();
  dataset["borderColor"] = JsonArray();
  
  // Add sensor data
  dataset["data"].add(sensorData.temperature);
  dataset["data"].add(sensorData.gasLevel);
  dataset["data"].add(sensorData.lightLevel);
  dataset["data"].add(sensorData.soundLevel);
  
  // Add colors
  dataset["backgroundColor"].add("#ef4444");
  dataset["backgroundColor"].add("#f59e0b");
  dataset["backgroundColor"].add("#f97316");
  dataset["backgroundColor"].add("#8b5cf6");
  dataset["borderColor"].add("#ef4444");
  dataset["borderColor"].add("#f59e0b");
  dataset["borderColor"].add("#f97316");
  dataset["borderColor"].add("#8b5cf6");
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// Handle ESP8266 configuration
void handleApiESP8266Config() {
  if (server.method() == HTTP_POST) {
    // Save new ESP8266 settings
    String newIP = server.arg("ip");
    int newPort = server.arg("port").toInt();
    
    if (newIP.length() > 0 && newPort > 0) {
      esp8266_ip = newIP;
      esp8266_port = newPort;
      saveESP8266Settings();
      
      DynamicJsonDocument doc(256);
      doc["success"] = true;
      doc["message"] = "ESP8266 settings updated";
      doc["ip"] = esp8266_ip;
      doc["port"] = esp8266_port;
      
      String response;
      serializeJson(doc, response);
      server.send(200, "application/json", response);
      
      showLCDMessage("ESP8266 Config", "Updated Successfully");
    } else {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid IP or port\"}");
    }
  } else {
    // Return current ESP8266 settings
    DynamicJsonDocument doc(256);
    doc["ip"] = esp8266_ip;
    doc["port"] = esp8266_port;
    doc["connected"] = esp8266_connected;
    
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  }
}

// ------------------------ Modern HTML Dashboard --------------------------
String htmlPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>LabGuard+ Dashboard</title>
  <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600;700&display=swap" rel="stylesheet">
  <link href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css" rel="stylesheet">
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <style>
    :root{--primary-color:#2563eb;--primary-dark:#1d4ed8;--secondary-color:#64748b;--success-color:#10b981;--warning-color:#f59e0b;--danger-color:#ef4444;--background:#f8fafc;--surface:#ffffff;--text-primary:#1e293b;--text-secondary:#64748b;--border-color:#e2e8f0;--shadow-sm:0 1px 2px 0 rgb(0 0 0/0.05);--shadow-md:0 4px 6px-1px rgb(0 0 0/0.1),0 2px 4px-2px rgb(0 0 0/0.1);--shadow-lg:0 10px 15px-3px rgb(0 0 0/0.1),0 4px 6px-4px rgb(0 0 0/0.1);--shadow-xl:0 20px 25px-5px rgb(0 0 0/0.1),0 8px 10px-6px rgb(0 0 0/0.1)}*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Inter',-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:var(--background);color:var(--text-primary);line-height:1.6;overflow-x:hidden}.container{max-width:1400px;margin:0 auto;padding:2rem}.header{text-align:center;margin-bottom:3rem;position:relative}.header::after{content:'';position:absolute;bottom:-1rem;left:50%;transform:translateX(-50%);width:60px;height:4px;background:linear-gradient(90deg,var(--primary-color),var(--success-color));border-radius:2px}.logo{display:flex;align-items:center;justify-content:center;gap:1rem;margin-bottom:1rem}.logo-icon{width:48px;height:48px;background:linear-gradient(135deg,var(--primary-color),var(--primary-dark));border-radius:12px;display:flex;align-items:center;justify-content:center;color:white;font-size:1.5rem;box-shadow:var(--shadow-lg)}
h1{font-size:2.5rem;font-weight:700;background:linear-gradient(135deg,var(--primary-color),var(--primary-dark));-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text;margin:0}.subtitle{color:var(--text-secondary);font-size:1.1rem;font-weight:400;margin-top:0.5rem}.dashboard{display:grid;grid-template-columns:repeat(auto-fit,minmax(350px,1fr));gap:2rem;margin-bottom:2rem}.card{background:var(--surface);border-radius:16px;padding:1.5rem;box-shadow:var(--shadow-md);border:1px solid var(--border-color);transition:all 0.3s cubic-bezier(0.4,0,0.2,1);position:relative;overflow:hidden}.card::before{content:'';position:absolute;top:0;left:0;right:0;height:4px;background:linear-gradient(90deg,var(--primary-color),var(--success-color))}.card:hover{transform:translateY(-4px);box-shadow:var(--shadow-xl)}.card-header{display:flex;align-items:center;gap:0.75rem;margin-bottom:1.5rem}.card-icon{width:40px;height:40px;border-radius:10px;display:flex;align-items:center;justify-content:center;color:white;font-size:1.1rem}.card-icon.primary{background:linear-gradient(135deg,var(--primary-color),var(--primary-dark))}.card-icon.success{background:linear-gradient(135deg,var(--success-color),#059669)}.card-icon.warning{background:linear-gradient(135deg,var(--warning-color),#d97706)}.card-icon.danger{background:linear-gradient(135deg,var(--danger-color),#dc2626)}.card-icon.analytics{background:linear-gradient(135deg,#8b5cf6,#7c3aed)}.card-icon.trends{background:linear-gradient(135deg,#06b6d4,#0891b2)}.card h3{font-size:1.25rem;font-weight:600;color:var(--text-primary);margin:0}.relay-grid{display:grid;gap:1rem}.relay-item{display:flex;align-items:center;justify-content:space-between;padding:1rem;background:var(--background);border-radius:12px;border:1px solid var(--border-color);transition:all 0.2s ease}.relay-item:hover{border-color:var(--primary-color);background:#f1f5f9}.relay-info{display:flex;align-items:center;gap:0.75rem}.relay-name{font-weight:500;color:var(--text-primary)}.status-indicator{display:flex;align-items:center;gap:0.5rem;font-weight:600}.status-on{color:var(--success-color)}.status-off{color:var(--text-secondary)}.status-dot{width:8px;height:8px;border-radius:50%;background:currentColor;animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}
50%{opacity:0.5}}.btn{padding:0.75rem 1.5rem;border:none;border-radius:8px;font-weight:500;font-size:0.875rem;cursor:pointer;transition:all 0.2s ease;display:inline-flex;align-items:center;gap:0.5rem;text-decoration:none}.btn-primary{background:var(--primary-color);color:white}.btn-primary:hover{background:var(--primary-dark);transform:translateY(-1px);box-shadow:var(--shadow-md)}.btn-secondary{background:var(--secondary-color);color:white}.btn-secondary:hover{background:#475569;transform:translateY(-1px);box-shadow:var(--shadow-md)}.btn-outline{background:transparent;color:var(--primary-color);border:1px solid var(--primary-color)}.btn-outline:hover{background:var(--primary-color);color:white}.btn-sm{padding:0.5rem 1rem;font-size:0.75rem}.sensor-grid{display:grid;grid-template-columns:repeat(2,1fr);gap:1rem}.sensor-item{display:flex;align-items:center;gap:0.75rem;padding:1rem;background:var(--background);border-radius:12px;border:1px solid var(--border-color)}.sensor-icon{width:32px;height:32px;border-radius:8px;display:flex;align-items:center;justify-content:center;color:white;font-size:0.875rem}.sensor-data{flex:1}.sensor-label{font-size:0.875rem;color:var(--text-secondary);font-weight:500}.sensor-value{font-size:1.125rem;font-weight:600;color:var(--text-primary)}.log-container{max-height:200px;overflow-y:auto;background:var(--background);border-radius:12px;padding:1rem;border:1px solid var(--border-color)}.log-entry{display:flex;align-items:flex-start;gap:0.75rem;padding:0.5rem 0;border-bottom:1px solid var(--border-color);font-size:0.875rem}.log-entry:last-child{border-bottom:none}.log-time{color:var(--text-secondary);font-weight:500;min-width:60px}.log-message{color:var(--text-primary);flex:1}.log-alert{color:var(--danger-color);font-weight:600}.form-group{margin-bottom:1rem}.form-label{display:block;font-weight:500;color:var(--text-primary);margin-bottom:0.5rem}.form-input{width:100%;padding:0.75rem;border:1px solid var(--border-color);border-radius:8px;font-size:0.875rem;transition:border-color 0.2s ease;background:var(--surface)}.form-input:focus{outline:none;border-color:var(--primary-color);box-shadow:0 0 0 3px rgb(37 99 235/0.1)}.footer{text-align:center;padding:2rem 0;color:var(--text-secondary);font-size:0.875rem}.uptime{display:inline-flex;align-items:center;gap:0.5rem;padding:0.5rem 1rem;background:var(--surface);border-radius:20px;box-shadow:var(--shadow-sm);border:1px solid var(--border-color)}.uptime-icon{color:var(--success-color)}
@media(max-width:768px){.container{padding:1rem}.dashboard{grid-template-columns:1fr;gap:1.5rem}.sensor-grid{grid-template-columns:1fr}
h1{font-size:2rem}}.log-container::-webkit-scrollbar{width:6px}.log-container::-webkit-scrollbar-track{background:var(--background);border-radius:3px}.log-container::-webkit-scrollbar-thumb{background:var(--border-color);border-radius:3px}.log-container::-webkit-scrollbar-thumb:hover{background:var(--secondary-color)}
@keyframes fadeInUp{from{opacity:0;transform:translateY(30px)}
to{opacity:1;transform:translateY(0)}}
@keyframes slideInLeft{from{opacity:0;transform:translateX(-30px)}
to{opacity:1;transform:translateX(0)}}
@keyframes glow{0%,100%{box-shadow:0 0 5px rgba(37,99,235,0.3)}
50%{box-shadow:0 0 20px rgba(37,99,235,0.6)}}.card{animation:fadeInUp 0.6s ease-out}.card:nth-child(1){animation-delay:0.1s}.card:nth-child(2){animation-delay:0.2s}.card:nth-child(3){animation-delay:0.3s}.card:nth-child(4){animation-delay:0.4s}.header{animation:slideInLeft 0.8s ease-out}.sensor-item:hover{transform:translateY(-2px);box-shadow:var(--shadow-lg);border-color:var(--primary-color)}.sensor-item:hover.sensor-icon{animation:glow 2s infinite}.status-badge{display:inline-flex;align-items:center;gap:0.25rem;padding:0.25rem 0.75rem;border-radius:20px;font-size:0.75rem;font-weight:600;text-transform:uppercase;letter-spacing:0.5px}.status-badge.online{background:rgba(16,185,129,0.1);color:var(--success-color);border:1px solid rgba(16,185,129,0.2)}.status-badge.offline{background:rgba(100,116,139,0.1);color:var(--text-secondary);border:1px solid rgba(100,116,139,0.2)}.status-badge.warning{background:rgba(245,158,11,0.1);color:var(--warning-color);border:1px solid rgba(245,158,11,0.2)}.status-badge.danger{background:rgba(239,68,68,0.1);color:var(--danger-color);border:1px solid rgba(239,68,68,0.2)}.progress-bar{width:100%;height:6px;background:var(--border-color);border-radius:3px;overflow:hidden;margin-top:0.5rem}.progress-fill{height:100%;border-radius:3px;transition:width 0.3s ease}.progress-fill.temp{background:linear-gradient(90deg,#ef4444,#f97316)}.progress-fill.gas{background:linear-gradient(90deg,#f59e0b,#eab308)}.progress-fill.sound{background:linear-gradient(90deg,#8b5cf6,#a855f7)}.progress-fill.light{background:linear-gradient(90deg,#f97316,#ea580c)}.btn-group{display:flex;gap:0.5rem;margin-top:1rem}.btn-sm{padding:0.5rem 1rem;font-size:0.75rem}.loading{display:inline-block;width:16px;height:16px;border:2px solid var(--border-color);border-radius:50%;border-top-color:var(--primary-color);animation:spin 1s ease-in-out infinite}
@keyframes spin{to{transform:rotate(360deg)}}.tooltip{position:relative;cursor:help}.tooltip::after{content:attr(data-tooltip);position:absolute;bottom:100%;left:50%;transform:translateX(-50%);background:var(--text-primary);color:white;padding:0.5rem;border-radius:6px;font-size:0.75rem;white-space:nowrap;opacity:0;pointer-events:none;transition:opacity 0.2s ease;z-index:1000}.tooltip:hover::after{opacity:1}.theme-toggle{position:fixed;top:2rem;right:2rem;z-index:1000}.theme-toggle.btn{width:48px;height:48px;border-radius:50%;padding:0;display:flex;align-items:center;justify-content:center;background:var(--surface);border:1px solid var(--border-color);box-shadow:var(--shadow-md)}
@media(max-width:1024px){.dashboard{grid-template-columns:repeat(2,1fr)}}
@media(max-width:768px){.container{padding:1rem}.dashboard{grid-template-columns:1fr;gap:1.5rem}.sensor-grid{grid-template-columns:1fr}
h1{font-size:2rem}.theme-toggle{top:1rem;right:1rem}}
body.dark-theme{--background:#0f172a;--surface:#1e293b;--text-primary:#f1f5f9;--text-secondary:#94a3b8;--border-color:#334155}
body.dark-theme.card{background:var(--surface);border-color:var(--border-color)}
body.dark-theme.sensor-item,body.dark-theme.relay-item,body.dark-theme.log-container{background:var(--background);border-color:var(--border-color)}
body.dark-theme.form-input{background:var(--background);border-color:var(--border-color);color:var(--text-primary)}
body.dark-theme.uptime{background:var(--surface);border-color:var(--border-color)}.chart-container{position:relative;height:300px;margin-top:1rem}.chart-tabs{display:flex;gap:0.5rem;margin-bottom:1rem;border-bottom:1px solid var(--border-color);padding-bottom:0.5rem}.chart-tab{padding:0.5rem 1rem;border:none;background:transparent;color:var(--text-secondary);border-radius:6px;cursor:pointer;font-size:0.875rem;font-weight:500;transition:all 0.2s ease}.chart-tab.active{background:var(--primary-color);color:white}.chart-tab:hover:not(.active){background:var(--background);color:var(--text-primary)}.chart-tab.active{background:var(--primary-color);color:white}.chart-stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:1rem;margin-top:1rem;padding-top:1rem;border-top:1px solid var(--border-color)}.stat-item{text-align:center;padding:0.75rem;background:var(--background);border-radius:8px;border:1px solid var(--border-color)}.stat-value{font-size:1.5rem;font-weight:700;color:var(--primary-color);margin-bottom:0.25rem}.stat-label{font-size:0.75rem;color:var(--text-secondary);text-transform:uppercase;letter-spacing:0.5px}.chart-controls{display:flex;gap:0.5rem;margin-bottom:1rem}.chart-btn{padding:0.25rem 0.75rem;border:1px solid var(--border-color);background:var(--surface);color:var(--text-primary);border-radius:4px;font-size:0.75rem;cursor:pointer;transition:all 0.2s ease}.chart-btn:hover,.chart-btn.active{background:var(--primary-color);color:white;border-color:var(--primary-color)}
  </style>
</head>
<body>
  <div class="theme-toggle">
    <button class="btn" onclick="toggleTheme()" data-tooltip="Toggle Dark Mode">
      <i class="fas fa-moon"></i>
    </button>
  </div>

  <div class="container">
    <header class="header">
      <div class="logo">
        <div class="logo-icon">
          <i class="fas fa-shield-alt"></i>
        </div>
        <h1>LabGuard+</h1>
      </div>
      <p class="subtitle">Advanced Laboratory Monitoring&Control System</p>
      <div style="margin-top: 1rem;">
        <span class="status-badge online">
          <i class="fas fa-circle" style="font-size: 0.5rem;"></i>
          System Online
        </span>
        <span class="status-badge warning" style="margin-left: 0.5rem;">
          <i class="fas fa-exclamation-triangle" style="font-size: 0.5rem;"></i>
          2 Alerts Active
        </span>
      </div>
    </header>

    <div class="dashboard">
      <div class="card">
        <div class="card-header">
          <div class="card-icon primary">
            <i class="fas fa-toggle-on"></i>
          </div>
          <h3>Relay Control (<span id="modeDisplay">Auto</span>)</h3>
        </div>
        <div class="relay-grid">
          <div class="relay-item">
            <div class="relay-info">
              <i class="fas fa-plug" id="relay1Icon" style="color: var(--primary-color);"></i>
              <span class="relay-name">Relay 1</span>
            </div>
            <div class="relay-controls">
              <span class="status-indicator status-on" id="relay1Status">
                <span class="status-dot"></span>ON
              </span>
              <button class="btn btn-outline" onclick="toggleRelay(1)" style="margin-left: 1rem;">
                <i class="fas fa-sync-alt"></i>Toggle
              </button>
            </div>
          </div>
          
          <div class="relay-item">
            <div class="relay-info">
              <i class="fas fa-plug" id="relay2Icon" style="color: var(--text-secondary);"></i>
              <span class="relay-name">Relay 2</span>
            </div>
            <div class="relay-controls">
              <span class="status-indicator status-off" id="relay2Status">
                <span class="status-dot"></span>OFF
              </span>
              <button class="btn btn-outline" onclick="toggleRelay(2)" style="margin-left: 1rem;">
                <i class="fas fa-sync-alt"></i>Toggle
              </button>
            </div>
          </div>
          
          <div class="relay-item">
            <div class="relay-info">
              <i class="fas fa-plug" id="relay3Icon" style="color: var(--primary-color);"></i>
              <span class="relay-name">Relay 3</span>
            </div>
            <div class="relay-controls">
              <span class="status-indicator status-on" id="relay3Status">
                <span class="status-dot"></span>ON
              </span>
              <button class="btn btn-outline" onclick="toggleRelay(3)" style="margin-left: 1rem;">
                <i class="fas fa-sync-alt"></i>Toggle
              </button>
            </div>
          </div>
          
          <div class="relay-item">
            <div class="relay-info">
              <i class="fas fa-plug" id="relay4Icon" style="color: var(--text-secondary);"></i>
              <span class="relay-name">Relay 4</span>
            </div>
            <div class="relay-controls">
              <span class="status-indicator status-off" id="relay4Status">
                <span class="status-dot"></span>OFF
              </span>
              <button class="btn btn-outline" onclick="toggleRelay(4)" style="margin-left: 1rem;">
                <i class="fas fa-sync-alt"></i>Toggle
              </button>
            </div>
          </div>
        </div>
        <div class="btn-group">
          <button class="btn btn-primary btn-sm" onclick="toggleMode()">
            <i class="fas fa-cogs"></i>Toggle Mode
          </button>
          <button class="btn btn-secondary btn-sm" onclick="allOn()">
            <i class="fas fa-power-off"></i>All ON
          </button>
          <button class="btn btn-outline btn-sm" onclick="allOff()">
            <i class="fas fa-stop"></i>All OFF
          </button>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <div class="card-icon success">
            <i class="fas fa-chart-line"></i>
          </div>
          <h3>Sensor Data</h3>
        </div>
        <div class="sensor-grid">
          <div class="sensor-item tooltip" data-tooltip="Current ambient temperature">
            <div class="sensor-icon" style="background: linear-gradient(135deg, #ef4444, #dc2626);">
              <i class="fas fa-thermometer-half"></i>
            </div>
            <div class="sensor-data">
              <div class="sensor-label">Temperature</div>
              <div class="sensor-value" id="tempValue">No Data</div>
              <div class="progress-bar">
                <div class="progress-fill temp" id="tempProgress" style="width: 0%;"></div>
              </div>
            </div>
          </div>
          
          <div class="sensor-item tooltip" data-tooltip="Gas concentration in parts per million">
            <div class="sensor-icon" style="background: linear-gradient(135deg, #f59e0b, #d97706);">
              <i class="fas fa-wind"></i>
            </div>
            <div class="sensor-data">
              <div class="sensor-label">Gas Level</div>
              <div class="sensor-value" id="gasValue">No Data</div>
              <div class="progress-bar">
                <div class="progress-fill gas" id="gasProgress" style="width: 0%;"></div>
              </div>
            </div>
          </div>
          
          <div class="sensor-item tooltip" data-tooltip="Ambient sound level in decibels">
            <div class="sensor-icon" style="background: linear-gradient(135deg, #8b5cf6, #7c3aed);">
              <i class="fas fa-volume-up"></i>
            </div>
            <div class="sensor-data">
              <div class="sensor-label">Sound Level</div>
              <div class="sensor-value" id="soundValue">No Data</div>
              <div class="progress-bar">
                <div class="progress-fill sound" id="soundProgress" style="width: 0%;"></div>
              </div>
            </div>
          </div>
          
          <div class="sensor-item tooltip" data-tooltip="Passive Infrared motion detection">
            <div class="sensor-icon" style="background: linear-gradient(135deg, #06b6d4, #0891b2);">
              <i class="fas fa-user"></i>
            </div>
            <div class="sensor-data">
              <div class="sensor-label">Motion (PIR)</div>
              <div class="sensor-value" id="pirValue">No Data</div>
            </div>
          </div>
          
          <div class="sensor-item tooltip" data-tooltip="Infrared proximity sensor">
            <div class="sensor-icon" style="background: linear-gradient(135deg, #84cc16, #65a30d);">
              <i class="fas fa-eye"></i>
            </div>
            <div class="sensor-data">
              <div class="sensor-label">IR Sensor</div>
              <div class="sensor-value" id="irValue">No Data</div>
            </div>
          </div>
          
          <div class="sensor-item tooltip" data-tooltip="Ambient light intensity in lux">
            <div class="sensor-icon" style="background: linear-gradient(135deg, #f97316, #ea580c);">
              <i class="fas fa-sun"></i>
            </div>
            <div class="sensor-data">
              <div class="sensor-label">Light Level</div>
              <div class="sensor-value" id="lightValue">No Data</div>
              <div class="progress-bar">
                <div class="progress-fill light" id="lightProgress" style="width: 0%;"></div>
              </div>
            </div>
          </div>
          
          <div class="sensor-item tooltip" data-tooltip="Ultrasonic distance measurement" style="grid-column: span 2;">
            <div class="sensor-icon" style="background: linear-gradient(135deg, #6366f1, #4f46e5);">
              <i class="fas fa-ruler-vertical"></i>
            </div>
            <div class="sensor-data">
              <div class="sensor-label">Distance</div>
              <div class="sensor-value" id="distanceValue">No Data</div>
              <div class="progress-bar">
                <div class="progress-fill" id="distanceProgress" style="width: 0%; background: linear-gradient(90deg, #6366f1, #4f46e5);"></div>
              </div>
            </div>
          </div>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <div class="card-icon warning">
            <i class="fas fa-terminal"></i>
          </div>
          <h3>System Log</h3>
        </div>
        <div class="log-container" id="logContainer">
          <!-- No demo log entries, will be filled by JS -->
        </div>
      </div>

      <!-- ESP8266 Configuration Card -->
      <div class="card">
        <div class="card-header">
          <div class="card-icon primary">
            <i class="fas fa-network-wired"></i>
          </div>
          <h3>ESP8266 Configuration</h3>
        </div>
        <form id="esp8266ConfigForm" onsubmit="saveESP8266Config(event)">
          <div class="form-group">
            <label class="form-label">
              <i class="fas fa-server" style="margin-right: 0.5rem; color: var(--primary-color);"></i>
              ESP8266 IP Address
            </label>
            <input type="text" class="form-input" id="esp8266IP" placeholder="192.168.4.1" required>
          </div>
          
          <div class="form-group">
            <label class="form-label">
              <i class="fas fa-plug" style="margin-right: 0.5rem; color: var(--secondary-color);"></i>
              ESP8266 Port
            </label>
            <input type="number" class="form-input" id="esp8266Port" placeholder="8080" min="1" max="65535" required>
          </div>
          
          <div class="form-group">
            <label class="form-label">
              <i class="fas fa-circle" style="margin-right: 0.5rem; color: var(--success-color);"></i>
              Connection Status
            </label>
            <div id="esp8266Status" class="status-badge offline">
              <i class="fas fa-circle" style="font-size: 0.5rem;"></i>
              Disconnected
            </div>
          </div>
          
          <button type="submit" class="btn btn-primary" style="width: 100%;">
            <i class="fas fa-save"></i>Save Configuration
          </button>
        </form>
      </div>

      <div class="card">
        <div class="card-header">
          <div class="card-icon danger">
            <i class="fas fa-cog"></i>
          </div>
          <h3>System Settings</h3>
        </div>
        <form onsubmit="saveSettings(event)">
          <div class="form-group">
            <label class="form-label">
              <i class="fas fa-thermometer-half" style="margin-right: 0.5rem; color: var(--danger-color);"></i>
              Temperature Threshold
            </label>
            <input type="number" class="form-input" id="tempThreshold" value="40" min="0" max="100" step="0.1">
          </div>
          
          <div class="form-group">
            <label class="form-label">
              <i class="fas fa-wind" style="margin-right: 0.5rem; color: var(--warning-color);"></i>
              Gas Level Threshold
            </label>
            <input type="number" class="form-input" id="gasThreshold" value="350" min="0" max="1000" step="1">
          </div>
          
          <div class="form-group">
            <label class="form-label">
              <i class="fas fa-volume-up" style="margin-right: 0.5rem; color: var(--secondary-color);"></i>
              Sound Threshold
            </label>
            <input type="number" class="form-input" id="soundThreshold" value="80" min="0" max="120" step="1">
          </div>
          
          <button type="submit" class="btn btn-primary" style="width: 100%;">
            <i class="fas fa-save"></i>Save Settings
          </button>
        </form>
      </div>

      <!-- Analytics Charts -->
      <div class="card">
        <div class="card-header">
          <div class="card-icon analytics">
            <i class="fas fa-chart-line"></i>
          </div>
          <h3>Sensor Analytics</h3>
        </div>
        
        <div class="chart-tabs">
          <button class="chart-tab active" onclick="switchChart('temperature')">Temperature</button>
          <button class="chart-tab" onclick="switchChart('gas')">Gas Level</button>
          <button class="chart-tab" onclick="switchChart('light')">Light Level</button>
          <button class="chart-tab" onclick="switchChart('sound')">Sound Level</button>
        </div>

        <div class="chart-controls">
          <button class="chart-btn active" onclick="setTimeRange('1h')">1H</button>
          <button class="chart-btn" onclick="setTimeRange('6h')">6H</button>
          <button class="chart-btn" onclick="setTimeRange('24h')">24H</button>
          <button class="chart-btn" onclick="setTimeRange('7d')">7D</button>
        </div>

        <div class="chart-container">
          <canvas id="sensorChart"></canvas>
        </div>

        <div class="chart-stats">
          <div class="stat-item">
            <div class="stat-value" id="currentValue">32.4°C</div>
            <div class="stat-label">Current</div>
          </div>
          <div class="stat-item">
            <div class="stat-value" id="avgValue">31.2°C</div>
            <div class="stat-label">Average</div>
          </div>
          <div class="stat-item">
            <div class="stat-value" id="maxValue">35.8°C</div>
            <div class="stat-label">Maximum</div>
          </div>
          <div class="stat-item">
            <div class="stat-value" id="minValue">28.1°C</div>
            <div class="stat-label">Minimum</div>
          </div>
        </div>
      </div>

      <div class="card">
        <div class="card-header">
          <div class="card-icon trends">
            <i class="fas fa-chart-area"></i>
          </div>
          <h3>System Trends</h3>
        </div>
        
        <div class="chart-tabs">
          <button class="chart-tab active" onclick="switchTrendChart('all')">All Sensors</button>
          <button class="chart-tab" onclick="switchTrendChart('environmental')">Environmental</button>
          <button class="chart-tab" onclick="switchTrendChart('safety')">Safety</button>
        </div>

        <div class="chart-container">
          <canvas id="trendChart"></canvas>
        </div>

        <div class="chart-stats">
          <div class="stat-item">
            <div class="stat-value" id="alertCount">Error</div>
            <div class="stat-label">ACTIVE ALERTS</div>
          </div>
          <div class="stat-item">
            <div class="stat-value" id="systemStatus">Error</div>
            <div class="stat-label">SYSTEM STATUS</div>
          </div>
          <div class="stat-item">
            <div class="stat-value" id="dataPoints">Error</div>
            <div class="stat-label">DATA POINTS</div>
          </div>
          <div class="stat-item">
            <div class="stat-value" id="uptimePercent">Error</div>
            <div class="stat-label">UPTIME</div>
          </div>
        </div>
      </div>
    </div>

    <footer class="footer">
      <div class="uptime">
        <i class="fas fa-clock uptime-icon"></i>
        <span id="uptimeDisplay">System Uptime: 12 minutes</span>
      </div>
    </footer>
  </div>

  <script>
    // Global variables for charts
    let sensorChart, trendChart;
    let currentSensorType = 'temperature';
    let currentTimeRange = '1h';

    // Theme toggle functionality
    function toggleTheme() {
      const body = document.body;
      const themeToggle = document.querySelector('.theme-toggle .btn i');
      
      if (body.classList.contains('dark-theme')) {
        body.classList.remove('dark-theme');
        themeToggle.className = 'fas fa-moon';
        localStorage.setItem('theme', 'light');
      } else {
        body.classList.add('dark-theme');
        themeToggle.className = 'fas fa-sun';
        localStorage.setItem('theme', 'dark');
      }
    }

    // Load saved theme
    const savedTheme = localStorage.getItem('theme');
    if (savedTheme === 'dark') {
      document.body.classList.add('dark-theme');
      document.querySelector('.theme-toggle .btn i').className = 'fas fa-sun';
    }

    // Relay control functions
    function toggleRelay(relayNumber) {
      const button = event.target.closest('.btn');
      const originalText = button.innerHTML;
      button.innerHTML = '<span class="loading"></span>';
      button.disabled = true;

      fetch(`/api/relay/${relayNumber}/toggle`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' }
      })
      .then(response => response.json())
      .then(data => {
        updateRelayDisplay(relayNumber, data.state);
        addLogEntry(`Relay ${relayNumber} toggled ${data.state ? 'ON' : 'OFF'}`);
        button.innerHTML = originalText;
        button.disabled = false;
      })
      .catch(error => {
        console.error('Error toggling relay:', error);
        button.innerHTML = originalText;
        button.disabled = false;
      });
    }

    function updateRelayDisplay(relayNumber, state) {
      const relayItem = document.querySelector(`#relay${relayNumber}Status`).closest('.relay-item');
      const statusIndicator = relayItem.querySelector('.status-indicator');
      const icon = relayItem.querySelector('.relay-info i');
      
      if (state) {
        statusIndicator.className = 'status-indicator status-on';
        statusIndicator.innerHTML = '<span class="status-dot"></span>ON';
        icon.style.color = 'var(--primary-color)';
      } else {
        statusIndicator.className = 'status-indicator status-off';
        statusIndicator.innerHTML = '<span class="status-dot"></span>OFF';
        icon.style.color = 'var(--text-secondary)';
      }
    }

    function toggleMode() {
      fetch('/api/mode/toggle', { method: 'POST' })
        .then(response => response.json())
        .then(data => {
          document.getElementById('modeDisplay').textContent = data.mode;
          addLogEntry(`System mode changed to ${data.mode}`);
        });
    }

    function allOn() {
      fetch('/api/relay/all/on', { method: 'POST' })
        .then(response => response.json())
        .then(data => {
          for (let i = 1; i <= 4; i++) {
            updateRelayDisplay(i, true);
          }
          addLogEntry('All relays turned ON');
        });
    }

    function allOff() {
      fetch('/api/relay/all/off', { method: 'POST' })
        .then(response => response.json())
        .then(data => {
          for (let i = 1; i <= 4; i++) {
            updateRelayDisplay(i, false);
          }
          addLogEntry('All relays turned OFF');
        });
    }

    // Settings functions
    function saveSettings(event) {
      event.preventDefault();
      const submitBtn = event.target.querySelector('button[type="submit"]');
      const originalText = submitBtn.innerHTML;
      submitBtn.innerHTML = '<span class="loading"></span> Saving...';
      submitBtn.disabled = true;

      const settings = {
        tempThreshold: document.getElementById('tempThreshold').value,
        gasThreshold: document.getElementById('gasThreshold').value,
        soundThreshold: document.getElementById('soundThreshold').value
      };

      fetch('/api/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(settings)
      })
      .then(response => response.json())
      .then(data => {
        submitBtn.innerHTML = '<i class="fas fa-check"></i> Saved!';
        addLogEntry('Settings updated successfully');
        setTimeout(() => {
          submitBtn.innerHTML = originalText;
          submitBtn.disabled = false;
        }, 2000);
      })
      .catch(error => {
        console.error('Error saving settings:', error);
        submitBtn.innerHTML = originalText;
        submitBtn.disabled = false;
      });
    }

    // Log functions
    function addLogEntry(message) {
      const logContainer = document.querySelector('.log-container');
      const time = new Date().toLocaleTimeString('en-US', {
        hour12: false,
        hour: '2-digit',
        minute: '2-digit'
      });
      
      const logEntry = document.createElement('div');
      logEntry.className = 'log-entry';
      logEntry.innerHTML = `
        <span class="log-time">${time}</span>
        <span class="log-message">${message}</span>
      `;
      
      logContainer.insertBefore(logEntry, logContainer.firstChild);
      
      // Keep only last 10 entries
      const entries = logContainer.querySelectorAll('.log-entry');
      if (entries.length > 10) {
        entries[entries.length - 1].remove();
      }
    }

    // Data fetching functions
    function fetchSensorData() {
      fetch('/api/sensors')
        .then(response => response.json())
        .then(data => {
          updateSystemStatus(data);
          updateSensorDisplay(data);
          updateStats(data.stats || {});
          updateBottomStats(data);
        })
        .catch(error => {
          updateSystemStatus({isOnline: false, activeAlerts: []});
          updateSensorDisplay({});
          updateStats({});
        });
    }

    function fetchRelayData() {
      fetch('/api/relays')
        .then(response => response.json())
        .then(data => {
          for (let i = 1; i <= 4; i++) {
            updateRelayDisplay(i, data[`relay${i}`]);
          }
        });
    }

    function fetchLogData() {
      fetch('/api/log')
        .then(response => response.json())
        .then(data => {
          updateLogDisplay(data.logs);
        })
        .catch(error => console.error('Error fetching log data:', error));
    }

    function fetchUptime() {
      fetch('/api/uptime')
        .then(response => response.json())
        .then(data => {
          document.getElementById('uptimeDisplay').textContent = `System Uptime: ${data.uptime}`;
        })
        .catch(error => console.error('Error fetching uptime:', error));
    }

    // Update display functions
    function updateSensorDisplay(data) {
      // Update temperature
      if (data.temperature !== undefined && !isNaN(data.temperature)) {
        document.getElementById('tempValue').textContent = `${data.temperature.toFixed(1)}°C`;
        const tempPercent = Math.min((data.temperature / 50) * 100, 100);
        document.getElementById('tempProgress').style.width = `${tempPercent}%`;
      } else {
        document.getElementById('tempValue').textContent = 'Error';
        document.getElementById('tempProgress').style.width = '0%';
      }

      // Update gas level
      if (data.gasLevel !== undefined && !isNaN(data.gasLevel)) {
        document.getElementById('gasValue').textContent = `${data.gasLevel} ppm`;
        const gasPercent = Math.min((data.gasLevel / 1000) * 100, 100);
        document.getElementById('gasProgress').style.width = `${gasPercent}%`;
      } else {
        document.getElementById('gasValue').textContent = 'Error';
        document.getElementById('gasProgress').style.width = '0%';
      }

      // Update sound level
      if (data.soundLevel !== undefined && !isNaN(data.soundLevel)) {
        document.getElementById('soundValue').textContent = `${data.soundLevel} dB`;
        const soundPercent = Math.min((data.soundLevel / 120) * 100, 100);
        document.getElementById('soundProgress').style.width = `${soundPercent}%`;
      } else {
        document.getElementById('soundValue').textContent = 'Error';
        document.getElementById('soundProgress').style.width = '0%';
      }

      // Update PIR motion
      if (data.motionDetected !== undefined) {
        const pirElement = document.getElementById('pirValue');
        if (data.motionDetected) {
          pirElement.innerHTML = '<span class="status-badge online">Detected</span>';
        } else {
          pirElement.innerHTML = '<span class="status-badge offline">Clear</span>';
        }
      } else {
        document.getElementById('pirValue').textContent = 'Error';
      }

      // Update IR sensor
      if (data.irTriggered !== undefined) {
        const irElement = document.getElementById('irValue');
        if (data.irTriggered) {
          irElement.innerHTML = '<span class="status-badge warning">Triggered</span>';
        } else {
          irElement.innerHTML = '<span class="status-badge offline">Clear</span>';
        }
      } else {
        document.getElementById('irValue').textContent = 'Error';
      }

      // Update light level
      if (data.lightLevel !== undefined && !isNaN(data.lightLevel)) {
        document.getElementById('lightValue').textContent = `${data.lightLevel} lux`;
        const lightPercent = Math.min((data.lightLevel / 1000) * 100, 100);
        document.getElementById('lightProgress').style.width = `${lightPercent}%`;
      } else {
        document.getElementById('lightValue').textContent = 'Error';
        document.getElementById('lightProgress').style.width = '0%';
      }

      // Update distance
      if (data.distance !== undefined && !isNaN(data.distance)) {
        document.getElementById('distanceValue').textContent = `${data.distance} cm`;
        const distancePercent = Math.min((data.distance / 200) * 100, 100);
        document.getElementById('distanceProgress').style.width = `${distancePercent}%`;
      } else {
        document.getElementById('distanceValue').textContent = 'Error';
        document.getElementById('distanceProgress').style.width = '0%';
      }
    }

    function updateLogDisplay(logs) {
      const logContainer = document.querySelector('.log-container');
      logContainer.innerHTML = '';
      
      logs.forEach(log => {
        const logEntry = document.createElement('div');
        logEntry.className = 'log-entry';
        if (log.type === 'alert') {
          logEntry.innerHTML = `
            <span class="log-time">${log.time}</span>
            <span class="log-message log-alert">${log.message}</span>
          `;
        } else {
          logEntry.innerHTML = `
            <span class="log-time">${log.time}</span>
            <span class="log-message">${log.message}</span>
          `;
        }
        logContainer.appendChild(logEntry);
      });
    }

    // Chart functions
    function initCharts() {
      const sensorCtx = document.getElementById('sensorChart').getContext('2d');
      const trendCtx = document.getElementById('trendChart').getContext('2d');

      sensorChart = new Chart(sensorCtx, {
        type: 'line',
        data: {
          labels: [],
          datasets: [{
            label: 'Temperature',
            data: [],
            borderColor: '#ef4444',
            backgroundColor: '#ef444420',
            borderWidth: 3,
            fill: true,
            tension: 0.4,
            pointBackgroundColor: '#ef4444',
            pointBorderColor: '#ffffff',
            pointBorderWidth: 2,
            pointRadius: 4,
            pointHoverRadius: 6
          }]
        },
        options: {
          responsive: true,
          maintainAspectRatio: false,
          plugins: {
            legend: { display: false },
            tooltip: {
              backgroundColor: 'rgba(0, 0, 0, 0.8)',
              titleColor: '#ffffff',
              bodyColor: '#ffffff',
              borderColor: '#ef4444',
              borderWidth: 1,
              cornerRadius: 8,
              displayColors: false
            }
          },
          scales: {
            x: {
              grid: { color: 'rgba(0, 0, 0, 0.1)' },
              ticks: { color: 'var(--text-secondary)' }
            },
            y: {
              grid: { color: 'rgba(0, 0, 0, 0.1)' },
              ticks: { color: 'var(--text-secondary)' }
            }
          }
        }
      });

      trendChart = new Chart(trendCtx, {
        type: 'bar',
        data: {
          labels: ['Temperature', 'Gas Level', 'Light Level', 'Sound Level'],
          datasets: [{
            label: 'Current Values',
            data: [0, 0, 0, 0],
            backgroundColor: ['#ef4444', '#f59e0b', '#f97316', '#8b5cf6'],
            borderColor: ['#ef4444', '#f59e0b', '#f97316', '#8b5cf6'],
            borderWidth: 2
          }]
        },
        options: {
          responsive: true,
          maintainAspectRatio: false,
          plugins: {
            legend: {
              display: true,
              position: 'top',
              labels: {
                color: 'var(--text-primary)',
                usePointStyle: true,
                padding: 20
              }
            },
            tooltip: {
              backgroundColor: 'rgba(0, 0, 0, 0.8)',
              titleColor: '#ffffff',
              bodyColor: '#ffffff',
              cornerRadius: 8
            }
          },
          scales: {
            x: {
              grid: { color: 'rgba(0, 0, 0, 0.1)' },
              ticks: { color: 'var(--text-secondary)' }
            },
            y: {
              grid: { color: 'rgba(0, 0, 0, 0.1)' },
              ticks: { color: 'var(--text-secondary)' }
            }
          }
        }
      });
    }

    function switchChart(sensorType) {
      currentSensorType = sensorType;
      // Update chart data based on sensor type
      fetch(`/api/chart/${sensorType}`)
        .then(response => response.json())
        .then(data => {
          sensorChart.data.labels = data.labels;
          sensorChart.data.datasets[0].data = data.values;
          sensorChart.data.datasets[0].label = data.label;
          sensorChart.data.datasets[0].borderColor = data.color;
          sensorChart.data.datasets[0].backgroundColor = data.color + '20';
          sensorChart.data.datasets[0].pointBackgroundColor = data.color;
          sensorChart.options.plugins.tooltip.borderColor = data.color;
          sensorChart.update();
          
          // Update stats
          updateStats(data);
        });

      // Update active tab
      document.querySelectorAll('.chart-tab').forEach(tab => tab.classList.remove('active'));
      event.target.classList.add('active');
    }

    function switchTrendChart(trendType) {
      fetch(`/api/trend/${trendType}`)
        .then(response => response.json())
        .then(data => {
          trendChart.data.labels = data.labels;
          trendChart.data.datasets = data.datasets;
          trendChart.update();
        });

      document.querySelectorAll('.chart-card:nth-child(2) .chart-tab').forEach(tab => tab.classList.remove('active'));
      event.target.classList.add('active');
    }

    function setTimeRange(range) {
      currentTimeRange = range;
      document.querySelectorAll('.chart-btn').forEach(btn => btn.classList.remove('active'));
      event.target.classList.add('active');
      
      fetch(`/api/chart/${currentSensorType}/${range}`)
        .then(response => response.json())
        .then(data => {
          sensorChart.data.labels = data.labels;
          sensorChart.data.datasets[0].data = data.values;
          sensorChart.update();
        });
    }

    function updateStats(data) {
      document.getElementById('currentValue').textContent = data.current + data.unit;
      document.getElementById('avgValue').textContent = data.average + data.unit;
      document.getElementById('maxValue').textContent = data.maximum + data.unit;
      document.getElementById('minValue').textContent = data.minimum + data.unit;
    }

    // Initialize dashboard
    document.addEventListener('DOMContentLoaded', function() {
      // Animate progress bars
      const progressBars = document.querySelectorAll('.progress-fill');
      progressBars.forEach(bar => {
        const width = bar.style.width;
        bar.style.width = '0%';
        setTimeout(() => {
          bar.style.width = width;
        }, 500);
      });

      // Initialize charts
      initCharts();

      // Load ESP8266 configuration
      loadESP8266Config();

      // Initial data fetch
      fetchSensorData();
      fetchRelayData();
      fetchLogData();
      fetchUptime();

      // Set up periodic updates
      setInterval(() => {
        fetchSensorData();
        fetchRelayData();
        fetchLogData();
        fetchUptime();
        loadESP8266Config(); // Update ESP8266 status periodically
      }, 5000); // Update every 5 seconds
    });

    function updateSystemStatus(data) {
      // System Online/Offline
      const statusBadge = document.querySelector('.status-badge.online');
      if (data.isOnline) {
        statusBadge.textContent = 'SYSTEM ONLINE';
        statusBadge.classList.remove('offline');
        statusBadge.classList.add('online');
      } else {
        statusBadge.textContent = 'SYSTEM OFFLINE';
        statusBadge.classList.remove('online');
        statusBadge.classList.add('offline');
      }
      // Alerts
      const alertBadge = document.querySelector('.status-badge.warning');
      if (data.activeAlerts && data.activeAlerts.length > 0) {
        alertBadge.textContent = `${data.activeAlerts.length} ALERT${data.activeAlerts.length > 1 ? 'S' : ''} ACTIVE`;
        alertBadge.classList.remove('no-alert');
        alertBadge.classList.add('warning');
      } else {
        alertBadge.textContent = 'NO ALERTS';
        alertBadge.classList.remove('warning');
        alertBadge.classList.add('no-alert');
      }
    }

    function updateStats(stats) {
      // Temperature
      document.getElementById('currentValue').textContent =
        stats.temperature !== undefined && stats.temperature !== null ? `${stats.temperature.toFixed(1)}°C` : 'Error';
      document.getElementById('avgValue').textContent =
        stats.temperature_avg !== undefined && stats.temperature_avg !== null ? `${stats.temperature_avg.toFixed(1)}°C` : 'Error';
      document.getElementById('maxValue').textContent =
        stats.temperature_max !== undefined && stats.temperature_max !== null ? `${stats.temperature_max.toFixed(1)}°C` : 'Error';
      document.getElementById('minValue').textContent =
        stats.temperature_min !== undefined && stats.temperature_min !== null ? `${stats.temperature_min.toFixed(1)}°C` : 'Error';
    }

    function updateRelayDisplay(relayNumber, state) {
      const relayItem = document.querySelector(`#relay${relayNumber}Status`).closest('.relay-item');
      const statusIndicator = relayItem.querySelector('.status-indicator');
      const icon = relayItem.querySelector('.relay-info i');
      if (state) {
        statusIndicator.className = 'status-indicator status-on';
        statusIndicator.innerHTML = '<span class="status-dot"></span>ON';
        icon.style.color = 'var(--primary-color)';
      } else {
        statusIndicator.className = 'status-indicator status-off';
        statusIndicator.innerHTML = '<span class="status-dot"></span>OFF';
        icon.style.color = 'var(--text-secondary)';
      }
    }

    function updateBottomStats(data) {
      document.getElementById('alertCount').textContent =
        data.activeAlerts && data.activeAlerts.length !== undefined ? data.activeAlerts.length : 'Error';
      document.getElementById('systemStatus').textContent =
        data.systemStatus ? data.systemStatus : 'Error';
      document.getElementById('dataPoints').textContent =
        data.dataPoints !== undefined ? data.dataPoints : 'Error';
      document.getElementById('uptimePercent').textContent =
        data.uptimeStr ? data.uptimeStr : 'Error';
    }

    // ESP8266 Configuration Functions
    function saveESP8266Config(event) {
      event.preventDefault();
      const submitBtn = event.target.querySelector('button[type="submit"]');
      const originalText = submitBtn.innerHTML;
      submitBtn.innerHTML = '<span class="loading"></span> Saving...';
      submitBtn.disabled = true;

      const config = {
        ip: document.getElementById('esp8266IP').value,
        port: document.getElementById('esp8266Port').value
      };

      fetch('/api/esp8266/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config)
      })
      .then(response => response.json())
      .then(data => {
        if (data.success) {
          submitBtn.innerHTML = '<i class="fas fa-check"></i> Saved!';
          addLogEntry('ESP8266 configuration updated: ' + data.ip + ':' + data.port);
          setTimeout(() => {
            submitBtn.innerHTML = originalText;
            submitBtn.disabled = false;
          }, 2000);
        } else {
          throw new Error(data.message);
        }
      })
      .catch(error => {
        console.error('Error saving ESP8266 config:', error);
        submitBtn.innerHTML = '<i class="fas fa-exclamation-triangle"></i> Error!';
        addLogEntry('ESP8266 configuration failed: ' + error.message);
        setTimeout(() => {
          submitBtn.innerHTML = originalText;
          submitBtn.disabled = false;
        }, 2000);
      });
    }

    function loadESP8266Config() {
      fetch('/api/esp8266/config')
        .then(response => response.json())
        .then(data => {
          document.getElementById('esp8266IP').value = data.ip;
          document.getElementById('esp8266Port').value = data.port;
          updateESP8266Status(data.connected);
        })
        .catch(error => {
          console.error('Error loading ESP8266 config:', error);
        });
    }

    function updateESP8266Status(connected) {
      const statusBadge = document.getElementById('esp8266Status');
      if (connected) {
        statusBadge.className = 'status-badge online';
        statusBadge.innerHTML = '<i class="fas fa-circle" style="font-size: 0.5rem;"></i>Connected';
      } else {
        statusBadge.className = 'status-badge offline';
        statusBadge.innerHTML = '<i class="fas fa-circle" style="font-size: 0.5rem;"></i>Disconnected';
      }
    }
  </script>
</body>
</html>
  )rawliteral";
  return html;
}

// ------------------------ Web Handlers --------------------------
void handleRoot() { server.send(200, "text/html", htmlPage()); }

void handleRelay(int n) {
  if (!autoMode) {
    int pin = (n == 1) ? RELAY1 : (n == 2) ? RELAY2 : (n == 3) ? RELAY3 : RELAY4;
    setRelay(pin, !getRelayState(pin));
    saveRelayStates();
    logEvent("Relay " + String(n) + " toggled manually");
  }
  server.send(200, "text/plain", "OK");
}

void handleToggleMode() {
  autoMode = !autoMode;
  EEPROM.write(ADDR_MODE, autoMode);
  EEPROM.commit();
  logEvent("Mode changed to " + String(autoMode ? "Auto" : "Manual"));
  server.send(200, "text/plain", "OK");
}

void handleSetThreshold() {
  if (server.hasArg("temp")) TEMP_THRESHOLD = server.arg("temp").toInt();
  if (server.hasArg("gas")) GAS_THRESHOLD = server.arg("gas").toInt();
  EEPROM.write(ADDR_THRESH_TEMP, TEMP_THRESHOLD);
  EEPROM.write(ADDR_THRESH_GAS, GAS_THRESHOLD);
  EEPROM.commit();
  logEvent("Thresholds Updated. TEMP=" + String(TEMP_THRESHOLD) + ", GAS=" + String(GAS_THRESHOLD));
  server.send(200, "text/plain", "OK");
}

void handleAllOn() {
  if (!autoMode) {
    setRelay(RELAY1, true);
    setRelay(RELAY2, true);
    setRelay(RELAY3, true);
    setRelay(RELAY4, true);
    saveRelayStates();
    logEvent("All relays turned ON");
  }
  server.send(200, "text/plain", "OK");
}

void handleAllOff() {
  if (!autoMode) {
    setRelay(RELAY1, false);
    setRelay(RELAY2, false);
    setRelay(RELAY3, false);
    setRelay(RELAY4, false);
    saveRelayStates();
    logEvent("All relays turned OFF");
  }
  server.send(200, "text/plain", "OK");
}

// ------------------------ Setup & Loop --------------------------
void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  pinMode(RELAY1, OUTPUT); pinMode(RELAY2, OUTPUT);
  pinMode(RELAY3, OUTPUT); pinMode(RELAY4, OUTPUT);
  pinMode(LED_RED, OUTPUT); pinMode(LED_WHITE, OUTPUT); pinMode(LED_GREEN, OUTPUT);
  pinMode(RESET_BUTTON, INPUT_PULLUP);
  
  // Initialize LCD display
  initLCD();
  
  // Load settings
  loadRelayStates();
  loadESP8266Settings();
  
  setLEDs(true, false, false);
  showLCDMessage("Connecting to", "Wi-Fi...");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  setLEDs(false, true, false);
  Serial.println("Wi-Fi connected: " + WiFi.localIP().toString());
  
  showLCDMessage("Wi-Fi Connected", WiFi.localIP().toString());

  if (MDNS.begin("labguard")) Serial.println("mDNS ready: http://labguard.local");

  tcpServer.begin();
  server.on("/", handleRoot);
  server.on("/api/sensors", handleApiSensors);
  server.on("/api/relays", handleApiRelays);
  server.on("/api/log", handleApiLog);
  server.on("/api/uptime", handleApiUptime);
  server.on("/relay1", []() { handleRelay(1); });
  server.on("/relay2", []() { handleRelay(2); });
  server.on("/relay3", []() { handleRelay(3); });
  server.on("/relay4", []() { handleRelay(4); });
  server.on("/mode", handleToggleMode);
  server.on("/set", handleSetThreshold);
  server.on("/allon", handleAllOn);
  server.on("/alloff", handleAllOff);
  server.on("/api/settings", HTTP_POST, handleApiSettings);
  server.on("/api/mode/toggle", HTTP_POST, handleApiModeToggle);
  server.on("/api/relay/all/on", HTTP_POST, handleApiAllOn);
  server.on("/api/relay/all/off", HTTP_POST, handleApiAllOff);
  server.on("/api/relay/1/toggle", HTTP_POST, handleApiRelayToggle);
  server.on("/api/relay/2/toggle", HTTP_POST, handleApiRelayToggle);
  server.on("/api/relay/3/toggle", HTTP_POST, handleApiRelayToggle);
  server.on("/api/relay/4/toggle", HTTP_POST, handleApiRelayToggle);
  server.on("/api/chart/temperature", handleApiChart);
  server.on("/api/chart/gas", handleApiChart);
  server.on("/api/chart/light", handleApiChart);
  server.on("/api/chart/sound", handleApiChart);
  server.on("/api/trend/all", handleApiTrend);
  server.on("/api/trend/environmental", handleApiTrend);
  server.on("/api/trend/safety", handleApiTrend);
  server.on("/api/esp8266/config", handleApiESP8266Config);
  server.begin();
  logEvent("System Boot Complete.");
}

void loop() {
  if (digitalRead(RESET_BUTTON) == LOW) {
    logEvent("Manual Reset Triggered");
    delay(500);
    ESP.restart();
  }

  if (millis() - lastUptimeLog > 60000) {
    systemUptimeMinutes++;
    lastUptimeLog = millis();
  }

  // Set isOnline to false if no sensor update for SENSOR_TIMEOUT
  if (millis() - lastSensorUpdate > SENSOR_TIMEOUT) {
    isOnline = false;
    esp8266_connected = false;
  }

  server.handleClient();
  client = tcpServer.available();
  if (client) {
    String msg = client.readStringUntil('\n');
    msg.trim();
    if (msg.startsWith("DATA:")) {
      parseSensorData(msg);
      esp8266_connected = true;
      isOnline = true;
      lastSensorUpdate = millis();
      setLEDs(false, true, true); // Green LED on when ESP8266 connected
    } else if (msg.startsWith("INFO:ESP8266_IP=")) {
      esp8266_actual_ip = msg.substring(15); // Remove "INFO:ESP8266_IP=" prefix
      logEvent("ESP8266 IP: " + esp8266_actual_ip);
    } else if (msg.startsWith("PONG:")) {
      // Keep-alive response from ESP8266
      esp8266_connected = true;
      lastSensorUpdate = millis();
    }
    logEvent("From ESP8266: " + msg);
    // Automation logic (as before)
    if (autoMode && msg.startsWith("ALERT:")) {
      if (msg == "ALERT:GAS_LEAK") {
        setRelay(RELAY1, true);
        notifyTelegram("⚠️ GAS Leak detected! Exhaust Fan ON");
      } else if (msg == "ALERT:TEMP_HIGH") {
        setRelay(RELAY3, true);
        notifyTelegram("🔥 High Temperature detected! Cooling Fan ON");
      } else if (msg == "ALERT:MOTION_PIR" || msg == "ALERT:PRESENCE_DETECTED") {
        setRelay(RELAY2, true);
        notifyTelegram("👁️ Motion Detected! Lights ON");
      } else if (msg == "ALERT:SOUND_EVENT" || msg == "ALERT:IR_TRIGGERED") {
        blinkBuzzer(3, 200);
        notifyTelegram("🔊 Sound/IR Triggered! Alarm Blinking");
      }
      saveRelayStates();
      delay(2000);
      setRelay(RELAY1, false); setRelay(RELAY2, false);
      setRelay(RELAY3, false); setRelay(RELAY4, false);
      saveRelayStates();
    }
  }

  // Update LCD display every 2 seconds
  static unsigned long lastLCDUpdate = 0;
  if (millis() - lastLCDUpdate > 2000) {
    updateLCD();
    lastLCDUpdate = millis();
  }

  // Send periodic ping to ESP8266 every 10 seconds
  static unsigned long lastPing = 0;
  if (millis() - lastPing > 10000) {
    if (client.connected()) {
      client.println("PING:ESP32");
    }
    lastPing = millis();
  }

  if (WiFi.status() != WL_CONNECTED) {
    setLEDs(true, false, false);
    WiFi.begin(ssid, password);
  }
} 