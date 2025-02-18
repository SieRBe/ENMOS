#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <AsyncMqttClient.h>

#define WIFI_SSID "id"        // Ganti dengan nama WiFi Anda
#define WIFI_PASSWORD "pw" // Ganti dengan password WiFi Anda

const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* mqtt_user = "";  // Optional
const char* mqtt_password = "";  // Optional

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

unsigned long previousMillis = 0;  // Stores last time temperature was published
const long interval = 10000;       // Interval at which to publish sensor readings

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
  mqttReconnectTimer.detach();  // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
  wifiReconnectTimer.once(2, connectToWifi);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected()) {
    mqttReconnectTimer.once(2, connectToMqtt);
  }
}

void onMqttPublish(uint16_t packetId) {
  Serial.print("Publish acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
}

void handleIncomingData() {
  if (Serial.available()) {
    String data = Serial.readStringUntil('\n');
    data.trim();
    
    if (data == "CHECK_WIFI") {
      Serial.print("WIFI:");
      Serial.println(WiFi.status() == WL_CONNECTED ? "1" : "0");
    }
    else if (data.startsWith("DATA#")) {
      // Coba kirim ke server
      if (sendToServer(data)) {
        Serial.println("ACK");  // Konfirmasi data berhasil dikirim
      } else {
        Serial.println("NACK"); // Beritahu WIO untuk mencoba lagi
      }
    }
  }
}

String arrData[3];

String voltage, frequency, temperature, humidity, Name_ID, warning;
float volt1, freq1, temp1, hum1;

unsigned long loopStartTime = millis();

void setup() {
  Serial.begin(9600);
  DataSerial.begin(19200);


  Serial.println();
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onPublish(onMqttPublish);
  mqttClient.setServer(mqtt_server, mqtt_port);
  // If your broker requires authentication (username and password), set them below
  mqttClient.setCredentials(mqtt_user, mqtt_password);

  connectToWifi();
}

void loop() {
  // Send WiFi status BEFORE handling other data
  if (WiFi.status() == WL_CONNECTED) {
    DataSerial.println("WIFI:1");
  } else {
    DataSerial.println("WIFI:0");
  }
  
  String Data = "";
  while (DataSerial.available() > 0) {
    Data += char(DataSerial.read());
  }
  
  // Add debug print
  Serial.print("WiFi Status: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");

  // Send WiFi status every second
  String wifiStatus = "WIFI:" + String(WiFi.status() == WL_CONNECTED ? 1 : 0);
  Serial.println(wifiStatus);
  delay(1000);

  //buang spasi datanya
  Data.trim();



  if (Data != "") {
    //parsing data (pecah data)
    int index = 0;
    for (int i = 0; i <= Data.length(); i++) {
      char delimiter = '#';
      if (Data[i] != delimiter)
        arrData[index] += Data[i];
      else
        index++;  //variabel index bertambah 1
    }

//Notification Alert
//ALERT TEMP
if (temp1 < 20) {
  warning = "Low temperature : " + temperature;
}

if (temp1 > 30) {
  warning = "High temperature : " + temperature;
}
//ALERT HUMD
if (hum1 < 40) {
  warning = "Low Humidity : " + humidity;
}
if (hum1 > 80) {
  warning = "High Humidity : " + humidity;
}

//ALERT VOLTAGE
if (volt1 < 213) {
warning = "Low voltage : " + voltage;
}
if (volt1 > 227) {
warning = "High Voltage : " + voltage;
}



    //pastikan bahwa data yang dikirim lengkap (ldr, temp, hum)
    if (index == 4) {
      //tampilkan nilai sensor ke serial monitor
      Serial.println("Voltage : " + arrData[1]);  //volt
      Serial.println("Frequency    : " + arrData[2]);  //Hz
      Serial.println("Temperature    : " + arrData[3]);  //temp
      Serial.println("Humidity    : " + arrData[4]);  //humd
      Serial.println();

      //isi variabel yang akan dikirim
      voltage  = arrData[1];
      frequency  = arrData[2];
      temperature  = arrData[3];
      humidity   = arrData[4];
  
      volt1 = arrData[1].toFloat();
      freq1 = arrData[2].toFloat();
      temp1 = arrData[3].toFloat();
      hum1 = arrData[4].toFloat();

      unsigned long currentMillis = millis();
      // Every X number of seconds (interval = 10 seconds)
      // it publishes a new MQTT message
      if (currentMillis - previousMillis >= interval) {
        // Save the last time a new reading was published
        previousMillis = currentMillis;

        String data_temp = temperature;
        uint16_t packetIdPub1 = mqttClient.publish(topic_temperature, 1, true, data_temp.c_str());
        Serial.printf("Publishing on topic %s at QoS 1, packetId: %i", topic_temperature, packetIdPub1);
        Serial.printf("Message: %.2f \n", data_temp.c_str());

        String data_volt = voltage;
        uint16_t packetIdPub2 = mqttClient.publish(topic_voltage, 1, true, data_volt.c_str());
        Serial.printf("Publishing on topic %s at QoS 1, packetId: %i", topic_voltage, packetIdPub2);
        Serial.printf("Message: %.2f \n", data_volt.c_str());

        String data_freq = frequency;
        uint16_t packetIdPub3 = mqttClient.publish(topic_frequency, 1, true, data_freq.c_str());
        Serial.printf("Publishing on topic %s at QoS 1, packetId: %i", topic_frequency, packetIdPub3);
        Serial.printf("Message: %.2f \n", data_freq.c_str());

        String data_hum = humidity;
        uint16_t packetIdPub4 = mqttClient.publish(topic_humidity, 1, true, data_hum.c_str());
        Serial.printf("Publishing on topic %s at QoS 1, packetId: %i", topic_humidity, packetIdPub4);
        Serial.printf("Message: %.2f \n", data_hum.c_str());

      }
    }

    delay(2500);


      
    arrData[0] = "";
    arrData[1] = "";
    arrData[2] = "";
    arrData[3] = "";
    arrData[4] = "";
    

  }

}
