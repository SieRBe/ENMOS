#include <SD.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <ModbusMaster.h>
#include <SHT31.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include "Wire.h"
#include <rpcWiFi.h>

RTC_SAMD51 rtc;
SHT31 sht;
SoftwareSerial serial(D2, D3);      // Untuk komunikasi dengan ESP
SoftwareSerial SerialMod(D1, D0);   // Untuk Modbus
ModbusMaster node;

// Konfigurasi WiFi
const char* ssid = "lime";
const char* password = "00000000";
bool wifiConnected = false;
unsigned long previousWiFiCheck = 0;
const long WIFI_CHECK_INTERVAL = 14000;

// Interval waktu
unsigned long previousMillisSensor = 0;
const long INTERVAL_SENSOR = 20000; // 20 detik untuk pembacaan sensor
unsigned long previousMillisCSV = 0;
const long CSV_SEND_INTERVAL = 20000;

char filename[] = "/jkw.csv";

void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        WiFi.disconnect();
        WiFi.begin(ssid, password);
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(500);
            attempts++;
        }
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            Serial.println("WiFi connected");
        } else {
            Serial.println("WiFi connection failed");
        }
    } else {
        wifiConnected = true;
    }
}

void saveToCSV(float temperature, float humidity, float voltage, float frequency, const DateTime& now) {
    if (!SD.exists(filename)) {
        File headerFile = SD.open(filename, FILE_WRITE);
        if (headerFile) {
            headerFile.println("Timestamp;Temperature;Humidity;Voltage;Frequency");
            headerFile.close();
        }
    }
    File file = SD.open(filename, FILE_WRITE);
    if (file) {
        file.printf("%04d/%02d/%02d %02d:%02d:%02d;%.2f;%.2f;%.2f;%.2f\n",
                    now.year(), now.month(), now.day(),
                    now.hour(), now.minute(), now.second(),
                    temperature, humidity, voltage, frequency);
        file.close();
        Serial.println("Data saved to CSV");
    }
}

void sendToESP(const String& data) {
    serial.println(data);
    Serial.println("Data sent to ESP: " + data);
}

void sendDataFromCSV() {
    File file = SD.open(filename);
    if (file && file.available()) {
        file.readStringUntil('\n'); // Skip header
        String remainingData;
        while (file.available()) {
            String line = file.readStringUntil('\n');
            if (line.length() > 0) {
                int pos1 = line.indexOf(';');
                int pos2 = line.indexOf(';', pos1 + 1);
                int pos3 = line.indexOf(';', pos2 + 1);
                int pos4 = line.indexOf(';', pos3 + 1);

                String timestamp = line.substring(0, pos1);
                String temp = line.substring(pos1 + 1, pos2);
                String hum = line.substring(pos2 + 1, pos3);
                String volt = line.substring(pos3 + 1, pos4);
                String freq = line.substring(pos4 + 1);

                String data = "1#" + volt + "#" + freq + "#" + temp + "#" + hum + "#" + timestamp;
                sendToESP(data);
            }
        }
        file.close();
        SD.remove(filename);
        Serial.println("All data sent and CSV cleared");
    }
}

void setup() {
    Serial.begin(115200);
    serial.begin(19200);
    SerialMod.begin(9600);
    Wire.begin();
    sht.begin(0x44);
    rtc.begin();

    if (!SD.begin(SDCARD_SS_PIN)) {
        Serial.println("SD card initialization failed!");
        return;
    }
    Serial.println("SD card initialized.");

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }
    wifiConnected = WiFi.status() == WL_CONNECTED;
    Serial.println(wifiConnected ? "WiFi connected" : "WiFi connection failed");
}

void loop() {
    unsigned long currentMillis = millis();

    if (currentMillis - previousWiFiCheck >= WIFI_CHECK_INTERVAL) {
        previousWiFiCheck = currentMillis;
        checkWiFiConnection();
    }

    if (currentMillis - previousMillisSensor >= INTERVAL_SENSOR) {
        previousMillisSensor = currentMillis;

        sht.read();
        float temperature = sht.getTemperature();
        float humidity = sht.getHumidity();
        DateTime now = rtc.now();

        uint8_t result = node.readHoldingRegisters(0x0002, 10);
        float voltage = result == node.ku8MBSuccess ? node.getResponseBuffer(0x00) / 100.0 : 0;
        float frequency = result == node.ku8MBSuccess ? node.getResponseBuffer(0x09) / 100.0 : 0;

        if (!wifiConnected) {
            saveToCSV(temperature, humidity, voltage, frequency, now);
        } else {
            String data = String("1#") + String(voltage, 2) + "#" + String(frequency, 2) + "#" + 
                          String(temperature, 2) + "#" + String(humidity, 2);
            sendToESP(data);
        }
    }

    if (currentMillis - previousMillisCSV >= CSV_SEND_INTERVAL && wifiConnected) {
        previousMillisCSV = currentMillis;
        sendDataFromCSV();
    }
}
