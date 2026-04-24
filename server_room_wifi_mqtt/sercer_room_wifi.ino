// ============================================================
//  SERVER ROOM MONITOR — ESP32-C6 WROOM-1
//  Versiune simpla: doar Serial Monitor, fara WiFi/MQTT
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Wire.h>
#include <MPU6050.h>
#include <ArduinoJson.h>

// ============================================================
//  CONFIGURARE PINI
// ============================================================
#define DHT_PIN       4
#define DHT_TYPE      DHT22

#define MQ3_PIN       0       // AOUT -> GPIO0 (ADC)

#define RELAY_PIN     23
#define LED_GREEN     18
#define LED_YELLOW    19
#define LED_RED       20
#define BUZZER_PIN    21

// MPU6050: SDA -> GPIO8, SCL -> GPIO9

// ============================================================
//  PRAGURI
// ============================================================
#define TEMP_WARN         28.0f
#define TEMP_CRITICAL     32.0f
#define HUM_WARN          70.0f
#define GAS_WARN          2700
#define VIBRATION_WARN    0.8f

// ============================================================
//  WIFI & MQTT
// ============================================================
const char* WIFI_SSID     = "SSID_TAU";
const char* WIFI_PASS     = "PAROLA_TA";

const char* MQTT_BROKER   = "broker.hivemq.com";  // broker public gratuit
const int   MQTT_PORT     = 1883;
const char* MQTT_CLIENT   = "serverroom-esp32c6";

// Topic-uri MQTT
const char* TOPIC_SENSORS = "serverroom/sensors";   // date senzori (JSON)
const char* TOPIC_ALERTS  = "serverroom/alerts";    // alerte string
const char* TOPIC_STATUS  = "serverroom/status";    // online/offline

// ============================================================
//  OBIECTE
// ============================================================
DHT     dht(DHT_PIN, DHT_TYPE);
MPU6050 mpu;
WiFiClient    wifiClient;
PubSubClient  mqtt(wifiClient);

// Stare sistem
enum SystemState { STATE_OK, STATE_WARN, STATE_CRITICAL };
SystemState currentState = STATE_OK;

// Vibratii - valori de repaus calibrate la boot
float accelBaseX = 0, accelBaseY = 0, accelBaseZ = 0;

unsigned long lastPublish   = 0;
const long    PUBLISH_INTERVAL = 2000; // ms

// ============================================================
//  PROTOTYPURI
// ============================================================
void connectWifi();
void connectMqtt();
void calibrateMPU();
void readSensors(float &temp, float &hum, int &gas, float &vibration);
void evaluateState(float temp, float hum, int gas, float vibration);
void applyActuators(SystemState state);
void publishData(float temp, float hum, int gas, float vibration, SystemState state);
void buzzerAlert(int times);
String stateToString(SystemState s);

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Server Room Monitor ===\n");

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
  delay(2000); // asteptam stabilizare I2C
  Wire.begin(8, 9); 
  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("[EROARE] MPU6050 nu a fost gasit! Verifica conexiunile SDA/SCL.");
  } else {
    Serial.println("[OK] MPU6050 conectat.");
    calibrateMPU();
  }

  connectWifi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setKeepAlive(30);
  connectMqtt();

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
  // Mentine conexiunile active
  if (!mqtt.connected()) connectMqtt();
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastPublish >= PUBLISH_INTERVAL) {
    lastPublish = now;

    float temp, hum, vibration;
    int   gas;

    readSensors(temp, hum, gas, vibration);
    evaluateState(temp, hum, gas, vibration);
    applyActuators(currentState);
    publishData(temp, hum, gas, vibration, currentState);

    // Log serial
    Serial.printf("[DATA] Temp=%.1f°C  Hum=%.1f%%  Gas=%d  Vib=%.3fg  State=%s\n",
      temp, hum, gas, vibration, stateToString(currentState).c_str());
  }
}

// ============================================================
//  CITIRE SENZORI
// ============================================================
void readSensors(float &temp, float &hum, int &gas, float &vibration) {
  // DHT22
  temp = dht.readTemperature();
  hum  = dht.readHumidity();
  if (isnan(temp)) temp = -1;
  if (isnan(hum))  hum  = -1;

  // MQ-3 (citire raw ADC 12-bit, 0-4095)
  gas = analogRead(MQ3_PIN);

  // MPU6050 — calculam devierea fata de repaus
  int16_t ax, ay, az, gx, gy, gz;
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  // Convertim in g (scara ±2g implicit)
  float axG = ax / 16384.0f;
  float ayG = ay / 16384.0f;
  float azG = az / 16384.0f;

  // Deviere fata de baseline calibrat
  float dX = axG - accelBaseX;
  float dY = ayG - accelBaseY;
  float dZ = azG - accelBaseZ;
  vibration = sqrt(dX*dX + dY*dY + dZ*dZ);
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
  Serial.printf("  Baseline: X=%.3f  Y=%.3f  Z=%.3f\n",
    accelBaseX, accelBaseY, accelBaseZ);
}

// ============================================================
//  EVALUARE STARE SISTEM
// ============================================================
void evaluateState(float temp, float hum, int gas, float vibration) {
  bool critical = false;
  bool warn     = false;

  // Temperatura critica
  if (temp >= TEMP_CRITICAL)  critical = true;
  // Gaz / fum detectat
  if (gas  >= GAS_WARN)       critical = true;
  // Vibratii puternice
  if (vibration >= VIBRATION_WARN) critical = true;

  // Temperatura ridicata (warning)
  if (temp >= TEMP_WARN) warn = true;
  // Umiditate ridicata (warning)
  if (hum  >= HUM_WARN)  warn = true;

  if (critical)      currentState = STATE_CRITICAL;
  else if (warn)     currentState = STATE_WARN;
  else               currentState = STATE_OK;
}

// ============================================================
//  CONTROL ACTUATORI
// ============================================================
void applyActuators(SystemState state) {
  // Reset toate
  digitalWrite(LED_GREEN,  LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED,    LOW);
  digitalWrite(RELAY_PIN,  LOW);
  digitalWrite(BUZZER_PIN, LOW);

  switch (state) {
    case STATE_OK:
      digitalWrite(LED_GREEN, HIGH);
      // Ventilatorul oprit
      break;

    case STATE_WARN:
      digitalWrite(LED_YELLOW, HIGH);
      digitalWrite(RELAY_PIN,  HIGH);  // Ventilator pornit
      break;

    case STATE_CRITICAL:
      digitalWrite(LED_RED,    HIGH);
      digitalWrite(RELAY_PIN,  HIGH);  // Ventilator pornit
      digitalWrite(BUZZER_PIN, HIGH);  // Buzzer continuu
      break;
  }
}

// ============================================================
//  PUBLICARE MQTT (JSON)
// ============================================================
void publishData(float temp, float hum, int gas, float vibration, SystemState state) {
  StaticJsonDocument<256> doc;
  doc["temp"]      = temp;
  doc["humidity"]  = hum;
  doc["gas_raw"]   = gas;
  doc["vibration"] = vibration;
  doc["state"]     = stateToString(state);
  doc["uptime_s"]  = millis() / 1000;

  char buffer[256];
  serializeJson(doc, buffer);
  mqtt.publish(TOPIC_SENSORS, buffer, true); // retained=true

  // Alerta separata daca e ceva rau
  if (state == STATE_CRITICAL) {
    String alert = "";
    if (gas >= GAS_WARN)            alert += "GAS_DETECTED ";
    if (vibration >= VIBRATION_WARN) alert += "VIBRATION ";
    float temp2, hum2, vib2; int gas2;
    readSensors(temp2, hum2, gas2, vib2); // re-read pentru mesaj
    if (temp >= TEMP_CRITICAL)       alert += "HIGH_TEMP ";
    mqtt.publish(TOPIC_ALERTS, alert.c_str());
  }
}

// ============================================================
//  BUZZER: N beep-uri
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
//  HELPER: state -> string
// ============================================================
String stateToString(SystemState s) {
  switch (s) {
    case STATE_OK:       return "OK";
    case STATE_WARN:     return "WARN";
    case STATE_CRITICAL: return "CRITICAL";
    default:             return "UNKNOWN";
  }
}

// ============================================================
//  WIFI CONNECT
// ============================================================
void connectWifi() {
  Serial.printf("Conectare WiFi la %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[OK] WiFi conectat! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WARN] WiFi esuat — continuu fara retea.");
  }
}

// ============================================================
//  MQTT CONNECT (cu reconnect automat)
// ============================================================
void connectMqtt() {
  int tries = 0;
  while (!mqtt.connected() && tries < 5) {
    Serial.print("Conectare MQTT...");
    if (mqtt.connect(MQTT_CLIENT, TOPIC_STATUS, 1, true, "offline")) {
      Serial.println(" OK");
      mqtt.publish(TOPIC_STATUS, "online", true);
    } else {
      Serial.printf(" Esuat (rc=%d), retry...\n", mqtt.state());
      delay(2000);
      tries++;
    }
  }
}

