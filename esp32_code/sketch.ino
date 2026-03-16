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
int LIGHT_THRESHOLD = 4;
int DIST_THRESHOLD = 20;

/* ---------------- STATE FLAGS ---------------- */
bool redManual = false;
bool yellowManual = false;
bool greenManual = false;
bool servoManual = false;

/* ---------------- BUZZER TIMER ---------------- */
bool buzzerActive = false;
unsigned long buzzerStart = 0;
int buzzerDuration = 0;

/* ---------------- TIMERS ---------------- */
unsigned long publishInterval = 2000;

/* ---------------- MQTT CALLBACK ---------------- */
void callback(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (int i = 0; i < length; i++) msg += (char)payload[i];
    Serial.println("[MQTT] " + String(topic) + " -> " + msg);

    /* ---------- LED CONTROL ---------- */
    if (String(topic) == PREFIX + "/actuators/led") {
        bool turnOn = msg.indexOf("on") >= 0;

        if (msg.indexOf("red") >= 0) {
            redManual = turnOn;
            digitalWrite(RED_LED_PIN, turnOn ? HIGH : LOW);
        } else if (msg.indexOf("yellow") >= 0) {
            yellowManual = turnOn;
            digitalWrite(YELLOW_LED_PIN, turnOn ? HIGH : LOW);
        } else if (msg.indexOf("green") >= 0) {
            greenManual = turnOn;
            digitalWrite(GREEN_LED_PIN, turnOn ? HIGH : LOW);
        }
    }

    /* ---------- BUZZER CONTROL (manual) ---------- */
    if (String(topic) == PREFIX + "/actuators/buzzer") {
        if (msg.indexOf("\"state\":\"on\"") >= 0) {
            int duration = 2000; // default duration in ms
            int start = msg.indexOf("duration");
            if (start >= 0) {
                start = msg.indexOf(":", start) + 1;
                int end = msg.indexOf("}", start);
                duration = msg.substring(start, end).toInt();
            }
            // Use tone() for manual buzzer
            tone(BUZZER_PIN, 262, duration); // 262 Hz, duration ms
            Serial.println("Manual buzzer ON for " + String(duration) + " ms");
        }
        else if (msg.indexOf("\"state\":\"off\"") >= 0) {
            noTone(BUZZER_PIN); // stop manual buzzer
            Serial.println("Manual buzzer OFF");
        }
    }

    /* ---------- SERVO CONTROL ---------- */
    if (String(topic) == PREFIX + "/actuators/servo") {
        int angle = msg.substring(msg.indexOf(":") + 1).toInt();
        servoManual = true;
        servo.write(angle);
    }

    /* ---------- RELAY CONTROL ---------- */
    if (String(topic) == PREFIX + "/actuators/relay") {
        digitalWrite(RELAY_PIN, msg.indexOf("on") >= 0 ? HIGH : LOW);
    }

    /* ---------- THRESHOLD CONFIG ---------- */
    if (String(topic) == PREFIX + "/config/thresholds") {
        int start, end;
        if (msg.indexOf("temp_max") >= 0) {
            start = msg.indexOf("temp_max");
            start = msg.indexOf(":", start) + 1;
            end = msg.indexOf(",", start);
            TEMP_THRESHOLD = msg.substring(start, end).toFloat();
        }
        if (msg.indexOf("light_min") >= 0) {
            start = msg.indexOf("light_min");
            start = msg.indexOf(":", start) + 1;
            end = msg.indexOf(",", start);
            LIGHT_THRESHOLD = msg.substring(start, end).toInt();
        }
        if (msg.indexOf("dist_min") >= 0) {
            start = msg.indexOf("dist_min");
            start = msg.indexOf(":", start) + 1;
            end = msg.indexOf("}", start);
            DIST_THRESHOLD = msg.substring(start, end).toInt();
        }
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
    return duration * 0.034 / 2;
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

/* ---------------- LOOP ---------------- */
void loop() {
    if (!client.connected()) reconnect();
    client.loop();

    static unsigned long lastMsg = 0;
    static unsigned long lastHeartbeat = 0;
    static int lastServoPos = -1; // servo memory for auto mode

    /* ---------- SENSOR PUBLISH ---------- */
    if (millis() - lastMsg > publishInterval) {
        lastMsg = millis();

        float temperature = dht.readTemperature();
        float humidity = dht.readHumidity();
        int light = map(analogRead(LDR_PIN), 32, 4064, 4, 0);
        int motion = digitalRead(PIR_PIN);
        float distance = readDistance();

        Serial.println("\n========= SENSOR DEBUG =========");
        Serial.println("Temp: " + String(temperature));
        Serial.println("Humidity: " + String(humidity));
        Serial.println("Light: " + String(light));
        Serial.println("Motion: " + String(motion ? "YES" : "NO"));
        Serial.println("Distance: " + String(distance));

        /* ---------- TEMPERATURE CONTROL ---------- */
        if (!isnan(temperature) && temperature > TEMP_THRESHOLD) {
            digitalWrite(RELAY_PIN, HIGH);
            if (!redManual) digitalWrite(RED_LED_PIN, HIGH);
            Serial.println("HIGH TEMPERATURE -> RELAY ON");
        } else {
            digitalWrite(RELAY_PIN, LOW);
            if (!redManual) digitalWrite(RED_LED_PIN, LOW);
        }

        /* ---------- MOTION BUZZER ---------- */
        if (motion) {
            tone(BUZZER_PIN, 262, 2000); // play 262Hz for 2 seconds
            Serial.println("Motion detected -> buzzer ON for 2s");
        }

        /* ---------- LIGHT LED ---------- */
        if (!yellowManual) {
            digitalWrite(YELLOW_LED_PIN, light < LIGHT_THRESHOLD ? HIGH : LOW);
        }

        /* ---------- SERVO AUTO ---------- */
        if (!servoManual) {
            if (!isnan(distance)) {
                int targetPos = (distance < DIST_THRESHOLD) ? 90 : 0;
                if (targetPos != lastServoPos) {
                    servo.write(targetPos);
                    lastServoPos = targetPos;
                    Serial.println("Servo moved to " + String(targetPos));
                }
            }
        }

        /* ---------- MQTT PUBLISH ---------- */
        client.publish((PREFIX + "/sensors/temperature").c_str(),
                       ("{\"value\":" + String(temperature) + ",\"unit\":\"C\"}").c_str());
        client.publish((PREFIX + "/sensors/humidity").c_str(),
                       ("{\"value\":" + String(humidity) + ",\"unit\":\"%\"}").c_str());
        client.publish((PREFIX + "/sensors/light").c_str(),
                       ("{\"value\":" + String(light) + "}").c_str());
        client.publish((PREFIX + "/sensors/motion").c_str(),
                       ("{\"detected\":" + String(motion ? "true" : "false") + "}").c_str());
        client.publish((PREFIX + "/sensors/distance").c_str(),
                       ("{\"value\":" + String(distance) + ",\"unit\":\"cm\"}").c_str());

        Serial.println("All sensor data published");
    }

    /* ---------- HEARTBEAT ---------- */
    if (millis() - lastHeartbeat > 10000) {
        lastHeartbeat = millis();
        String status = "{\"uptime\":" + String(millis() / 1000) +
                        ",\"rssi\":" + String(WiFi.RSSI()) +
                        ",\"free_heap\":" + String(ESP.getFreeHeap()) + "}";
        client.publish((PREFIX + "/system/status").c_str(), status.c_str());
        Serial.println("Heartbeat sent");
    }
}