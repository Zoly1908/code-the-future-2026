// ============================================================
//  SERVER ROOM MONITOR — ESP32-C6 WROOM-1
//  Firebase Realtime Database (live) + Firestore (istoric)
//  Logica stare per senzor cu prioritate LED: Rosu > Galben > Verde
// ============================================================

#include <DHT.h>
#include <Wire.h>
#include <MPU6050.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>

#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <time.h>
#define NTP_SERVER  "pool.ntp.org"
#define UTC_OFFSET  7200  // Romania UTC+2 vara, pune 3600 iarna

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
#define TEMP_WARN       28.0f   // galben + beep incet + fan
#define TEMP_CRITICAL   32.0f   // rosu  + beep rapid + fan
#define HUM_WARN        70.0f   // galben (fara buzzer, fara fan)
#define GAS_WARN        2800    // rosu  + beep rapid
#define VIBRATION_WARN  0.8f    // rosu  + beep rapid



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

unsigned long lastSend      = 0;
unsigned long lastFirestore = 0;
const long SEND_INTERVAL      = 2000;
const long FIRESTORE_INTERVAL = 30000;

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

  dht.begin();
  Serial.println("[OK] DHT22 initializat.");

  delay(2000);
  Wire.begin(8, 9);
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("[EROARE] MPU6050 nu a fost gasit!");
  } else {
    Serial.println("[OK] MPU6050 conectat.");
    calibrateMPU();
  }

  connectWifi();

  // Sincronizare timp NTP
  configTime(UTC_OFFSET, 0, NTP_SERVER);
  Serial.print("Sincronizare NTP");
  struct tm ti;
  while (!getLocalTime(&ti)) { Serial.print("."); delay(500); }
  Serial.println(" OK");

  config.api_key               = API_KEY;
  config.database_url          = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;
  auth.user.email              = "";
  auth.user.password           = "";

  Firebase.signUp(&config, &auth, "", "");
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("[OK] Firebase initializat.");

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

  // ---- Evaluare stare per senzor ----
  // Prioritate LED: 2=rosu > 1=galben > 0=verde
  int  ledPriority = 0;
  bool fanOn       = false;
  bool buzzerRapid = false;
  bool buzzerIncet = false;
  String alerte    = "";
  String stare     = "OK";

  // TEMPERATURA
  if (!isnan(temp)) {
    if (temp > TEMP_CRITICAL) {
      // Temp>32 -> rosu, beep rapid, fan
      if (ledPriority < 2) ledPriority = 2;
      fanOn       = true;
      buzzerRapid = true;
      alerte += "TEMP_CRITICA ";
      stare = "CRITIC";
    } else if (temp > TEMP_WARN) {
      // Temp>28 -> galben, beep incet, fan
      if (ledPriority < 1) ledPriority = 1;
      fanOn       = true;
      buzzerIncet = true;
      alerte += "TEMP_WARN ";
      if (stare == "OK") stare = "WARN";
    }
  }

  // UMIDITATE
  if (!isnan(hum) && hum > HUM_WARN) {
    // Humidity>70 -> galben (fara fan, fara buzzer)
    if (ledPriority < 1) ledPriority = 1;
    alerte += "HUM_WARN ";
    if (stare == "OK") stare = "WARN";
  }

  // GAZ
  if (gas > GAS_WARN) {
    // Gas>2800 -> rosu, beep rapid
    if (ledPriority < 2) ledPriority = 2;
    buzzerRapid = true;
    alerte += "GAZ_DETECTAT ";
    stare = "CRITIC";
  }

  // VIBRATII
  if (vibration > VIBRATION_WARN) {
    // Gyro>0.8 -> rosu, beep rapid
    if (ledPriority < 2) ledPriority = 2;
    buzzerRapid = true;
    alerte += "VIBRATII ";
    stare = "CRITIC";
  }

  // ---- Serial Monitor ----
  Serial.println("--- Citire noua ---");
  Serial.print("  Temperatura : "); Serial.print(isnan(temp) ? -1 : temp); Serial.println(" °C");
  Serial.print("  Umiditate   : "); Serial.print(isnan(hum)  ? -1 : hum);  Serial.println(" %");
  Serial.print("  Gaz (raw)   : "); Serial.println(gas);
  Serial.print("  Vibratie    : "); Serial.print(vibration, 3); Serial.println(" g");
  Serial.print("  Stare       : "); Serial.println(stare);
  Serial.print("  LED         : "); Serial.println(ledPriority == 2 ? "ROSU" : ledPriority == 1 ? "GALBEN" : "VERDE");
  if (alerte.length() > 0) Serial.println("  Alerte: " + alerte);
  Serial.println("--------------------------------------------");

  // ---- Actuatori ----
  digitalWrite(LED_GREEN,  LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED,    LOW);
  digitalWrite(RELAY_PIN,  LOW);
  digitalWrite(BUZZER_PIN, LOW);

  // LED — doar cel mai prioritar
  if      (ledPriority == 2) digitalWrite(LED_RED,    HIGH);
  else if (ledPriority == 1) digitalWrite(LED_YELLOW, HIGH);
  else                       digitalWrite(LED_GREEN,  HIGH);

  // Fan
  if (fanOn) digitalWrite(RELAY_PIN, HIGH);

  // Buzzer — rapid are prioritate peste incet
  if (buzzerRapid) {
    for (int i = 0; i < 3; i++) {
      digitalWrite(BUZZER_PIN, HIGH); delay(80);
      digitalWrite(BUZZER_PIN, LOW);  delay(80);
    }
  } else if (buzzerIncet) {
    digitalWrite(BUZZER_PIN, HIGH); delay(400);
    digitalWrite(BUZZER_PIN, LOW);
  }

  // ---- Firebase Realtime Database ----
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

  // ---- Firestore (istoric la 30s) ----
  if (Firebase.ready() && (now - lastFirestore >= FIRESTORE_INTERVAL)) {
    lastFirestore = now;

    FirebaseJson content;
    content.set("fields/temp/doubleValue",      isnan(temp) ? -1 : temp);
    content.set("fields/humidity/doubleValue",  isnan(hum)  ? -1 : hum);
    content.set("fields/gas_raw/integerValue",  gas);
    content.set("fields/vibration/doubleValue", vibration);
    content.set("fields/state/stringValue",     stare.c_str());
    content.set("fields/alerts/stringValue",    alerte.c_str());
    // content.set("fields/timestamp/integerValue",(int)(millis() / 1000));
    time_t now; time(&now);
    content.set("fields/timestamp/integerValue", (int)now);

    if (Firebase.Firestore.createDocument(
          &fbdoFirestore, PROJECT_ID, "",
          "history", "", content.raw(), ""
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
    digitalWrite(BUZZER_PIN, HIGH); delay(150);
    digitalWrite(BUZZER_PIN, LOW);  delay(150);
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
