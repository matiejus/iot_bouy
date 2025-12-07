#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ===== CONFIG =====
const char* ssid = "OpenWrt";
const char* password = "dziumbras";

const char* mqtt_server = "broker.hivemq.com";
const uint16_t mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_pass = "";

const char* mqtt_topic_data = "mcu/sensors_output";

const uint16_t DEVICE_ID = 0004;

unsigned long publishInterval = 5UL * 1000UL; // publish every 30s
bool sensorsOn = true;

//


bool beaconActive = false;
unsigned long beaconStart = 0;
unsigned long lastBlink = 0;
const unsigned long beaconDuration = 5UL * 60UL * 1000UL; // 5 min


// Pins
const int TDS_PIN      = 5;
const int TURB_PIN     = 4;
const int DS18B20_PIN  = 6;
const int POWER_MOSFET = 7;
// Beacon mode
// pin 14 is reversed for god knows what
//const int BEACON_LED_PIN = 14;

const int BEACON_LED_PIN = 13; 
// ===================

WiFiClient espClient;
PubSubClient mqtt(espClient);
OneWire oneWire(DS18B20_PIN);
DallasTemperature sensors(&oneWire);

unsigned long lastPublish = 0;

//json

int extractId(String &msg) {
  int idIndex = msg.indexOf("\"id\"");
  if (idIndex == -1) return -1;

  int colon = msg.indexOf(":", idIndex);
  if (colon == -1) return -1;

  // find the number after :
  int start = colon + 1;
  while (start < msg.length() && !isDigit(msg[start])) start++;

  int end = start;
  while (end < msg.length() && isDigit(msg[end])) end++;

  return msg.substring(start, end).toInt();
}


// ---------- WiFi ----------
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("Connecting to WiFi %s\n", ssid);
  WiFi.begin(ssid, password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 15000) {
      Serial.println("\nWiFi connect timeout, retrying...");
      WiFi.disconnect();
      WiFi.reconnect();
      start = millis();
    }
  }
  Serial.println("\nWiFi connected, IP: " + WiFi.localIP().toString());
}

// ---------- MQTT ----------
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  Serial.printf("Command received on [%s]: %s\n", topic, msg.c_str());

  msg.trim();
  msg.toLowerCase();

  if (msg.startsWith("set_data_interval")) {
    int value = msg.substring(msg.lastIndexOf(' ') + 1).toInt();
    if (value > 0) publishInterval = (unsigned long)value * 1000UL;
  } 
  else if (msg.startsWith("sensors_on")) {
    if (msg.indexOf("true") != -1) {
      sensorsOn = true;
      digitalWrite(POWER_MOSFET, HIGH);
    } else if (msg.indexOf("false") != -1) {
      sensorsOn = false;
      digitalWrite(POWER_MOSFET, LOW);
    }
  }
  else if (strcmp(topic, "mcu/beacon_mode") == 0) {
    int targetId = extractId(msg);
    Serial.printf("Beacon parsed ID: %d\n", targetId);

    if (targetId == DEVICE_ID) {
      beaconActive = true;
      beaconStart = millis();
      lastBlink = millis();
      Serial.println("Beacon mode activated!");
    }
  }

}


void reconnectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT...");
    String clientId = "esp-client-" + String((uint32_t)ESP.getEfuseMac(), HEX);

    if (mqtt.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("connected");

      // Subscribe ONLY to needed topics
      mqtt.subscribe("mcu/beacon_mode");
      Serial.println("Subscribed to topic: mcu/beacon_mode");

    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" try again in 3s");
      delay(3000);
    }
  }
}

// ---------- Helpers ----------
struct AnalogReading { int raw; float voltage; };
AnalogReading readAnalogSafe(int pin) {
  AnalogReading r = {0, 0.0f};
  r.raw = analogRead(pin);
  r.voltage = (r.raw / 4095.0f) * 3.3f;
  return r;
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);
  delay(200);

  #ifdef ARDUINO_ARCH_ESP32
  analogSetPinAttenuation(TDS_PIN, ADC_11db);
  analogSetPinAttenuation(TURB_PIN, ADC_11db);
  #endif

  pinMode(POWER_MOSFET, OUTPUT);
  digitalWrite(POWER_MOSFET, HIGH); // Power ON sensors

  connectWiFi();
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);
  sensors.begin();

  Serial.println("DS18B20 init done");

  pinMode(BEACON_LED_PIN, OUTPUT);
  digitalWrite(BEACON_LED_PIN, LOW);

  mqtt.subscribe("mcu/beacon_mode");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) reconnectMQTT();
  mqtt.loop();

  unsigned long now = millis();
  if (sensorsOn && (now - lastPublish >= publishInterval)) {
    lastPublish = now;

    AnalogReading tds = readAnalogSafe(TDS_PIN);
    AnalogReading turb = readAnalogSafe(TURB_PIN);

    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0);
    if (tempC == DEVICE_DISCONNECTED_C) {
      Serial.println("DS18B20 read failed");
      tempC = NAN;
    }

    String payload = "{";
    payload += "\"tds_raw\":" + String(tds.raw) + ",";
    payload += "\"tds_volt\":" + String(tds.voltage, 3) + ",";
    payload += "\"turb_raw\":" + String(turb.raw) + ",";
    payload += "\"turb_volt\":" + String(turb.voltage, 3) + ",";
    payload += "\"temp_c\":" + (isnan(tempC) ? "null" : String(tempC, 2))+",";
    payload += "\"device_id\":" + String(DEVICE_ID);
    payload += "}";

    Serial.println("Publishing: " + payload);
    if (!mqtt.publish(mqtt_topic_data, payload.c_str())) {
      Serial.println("Publish failed, reconnecting MQTT");
      reconnectMQTT();
      mqtt.publish(mqtt_topic_data, payload.c_str());
    }
  }

  // ----- BEACON MODE -----
  if (beaconActive) {
    unsigned long now = millis();

    // Stop after 5 minutes
    if (now - beaconStart >= beaconDuration) {
      beaconActive = false;
      digitalWrite(BEACON_LED_PIN, LOW);
      Serial.println("Beacon mode ended");
    } else {
      // Blink LED every 500 ms
      if (now - lastBlink >= 500) {
        lastBlink = now;
        digitalWrite(BEACON_LED_PIN, !digitalRead(BEACON_LED_PIN));
      }
    }
  }


  delay(10);
}
