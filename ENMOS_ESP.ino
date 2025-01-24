#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

#define WIFI_SSID "lime"
#define WIFI_PASSWORD "00000000"

const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_password = "";

SoftwareSerial DataSerial(12, 13);

const char* topic_voltage = "voltage_data";
const char* topic_frequency = "frequency_data";
const char* topic_temperature = "temperature_data";
const char* topic_humidity = "humidity_data";

AsyncMqttClient mqttClient;
Ticker mqttReconnectTimer;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
Ticker wifiReconnectTimer;

unsigned long previousMillis = 0;
const long interval = 10000;

String arrData[5];
String voltage, frequency, temperature, humidity, warning;
float volt1, freq1, temp1, hum1;

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println("Connected to Wi-Fi.");
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
  Serial.print("Publish acknowledged. packetId: ");
  Serial.println(packetId);
}

void sendWiFiStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    DataSerial.println("WIFI:1");
  } else {
    DataSerial.println("WIFI:0");
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
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCredentials(mqtt_user, mqtt_password);

  connectToWifi();
}

void loop() {
  // Send WiFi status
  sendWiFiStatus();

  String Data = "";
  while (DataSerial.available() > 0) {
    Data += char(DataSerial.read());
  }
  
  Data.trim();

  if (Data != "") {
    int index = 0;
    for (int i = 0; i <= Data.length(); i++) {
      char delimiter = '#';
      if (Data[i] != delimiter)
        arrData[index] += Data[i];
      else
        index++;
    }

    // Notification Alerts
    if (temp1 < 20) {
      warning = "Low temperature : " + temperature;
    }
    if (temp1 > 30) {
      warning = "High temperature : " + temperature;
    }
    if (hum1 < 40) {
      warning = "Low Humidity : " + humidity;
    }
    if (hum1 > 80) {
      warning = "High Humidity : " + humidity;
    }
    if (volt1 < 213) {
      warning = "Low voltage : " + voltage;
    }
    if (volt1 > 227) {
      warning = "High Voltage : " + voltage;
    }

    if (index == 4) {
      Serial.println("Voltage : " + arrData[1]);
      Serial.println("Frequency : " + arrData[2]);
      Serial.println("Temperature : " + arrData[3]);
      Serial.println("Humidity : " + arrData[4]);

      voltage = arrData[1];
      frequency = arrData[2];
      temperature = arrData[3];
      humidity = arrData[4];

      volt1 = arrData[1].toFloat();
      freq1 = arrData[2].toFloat();
      temp1 = arrData[3].toFloat();
      hum1 = arrData[4].toFloat();

      unsigned long currentMillis = millis();
      if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;

        mqttClient.publish(topic_temperature, 1, true, temperature.c_str());
        mqttClient.publish(topic_voltage, 1, true, voltage.c_str());
        mqttClient.publish(topic_frequency, 1, true, frequency.c_str());
        mqttClient.publish(topic_humidity, 1, true, humidity.c_str());
      }
    }

    delay(2500);
    
    // Reset data arrays
    for (int i = 0; i < 5; i++) {
      arrData[i] = "";
    }
  }
}
