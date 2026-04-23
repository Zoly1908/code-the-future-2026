#include <DHT.h>

// ----------------------
// CONFIGURARE PINI
// ----------------------

#define RELAY_PIN 23      // Releu conectat la GPIO23
#define DHT_PIN 4         // DHT22 conectat la GPIO4
#define DHT_TYPE DHT22

// Prag temperatură
#define TEMP_LIMIT 30.0

DHT dht(DHT_PIN, DHT_TYPE);

void setup() {
  Serial.begin(115200);

  // Pornim senzorul
  dht.begin();

  // Setăm releul ca output
  pinMode(RELAY_PIN, OUTPUT);

  // Ventilator oprit la start
  digitalWrite(RELAY_PIN, LOW);

  Serial.println("Sistem Smart Cooling pornit...");
}

void loop() {
  // Citim temperatura
  float temp = dht.readTemperature();

  // Verificăm dacă citirea e validă
  if (isnan(temp)) {
    Serial.println("Eroare citire DHT22!");
    delay(2000);
    return;
  }

  Serial.print("Temperatura: ");
  Serial.print(temp);
  Serial.println(" °C");

  // Dacă depășește 30°C -> pornește ventilatorul
  if (temp > TEMP_LIMIT) {
    Serial.println("Temperatura mare -> Ventilator ON");
    digitalWrite(RELAY_PIN, HIGH);
  } 
  else {
    Serial.println("Temperatura normala -> Ventilator OFF");
    digitalWrite(RELAY_PIN, LOW);
  }

  delay(2000);
}