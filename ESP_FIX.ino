#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

// WiFi credentials
#define WIFI_SSID "lime"
#define WIFI_PASSWORD "00000000"
#define MAX_WIFI_RECONNECT_ATTEMPTS 10
#define WIFI_RECONNECT_DELAY 1000 // 1 detik

// MQTT settings
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_password = "";

// Topics
const char* topic_voltage = "voltage_data";
const char* topic_frequency = "frequency_data";
const char* topic_temperature = "temperature_data";
const char* topic_humidity = "humidity_data";
const char* topic_timestamp = "timestamp_data";

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
const long wifiCheckInterval = 2000; // Check WiFi every 2 seconds
int wifiReconnectAttempts = 0;

// Data arrays
String arrData[5];
String voltage, frequency, temperature, humidity, timestamp;

void setupWiFi() {
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP); // Disable WiFi sleep mode
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    
    // Optional: Set static IP if needed
    // IPAddress staticIP(192, 168, 1, 200);
    // IPAddress gateway(192, 168, 1, 1);
    // IPAddress subnet(255, 255, 255, 0);
    // IPAddress dns(8, 8, 8, 8);
    // WiFi.config(staticIP, gateway, subnet, dns);
}

void connectToWifi() {
    Serial.println("Connecting to Wi-Fi...");
    setupWiFi();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    // Wait for connection with timeout
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConnected to WiFi");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        wifiReconnectAttempts = 0;
        connectToMqtt(); // Connect to MQTT after successful WiFi connection
    } else {
        Serial.println("\nFailed to connect to WiFi");
    }
}

void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi connection lost. Attempting to reconnect...");
        
        if (wifiReconnectAttempts < MAX_WIFI_RECONNECT_ATTEMPTS) {
            WiFi.disconnect();
            delay(100);
            WiFi.reconnect();
            wifiReconnectAttempts++;
            
            // Wait briefly to check if reconnection was successful
            int checkAttempts = 0;
            while (WiFi.status() != WL_CONNECTED && checkAttempts < 10) {
                delay(100);
                checkAttempts++;
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("WiFi reconnected successfully");
                wifiReconnectAttempts = 0;
                connectToMqtt(); // Reconnect to MQTT after successful WiFi reconnection
            }
        } else {
            Serial.println("Maximum reconnection attempts reached. Restarting ESP...");
            ESP.restart(); // Restart ESP if unable to reconnect after maximum attempts
        }
    } else {
        wifiReconnectAttempts = 0; // Reset counter when connected
    }
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
    Serial.println("Connected to Wi-Fi.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    wifiReconnectAttempts = 0;
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
            voltage = arrData[1];
            frequency = arrData[2];
            temperature = arrData[3];
            humidity = arrData[4];
            timestamp = arrData[5];
            
            unsigned long currentMillis = millis();
            if (currentMillis - previousMillis >= interval) {
                previousMillis = currentMillis;
                
                // Only publish if WiFi is connected
                if (WiFi.status() == WL_CONNECTED) {
                    mqttClient.publish(topic_temperature, 1, true, temperature.c_str());
                    mqttClient.publish(topic_voltage, 1, true, voltage.c_str());
                    mqttClient.publish(topic_frequency, 1, true, frequency.c_str());
                    mqttClient.publish(topic_humidity, 1, true, humidity.c_str());
                    mqttClient.publish(topic_timestamp, 1, true, timestamp.c_str());
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
    
    // Register WiFi event handlers
    wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
    wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
    
    // Configure MQTT client
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onPublish(onMqttPublish);
    mqttClient.setServer(mqtt_server, mqtt_port);
    mqttClient.setCredentials(mqtt_user, mqtt_password);
    
    // Initial WiFi connection
    connectToWifi();
}

void loop() {
    // Check WiFi connection status frequently
    unsigned long currentMillis = millis();
    if (currentMillis - lastWifiCheck >= wifiCheckInterval) {
        lastWifiCheck = currentMillis;
        checkWiFiConnection();
    }
    
    // Read and process data from serial
    String Data = "";
    while (DataSerial.available() > 0) {
        Data += char(DataSerial.read());
    }
    
    if (Data != "") {
        processData(Data);
        delay(100); // Short delay to prevent CPU hogging
    }
}
