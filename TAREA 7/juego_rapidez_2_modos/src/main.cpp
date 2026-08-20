#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ============================================================
//  JUEGO DE RAPIDEZ - 2 MODOS
//  Cambio de modo: presiona IZQ + DER al mismo tiempo,
//  suelta, y repite otra vez (2 veces corridas).
//  MODO 1 = CLASICO   |   MODO 2 = ALEATORIO
// ============================================================

// Descomenta la siguiente linea para simular en Wokwi:
// #define SIMULACION_WOKWI

#ifdef SIMULACION_WOKWI
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
#else
const char* WIFI_SSID = "Rapidito";
const char* WIFI_PASS = "Adm1N2584km";
#endif

const char* MQTT_HOST    = "broker.emqx.io";  // sin "mqtt://"
const uint16_t MQTT_PORT = 1883;

// ---------------- PINES ----------------
constexpr uint8_t BTN1_PIN = 5;    // Boton izquierdo
constexpr uint8_t BTN2_PIN = 4;    // Boton derecho
constexpr uint8_t LED1_PIN = 14;   // LED izquierdo
constexpr uint8_t LED2_PIN = 12;   // LED derecho

constexpr uint32_t DEBOUNCE_US     = 5000;  // 5 ms antirrebote
constexpr uint32_t COMBO_WINDOW_MS = 1500;  // ventana max entre las 2 pulsaciones dobles
constexpr uint32_t RESULT_SHOW_MS  = 2500;  // tiempo mostrando resultados

WiFiClient net;
PubSubClient mqtt(net);

enum GameState : uint8_t { WAITING_START, ARMING, REACT_LED1, REACT_LED2, SHOW_RESULTS };
enum GameMode  : uint8_t { MODO_CLASICO = 1, MODO_ALEATORIO = 2 };

GameState st   = WAITING_START;
GameMode  modo = MODO_CLASICO;

struct Btn {
  bool raw = false, stable = true, prev = true;
  uint32_t tChange = 0;
};
Btn b1, b2;

// detector del gesto "doble pulsacion simultanea"
bool     comboBothPrev = false;
uint8_t  comboCount    = 0;
uint32_t comboLastMs   = 0;

uint32_t tLedOn = 0, tRelease = 0, tArm = 0, tArmDur = 0, tResults = 0;
uint32_t rxn1 = 0, rxn2 = 0;
uint8_t  objetivo  = 1;
bool     publicado = false;

void ledsOff() { digitalWrite(LED1_PIN, LOW); digitalWrite(LED2_PIN, LOW); }

void resetJuego() {
  ledsOff();
  st        = WAITING_START;
  publicado = false;
  rxn1 = rxn2 = 0;
}

void indicarModo() {
  for (uint8_t i = 0; i < (uint8_t)modo; i++) {
    digitalWrite(LED1_PIN, HIGH);
    digitalWrite(LED2_PIN, HIGH);
    delay(150);
    ledsOff();
    delay(200);
  }
}

void cambiarModo() {
  modo = (modo == MODO_CLASICO) ? MODO_ALEATORIO : MODO_CLASICO;
  resetJuego();
  indicarModo();
  Serial.printf("\n>>> CAMBIO DE MODO -> %d (%s)\n", (int)modo,
                modo == MODO_CLASICO ? "CLASICO" : "ALEATORIO");
}

void updateBtn(Btn &b, uint8_t pin, uint32_t nowUs) {
  bool raw = digitalRead(pin) == LOW;
  if (raw != b.raw) { b.raw = raw; b.tChange = nowUs; }
  if (nowUs - b.tChange > DEBOUNCE_US) b.stable = raw;
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("MQTT [%s]: %.*s\n", topic, length, (char*)payload);
}

void wifiMqtt() {
  if (WiFi.status() != WL_CONNECTED) return;
  static uint32_t tTry = 0;
  if (!mqtt.connected()) {
    if (millis() - tTry > 5000) {
      tTry = millis();
      String id = String("ESP32-Juego-") + String((uint32_t)micros(), HEX);
      if (mqtt.connect(id.c_str())) mqtt.subscribe("reaction_game/control");
    }
    return;
  }
  mqtt.loop();
}

void publicarResultado() {
  if (!mqtt.connected()) {
    Serial.println("(MQTT no conectado: resultado no publicado)");
    return;
  }
  String payload = String("{\"mode\":") + (int)modo +
                   ",\"reaction1_ms\":" + rxn1 +
                   ",\"reaction2_ms\":" + rxn2 + "}";
  mqtt.publish("reaction_game/times", payload.c_str());
  Serial.println("MQTT -> " + payload);
}

void setup() {
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  ledsOff();

  Serial.begin(115200);
  Serial.println("\n=== JUEGO DE RAPIDEZ - 2 MODOS ===");
  Serial.println("Cambio de modo: IZQ+DER juntos x2 veces seguidas");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
}

void loop() {
  wifiMqtt();

  uint32_t nowUs = micros();
  uint32_t nowMs = millis();

  updateBtn(b1, BTN1_PIN, nowUs);
  updateBtn(b2, BTN2_PIN, nowUs);

  bool p1 = !b1.stable, p2 = !b2.stable;
  bool press1 = p1 && b1.prev, rel1 = !p1 && !b1.prev;
  bool press2 = p2 && b2.prev, rel2 = !p2 && !b2.prev;
  b1.prev = p1;
  b2.prev = p2;

  // ---- GESTO DE CAMBIO DE MODO ----
  // Cuenta pulsaciones donde AMBOS botones estan presionados a la vez.
  // Si ocurren 2 seguidas (dentro de COMBO_WINDOW_MS) -> cambia el modo.
  bool both = p1 && p2;
  if (both && !comboBothPrev) {
    if (comboCount != 0 && nowMs - comboLastMs > COMBO_WINDOW_MS) comboCount = 0;
    comboCount++;
    comboLastMs = nowMs;
    if (comboCount >= 2) { comboCount = 0; cambiarModo(); }
  }
  comboBothPrev = both;

  switch (st) {
    case WAITING_START:
      ledsOff();
      if (press1) {
        tArm    = nowUs;
        tArmDur = random(1000000UL, 8000000UL);  // espera aleatoria 1-8 s
        st      = ARMING;
      }
      break;

    case ARMING:
      if (rel1) {  // solto antes de encender el LED: salida en falso
        Serial.println("Salida en falso!");
        resetJuego();
        break;
      }
      if (nowUs - tArm >= tArmDur) {
        objetivo = (modo == MODO_CLASICO) ? 1 : (uint8_t)random(1, 3);
        tLedOn   = nowUs;
        Serial.println("¡YA!");
        if (objetivo == 1) { digitalWrite(LED1_PIN, HIGH); st = REACT_LED1; }
        else               { digitalWrite(LED2_PIN, HIGH); st = REACT_LED2; }
      }
      break;

    case REACT_LED1:  // reaccion: soltar BTN1 lo mas rapido posible
      if (rel1) {
        rxn1 = (nowUs - tLedOn) / 1000;
        digitalWrite(LED1_PIN, LOW);
        tRelease = nowUs;
        Serial.printf("Reaccion LED1: %lu ms\n", (unsigned long)rxn1);
        st = (modo == MODO_CLASICO) ? REACT_LED2 : SHOW_RESULTS;
      }
      break;

    case REACT_LED2:  // reaccion: presionar BTN2 lo mas rapido posible
      if (press2) {
        rxn2 = (nowUs - ((modo == MODO_CLASICO) ? tRelease : tLedOn)) / 1000;
        Serial.printf("Reaccion LED2: %lu ms\n", (unsigned long)rxn2);
        st = SHOW_RESULTS;
      }
      break;

    case SHOW_RESULTS:
      if (!publicado) {
        publicado = true;
        ledsOff();
        digitalWrite(LED1_PIN, HIGH);  // ambos LEDs = mostrando resultado
        digitalWrite(LED2_PIN, HIGH);
        tResults = nowMs;
        publicarResultado();
      }
      if (nowMs - tResults >= RESULT_SHOW_MS) resetJuego();
      break;
  }
}
