#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

// WiFi credentials
#define WIFI_SSID "lime"
#define WIFI_PASSWORD "00000000"
#define Username "brokerTTH"
#define Password "brokerTTH"

// MQTT Broker settings
#define MQTT_HOST IPAddress(36, 95, 203, 54)
#define MQTT_PORT 1884

// MQTT Topics
#define MQTT_PUB_RECORD "ENMOSV2/records"
#define MQTT_PUB_TEMP "ENMOSV2/Txm3r8gHsxcdSqbWcKOpxjFi2mtenQ/resource/temp"
#define MQTT_PUB_HUM  "ENMOSV2/Txm3r8gHsxcdSqbWcKOpxjFi2mtenQ/resource/hum"
#define MQTT_PUB_VOLT "ENMOSV2/Txm3r8gHsxcdSqbWcKOpxjFi2mtenQ/resource/volt" 
#define MQTT_PUB_FREQ "ENMOSV2/Txm3r8gHsxcdSqbWcKOpxjFi2mtenQ/resource/freq"
#define MQTT_PUB_TIME "ENMOSV2/Txm3r8gHsxcdSqbWcKOpxjFi2mtenQ/resource/tim"
#define MQTT_PUB_WARNING "ENMOSV2/Warning"

SoftwareSerial DataSerial(12, 13);
AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;
WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

// Timing variables
unsigned long previousMillis = 0;
const long interval = 10000;
unsigned long lastWifiCheck = 0;
const long wifiCheckInterval = 2000;
int wifiReconnectAttempts = 0;

// Data variables
String arrData[5];
String volt, freq, temp, hum, tim, Name_ID, warning;
float volt1, freq1, temp1, hum1, tim1;

void setupWiFi() {
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
}

void connectToWifi() {
    Serial.println("Connecting to Wi-Fi...");
    setupWiFi();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
    Serial.println("Connected to Wi-Fi.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    connectToMqtt();
}

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
    Serial.println("Disconnected from Wi-Fi.");
    mqttReconnectTimer.detach();
    wifiReconnectTimer.once(2, connectToWifi);
}

void connectToMqtt() {
    Serial.println("Connecting to MQTT...");
    mqttClient.connect();
}

void onMqttConnect(bool sessionPresent) {
    Serial.println("Connected to MQTT.");
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    Serial.println("Disconnected from MQTT.");
    if (WiFi.isConnected()) {
        mqttReconnectTimer.once(2, connectToMqtt);
    }
}

void onMqttPublish(uint16_t packetId) {
    Serial.printf("Publish acknowledged. Packet ID: %i\n", packetId);
}

void checkWarnings() {
    // Temperature warnings
    if (temp1 < 20) {
        warning = "Low temperature : " + temp;
    }
    if (temp1 > 30) {
        warning = "High temperature : " + temp;
    }
    // Humidity warnings
    if (hum1 < 40) {
        warning = "Low Humidity : " + hum;
    }
    if (hum1 > 80) {
        warning = "High Humidity : " + hum;
    }
    // Voltage warnings
    if (volt1 < 213) {
        warning = "Low voltage : " + volt;
    }
    if (volt1 > 227) {
        warning = "High Voltage : " + volt;
    }
}

void processData(String Data) {
    Data.trim();
    if (Data != "") {
        int index = 0;
        for (int i = 0; i <= Data.length(); i++) {
            if (Data[i] != '#')
                arrData[index] += Data[i];
            else
                index++;
        }
        
        if (index == 5) {
            temp = arrData[1];
            volt = arrData[3];
            freq = arrData[4];
            hum = arrData[2];
            tim = arrData[5];
            Name_ID = "Txm3r8gHsxcdSqbWcKOpxjFi2mtenQ";
            
            // Convert to float for checks
            temp1 = temp.toFloat();
            volt1 = volt.toFloat();
            freq1 = freq.toFloat();
            hum1 = hum.toFloat();
            tim1 = tim.toFloat();
            
            checkWarnings();
            
            unsigned long currentMillis = millis();
            if (currentMillis - previousMillis >= interval) {
                previousMillis = currentMillis;
                
                if (WiFi.status() == WL_CONNECTED) {
                    // Publish temperature
                    mqttClient.publish(MQTT_PUB_TEMP, 1, true, temp.c_str());
                    
                    // Publish voltage
                    mqttClient.publish(MQTT_PUB_VOLT, 1, true, volt.c_str());
                    
                    // Publish frequency
                    mqttClient.publish(MQTT_PUB_FREQ, 1, true, freq.c_str());
                    
                    // Publish humidity
                    mqttClient.publish(MQTT_PUB_HUM, 1, true, hum.c_str());
                    
                    // Publish timestamp
                    mqttClient.publish(MQTT_PUB_TIME, 1, true, tim.c_str());
                    
                    // Publish record
                    String data_record = Name_ID + "#" + temp + "#" + volt + "#" + freq + "#" + hum;
                    mqttClient.publish(MQTT_PUB_RECORD, 1, true, data_record.c_str());
                    
                    // Publish warning if any
                    if (warning != "") {
                        String data_warning = Name_ID + "#" + warning;
                        mqttClient.publish(MQTT_PUB_WARNING, 1, true, data_warning.c_str());
                    }
                }
            }
        }
        
        // Clear the array for next reading
        for (int i = 0; i < 5; i++) {
            arrData[i] = "";
        }
    }
}

void setup() {
    Serial.begin(9600);
    DataSerial.begin(19200);
    
    wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
    
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onPublish(onMqttPublish);
    mqttClient.setServer(MQTT_HOST, MQTT_PORT);
    mqttClient.setCredentials(Username, Password);
    
    connectToWifi();
}

void loop() {
    String Data = "";
    while (DataSerial.available() > 0) {
        Data += char(DataSerial.read());
    }
    
    if (Data != "") {
        processData(Data);
        delay(100);
    }
}
