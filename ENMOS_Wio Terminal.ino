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
SoftwareSerial serial(D2, D3);      // For data transmission
SoftwareSerial SerialMod(D1, D0);   // For Modbus
ModbusMaster node;

// WiFi Configuration
const char* ssid = "lime";
const char* password = "00000000";
bool wifiConnected = false;
unsigned long previousWiFiCheck = 0;
const long WIFI_CHECK_INTERVAL = 14000;  // Check WiFi every 14 seconds

typedef struct {
  float V;
  float F;
} READING;

unsigned long previousMillis2 = 0;
const long INTERVAL = 20000;        // Sensor reading interval (20 seconds)
unsigned long previousMillisSensor = 0;

char filename[25] = "/jkw.csv";

void checkWiFiConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        wifiConnected = false;
        Serial.println("WiFi disconnected - Switching to CSV storage mode");
        WiFi.disconnect();
        WiFi.begin(ssid, password);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(500);
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            wifiConnected = true;
            Serial.println("WiFi reconnected - Switching to direct transmission mode");
        }
    } else {
        wifiConnected = true;
    }
}

void saveToCSV(DateTime now, float temperature, float humidity, float voltage, float frequency) {
    if (!SD.exists(filename)) {
        File headerFile = SD.open(filename, FILE_WRITE);
        if (headerFile) {
            headerFile.println("Timestamp,Temperature,Humidity,Voltage,Frequency");
            headerFile.close();
        }
    }
    
    File file = SD.open(filename, FILE_WRITE);
    if (file) {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), 
                 "%04d/%02d/%02d %02d:%02d:%02d,%.2f,%.2f,%.2f,%.2f\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second(),
                  temperature, humidity, voltage, frequency);
        
        file.print(buffer);
        file.close();
        Serial.println("Data saved to CSV: " + String(buffer));
    } else {
        Serial.println("Error opening CSV file for writing");
    }
}

void setup() {
    Serial.begin(115200);
    serial.begin(19200);
    SerialMod.begin(9600);
    
    Wire.begin();
    sht.begin(0x44);
    rtc.begin();
    
    DateTime now = DateTime(F(__DATE__), F(__TIME__));
    rtc.adjust(now);
    
    node.begin(17, SerialMod);
    
    if (!SD.begin(SDCARD_SS_PIN)) {
        Serial.println("SD card initialization failed!");
        return;
    }
    Serial.println("SD card initialized.");

    // Initialize WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("Connecting to WiFi...");
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("WiFi connected - Operating in direct transmission mode");
    } else {
        Serial.println("WiFi connection failed - Operating in CSV storage mode");
    }
}

void loop() {
    unsigned long currentMillis = millis();

    // Check WiFi status every interval
    if (currentMillis - previousWiFiCheck >= WIFI_CHECK_INTERVAL) {
        previousWiFiCheck = currentMillis;
        checkWiFiConnection();
    }

    // Read sensors at specified interval
    if (currentMillis - previousMillisSensor >= INTERVAL) {
        previousMillisSensor = currentMillis;
        Serial.println("Reading sensors...");
        
        // Read SHT31 sensor
        sht.read();
        float temperature = sht.getTemperature();
        float humidity = sht.getHumidity();
        
        DateTime now = rtc.now();
        
        // Read Modbus data
        READING r;
        uint8_t result = node.readHoldingRegisters(0002, 10);
        if (result == node.ku8MBSuccess) {
            r.V = (float)node.getResponseBuffer(0x00) / 100;
            r.F = (float)node.getResponseBuffer(0x09) / 100;
        } else {
            r.V = 0;
            r.F = 0;
            Serial.println("Modbus read failed");
        }

        if (!wifiConnected) {
            // Save to CSV when WiFi is disconnected
            saveToCSV(now, temperature, humidity, r.V, r.F);
        } else {
            // Direct transmission when WiFi is connected
            String datakirim = String("1#") + 
                             String(r.V, 2) + "#" +
                             String(r.F, 2) + "#" +
                             String(temperature, 2) + "#" +
                             String(humidity, 2);
            
            serial.println(datakirim);
            Serial.println("Data transmitted: " + datakirim);
        }
    }
}
