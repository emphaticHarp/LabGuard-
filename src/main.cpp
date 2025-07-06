// LabGuard+ ESP8266 Code - Final Version
// --------------------------------------------------
// Author: Soumy - IoT Developer & Embedded Systems Engineer
// ➤ Connects to Wi-Fi
// ➤ Communicates with ESP32 via TCP
// ➤ Reads data from sensors
// ➤ Sends alerts and full sensor data
// ➤ LED status indicators
// ➤ Manual reset push button

#include <ESP8266WiFi.h>
#include <DHT.h>

// --- Function Prototypes ---
void setLEDs(bool red, bool white, bool green, bool blue);
void connectToWiFi();
void connectToESP32();
void sendTestMessages();
bool testSensorHealth();
void blinkLED(int pin, int times, int delayTime);
void readAndSendSensorData();
long getUltrasonicDistance();

// ---------------------- Wi-Fi Credentials ----------------------
const char* ssid = "apple";
const char* password = "12345678";

// ---------------------- ESP32 TCP Server Info ------------------
const char* esp32_ip = "192.168.4.1";
const uint16_t esp32_port = 8080;
WiFiClient client;

// ---------------------- Sensor Pins ----------------------------
#define DHTPIN D1       // DHT11 Temp Sensor (D1 / GPIO5)
#define PIR_PIN D2      // PIR Motion Sensor (D2 / GPIO4)
#define IR_PIN D7       // IR Sensor (D7 / GPIO13) [Moved to avoid GPIO0 boot issue]
#define SOUND_PIN D4    // KY-037 Sound Sensor (D4 / GPIO2)
#define TRIG_PIN D5     // Ultrasonic Trigger (D5 / GPIO14)
#define ECHO_PIN D6     // Ultrasonic Echo (D6 / GPIO12)
#define MQ2_PIN A0      // MQ-2 Gas Sensor (A0)
#define LDR_PIN D9      // LDR Light Sensor (D9 / GPIO3)
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ---------------------- LED Indicator Pins ----------------------
#define LED_RED D4      // Red LED (No Wi-Fi) (D4 / GPIO2)
#define LED_WHITE D3    // White LED (Wi-Fi Connected) (D3 / GPIO0)
#define LED_GREEN D8    // Green LED (System OK) (D8 / GPIO15)
#define LED_BLUE D0     // Blue LED (ESP32 Connected) (D0 / GPIO16) [New LED for connection status]

// ---------------------- Reset Button ----------------------------
#define RESET_BUTTON D10  // Manual Reset Button (D10 / GPIO16 to GND)

// ---------------------- Thresholds ------------------------------
float TEMP_THRESHOLD = 40.0;
int GAS_THRESHOLD = 350;
int SOUND_TRIGGER = HIGH;
int PRESENCE_DISTANCE_CM = 100;
int LIGHT_THRESHOLD = 500;

// ---------------------- Timing ------------------------------
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 5000;
bool esp32Ready = false;
bool sensorReady = false;

// ---------------------- Setup ------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_WHITE, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(IR_PIN, INPUT);
  pinMode(SOUND_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(RESET_BUTTON, INPUT_PULLUP);
  dht.begin();

  setLEDs(true, false, false, false);
  connectToWiFi();
  connectToESP32();

  if (esp32Ready) {
    sendTestMessages();
    sensorReady = testSensorHealth();
  }

  if (esp32Ready && sensorReady) {
    setLEDs(false, true, true, true); // All good - blue LED on for ESP32 connection
  } else {
    setLEDs(true, false, false, false);
  }
}

// ---------------------- Loop ------------------------------
void loop() {
  // Wi-Fi auto-reconnect logic
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("📡 Wi-Fi dropped! Reconnecting...");
    connectToWiFi();
  }

  // Debounced reset button
  static unsigned long lastResetPress = 0;
  if (digitalRead(RESET_BUTTON) == LOW && millis() - lastResetPress > 300) {
    lastResetPress = millis();
    Serial.println("🔁 Manual Reset Pressed!");
    ESP.restart();
  }

  if (!client.connected()) {
    setLEDs(true, false, false, false);
    connectToESP32();
    return;
  }

  if (WiFi.RSSI() < -80) {
    blinkLED(LED_RED, 3, 200);
    setLEDs(true, false, false, false);
    return;
  }

  if (millis() - lastSendTime > sendInterval) {
    readAndSendSensorData();
    lastSendTime = millis();
  }

  if (client.available()) {
    String reply = client.readStringUntil('\n');
    reply.trim();
    Serial.println("📥 ESP32 says: " + reply);
    
    // Handle different message types from ESP32
    if (reply.startsWith("THRESHOLDS:")) {
      Serial.println("🔄 New thresholds received from ESP32:");
      Serial.println(reply);
      
      // Parse threshold updates (format: THRESHOLDS:TEMP=40,GAS=350,SOUND=80)
      if (reply.indexOf("TEMP=") != -1) {
        int tempStart = reply.indexOf("TEMP=") + 5;
        int tempEnd = reply.indexOf(",", tempStart);
        if (tempEnd == -1) tempEnd = reply.length();
        TEMP_THRESHOLD = reply.substring(tempStart, tempEnd).toFloat();
        Serial.println("🌡️ New Temperature Threshold: " + String(TEMP_THRESHOLD) + "°C");
      }
      
      if (reply.indexOf("GAS=") != -1) {
        int gasStart = reply.indexOf("GAS=") + 4;
        int gasEnd = reply.indexOf(",", gasStart);
        if (gasEnd == -1) gasEnd = reply.length();
        GAS_THRESHOLD = reply.substring(gasStart, gasEnd).toInt();
        Serial.println("💨 New Gas Threshold: " + String(GAS_THRESHOLD) + " ppm");
      }
      
      if (reply.indexOf("SOUND=") != -1) {
        int soundStart = reply.indexOf("SOUND=") + 6;
        int soundEnd = reply.indexOf(",", soundStart);
        if (soundEnd == -1) soundEnd = reply.length();
        SOUND_TRIGGER = reply.substring(soundStart, soundEnd).toInt();
        Serial.println("🔊 New Sound Threshold: " + String(SOUND_TRIGGER));
      }
    } else if (reply.startsWith("CONFIG:")) {
      Serial.println("⚙️ Configuration update from ESP32:");
      Serial.println(reply);
    } else if (reply.startsWith("PING")) {
      Serial.println("🏓 PING received from ESP32 - connection alive");
      client.println("PONG:ESP8266");
    }
  }
}

// ---------------------- Wi-Fi Connection -----------------------
void connectToWiFi() {
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500); Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Wi-Fi connected!");
    Serial.println("📶 IP: " + WiFi.localIP().toString());
    setLEDs(false, true, false, false);
  } else {
    Serial.println("\n❌ Wi-Fi failed. Restarting...");
    setLEDs(true, false, false, false);
    ESP.restart();
  }
}

// ---------------------- ESP32 Connection -----------------------
void connectToESP32() {
  if (client.connect(esp32_ip, esp32_port)) {
    Serial.println("✅ Connected to ESP32.");
    Serial.println("🔗 ESP32 IP: " + String(esp32_ip) + ":" + String(esp32_port));
    client.println("HELLO:ESP8266");
    esp32Ready = true;
    setLEDs(false, true, true, true); // Blue LED on when connected to ESP32
  } else {
    Serial.println("❌ ESP32 connection failed.");
    Serial.println("🔗 Trying to connect to: " + String(esp32_ip) + ":" + String(esp32_port));
    esp32Ready = false;
    setLEDs(false, true, true, false); // Blue LED off when not connected
  }
}

// ---------------------- Test Functions --------------------------
bool testSensorHealth() {
  float temp = dht.readTemperature();
  int gas = analogRead(MQ2_PIN);
  int sound = digitalRead(SOUND_PIN);
  long dist = getUltrasonicDistance();
  return !isnan(temp) && gas > 0 && sound >= 0 && dist >= 0;
}

void sendTestMessages() {
  client.println("TEST:SENSOR_CHECK");
  delay(500);
  // Send ESP8266 IP address to ESP32
  client.println("INFO:ESP8266_IP=" + WiFi.localIP().toString());
  delay(500);
}

// ---------------------- Sensor Read + Send ----------------------
void readAndSendSensorData() {
  float temp = dht.readTemperature();
  int gas = analogRead(MQ2_PIN);
  int sound = digitalRead(SOUND_PIN);
  int pir = digitalRead(PIR_PIN);
  int ir = digitalRead(IR_PIN);
  int light = analogRead(LDR_PIN);
  long dist = getUltrasonicDistance();

  if (temp > TEMP_THRESHOLD) client.println("ALERT:TEMP_HIGH");
  if (gas > GAS_THRESHOLD) client.println("ALERT:GAS_LEAK");
  if (sound == SOUND_TRIGGER) client.println("ALERT:SOUND_EVENT");
  if (pir == HIGH) client.println("ALERT:MOTION_PIR");
  if (ir == LOW) client.println("ALERT:IR_TRIGGERED");
  if (dist > 0 && dist < PRESENCE_DISTANCE_CM) client.println("ALERT:PRESENCE_DETECTED");
  if (light < LIGHT_THRESHOLD) client.println("ALERT:ROOM_DARK");

  if (dist == -1) {
    Serial.println("No object detected by ultrasonic sensor.");
  }

  String data = "DATA:TEMP=" + String(temp) + ",GAS=" + String(gas) + ",SOUND=" + String(sound) + ",PIR=" + String(pir) + ",IR=" + String(ir) + ",LIGHT=" + String(light) + ",DIST=" + String(dist);
  client.println(data);
}

// ---------------------- Ultrasonic Sensor -----------------------
long getUltrasonicDistance() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 20000);
  return (duration == 0) ? -1 : duration * 0.034 / 2;
}

// ---------------------- LED Functions ---------------------------
void setLEDs(bool red, bool white, bool green, bool blue) {
  digitalWrite(LED_RED, red ? HIGH : LOW);
  digitalWrite(LED_WHITE, white ? HIGH : LOW);
  digitalWrite(LED_GREEN, green ? HIGH : LOW);
  digitalWrite(LED_BLUE, blue ? HIGH : LOW);
}

void blinkLED(int pin, int times, int delayTime) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH); delay(delayTime);
    digitalWrite(pin, LOW); delay(delayTime);
  }
}
