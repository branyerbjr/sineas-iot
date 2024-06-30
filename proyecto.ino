#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <PubSubClient.h>

// Definición de credenciales WiFi del punto de acceso
char defaultSSID[15];

// Definición de credenciales MQTT
const char* mqtt_server = "";
const int mqtt_port = 1883;
const char* mqtt_user = "";
const char* mqtt_password = "";

// Inicialización de objetos WiFi y MQTT
WiFiClient espClient;
PubSubClient client(espClient);

Preferences preferences;
WebServer server(80);

// Definición de pines
const int relayPin = 27;  // Cambia el número de pin según tu configuración

const int reconnectInterval = 600000; // 10 minutos en milisegundos
unsigned long previousMillis = 0;
bool connected = false;

void setup() {
  Serial.begin(115200);
  preferences.begin("wifiCreds", false);

  // Configurar el pin del relé como salida
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, HIGH); // Inicializar el relé en estado apagado

  // Generar un número aleatorio para el nombre del punto de acceso
  randomSeed(analogRead(0));
  int randomNumber = random(10000, 99999);
  snprintf(defaultSSID, sizeof(defaultSSID), "SINEASAP-%05d", randomNumber);

  WiFi.mode(WIFI_AP_STA);
  createAccessPoint();

  server.on("/config", HTTP_POST, handleConfig);
  server.begin();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  connectToWiFi();
}

void loop() {
  server.handleClient();

  unsigned long currentMillis = millis();
  if (WiFi.status() != WL_CONNECTED && currentMillis - previousMillis >= reconnectInterval) {
    previousMillis = currentMillis;
    createAccessPoint();
    connectToWiFi();
  }

  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    reconnectMqtt();
  }

  client.loop();
}

void connectToWiFi() {
  String ssid = preferences.getString("ssid", "");
  String password = preferences.getString("password", "");

  if (ssid.length() > 0) {
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.println("Connecting to WiFi...");

    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED && attempt < 10) {
      delay(1000);
      Serial.print(".");
      attempt++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      connected = true;
      WiFi.softAPdisconnect(true); // Desactivar el punto de acceso si está conectado
      Serial.println("Access Point Disabled.");
    } else {
      Serial.println("Failed to connect.");
      connected = false;
      createAccessPoint(); // Crear AP si la conexión WiFi falla
    }
  } else {
    Serial.println("No saved WiFi credentials.");
    connected = false;
    createAccessPoint(); // Crear AP si no hay credenciales guardadas
  }
}

void createAccessPoint() {
  Serial.println("Creating Access Point...");
  WiFi.softAP(defaultSSID);

  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  connected = false;
}

void handleConfig() {
  if (server.hasArg("plain") == false) {
    server.send(400, "text/plain", "Body not received");
    return;
  }

  String body = server.arg("plain");
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  const char* ssid = doc["ssid"];
  const char* password = doc["password"];
  const char* mqtt_topic = doc["mqtt_topic"];

  if (ssid && password && mqtt_topic) {
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.putString("mqtt_topic", mqtt_topic);

    server.send(200, "application/json", "{\"status\":\"saved\"}");

    WiFi.disconnect();
    connectToWiFi();
  } else {
    server.send(400, "application/json", "{\"status\":\"failed\",\"reason\":\"Missing ssid, password, or mqtt_topic\"}");
  }
}

void reconnectMqtt() {
  String mqtt_topic = preferences.getString("mqtt_topic", "");

  while (!client.connected() && WiFi.status() == WL_CONNECTED) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("connected");

      // Suscribirse al tema MQTT para el control del relé
      if (mqtt_topic.length() > 0) {
        client.subscribe(mqtt_topic.c_str());
        Serial.print("Subscribed to topic: ");
        Serial.println(mqtt_topic);
      }
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Manejar mensajes MQTT recibidos en el tema suscrito
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Mensaje recibido en el tema '");
  Serial.print(topic);
  Serial.print("': ");
  Serial.println(message);

  // Verificar el mensaje y controlar el relé en consecuencia
  String mqtt_topic = preferences.getString("mqtt_topic", "");
  if (String(topic) == mqtt_topic) {
    if (message == "on") {
      digitalWrite(relayPin, LOW); // Encender el relé
      Serial.println("Relé encendido");
    } else if (message == "off") {
      digitalWrite(relayPin, HIGH); // Apagar el relé
      Serial.println("Relé apagado");
    }
  }
}
