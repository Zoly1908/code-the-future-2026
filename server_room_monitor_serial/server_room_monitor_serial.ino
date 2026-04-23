// ============================================================
//  SERVER ROOM MONITOR — ESP32-C6 WROOM-1
//  Versiune simpla: doar Serial Monitor, fara WiFi/MQTT
// ============================================================

#include <DHT.h>
#include <Wire.h>
#include <MPU6050.h>

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
//  OBIECTE
// ============================================================
DHT     dht(DHT_PIN, DHT_TYPE);
MPU6050 mpu;

float accelBaseX = 0, accelBaseY = 0, accelBaseZ = 0;

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== Server Room Monitor (Serial Only) ===\n");

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
  // Citire DHT22
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  // Citire MQ-3
  int gas = analogRead(MQ3_PIN);

  // Citire MPU6050
  int16_t ax, ay, az, gx, gy, gz;
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
  float axG = ax / 16384.0f;
  float ayG = ay / 16384.0f;
  float azG = az / 16384.0f;
  float dX = axG - accelBaseX;
  float dY = ayG - accelBaseY;
  float dZ = azG - accelBaseZ;
  float vibration = sqrt(dX*dX + dY*dY + dZ*dZ);

  // ---- Afisare date citite ----
  Serial.println("--- Citire noua ---");

  if (isnan(temp)) {
    Serial.println("[EROARE] DHT22: nu pot citi temperatura!");
  } else {
    Serial.print("  Temperatura : ");
    Serial.print(temp);
    Serial.println(" °C");
  }

  if (isnan(hum)) {
    Serial.println("[EROARE] DHT22: nu pot citi umiditatea!");
  } else {
    Serial.print("  Umiditate   : ");
    Serial.print(hum);
    Serial.println(" %");
  }

  Serial.print("  Gaz (raw)   : ");
  Serial.println(gas);

  Serial.print("  Vibratie    : ");
  Serial.print(vibration, 3);
  Serial.println(" g");

  // ---- Evaluare si alerte ----
  bool critical = false;
  bool warn     = false;
  String alerte = "";

  if (!isnan(temp) && temp >= TEMP_CRITICAL) {
    critical = true;
    alerte += "[CRITIC] Temperatura prea mare: " + String(temp) + " C\n";
  } else if (!isnan(temp) && temp >= TEMP_WARN) {
    warn = true;
    alerte += "[ATENTIE] Temperatura ridicata: " + String(temp) + " C\n";
  }

  if (!isnan(hum) && hum >= HUM_WARN) {
    warn = true;
    alerte += "[ATENTIE] Umiditate ridicata: " + String(hum) + " %\n";
  }

  if (gas >= GAS_WARN) {
    critical = true;
    alerte += "[CRITIC] Gaz / fum detectat! Valoare: " + String(gas) + "\n";
  }

  if (vibration >= VIBRATION_WARN) {
    critical = true;
    alerte += "[CRITIC] Vibratii puternice detectate! Valoare: " + String(vibration, 3) + " g\n";
  }

  // ---- Aplicare actuatori + mesaj stare ----
  digitalWrite(LED_GREEN,  LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED,    LOW);
  digitalWrite(RELAY_PIN,  LOW);
  digitalWrite(BUZZER_PIN, LOW);

  if (critical) {
    digitalWrite(LED_RED,    HIGH);
    digitalWrite(RELAY_PIN,  HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("  Stare      : *** CRITIC ***");
    Serial.print(alerte);
  } else if (warn) {
    digitalWrite(LED_YELLOW, HIGH);
    digitalWrite(RELAY_PIN,  HIGH);
    Serial.println("  Stare      : ! ATENTIE !");
    Serial.print(alerte);
  } else {
    digitalWrite(LED_GREEN, HIGH);
    Serial.println("  Stare      : OK - totul normal");
  }

  Serial.println("--------------------------------------------");
  delay(2000);
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
