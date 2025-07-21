#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "HX711.h"
#include <ESP8266Firebase.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>


#define WIFI_SSID "DESKTOP-MGI8FHS_4192"
#define WIFI_PASSWORD "8q2103!K"
#define FIREBASE_URL "https://grainguard-c1dbd-default-rtdb.firebaseio.com/"


#define USER_ID "YseVsgVE2hXlcmAfH9r7gt6uZ312"
#define CONTAINER_NAME "sugar" 

Firebase firebase(FIREBASE_URL);


const int DATA_PIN = 12; 
const int CLOCK_PIN = 13; 

HX711 loadCell;

// --- Global Variables for Calibration ---
static float calibration_factor = -1000;

// --- NTP Client Configuration ---
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);


float last_uploaded_weight = -999.0;
const float WEIGHT_CHANGE_THRESHOLD = 1.0;

// --- Function Prototypes ---
void initializeScale();
void showInitialReadings();
void checkUserInput();
void displayDataAndUpload();
void connectToWiFi();
void testFirebaseConnection();
void initializeNTPClient();

void setup() {
  Serial.begin(9600);
  while (!Serial);
  Serial.println("\n--- ESP8266 Weight to Firebase Tool ---");

  connectToWiFi();
  initializeNTPClient();
  testFirebaseConnection();
  initializeScale();
  showInitialReadings();

  Serial.println("\nTaring the scale (setting current reading as zero)...");
  loadCell.tare(20);
  loadCell.set_scale(calibration_factor);

  Serial.println("\n--- Ready for Weight Measurement ---");
  Serial.println("Place known weight to calibrate.");
  Serial.println("Commands: '+' = increase factor by 100, '-' = decrease factor by 100, 't' = tare.");
}

void loop() {
  displayDataAndUpload();
  checkUserInput();
  delay(2000);
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println("WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void initializeNTPClient() {
  Serial.println("Initializing NTP client...");
  timeClient.begin();
  while (!timeClient.update()) {
    Serial.print("Waiting for NTP sync...");
    delay(1000);
  }
  Serial.println("NTP time synchronized!");
}

void testFirebaseConnection() {
  Serial.println("Attempting to connect to Firebase...");
  if (firebase.setString("ConnectionTest", "Testing")) {
    Serial.println("Firebase write successful!");
    String readValue = firebase.getString("ConnectionTest");
    if (readValue == "Testing") {
      Serial.println("Firebase read successful!");
      Serial.println("Firebase connection verified!");
      String deviceStatusPath = String("users/") + USER_ID + "/deviceStatus";
      firebase.setString(deviceStatusPath, "Device Connected");
    } else {
      Serial.println("Failed to read from Firebase - connection issue.");
      Serial.print("Received value: ");
      Serial.println(readValue);
    }
  } else {
    Serial.println("Failed to write to Firebase - check URL/rules.");
  }
}

void initializeScale() {
  pinMode(CLOCK_PIN, OUTPUT);
  digitalWrite(CLOCK_PIN, LOW);

  loadCell.begin(DATA_PIN, CLOCK_PIN);
  loadCell.power_down();
  delay(500);
  loadCell.power_up();
  Serial.println("HX711 scale initialized.");
}

void showInitialReadings() {
  Serial.println("Reading initial raw values (no weight)...");
  for (byte count = 0; count < 5; count++) {
    Serial.print("Raw reading ");
    Serial.print(count + 1);
    Serial.print(": ");
    Serial.println(loadCell.read());
    delay(200);
  }
}

void checkUserInput() {
  if (Serial.available()) {
    char command = Serial.read();
    switch (command) {
      case '+':
        calibration_factor += 100;
        loadCell.set_scale(calibration_factor);
        Serial.print("New factor: ");
        Serial.println(calibration_factor);
        break;
      case '-':
        calibration_factor -= 100;
        loadCell.set_scale(calibration_factor);
        Serial.print("New factor: ");
        Serial.println(calibration_factor);
        break;
      case 't':
        Serial.println("Taring...");
        loadCell.tare(20);
        Serial.println("Tare complete.");
        last_uploaded_weight = -999.0;
        break;
      default:
        break;
    }
  }
}

void displayDataAndUpload() {
  float units = loadCell.get_units(10);
  float weight_grams = units * 3.12;

  Serial.print("Weight: ");
  Serial.print(weight_grams, 2);
  Serial.print("g");
  Serial.print(" | Factor: ");
  Serial.println(calibration_factor);

  // --- Construct the path without container ID ---
  String containerPath = String("users/") + USER_ID + "/containers/" + CONTAINER_NAME;

 
  if (WiFi.status() == WL_CONNECTED) {
    String currentWeightPath = containerPath + "/currentWeight";
    if (!firebase.setFloat(currentWeightPath, weight_grams)) {
      Serial.println("Firebase 'currentWeight' upload failed!");
    }
  } else {
    Serial.println("WiFi disconnected. Attempting to reconnect...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    delay(1000);
  }

 
  if (WiFi.status() == WL_CONNECTED && (abs(weight_grams - last_uploaded_weight) >= WEIGHT_CHANGE_THRESHOLD || last_uploaded_weight == -999.0)) {
    timeClient.update();
    unsigned long epochTime = timeClient.getEpochTime();

    char timestampBuffer[25];
    time_t rawtime = epochTime;
    struct tm * ti = localtime(&rawtime);

    sprintf(timestampBuffer, "%04d-%02d-%02d_%02d-%02d-%02d",
            ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
            ti->tm_hour, ti->tm_min, ti->tm_sec);

    String historyPath = containerPath + "/weightHistory/" + String(timestampBuffer);

    if (firebase.setFloat(historyPath, weight_grams)) {
      Serial.print("History uploaded: ");
      Serial.print(timestampBuffer);
      Serial.print(" -> ");
      Serial.print(weight_grams, 2);
      Serial.println("g");
      last_uploaded_weight = weight_grams;
    } else {
      Serial.println("Firebase 'weightHistory' upload failed!");
    }
  }
}