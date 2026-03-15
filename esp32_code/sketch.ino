#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ESP32Servo.h>

#define DHTPIN 4
#define DHTTYPE DHT22

const char* ssid = "Wokwi-GUEST";
const char* password = "";
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

const String PREFIX = "zewail/202200059";

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);
Servo servo;

/* ---------------- PINS ---------------- */

#define LDR_PIN 34
#define PIR_PIN 27
#define BUZZER_PIN 15
#define TRIG_PIN 5
#define ECHO_PIN 18
#define SERVO_PIN 19
#define RELAY_PIN 26
#define RED_LED_PIN 2
#define YELLOW_LED_PIN 16
#define GREEN_LED_PIN 17

/* ---------------- THRESHOLDS ---------------- */

float TEMP_THRESHOLD = 30;
int LIGHT_THRESHOLD = 500;
int DIST_THRESHOLD = 20;


unsigned long publishInterval = 2000;

/* ---------------- MQTT CALLBACK ---------------- */

void callback(char* topic, byte* payload, unsigned int length) {

  String msg = "";
  for (int i = 0; i < length; i++)
    msg += (char)payload[i];

  Serial.println("[MQTT] " + String(topic) + " -> " + msg);

  

  if (String(topic) == PREFIX + "/actuators/led") {

    if (msg.indexOf("red") >= 0) {

      if (msg.indexOf("\"state\":\"on\"") >= 0)
        digitalWrite(RED_LED_PIN, HIGH);
      else if (msg.indexOf("\"state\":\"off\"") >= 0)
        digitalWrite(RED_LED_PIN, LOW);

    }

    else if (msg.indexOf("yellow") >= 0) {

      if (msg.indexOf("\"state\":\"on\"") >= 0)
        digitalWrite(YELLOW_LED_PIN, HIGH);
      else if (msg.indexOf("\"state\":\"off\"") >= 0)
        digitalWrite(YELLOW_LED_PIN, LOW);

    }

    else if (msg.indexOf("green") >= 0) {

      if (msg.indexOf("\"state\":\"on\"") >= 0)
        digitalWrite(GREEN_LED_PIN, HIGH);
      else if (msg.indexOf("\"state\":\"off\"") >= 0)
        digitalWrite(GREEN_LED_PIN, LOW);

    }

  }

  if (String(topic) == PREFIX + "/actuators/buzzer") {

    if (msg.indexOf("\"state\":\"on\"") >= 0) {

      int duration = 2000; // default

      if (msg.indexOf("duration") >= 0) {
        duration = msg.substring(msg.indexOf("duration") + 9).toInt();
      }

      digitalWrite(BUZZER_PIN, HIGH);
      delay(duration);
      digitalWrite(BUZZER_PIN, LOW);

    }

    else if (msg.indexOf("\"state\":\"off\"") >= 0) {

      digitalWrite(BUZZER_PIN, LOW);

    }
  }

  if (String(topic) == PREFIX + "/actuators/servo") {

    int angle = msg.substring(msg.indexOf(":") + 1).toInt();
    servo.write(angle);
  }

  if (String(topic) == PREFIX + "/actuators/relay") {

    if (msg.indexOf("on") > 0)
      digitalWrite(RELAY_PIN, HIGH);
    else
      digitalWrite(RELAY_PIN, LOW);
  }

  

  if (String(topic) == PREFIX + "/config/thresholds") {

    if (msg.indexOf("temp_max") >= 0)
      TEMP_THRESHOLD = msg.substring(msg.indexOf("temp_max") + 9).toFloat();

    if (msg.indexOf("light_min") >= 0)
      LIGHT_THRESHOLD = msg.substring(msg.indexOf("light_min") + 10).toInt();

    if (msg.indexOf("dist_min") >= 0)
      DIST_THRESHOLD = msg.substring(msg.indexOf("dist_min") + 9).toInt();

    Serial.println("Thresholds updated from dashboard");
  }

  if (String(topic) == PREFIX + "/config/interval") {

    publishInterval = msg.substring(msg.indexOf(":") + 1).toInt();
  }
}

/* ---------------- WIFI ---------------- */

void setup_wifi() {

  Serial.print("Connecting to WiFi");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
}

/* ---------------- MQTT RECONNECT ---------------- */

void reconnect() {

  while (!client.connected()) {

    Serial.print("Connecting MQTT...");

    String clientId = "ESP32-" + String(random(1000));

    if (client.connect(clientId.c_str())) {

      Serial.println("connected");

      client.subscribe((PREFIX + "/actuators/led").c_str());
      client.subscribe((PREFIX + "/actuators/buzzer").c_str());
      client.subscribe((PREFIX + "/actuators/servo").c_str());
      client.subscribe((PREFIX + "/actuators/relay").c_str());

      client.subscribe((PREFIX + "/config/thresholds").c_str());
      client.subscribe((PREFIX + "/config/interval").c_str());

    } else {

      delay(2000);
    }
  }
}

/* ---------------- ULTRASONIC ---------------- */

float readDistance() {

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);

  float distance = duration * 0.034 / 2;

  return distance;
}

/* ---------------- SETUP ---------------- */

void setup() {

  Serial.begin(115200);

  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  servo.attach(SERVO_PIN);

  dht.begin();

  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}



void loop() {
  if (!client.connected())
    reconnect();

  client.loop();

  static unsigned long lastMsg = 0;
  static unsigned long lastHeartbeat = 0;

  /* ---------- SENSOR PUBLISH ---------- */

  if (millis() - lastMsg > publishInterval) {

    lastMsg = millis();

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    int light = map(analogRead(LDR_PIN),32,4064,4,0);
    int motion = digitalRead(PIR_PIN);

    float distance = readDistance();

    Serial.println("\n========= SENSOR DEBUG =========");

    Serial.print("Temp: ");
    Serial.println(temperature);

    Serial.print("Humidity: ");
    Serial.println(humidity);

    Serial.print("Light: ");
    Serial.println(light);

    Serial.print("Motion: ");
    Serial.println(motion ? "YES" : "NO");

    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.println(" cm");

    /* ---------- AUTOMATIC CONTROL ---------- */
    /* ---------- TEMPERATURE CONTROL (RELAY ONLY) ---------- */

    if (temperature > TEMP_THRESHOLD) {

      digitalWrite(RELAY_PIN, HIGH);
      Serial.println("HIGH TEMPERATURE -> RELAY ON");

    } else {

      digitalWrite(RELAY_PIN, LOW);
    }

    /* ---------- BUZZER (MOTION) ---------- */

    if (motion) {

      digitalWrite(BUZZER_PIN, HIGH);
      delay(2000);
      digitalWrite(BUZZER_PIN, LOW);
    }

    /* ---------- LED STATUS ---------- */

    if (temperature > TEMP_THRESHOLD) {

      digitalWrite(RED_LED_PIN, HIGH);
      digitalWrite(YELLOW_LED_PIN, LOW);
      digitalWrite(GREEN_LED_PIN, LOW);

    }

    else if (light < LIGHT_THRESHOLD) {

      digitalWrite(RED_LED_PIN, LOW);
      digitalWrite(YELLOW_LED_PIN, HIGH);
      digitalWrite(GREEN_LED_PIN, LOW);

    }

    else {

      digitalWrite(RED_LED_PIN, LOW);
      digitalWrite(YELLOW_LED_PIN, LOW);
      digitalWrite(GREEN_LED_PIN, HIGH);

    }

    if (distance < DIST_THRESHOLD)
      servo.write(90);
    else
      servo.write(0);

    

    String temp = "{\"value\":" + String(temperature) + ",\"unit\":\"C\"}";
    String hum = "{\"value\":" + String(humidity) + ",\"unit\":\"%\"}";
    String lightMsg = "{\"value\":" + String(light) + "}";
    String motionMsg = "{\"detected\":" + String(motion ? "true" : "false") + "}";
    String distMsg = "{\"value\":" + String(distance) + ",\"unit\":\"cm\"}";

    client.publish((PREFIX + "/sensors/temperature").c_str(), temp.c_str());
    client.publish((PREFIX + "/sensors/humidity").c_str(), hum.c_str());
    client.publish((PREFIX + "/sensors/light").c_str(), lightMsg.c_str());
    client.publish((PREFIX + "/sensors/motion").c_str(), motionMsg.c_str());
    client.publish((PREFIX + "/sensors/distance").c_str(), distMsg.c_str());

    Serial.println("All sensor data published");
  }

  /* ---------- HEARTBEAT ---------- */

  if (millis() - lastHeartbeat > 10000) {

    lastHeartbeat = millis();

    String status = "{\"uptime\":" + String(millis()/1000) +
                    ",\"rssi\":" + String(WiFi.RSSI()) + "}";

    client.publish((PREFIX + "/system/status").c_str(), status.c_str());

    Serial.println("Heartbeat sent");
  }
}