#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include <OneWire.h>
#include <DallasTemperature.h>

// ==========================================================
// DEVICE
// ==========================================================

//GANTI DENGAN NAMA DEVICE
#define DEVICE_ID "PLTS-01" 

// ==========================================================
// WIFI
// ==========================================================

//GANTI DENGAN WIFI YANG AKAN DIHUBUNGKAN
const char* ssid = "GREENFARM";
const char* password = "@GEI123456";

// ==========================================================
// MQTT
// ==========================================================

// GANTI dengan IPv4 laptop yang menjalankan Mosquitto
const char* mqtt_server = "192.168.1.82";
const int mqtt_port = 1883;

// ==========================================================
// PIN WEMOS D1 MINI
// ==========================================================

#define LED_NORMAL        D6
#define LED_PANAS         D5
#define LED_SANGAT_PANAS  D0

#define RELAY             D1

#define ONE_WIRE_BUS      D2

// ==========================================================
// RELAY LOGIC
// ==========================================================

// Relay ACTIVE-HIGH
#define RELAY_ON  HIGH
#define RELAY_OFF LOW

// ==========================================================
// DS18B20
// ==========================================================

OneWire oneWire(ONE_WIRE_BUS);

DallasTemperature sensors(&oneWire);

// ==========================================================
// WIFI + MQTT
// ==========================================================

WiFiClient espClient;

PubSubClient mqttClient(espClient);

// ==========================================================
// VARIABLE
// ==========================================================

float suhu = 0.0;

String statusSuhu = "BELUM ADA DATA";

bool modeManual = false;

bool relayManual = false;

bool sensorError = false;

// ==========================================================
// TIMER
// ==========================================================

unsigned long previousSensorMillis = 0;

const unsigned long sensorInterval = 3000;

unsigned long previousMQTTReconnect = 0;

const unsigned long mqttReconnectInterval = 3000;

// ==========================================================
// MQTT TOPIC
// ==========================================================

String topicStatus;

String topicMode;

String topicFan;

// ==========================================================
// FUNGSI RELAY
// ==========================================================

void setRelay(bool kondisi)
{
  if (kondisi)
  {
    digitalWrite(RELAY, RELAY_ON);
  }
  else
  {
    digitalWrite(RELAY, RELAY_OFF);
  }
}

// ==========================================================
// CEK STATUS RELAY
// ==========================================================

bool getRelayStatus()
{
  if (RELAY_ON == HIGH)
  {
    return digitalRead(RELAY) == HIGH;
  }
  else
  {
    return digitalRead(RELAY) == LOW;
  }
}

// ==========================================================
// WIFI
// ==========================================================

void connectWiFi()
{
  Serial.println();
  Serial.println("================================");
  Serial.println("CONNECTING WIFI");
  Serial.println("================================");

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);

    Serial.print(".");
  }

  Serial.println();

  Serial.println("WiFi Connected!");

  Serial.print("IP Wemos : ");
  Serial.println(WiFi.localIP());

  Serial.print("Gateway  : ");
  Serial.println(WiFi.gatewayIP());

  Serial.print("Broker   : ");
  Serial.println(mqtt_server);

  Serial.print("IP Wemos : ");
  Serial.println(WiFi.localIP());

  Serial.print("RSSI      : ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  Serial.println("================================");
}

void testTCP()
{
  WiFiClient testClient;

  Serial.print("TCP test ke broker... ");

  if (testClient.connect("192.168.1.20", 1883))
  {
    Serial.println("BERHASIL");
    testClient.stop();
  }
  else
  {
    Serial.println("GAGAL");
  }
}

// ==========================================================
// MQTT CALLBACK
// ==========================================================

void mqttCallback(
  char* topic,
  byte* payload,
  unsigned int length
)
{
  String message = "";

  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  message.trim();

  Serial.println();
  Serial.println("================================");

  Serial.print("MQTT Topic   : ");
  Serial.println(topic);

  Serial.print("MQTT Message : ");
  Serial.println(message);

  // ========================================================
  // MODE AUTO / MANUAL
  // ========================================================

  if (String(topic) == topicMode)
  {
    if (message == "MANUAL")
    {
      modeManual = true;

      Serial.println("MODE → MANUAL");

      setRelay(relayManual);
    }

    else if (message == "AUTO")
    {
      modeManual = false;

      Serial.println("MODE → AUTO");

      // Kembali ke kontrol suhu
      if (!sensorError)
      {
        if (suhu < 30.0)
        {
          setRelay(false);
        }
        else
        {
          setRelay(true);
        }
      }
    }
  }

  // ========================================================
  // KONTROL FAN MANUAL
  // ========================================================

  else if (String(topic) == topicFan)
  {
    if (message == "ON")
    {
      relayManual = true;

      Serial.println("FAN MANUAL → ON");
    }

    else if (message == "OFF")
    {
      relayManual = false;

      Serial.println("FAN MANUAL → OFF");
    }

    if (modeManual)
    {
      setRelay(relayManual);
    }
    else
    {
      Serial.println(
        "Perintah FAN diabaikan karena MODE AUTO"
      );
    }
  }

  Serial.println("================================");
}

// ==========================================================
// MQTT CONNECT
// ==========================================================

bool connectMQTT()
{
  Serial.println();

  Serial.print("Connecting MQTT... ");

  String clientID =
    String("Wemos-") +
    DEVICE_ID +
    "-" +
    String(ESP.getChipId());

  if (mqttClient.connect(clientID.c_str()))
  {
    Serial.println("CONNECTED");

    mqttClient.subscribe(topicMode.c_str());

    mqttClient.subscribe(topicFan.c_str());

    Serial.println("Subscribed:");

    Serial.println(topicMode);

    Serial.println(topicFan);

    return true;
  }

  Serial.print("FAILED, rc = ");

  Serial.println(mqttClient.state());

  return false;
}

// ==========================================================
// PUBLISH STATUS
// ==========================================================

void publishStatus()
{
  if (!mqttClient.connected())
  {
    return;
  }

  bool relayAktif = getRelayStatus();

  String mode;

  if (modeManual)
  {
    mode = "MANUAL";
  }
  else
  {
    mode = "AUTO";
  }

  String sensorStatus;

  if (sensorError)
  {
    sensorStatus = "ERROR";
  }
  else
  {
    sensorStatus = "OK";
  }

  // ========================================================
  // JSON
  // ========================================================

  String payload = "{";

  payload += "\"device_id\":\"";
  payload += DEVICE_ID;
  payload += "\",";

  payload += "\"temperature\":";
  payload += String(suhu, 2);
  payload += ",";

  payload += "\"temperature_status\":\"";
  payload += statusSuhu;
  payload += "\",";

  payload += "\"fan\":";
  payload += relayAktif ? "true" : "false";
  payload += ",";

  payload += "\"mode\":\"";
  payload += mode;
  payload += "\",";

  payload += "\"sensor\":\"";
  payload += sensorStatus;
  payload += "\",";

  payload += "\"wifi_rssi\":";
  payload += WiFi.RSSI();

  payload += "}";

  // ========================================================
  // PUBLISH
  // ========================================================

  mqttClient.publish(
    topicStatus.c_str(),
    payload.c_str(),
    true
  );

  Serial.println();
  Serial.println("MQTT PUBLISH");

  Serial.print("Topic   : ");
  Serial.println(topicStatus);

  Serial.print("Payload : ");
  Serial.println(payload);
}

// ==========================================================
// BACA SUHU
// ==========================================================

void bacaSuhu()
{
  sensors.requestTemperatures();

  suhu = sensors.getTempCByIndex(0);

  // ========================================================
  // SENSOR ERROR
  // ========================================================

  if (suhu == DEVICE_DISCONNECTED_C)
  {
    sensorError = true;

    statusSuhu = "SENSOR ERROR";

    digitalWrite(
      LED_NORMAL,
      LOW
    );

    digitalWrite(
      LED_PANAS,
      LOW
    );

    digitalWrite(
      LED_SANGAT_PANAS,
      LOW
    );

    // Sama seperti program awal:
    // sensor error → relay OFF
    setRelay(false);

    Serial.println();
    Serial.println("================================");

    Serial.println(
      "ERROR: DS18B20 TIDAK TERDETEKSI"
    );

    Serial.println("================================");

    publishStatus();

    return;
  }

  sensorError = false;

  // ========================================================
  // STATUS SUHU + LED
  // ========================================================

  if (suhu < 30.0)
  {
    digitalWrite(
      LED_NORMAL,
      HIGH
    );

    digitalWrite(
      LED_PANAS,
      LOW
    );

    digitalWrite(
      LED_SANGAT_PANAS,
      LOW
    );

    statusSuhu = "NORMAL";
  }

  else if (suhu <= 40.0)
  {
    digitalWrite(
      LED_NORMAL,
      LOW
    );

    digitalWrite(
      LED_PANAS,
      HIGH
    );

    digitalWrite(
      LED_SANGAT_PANAS,
      LOW
    );

    statusSuhu = "PANAS";
  }

  else
  {
    digitalWrite(
      LED_NORMAL,
      LOW
    );

    digitalWrite(
      LED_PANAS,
      LOW
    );

    digitalWrite(
      LED_SANGAT_PANAS,
      HIGH
    );

    statusSuhu = "SANGAT PANAS";
  }

  // ========================================================
  // KONTROL FAN
  // ========================================================

  if (modeManual)
  {
    setRelay(relayManual);
  }

  else
  {
    if (suhu < 30.0)
    {
      setRelay(false);
    }
    else
    {
      setRelay(true);
    }
  }

  // ========================================================
  // SERIAL
  // ========================================================

  Serial.println();
  Serial.println("================================");

  Serial.print("Device     : ");
  Serial.println(DEVICE_ID);

  Serial.print("Suhu       : ");
  Serial.print(suhu);
  Serial.println(" °C");

  Serial.print("Status     : ");
  Serial.println(statusSuhu);

  Serial.print("Mode       : ");

  if (modeManual)
  {
    Serial.println("MANUAL");
  }
  else
  {
    Serial.println("AUTO");
  }

  Serial.print("Fan        : ");

  if (getRelayStatus())
  {
    Serial.println("ON");
  }
  else
  {
    Serial.println("OFF");
  }

  Serial.print("WiFi RSSI  : ");

  Serial.print(WiFi.RSSI());

  Serial.println(" dBm");

  Serial.println("================================");

  // ========================================================
  // MQTT
  // ========================================================

  publishStatus();
}

// ==========================================================
// SETUP
// ==========================================================

void setup()
{
  Serial.begin(115200);

  delay(100);

  Serial.println();
  Serial.println();

  Serial.println("================================");

  Serial.println("COOLING FAN INVERTER PLTS");

  Serial.println("MQTT VERSION");

  Serial.println("================================");

  // ========================================================
  // PIN
  // ========================================================

  pinMode(
    LED_NORMAL,
    OUTPUT
  );

  pinMode(
    LED_PANAS,
    OUTPUT
  );

  pinMode(
    LED_SANGAT_PANAS,
    OUTPUT
  );

  pinMode(
    RELAY,
    OUTPUT
  );

  // ========================================================
  // KONDISI AWAL
  // ========================================================

  digitalWrite(
    LED_NORMAL,
    LOW
  );

  digitalWrite(
    LED_PANAS,
    LOW
  );

  digitalWrite(
    LED_SANGAT_PANAS,
    LOW
  );

  setRelay(false);

  // ========================================================
  // DS18B20
  // ========================================================

  sensors.begin();

  Serial.println(
    "DS18B20 initialized."
  );

  // ========================================================
  // MQTT TOPICS
  // ========================================================

  topicStatus =
    String("plts/") +
    DEVICE_ID +
    "/status";

  topicMode =
    String("plts/") +
    DEVICE_ID +
    "/command/mode";

  topicFan =
    String("plts/") +
    DEVICE_ID +
    "/command/fan";

  // ========================================================
  // WIFI
  // ========================================================

  connectWiFi();

  testTCP();

  // ========================================================
  // MQTT
  // ========================================================

  mqttClient.setServer(
    mqtt_server,
    mqtt_port
  );

  mqttClient.setCallback(
    mqttCallback
  );

  mqttClient.setBufferSize(512);

  connectMQTT();

  // Baca langsung saat startup
  bacaSuhu();

  Serial.println();
  Serial.println("SYSTEM READY");
}

// ==========================================================
// LOOP
// ==========================================================

void loop()
{
  // ========================================================
  // WIFI RECONNECT
  // ========================================================

  if (WiFi.status() != WL_CONNECTED)
  {
    connectWiFi();
  }

  // ========================================================
  // MQTT
  // ========================================================

  if (mqttClient.connected())
  {
    mqttClient.loop();
  }
  else
  {
    unsigned long currentMillis = millis();

    if (
      currentMillis -
      previousMQTTReconnect >=
      mqttReconnectInterval
    )
    {
      previousMQTTReconnect =
        currentMillis;

      connectMQTT();
    }
  }

  // ========================================================
  // SENSOR TIMER
  // ========================================================

  unsigned long currentMillis = millis();

  if (
    currentMillis -
    previousSensorMillis >=
    sensorInterval
  )
  {
    previousSensorMillis =
      currentMillis;

    bacaSuhu();
  }
}
