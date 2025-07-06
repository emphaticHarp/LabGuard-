# 🔧 LabGuard+ – Intelligent IoT-Based Lab Monitoring & Control System

## 👨‍💻 **Author**
**Soumy** - IoT Developer & Embedded Systems Engineer

## 📋 Project Overview

**LabGuard+** is an advanced Internet of Things (IoT)-based safety and automation system designed for real-time monitoring and control of laboratory environments. The system integrates multiple environmental sensors, dual microcontrollers (ESP8266 & ESP32), and a smart web-based dashboard with remote access and Telegram alerts.

## 🎯 Key Features

### ✅ **Safety Monitoring**
- 🔥 **Temperature Monitoring** - DHT11 sensor with configurable thresholds
- 💨 **Gas Leak Detection** - MQ-2 sensor for LPG, smoke detection
- 🧍 **Motion Detection** - PIR sensor for human/animal movement
- 🔊 **Sound Monitoring** - KY-037 sensor for unusual noises
- 🌑 **Light Detection** - LDR sensor for darkness conditions
- 📏 **Presence Detection** - Ultrasonic sensor for distance-based presence
- 🔴 **IR Intrusion** - IR sensor for entry/exit detection

### ✅ **Automation & Control**
- 🌀 **Exhaust Fan Control** - Automatic activation on gas leak
- 💡 **Room Light Control** - Automatic activation on motion/presence
- ❄️ **Cooling Fan Control** - Automatic activation on high temperature
- 🔔 **Buzzer Alarm** - Blinking alerts for sound/IR events
- 🔄 **Auto Reset** - All relays reset after 2 seconds
- 💾 **State Persistence** - EEPROM storage for relay states

### ✅ **Smart Dashboard**
- 🌐 **Web Interface** - Accessible via IP or `http://labguard.local`
- 📱 **Real-time Updates** - Auto-refresh every 5 seconds
- 🎛️ **Manual Control** - Toggle relays in manual mode
- 📊 **Live Sensor Data** - Real-time sensor readings display
- 📝 **System Logs** - Event history and alerts
- ⚙️ **Threshold Settings** - Configurable temperature and gas thresholds
- ⏱️ **Uptime Tracker** - System uptime monitoring

### ✅ **Communication & Alerts**
- 📡 **TCP Communication** - ESP8266 ↔ ESP32 data exchange
- 📱 **Telegram Notifications** - Instant alerts for critical events
- 🔗 **Wi-Fi Auto Recovery** - Automatic reconnection
- 💡 **LED Status Indicators** - Visual system health monitoring
- 🔄 **mDNS Support** - Easy access via `labguard.local`

## 🧩 Hardware Components

| Component | Quantity | Purpose |
|-----------|----------|---------|
| ESP8266 NodeMCU | 1 | Sensor hub – collects environmental data |
| ESP32 Dev Module | 1 | Central controller – dashboard & automation |
| DHT11 Temperature Sensor | 1 | Ambient temperature monitoring |
| MQ-2 Gas Sensor | 1 | Gas leakage detection (LPG, smoke) |
| KY-037 Sound Sensor | 1 | Loud sound detection |
| PIR Motion Sensor | 1 | Human/animal motion detection |
| IR Sensor | 1 | Object interruption detection |
| Ultrasonic Sensor (HC-SR04) | 1 | Distance-based presence detection |
| LDR Module | 1 | Light intensity monitoring |
| I2C LCD Display (16x2) | 1 | Status display on ESP32 |
| 4-Channel Relay Module | 1 | Controls exhaust fan, light, fan, buzzer |
| LEDs (Red, White, Green, Blue) | 4 | Status indicators |
| Push Buttons | 2 | Manual hardware reset |
| Jumper Wires | As needed | Circuit connections |
| Breadboard/PCB | 1 | Prototyping |
| 5V Power Supply | 1 | External power source |
| 220Ω Resistors | 7 | LED current limiting |

## 🔌 Pin Connections

### 📲 ESP8266 (Sensor Node)
| Sensor | GPIO Pin | Purpose |
|--------|----------|---------|
| DHT11 Temp Sensor | D1 (GPIO 5) | Temperature reading |
| MQ-2 Gas Sensor | A0 | Gas level reading |
| PIR Motion Sensor | D2 (GPIO 4) | Motion detection |
| IR Sensor | D7 (GPIO 13) | Intrusion detection |
| Ultrasonic Trigger | D5 (GPIO 14) | Distance measurement |
| Ultrasonic Echo | D6 (GPIO 12) | Distance measurement |
| LDR Sensor | D9 (GPIO 3) | Light level reading |
| KY-037 Sound Sensor | D4 (GPIO 2) | Sound detection |
| RED LED | D4 (GPIO 2) | No Wi-Fi indicator |
| WHITE LED | D3 (GPIO 0) | Wi-Fi connected indicator |
| GREEN LED | D8 (GPIO 15) | System OK indicator |
| BLUE LED | D0 (GPIO 16) | ESP32 connection indicator |
| Reset Button | D10 (GPIO 16) | Manual reset |

### 💻 ESP32 (Controller)
| Output Device | D Pin | GPIO Pin | Purpose |
|---------------|-------|----------|---------|
| I2C LCD SDA | D21 | GPIO 21 | LCD data line |
| I2C LCD SCL | D22 | GPIO 22 | LCD clock line |
| Relay 1 (Exhaust Fan) | D14 | GPIO 14 | Gas leak response |
| Relay 2 (Room Light) | D27 | GPIO 27 | Motion/presence response |
| Relay 3 (Cooling Fan) | D26 | GPIO 26 | High temperature response |
| Relay 4 (Buzzer) | D25 | GPIO 25 | Sound/IR alert |
| RED LED | D32 | GPIO 32 | Wi-Fi failure indicator |
| WHITE LED | D33 | GPIO 33 | Wi-Fi connected indicator |
| GREEN LED | D12 | GPIO 12 | ESP8266 communication OK |
| Reset Button | D15 | GPIO 15 | Manual reset |

## 🔌 Detailed Wiring Instructions

### ESP8266 Sensor Node Wiring

#### Power Connections
```
ESP8266 NodeMCU    →    Component
3.3V              →    DHT11 VCC
3.3V              →    MQ-2 VCC
3.3V              →    PIR VCC
3.3V              →    IR Sensor VCC
3.3V              →    KY-037 VCC
3.3V              →    HC-SR04 VCC
3.3V              →    LDR (via voltage divider)
3.3V              →    All LEDs (via 220Ω resistors)
GND               →    All component GND pins
```

#### Sensor Connections
```
ESP8266 Pin    →    Component Pin    →    Description
D1 (GPIO5)    →    DHT11 DATA       →    Temperature & Humidity
A0            →    MQ-2 AOUT        →    Gas level (analog)
D2 (GPIO4)    →    PIR OUT          →    Motion detection
D7 (GPIO13)   →    IR Sensor OUT    →    Object detection
D4 (GPIO2)    →    KY-037 DO        →    Sound detection
D5 (GPIO14)   →    HC-SR04 TRIG     →    Ultrasonic trigger
D6 (GPIO12)   →    HC-SR04 ECHO     →    Ultrasonic echo
D9 (GPIO3)    →    LDR (via divider) →   Light level (analog)
```

#### LED Connections (with 220Ω resistors)
```
ESP8266 Pin    →    LED Color    →    Resistor    →    Description
D4 (GPIO2)    →    Red LED      →    220Ω        →    Wi-Fi failure
D3 (GPIO0)    →    White LED    →    220Ω        →    Wi-Fi connected
D8 (GPIO15)   →    Green LED    →    220Ω        →    System OK
D0 (GPIO16)   →    Blue LED     →    220Ω        →    ESP32 connected
```

#### LDR Voltage Divider Circuit
```
3.3V ── 10kΩ Resistor ── LDR ── GND
                    │
                    └── D9 (GPIO3) [Analog Input]
```

### ESP32 Master Controller Wiring

#### Power Connections
```
ESP32 Dev Module    →    Component
3.3V               →    I2C LCD VCC
3.3V               →    Relay Module VCC
3.3V               →    All LEDs (via 220Ω resistors)
5V                 →    Relay Module VCC (if needed)
GND                →    All component GND pins
```

#### I2C LCD Connections
```
ESP32 Pin    →    LCD Pin    →    Description
D21          →    SDA        →    I2C Data
D22          →    SCL        →    I2C Clock
3.3V         →    VCC        →    Power
GND          →    GND        →    Ground
```

#### Relay Module Connections
```
ESP32 Pin    →    Relay Pin    →    Description
D14          →    IN1         →    Relay 1 (Exhaust Fan)
D27          →    IN2         →    Relay 2 (Room Light)
D26          →    IN3         →    Relay 3 (Cooling Fan)
D25          →    IN4         →    Relay 4 (Buzzer)
```

#### LED Connections (with 220Ω resistors)
```
ESP32 Pin    →    LED Color    →    Resistor    →    Description
D32          →    Red LED      →    220Ω        →    Wi-Fi failure
D33          →    White LED    →    220Ω        →    Wi-Fi connected
D12          →    Green LED    →    220Ω        →    ESP8266 communication
```

## 🚀 Setup Instructions

### 1. **Hardware Assembly**
1. **ESP8266 Sensor Node:**
   - Connect all sensors to their respective pins
   - Add 220Ω resistors to all LEDs
   - Connect the LDR with voltage divider circuit
   - Connect reset button
   - Double-check all connections

2. **ESP32 Master Controller:**
   - Connect I2C LCD display
   - Connect relay module
   - Add 220Ω resistors to all LEDs
   - Connect reset button
   - Double-check all connections

### 2. **Software Setup**

#### ESP8266 (Sensor Node)
```bash
# Navigate to project root
cd "smart final"

# Build and upload ESP8266 code
pio run -e esp8266_sensor_node -t upload
```

#### ESP32 (Controller)
```bash
# Navigate to ESP32 controller folder
cd esp32_controller

# Build and upload ESP32 code
pio run -e esp32_controller -t upload
```

### 3. **Configuration**
1. Update Wi-Fi credentials in both codes:
   ```cpp
   const char* ssid = "your_wifi_name";
   const char* password = "your_wifi_password";
   ```

2. **ESP8266 Configuration (Optional):**
   - The ESP32 web dashboard now includes an "ESP8266 Configuration" section
   - You can set the ESP8266 IP and port without editing code
   - Default ESP32 IP for ESP8266: `192.168.4.1:8080`
   - Settings are saved in EEPROM and persist across reboots

3. Update Telegram credentials in ESP32 code:
   ```cpp
   String botToken = "your_bot_token";
   String chatID = "your_chat_id";
   ```

## 🌐 Accessing the System

### **Web Dashboard**
- **IP Access**: `http://<esp32_ip_address>`
- **mDNS Access**: `http://labguard.local`
- **Auto-refresh**: Every 5 seconds
- **Features**: Live sensor data, relay control, system logs, settings

### **Dashboard Features**
- **Relay Control**: Toggle relays manually (when not in auto mode)
- **Mode Toggle**: Switch between Auto/Manual modes
- **Sensor Data**: Real-time readings from all sensors
- **System Logs**: Event history and alerts
- **Threshold Settings**: Configure temperature and gas thresholds
- **Uptime**: System uptime tracking
- **ESP8266 Configuration**: Set ESP8266 IP and port without code editing
- **Connection Status**: Live ESP8266 connection status
- **I2C LCD Display**: Shows ESP32 and ESP8266 IP addresses

## 🔧 System Operation

### **Startup Sequence**
1. Both devices connect to Wi-Fi
2. ESP32 LCD displays its own IP address
3. ESP8266 establishes TCP connection to ESP32
4. ESP32 LCD shows ESP8266 connection status and IP
5. LED indicators show connection status:
   - 🔴 Red: No Wi-Fi
   - ⚪ White: Wi-Fi Connected
   - 🟢 Green: ESP8266 ↔ ESP32 Communication + Sensors OK
   - 🔵 Blue (ESP8266): Connected to ESP32

### **Data Flow**
1. **ESP8266** reads sensors every 5 seconds
2. **Alerts** sent immediately when thresholds exceeded
3. **Full data** sent in readable format
4. **ESP32** processes data and triggers automation
5. **Telegram alerts** sent for critical events
6. **Web dashboard** updated in real-time

### **Automation Rules**
- **Gas Leak** → Exhaust Fan ON
- **High Temperature** → Cooling Fan ON
- **Motion/Presence** → Room Light ON
- **Sound/IR Event** → Buzzer Blinks 3 times
- **Auto Reset** → All relays OFF after 2 seconds

## 📱 Telegram Integration

### **Setting Up Telegram Bot**
1. Create a bot via @BotFather on Telegram
2. Get your bot token
3. Get your chat ID
4. Update credentials in ESP32 code

### **Alert Messages**
- ⚠️ **Gas Leak**: "GAS Leak detected! Exhaust Fan ON"
- 🔥 **High Temperature**: "High Temperature detected! Cooling Fan ON"
- 👁️ **Motion**: "Motion Detected! Lights ON"
- 🔊 **Sound/IR**: "Sound/IR Triggered! Alarm Blinking"

## 🔧 Troubleshooting

### **Common Issues**
1. **Wi-Fi Connection Failed**
   - Check credentials
   - Verify network availability
   - Check signal strength

2. **ESP8266 ↔ ESP32 Communication Failed**
   - Verify ESP32 IP address
   - Check network connectivity
   - Ensure both devices on same network

3. **Sensors Not Reading**
   - Check wiring connections
   - Verify power supply
   - Test individual sensors

4. **Relays Not Responding**
   - Check relay module power
   - Verify GPIO connections
   - Test relay module independently

5. **I2C LCD Not Displaying**
   - Check I2C address (default: 0x27)
   - Verify SDA/SCL connections (D21/D22)
   - Check power supply (3.3V)
   - Use I2C scanner to find correct address

6. **ESP8266 Configuration Not Saving**
   - Check EEPROM size allocation
   - Verify web dashboard connection
   - Check serial monitor for errors

### **LED Status Guide**
- **Red LED ON**: Wi-Fi connection failed
- **White LED ON**: Wi-Fi connected
- **Green LED ON**: System operational
- **Blue LED ON (ESP8266)**: Connected to ESP32
- **Green LED ON (ESP32)**: ESP8266 communication active
- **LED Blinking**: System activity/alert

### **I2C LCD Display**
- **Line 1**: Shows ESP32 IP address
- **Line 2**: Shows ESP8266 connection status and IP
- **Updates**: Every 2 seconds automatically
- **I2C Address**: Default 0x27 (change if needed)
- **Connections**: SDA→D21, SCL→D22

## 📊 System Specifications

- **Operating Voltage**: 5V DC
- **Wi-Fi**: 802.11 b/g/n
- **Communication**: TCP/IP, HTTP
- **Storage**: EEPROM for state persistence
- **Refresh Rate**: 5 seconds (dashboard)
- **Alert Response**: < 1 second
- **Uptime Tracking**: Continuous
- **Log Buffer**: 2000 characters

## 🔒 Safety Features

- **Automatic Shutdown**: Relays auto-reset after alerts
- **Manual Override**: Manual control available
- **State Persistence**: Settings saved across reboots
- **Error Recovery**: Automatic Wi-Fi reconnection
- **Visual Feedback**: LED status indicators
- **Remote Monitoring**: Web dashboard + Telegram alerts

## 📈 Future Enhancements

- [ ] **Mobile App**: Native Android/iOS app
- [ ] **Database Integration**: Cloud storage for logs
- [ ] **Advanced Analytics**: Trend analysis and predictions
- [ ] **Multi-zone Support**: Multiple lab monitoring
- [ ] **Voice Alerts**: Audio notifications
- [ ] **Energy Monitoring**: Power consumption tracking
- [ ] **API Integration**: Third-party service integration

---

## 🤝 Contributing

Feel free to contribute to this project by:
- Reporting bugs
- Suggesting new features
- Improving documentation
- Optimizing code performance

## 📄 License

This project is open source and available under the MIT License.

---

**LabGuard+** - Making laboratory safety intelligent and automated! 🔬✨ 