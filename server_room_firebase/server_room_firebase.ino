// ============================================================
//  SERVER ROOM MONITOR — ESP32-C6 WROOM-1
//  Firebase Realtime Database (live) + Firestore (istoric)
// ============================================================

#include <DHT.h>
#include <Wire.h>
#include <MPU6050.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>

// Firebase helper tokens
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// ============================================================
//  CONFIGURARE PINI
// ============================================================
#define DHT_PIN       4
#define DHT_TYPE      DHT22
#define MQ3_PIN       0
#define RELAY_PIN     23
#define LED_GREEN     18
#define LED_YELLOW    19
#define LED_RED       20
#define BUZZER_PIN    21

// ============================================================
//  PRAGURI
// ============================================================
#define TEMP_WARN       28.0f
#define TEMP_CRITICAL   32.0f
#define HUM_WARN        70.0f
#define GAS_WARN        3000
#define VIBRATION_WARN  0.8f

// ============================================================
//  WIFI
// ============================================================
const char* WIFI_SSID = "Robitza's iPhone";
const char* WIFI_PASS = "1panala8";

// ============================================================
//  FIREBASE — cheile tale
// ============================================================
#define API_KEY         "AIzaSyDEnIozeZ94N2l5Sk8eWch1EJsd0JW4zow"
#define DATABASE_URL    "https://server-room-digital-twin-default-rtdb.europe-west1.firebasedatabase.app"
#define PROJECT_ID      "server-room-digital-twin"

// ============================================================
//  OBIECTE
// ============================================================
DHT          dht(DHT_PIN, DHT_TYPE);
MPU6050      mpu;
FirebaseData fbdo;
FirebaseData fbdoFirestore;
FirebaseAuth auth;
FirebaseConfig config;

float accelBaseX = 0, accelBaseY = 0, accelBaseZ = 0;

unsigned long lastSend     = 0;
unsigned long lastFirestore = 0;
const long SEND_INTERVAL       = 2000;   // Realtime DB: la 2 secunde
const long FIRESTORE_INTERVAL  = 30000;  // Firestore istoric: la 30 secunde

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Server Room Monitor + Firebase ===\n");

  pinMode(RELAY_PIN,  OUTPUT);
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED,    OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(RELAY_PIN,  LOW);
  digitalWrite(LED_GREEN,  LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED,    LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // DHT22
  dht.begin();
  Serial.println("[OK] DHT22 initializat.");

  // MPU6050
  delay(2000);
  Wire.begin(8, 9);
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("[EROARE] MPU6050 nu a fost gasit!");
  } else {
    Serial.println("[OK] MPU6050 conectat.");
    calibrateMPU();
  }

  // WiFi
  connectWifi();

  // Firebase config
  config.api_key           = API_KEY;
  config.database_url      = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;

  // Autentificare anonima (fara user/pass)
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("[OK] Firebase initializat.");

  // Semnal pornire
  buzzerBeep(2);
  digitalWrite(LED_GREEN, HIGH);
  Serial.println("\n[OK] Sistem pornit. Incep citirile...\n");
  Serial.println("--------------------------------------------");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  unsigned long now = millis();
  if (now - lastSend < SEND_INTERVAL) return;
  lastSend = now;

  // ---- Citire senzori ----
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();
  int   gas  = analogRead(MQ3_PIN);

  int16_t ax, ay, az, gx, gy, gz;
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
  float axG = ax / 16384.0f;
  float ayG = ay / 16384.0f;
  float azG = az / 16384.0f;
  float vibration = sqrt(
    pow(axG - accelBaseX, 2) +
    pow(ayG - accelBaseY, 2) +
    pow(azG - accelBaseZ, 2)
  );

  // ---- Evaluare stare ----
  bool critical = false;
  bool warn     = false;
  String alerte = "";

  if (!isnan(temp) && temp >= TEMP_CRITICAL) {
    critical = true;
    alerte += "TEMP_CRITICA ";
  } else if (!isnan(temp) && temp >= TEMP_WARN) {
    warn = true;
    alerte += "TEMP_WARN ";
  }
  if (!isnan(hum) && hum >= HUM_WARN) {
    warn = true;
    alerte += "HUM_WARN ";
  }
  if (gas >= GAS_WARN) {
    critical = true;
    alerte += "GAZ_DETECTAT ";
  }
  if (vibration >= VIBRATION_WARN) {
    critical = true;
    alerte += "VIBRATII ";
  }

  String stare = critical ? "CRITIC" : (warn ? "WARN" : "OK");

  // ---- Serial Monitor ----
  Serial.println("--- Citire noua ---");
  Serial.print("  Temperatura : "); Serial.print(isnan(temp) ? -1 : temp); Serial.println(" °C");
  Serial.print("  Umiditate   : "); Serial.print(isnan(hum)  ? -1 : hum);  Serial.println(" %");
  Serial.print("  Gaz (raw)   : "); Serial.println(gas);
  Serial.print("  Vibratie    : "); Serial.print(vibration, 3); Serial.println(" g");
  Serial.print("  Stare       : "); Serial.println(stare);
  if (alerte.length() > 0) Serial.println("  Alerte: " + alerte);
  Serial.println("--------------------------------------------");

  // ---- Actuatori ----
  digitalWrite(LED_GREEN,  LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED,    LOW);
  digitalWrite(RELAY_PIN,  LOW);
  digitalWrite(BUZZER_PIN, LOW);

  if (critical) {
    digitalWrite(LED_RED,    HIGH);
    digitalWrite(RELAY_PIN,  HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
  } else if (warn) {
    digitalWrite(LED_YELLOW, HIGH);
    digitalWrite(RELAY_PIN,  HIGH);
  } else {
    digitalWrite(LED_GREEN,  HIGH);
  }

  // ---- Firebase Realtime Database (live, pentru Unity) ----
  if (Firebase.ready()) {
    FirebaseJson jsonLive;
    jsonLive.set("temp",      isnan(temp) ? -1 : temp);
    jsonLive.set("humidity",  isnan(hum)  ? -1 : hum);
    jsonLive.set("gas_raw",   gas);
    jsonLive.set("vibration", vibration);
    jsonLive.set("state",     stare.c_str());
    jsonLive.set("alerts",    alerte.c_str());
    jsonLive.set("uptime_s",  (int)(millis() / 1000));

    if (Firebase.RTDB.setJSON(&fbdo, "/serverroom/live", &jsonLive)) {
      Serial.println("[Firebase RTDB] Date live trimise OK");
    } else {
      Serial.println("[Firebase RTDB] Eroare: " + fbdo.errorReason());
    }
  }

  // ---- Firestore (istoric, la fiecare 30 secunde) ----
  if (Firebase.ready() && (now - lastFirestore >= FIRESTORE_INTERVAL)) {
    lastFirestore = now;

    FirebaseJson content;
    content.set("fields/temp/doubleValue",      isnan(temp) ? -1 : temp);
    content.set("fields/humidity/doubleValue",  isnan(hum)  ? -1 : hum);
    content.set("fields/gas_raw/integerValue",  gas);
    content.set("fields/vibration/doubleValue", vibration);
    content.set("fields/state/stringValue",     stare.c_str());
    content.set("fields/alerts/stringValue",    alerte.c_str());
    content.set("fields/timestamp/integerValue",(int)(millis() / 1000));

    if (Firebase.Firestore.createDocument(
          &fbdoFirestore,
          PROJECT_ID,
          "",                  // location default
          "history",           // collection
          "",                  // auto document ID
          content.raw(),
          ""
        )) {
      Serial.println("[Firestore] Document istoric salvat OK");
    } else {
      Serial.println("[Firestore] Eroare: " + fbdoFirestore.errorReason());
    }
  }
}

// ============================================================
//  CALIBRARE MPU
// ============================================================
void calibrateMPU() {
  Serial.print("  Calibrare MPU6050");
  float sumX = 0, sumY = 0, sumZ = 0;
  int16_t ax, ay, az, gx, gy, gz;
  for (int i = 0; i < 50; i++) {
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    sumX += ax / 16384.0f;
    sumY += ay / 16384.0f;
    sumZ += az / 16384.0f;
    delay(10);
    if (i % 10 == 0) Serial.print(".");
  }
  accelBaseX = sumX / 50.0f;
  accelBaseY = sumY / 50.0f;
  accelBaseZ = sumZ / 50.0f;
  Serial.println(" DONE");
}

// ============================================================
//  BUZZER
// ============================================================
void buzzerBeep(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
    delay(150);
  }
}

// ============================================================
//  WIFI
// ============================================================
void connectWifi() {
  Serial.print("Conectare WiFi la ");
  Serial.print(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[OK] WiFi conectat! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n[WARN] WiFi esuat — continuu fara retea.");
  }
}
