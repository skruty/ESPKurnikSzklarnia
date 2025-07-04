#include <Arduino.h>
#include <WiFi.h>
#include <DHT.h>
#include <time.h>
#include <HTTPClient.h>

// --- WiFi i Influx ---
const char* ssid = "Klimek_Tenda";
const char* password = "internet123";
const char* influx_host = "http://192.168.0.105:8086/write?db=czujniki";

// --- Piny ---
#define DHTPIN1 14
#define DHTPIN2 27  // <- nowy czujnik
#define DHTTYPE DHT11
#define LIGHT_SENSOR_PIN 36
#define SOIL_SENSOR_PIN 34
#define RELAY_VALVE_PIN 13
#define RELAY_DOOR_PIN 12
#define BUZZER_PIN 25
#define SERVO_PIN 26

// --- Obiekty i zmienne ---
DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);

bool drzwi_otwarte = false;
bool pasza_podana = false;
unsigned long ostatnie_podlanie = 0;

void otworz_drzwi() {
  digitalWrite(RELAY_DOOR_PIN, HIGH);
  drzwi_otwarte = true;
}

void zamknij_drzwi() {
  digitalWrite(RELAY_DOOR_PIN, LOW);
  drzwi_otwarte = false;
}

void podlej() {
  digitalWrite(RELAY_VALVE_PIN, HIGH);
  delay(6000);
  digitalWrite(RELAY_VALVE_PIN, LOW);
  ostatnie_podlanie = millis();
}

void dozuj_pasze() {
  ledcAttachPin(SERVO_PIN, 0);
  ledcSetup(0, 50, 16);
  ledcWrite(0, 1638);
  delay(500);
  ledcWrite(0, 4915);
  delay(500);
  ledcWrite(0, 1638);
  delay(500);
  ledcDetachPin(SERVO_PIN);
  pasza_podana = true;
}

void przypomnienie() {
  if (drzwi_otwarte) Serial.println("Przypomnienie: zamknij drzwi!");
}

bool noc() {
  return analogRead(LIGHT_SENSOR_PIN) < 1000;
}

void scanWiFi() {
  Serial.println("Skanowanie dostępnych sieci WiFi...");
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; ++i) {
    Serial.printf(" %d: %s (%d dBm)%s\n", i + 1,
                  WiFi.SSID(i).c_str(),
                  WiFi.RSSI(i),
                  (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " [OPEN]" : "");
  }
  Serial.println("-----------------------------");
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_VALVE_PIN, OUTPUT);
  pinMode(RELAY_DOOR_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  dht1.begin();
  dht2.begin();

  scanWiFi();
  Serial.print("Łączenie z WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(1000);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nPołączono z WiFi");
    Serial.println(WiFi.localIP());
    configTime(0, 0, "pool.ntp.org");
  } else {
    Serial.println("\nBłąd połączenia z WiFi!");
  }
}

void loop() {
  static unsigned long timer = 0;
  if (millis() - timer > 30000) {
    timer = millis();

    float temperatura = dht1.readTemperature();
    float wilgotnosc = dht1.readHumidity();
    float temperatura2 = dht2.readTemperature();
    float wilgotnosc2 = dht2.readHumidity();

    int swiatlo = analogRead(LIGHT_SENSOR_PIN);
    int gleba = analogRead(SOIL_SENSOR_PIN);

    Serial.printf("T1=%.1f°C H1=%.1f%% T2=%.1f°C H2=%.1f%% Światło=%d Gleba=%d\n",
                  temperatura, wilgotnosc, temperatura2, wilgotnosc2, swiatlo, gleba);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) return;

    int godzina = timeinfo.tm_hour;
    int minuta = timeinfo.tm_min;

    if (!drzwi_otwarte && swiatlo > 2000) otworz_drzwi();
    if (godzina == 20 && minuta < 5) przypomnienie();
    if (!pasza_podana && godzina == 7) dozuj_pasze();
    if (godzina == 8) pasza_podana = false;
    if (noc() && gleba > 3000 && millis() - ostatnie_podlanie > 3600000) podlej();

    if (WiFi.status() == WL_CONNECTED) {
      WiFiClient client;
      HTTPClient http;
      http.begin(client, influx_host);
      http.addHeader("Content-Type", "text/plain");

      String dane = "szklarnia temperatura=" + String(temperatura, 1) +
                    ",wilgotnosc=" + String(wilgotnosc, 1) +
                    ",temperatura2=" + String(temperatura2, 1) +
                    ",wilgotnosc2=" + String(wilgotnosc2, 1) +
                    ",swiatlo=" + String(swiatlo) +
                    ",gleba=" + String(gleba);

      Serial.println("Wysyłam: " + dane);

      int httpResponseCode = http.POST(dane);
      Serial.println("Influx HTTP response: " + String(httpResponseCode));
      http.end();
    } else {
      Serial.println("Brak połączenia WiFi, pominięto wysyłkę.");
    }
  }
}
