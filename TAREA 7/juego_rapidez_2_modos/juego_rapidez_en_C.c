// ============================================================
//  JUEGO DE RAPIDEZ - 2 MODOS (VERSION EN C)
//  Cambio de modo: presiona IZQ + DER al mismo tiempo,
//  suelta, y repite otra vez (2 veces corridas).
//  MODO 1 = CLASICO   |   MODO 2 = ALEATORIO
// ============================================================
//  Nota: usa el framework Arduino de ESP32 (las librerias
//  WiFi.h y PubSubClient son internamente C++), pero todo el
//  codigo del juego esta escrito en estilo C puro:
//  sin String, sin referencias, con structs, punteros,
//  char arrays y snprintf.
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <stdio.h>

// Descomenta la siguiente linea para simular en Wokwi:
// #define SIMULACION_WOKWI

#ifdef SIMULACION_WOKWI
static const char* WIFI_SSID = "Wokwi-GUEST";
static const char* WIFI_PASS = "";
#else
static const char* WIFI_SSID = "Rapidito";
static const char* WIFI_PASS = "Adm1N2584km";
#endif

static const char* MQTT_HOST = "broker.emqx.io";  // sin "mqtt://"
#define MQTT_PORT 1883

static const char* NTP_SERVER_1 = "pool.ntp.org";
static const char* NTP_SERVER_2 = "time.nist.gov";
#define GMT_OFFSET_SEC (-4L * 3600L)  // UTC-4 (Republica Dominicana)

// ---------------- PINES ----------------
#define BTN1_PIN 5   // Boton izquierdo
#define BTN2_PIN 4   // Boton derecho
#define LED1_PIN 14  // LED izquierdo
#define LED2_PIN 12  // LED derecho

#define DEBOUNCE_US     5000UL  // 5 ms antirrebote
#define COMBO_WINDOW_MS 1500UL  // ventana max entre las 2 pulsaciones dobles
#define RESULT_SHOW_MS  2500UL  // tiempo mostrando resultados

// ---------------- TIPOS ----------------
typedef enum {
  WAITING_START,
  ARMING,
  REACT_LED1,
  REACT_LED2,
  SHOW_RESULTS
} GameState;

typedef enum {
  MODO_CLASICO = 1,
  MODO_ALEATORIO = 2
} GameMode;

typedef struct {
  bool raw;
  bool stable;
  bool prev;
  uint32_t tChange;
} Btn;

// ---------------- VARIABLES GLOBALES ----------------
static WiFiClient net;
static PubSubClient mqtt(net);

static GameState st   = WAITING_START;
static GameMode  modo = MODO_CLASICO;

static Btn b1 = { false, true, true, 0 };
static Btn b2 = { false, true, true, 0 };

// detector del gesto "doble pulsacion simultanea"
static bool     comboBothPrev = false;
static uint8_t  comboCount    = 0;
static uint32_t comboLastMs   = 0;

static uint32_t tLedOn = 0, tRelease = 0, tArm = 0, tArmDur = 0, tResults = 0;
static uint32_t rxn1 = 0, rxn2 = 0;
static uint8_t  objetivo  = 1;
static bool     publicado = false;

// ---------------- PROTOTIPOS ----------------
static void ledsOff(void);
static void resetJuego(void);
static void indicarModo(void);
static void cambiarModo(void);
static void updateBtn(Btn* b, uint8_t pin, uint32_t nowUs);
static void mqttCallback(char* topic, byte* payload, unsigned int length);
static void wifiMqtt(void);
static void horaActual(char* buf, size_t len);
static void publicarResultado(void);

// ---------------- FUNCIONES ----------------

static void ledsOff(void) {
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
}

static void resetJuego(void) {
  ledsOff();
  st        = WAITING_START;
  publicado = false;
  rxn1 = 0;
  rxn2 = 0;
}

static void indicarModo(void) {
  uint8_t i;
  for (i = 0; i < (uint8_t)modo; i++) {
    digitalWrite(LED1_PIN, HIGH);
    digitalWrite(LED2_PIN, HIGH);
    delay(150);
    ledsOff();
    delay(200);
  }
}

static void cambiarModo(void) {
  modo = (modo == MODO_CLASICO) ? MODO_ALEATORIO : MODO_CLASICO;
  resetJuego();
  indicarModo();
  Serial.printf("\n>>> CAMBIO DE MODO -> %d (%s)\n", (int)modo,
                (modo == MODO_CLASICO) ? "CLASICO" : "ALEATORIO");
}

static void updateBtn(Btn* b, uint8_t pin, uint32_t nowUs) {
  bool raw = (digitalRead(pin) == LOW);
  if (raw != b->raw) {
    b->raw = raw;
    b->tChange = nowUs;
  }
  if ((nowUs - b->tChange) > DEBOUNCE_US) {
    b->stable = raw;
  }
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("MQTT [%s]: %.*s\n", topic, (int)length, (char*)payload);
}

static void wifiMqtt(void) {
  static uint32_t tTry = 0;
  char id[40];

  if (WiFi.status() != WL_CONNECTED) return;

  if (!mqtt.connected()) {
    if ((millis() - tTry) > 5000UL) {
      tTry = millis();
      snprintf(id, sizeof(id), "ESP32-Juego-%08X", (unsigned)micros());
      if (mqtt.connect(id)) {
        mqtt.subscribe("reaction_game/control");
      }
    }
    return;
  }
  mqtt.loop();
}

static void horaActual(char* buf, size_t len) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
    snprintf(buf, len, "sin_hora");
    return;
  }
  strftime(buf, len, "%H:%M:%S", &timeinfo);
}

static void publicarResultado(void) {
  char payload[160];
  char ts[24];

  if (!mqtt.connected()) {
    Serial.println("(MQTT no conectado: resultado no publicado)");
    return;
  }

  horaActual(ts, sizeof(ts));
  snprintf(payload, sizeof(payload),
           "{\"mode\":%d,\"reaction1_ms\":%lu,\"reaction2_ms\":%lu,\"time\":\"%s\"}",
           (int)modo, (unsigned long)rxn1, (unsigned long)rxn2, ts);

  mqtt.publish("reaction_game/times", payload);
  Serial.print("MQTT -> ");
  Serial.println(payload);
}

// ---------------- PRINCIPAL ----------------

void setup(void) {
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  ledsOff();

  Serial.begin(115200);
  Serial.println("\n=== JUEGO DE RAPIDEZ - 2 MODOS (C) ===");
  Serial.println("Cambio de modo: IZQ+DER juntos x2 veces seguidas");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTime(GMT_OFFSET_SEC, 0, NTP_SERVER_1, NTP_SERVER_2);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
}

void loop(void) {
  uint32_t nowUs;
  uint32_t nowMs;
  bool p1, p2, press1, rel1, press2, rel2, both;

  wifiMqtt();

  nowUs = micros();
  nowMs = millis();

  updateBtn(&b1, BTN1_PIN, nowUs);
  updateBtn(&b2, BTN2_PIN, nowUs);

  p1 = !b1.stable;
  p2 = !b2.stable;
  press1 = p1 && b1.prev;
  rel1   = !p1 && !b1.prev;
  press2 = p2 && b2.prev;
  rel2   = !p2 && !b2.prev;
  b1.prev = p1;
  b2.prev = p2;

  // ---- GESTO DE CAMBIO DE MODO ----
  // Cuenta pulsaciones donde AMBOS botones estan presionados a la vez.
  // Si ocurren 2 seguidas (dentro de COMBO_WINDOW_MS) -> cambia el modo.
  both = p1 && p2;
  if (both && !comboBothPrev) {
    if (comboCount != 0 && (nowMs - comboLastMs) > COMBO_WINDOW_MS) {
      comboCount = 0;
    }
    comboCount++;
    comboLastMs = nowMs;
    if (comboCount >= 2) {
      comboCount = 0;
      cambiarModo();
    }
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
      if ((nowUs - tArm) >= tArmDur) {
        objetivo = (modo == MODO_CLASICO) ? 1 : (uint8_t)random(1, 3);
        tLedOn   = nowUs;
        Serial.println("¡YA!");
        if (objetivo == 1) { digitalWrite(LED1_PIN, HIGH); st = REACT_LED1; }
        else               { digitalWrite(LED2_PIN, HIGH); st = REACT_LED2; }
      }
      break;

    case REACT_LED1:  // reaccion: soltar BTN1 lo mas rapido posible
      if (rel1) {
        rxn1 = (nowUs - tLedOn) / 1000UL;
        digitalWrite(LED1_PIN, LOW);
        tRelease = nowUs;
        Serial.printf("Reaccion LED1: %lu ms\n", (unsigned long)rxn1);
        st = (modo == MODO_CLASICO) ? REACT_LED2 : SHOW_RESULTS;
      }
      break;

    case REACT_LED2:  // reaccion: presionar BTN2 lo mas rapido posible
      if (press2) {
        rxn2 = (nowUs - ((modo == MODO_CLASICO) ? tRelease : tLedOn)) / 1000UL;
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
      if ((nowMs - tResults) >= RESULT_SHOW_MS) resetJuego();
      break;
  }
}
