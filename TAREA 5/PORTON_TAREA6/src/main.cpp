#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= Hardware =================
constexpr uint8_t PIN_MOTOR_IN1 = 26;   // LED rojo en Wokwi = motor abriendo
constexpr uint8_t PIN_MOTOR_IN2 = 27;   // LED amarillo en Wokwi = motor cerrando
constexpr uint8_t PIN_MOTOR_PWM = 25;   // LED verde en Wokwi = velocidad PWM
constexpr uint8_t PWM_CHANNEL   = 0;
constexpr uint32_t PWM_FREQ_HZ  = 20000;
constexpr uint8_t PWM_RES_BITS  = 10;

constexpr uint8_t PIN_ENCODER_A = 32;
constexpr uint8_t PIN_ENCODER_B = 33;

constexpr uint8_t PIN_LIMIT_CLOSED = 16;  // boton activo en LOW
constexpr uint8_t PIN_LIMIT_OPEN   = 17;  // boton activo en LOW
constexpr uint8_t PIN_FTC_SENSOR   = 14;  // sensor obstaculo, activo en LOW
constexpr uint8_t PIN_LOCAL_OPEN   = 18;
constexpr uint8_t PIN_LOCAL_CLOSE  = 19;
constexpr uint8_t PIN_LOCAL_STOP   = 23;

constexpr uint8_t PIN_BUZZER     = 13;    // buzzer de aviso
constexpr uint8_t PIN_STATUS_LED = 2;     // LED integrado
constexpr uint8_t PIN_I2C_SDA    = 21;
constexpr uint8_t PIN_I2C_SCL    = 22;
constexpr uint8_t OLED_ADDR      = 0x3C;

// ================= Red =================
const char *WIFI_SSID     = "Wokwi-GUEST";
const char *WIFI_PASSWORD = "";
const char *MQTT_HOST     = "broker.emqx.io";
constexpr uint16_t MQTT_PORT = 1883;
const char *DEVICE_ID     = "porton_wokwi_01";

char TOPIC_CMD[48];
char TOPIC_STATE[48];
char TOPIC_EVENT[48];
char TOPIC_FAULT[48];
char TOPIC_CONFIG_SET[64];

// ================= Configuracion =================
uint32_t motorDutyPercent          = 70;
uint32_t movementTimeoutMs         = 20000;
uint32_t stallTimeoutMs            = 3000;
uint32_t obstructionReverseDelayMs = 800;
bool     ftcReverse                = true;
bool     autoCloseEnabled          = false;
uint32_t autoCloseDelayMs          = 10000;
int32_t  countsClosed              = 0;
int32_t  countsOpen                = 2000;

// ================= Buzzer =================
constexpr uint32_t BEEP_MOVE_PERIOD_MS = 1000;  // pitido periodico mientras se mueve
constexpr uint32_t BEEP_MOVE_ON_MS     = 60;

struct BuzzStep {
  uint16_t on_ms;
  uint16_t off_ms;
};

BuzzStep PATRON_ABRIR[]   = {{90, 110}, {90, 110}, {90, 0}, {0, 0}};             // 2 pitidos al abrir
BuzzStep PATRON_CERRAR[]  = {{90, 90}, {90, 90}, {90, 90}, {90, 0}, {0, 0}};     // 3 pitidos al cerrar
BuzzStep PATRON_LLEGADA[] = {{350, 0}, {0, 0}};                                  // pitido largo al llegar
BuzzStep PATRON_FALLA[]   = {{60, 160}, {60, 160}, {60, 160}, {60, 0}, {0, 0}};

BuzzStep *buzzSeq = nullptr;
uint8_t buzzIdx = 0;
uint32_t buzzStepStartMs = 0;
bool buzzOn = false;

void buzzerSilence() {
  buzzSeq = nullptr;
  buzzIdx = 0;
  buzzOn = false;
  digitalWrite(PIN_BUZZER, LOW);
}

void buzzerPlay(BuzzStep *seq) {
  buzzSeq = seq;
  buzzIdx = 0;
  buzzStepStartMs = millis();
  buzzOn = true;
  digitalWrite(PIN_BUZZER, HIGH);
}

void buzzerTick(bool moving) {
  const uint32_t now = millis();

  if (buzzSeq != nullptr) {
    const BuzzStep &st = buzzSeq[buzzIdx];
    if (st.on_ms == 0 && st.off_ms == 0) {
      buzzerSilence();
      return;
    }
    const uint32_t elapsed = now - buzzStepStartMs;
    if (buzzOn) {
      if (elapsed >= st.on_ms) {
        buzzOn = false;
        digitalWrite(PIN_BUZZER, LOW);
        buzzStepStartMs = now;
      }
    } else if (elapsed >= st.off_ms) {
      buzzIdx++;
      buzzStepStartMs = now;
      const BuzzStep &nx = buzzSeq[buzzIdx];
      if (nx.on_ms == 0 && nx.off_ms == 0) {
        buzzerSilence();
        return;
      }
      buzzOn = true;
      digitalWrite(PIN_BUZZER, HIGH);
    }
    return;
  }

  static uint32_t moveBeepT0 = 0;
  static bool moveBeepPhase = false;

  if (moving) {
    if (!moveBeepPhase && now - moveBeepT0 >= BEEP_MOVE_PERIOD_MS - BEEP_MOVE_ON_MS) {
      moveBeepPhase = true;
      moveBeepT0 = now;
      digitalWrite(PIN_BUZZER, HIGH);
    } else if (moveBeepPhase && now - moveBeepT0 >= BEEP_MOVE_ON_MS) {
      moveBeepPhase = false;
      digitalWrite(PIN_BUZZER, LOW);
    }
  } else {
    moveBeepPhase = false;
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// ================= Motor =================
enum MotorDir { MOTOR_STOP, MOTOR_ABRIR, MOTOR_CERRAR };
MotorDir motorDir = MOTOR_STOP;

void motorStop() {
  motorDir = MOTOR_STOP;
  ledcWrite(PWM_CHANNEL, 0);
  digitalWrite(PIN_MOTOR_IN1, LOW);
  digitalWrite(PIN_MOTOR_IN2, LOW);
}

void motorDrive(MotorDir dir) {
  if (dir == MOTOR_STOP) {
    motorStop();
    return;
  }
  motorDir = dir;
  digitalWrite(PIN_MOTOR_IN1, dir == MOTOR_ABRIR ? HIGH : LOW);
  digitalWrite(PIN_MOTOR_IN2, dir == MOTOR_CERRAR ? HIGH : LOW);
  ledcWrite(PWM_CHANNEL, (uint32_t)(1023UL * motorDutyPercent) / 100UL);
}

// ================= Encoder =================
volatile int32_t encoderCount = 0;
bool encoderInverted = false;

void IRAM_ATTR encoderIsr() {
  const int a = digitalRead(PIN_ENCODER_A);
  const int b = digitalRead(PIN_ENCODER_B);
  int delta = (a == b) ? 1 : -1;
  if (encoderInverted) delta = -delta;
  encoderCount += delta;
}

void encoderReset(int32_t value) {
  noInterrupts();
  encoderCount = value;
  interrupts();
}

int32_t encoderGet() {
  noInterrupts();
  const int32_t v = encoderCount;
  interrupts();
  return v;
}

// ================= Botones locales =================
struct Boton {
  uint8_t pin;
  bool estable;
  uint32_t ultimoCambioMs;
};

Boton botones[] = {
  {PIN_LOCAL_OPEN, true, 0},
  {PIN_LOCAL_CLOSE, true, 0},
  {PIN_LOCAL_STOP, true, 0},
};

bool botonFuePresionado(Boton &b) {
  const bool leido = digitalRead(b.pin) == LOW;
  static bool pendiente[sizeof(botones) / sizeof(botones[0])] = {false, false, false};
  const uint8_t idx = &b - botones;
  bool disparo = false;

  if (leido != b.estable && millis() - b.ultimoCambioMs > 30) {
    b.estable = leido;
    b.ultimoCambioMs = millis();
    if (leido) {
      disparo = true;
    }
  }
  (void)pendiente;
  (void)idx;
  return disparo;
}

// ================= Estado =================
enum PortonState {
  ST_INIT,
  ST_DETENIDO,
  ST_CERRADO,
  ST_ABRIENDO,
  ST_ABIERTO,
  ST_CERRANDO,
  ST_OBSTRUIDO,
  ST_FALLA,
};

enum PortonCmd { CMD_NONE, CMD_OPEN, CMD_CLOSE, CMD_STOP, CMD_TOGGLE, CMD_RESET_FAULT };

PortonState estado = ST_INIT;
PortonCmd cmdPendiente = CMD_NONE;
uint32_t stateEnterMs = 0;
int32_t lastEncCount = 0;
uint32_t lastEncMoveMs = 0;
bool resumeAfterObstruction = false;
uint32_t obstructionClearMs = 0;

const char *stateName(PortonState s) {
  switch (s) {
    case ST_INIT: return "INIT";
    case ST_DETENIDO: return "DETENIDO";
    case ST_CERRADO: return "CERRADO";
    case ST_ABRIENDO: return "ABRIENDO";
    case ST_ABIERTO: return "ABIERTO";
    case ST_CERRANDO: return "CERRANDO";
    case ST_OBSTRUIDO: return "OBSTRUIDO";
    case ST_FALLA: return "FALLA";
  }
  return "?";
}

struct Entradas {
  bool limit_closed;
  bool limit_open;
  bool ftc_blocked;
};

Entradas leerEntradas() {
  Entradas e;
  e.limit_closed = digitalRead(PIN_LIMIT_CLOSED) == LOW;
  e.limit_open = digitalRead(PIN_LIMIT_OPEN) == LOW;
  e.ftc_blocked = digitalRead(PIN_FTC_SENSOR) == LOW;
  return e;
}

// ================= Red / MQTT =================
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Adafruit_SSD1306 display(128, 64, &Wire, -1);

bool mqttConectado = false;
uint32_t lastWifiAttemptMs = 0;
uint32_t lastMqttAttemptMs = 0;

void connectWifiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) return;
  const uint32_t now = millis();
  if (lastWifiAttemptMs != 0 && now - lastWifiAttemptMs < 5000) return;
  lastWifiAttemptMs = now;
  Serial.printf("WiFi conectando a %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectMqttIfNeeded() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  const uint32_t now = millis();
  if (lastMqttAttemptMs != 0 && now - lastMqttAttemptMs < 5000) return;
  lastMqttAttemptMs = now;
  String clientId = String(DEVICE_ID) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqtt.connect(clientId.c_str())) {
    mqttConectado = true;
    Serial.println("MQTT conectado");
    mqtt.subscribe(TOPIC_CMD);
    mqtt.subscribe(TOPIC_CONFIG_SET);
  } else {
    Serial.printf("MQTT fallo rc=%d\n", mqtt.state());
  }
}

void serviceNetwork() {
  connectWifiIfNeeded();
  connectMqttIfNeeded();
  if (mqtt.connected()) {
    mqtt.loop();
  } else {
    mqttConectado = false;
  }
}

void publishEvent(const char *evento, const char *detalle) {
  if (!mqtt.connected()) return;
  char payload[192];
  snprintf(payload, sizeof(payload), "{\"event\":\"%s\",\"details\":\"%s\"}", evento, detalle);
  mqtt.publish(TOPIC_EVENT, payload);
}

void publishFault(const char *texto) {
  if (!mqtt.connected()) return;
  char payload[128];
  snprintf(payload, sizeof(payload), "{\"fault\":\"%s\"}", texto);
  mqtt.publish(TOPIC_FAULT, payload, true);
}

void publishState(const Entradas &e) {
  if (!mqtt.connected()) return;
  char payload[224];
  snprintf(payload, sizeof(payload),
           "{\"device\":\"%s\",\"state\":\"%s\",\"encoder\":%ld,"
           "\"limit_closed\":%s,\"limit_open\":%s,\"ftc_blocked\":%s}",
           DEVICE_ID, stateName(estado), (long)encoderGet(),
           e.limit_closed ? "true" : "false",
           e.limit_open ? "true" : "false",
           e.ftc_blocked ? "true" : "false");
  mqtt.publish(TOPIC_STATE, payload, true);
}

bool textoContiene(const char *payload, const char *aguja) {
  return payload && strstr(payload, aguja) != nullptr;
}

uint32_t numeroDespuesDe(const char *payload, const char *clave, uint32_t def) {
  const char *pos = strstr(payload, clave);
  if (!pos) return def;
  pos = strchr(pos, ':');
  if (!pos) return def;
  unsigned long v = 0;
  if (sscanf(pos + 1, "%lu", &v) != 1) return def;
  return (uint32_t)v;
}

void aplicarConfig(const char *payload) {
  if (!payload) return;
  motorDutyPercent = numeroDespuesDe(payload, "\"duty\"", motorDutyPercent);
  if (motorDutyPercent > 100) motorDutyPercent = 100;
  movementTimeoutMs = numeroDespuesDe(payload, "\"movement_timeout_ms\"", movementTimeoutMs);
  autoCloseDelayMs = numeroDespuesDe(payload, "\"auto_close_delay_ms\"", autoCloseDelayMs);

  if (textoContiene(payload, "\"auto_close_enabled\":true")) autoCloseEnabled = true;
  else if (textoContiene(payload, "\"auto_close_enabled\":false")) autoCloseEnabled = false;

  if (textoContiene(payload, "STOP_ONLY")) ftcReverse = false;
  else if (textoContiene(payload, "STOP_AND_REVERSE")) ftcReverse = true;

  Serial.printf("Config aplicada duty=%lu timeout=%lu autoclose=%d delay=%lu\n",
                (unsigned long)motorDutyPercent, (unsigned long)movementTimeoutMs,
                autoCloseEnabled, (unsigned long)autoCloseDelayMs);
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  char data[256] = {0};
  const unsigned int n = length < sizeof(data) - 1 ? length : sizeof(data) - 1;
  memcpy(data, payload, n);
  Serial.printf("MQTT RX %s -> %s\n", topic, data);

  if (strcmp(topic, TOPIC_CONFIG_SET) == 0) {
    aplicarConfig(data);
    return;
  }

  if (strcmp(topic, TOPIC_CMD) == 0) {
    if (strstr(data, "\"cmd\":\"open\"") || strstr(data, "open")) cmdPendiente = CMD_OPEN;
    else if (strstr(data, "\"cmd\":\"close\"") || strstr(data, "close")) cmdPendiente = CMD_CLOSE;
    else if (strstr(data, "\"cmd\":\"stop\"") || strstr(data, "stop")) cmdPendiente = CMD_STOP;
    else if (strstr(data, "\"cmd\":\"toggle\"") || strstr(data, "toggle")) cmdPendiente = CMD_TOGGLE;
    else if (strstr(data, "\"cmd\":\"reset\"") || strstr(data, "reset")) cmdPendiente = CMD_RESET_FAULT;
  }
}

// ================= Maquina de estados =================
void enterState(PortonState nuevo, const char *porque, const Entradas &e) {
  if (estado == nuevo) return;
  const PortonState anterior = estado;
  estado = nuevo;
  stateEnterMs = millis();
  lastEncCount = encoderGet();
  lastEncMoveMs = stateEnterMs;
  Serial.printf("%s -> %s (%s)\n", stateName(anterior), stateName(nuevo), porque);
  char detalle[96];
  snprintf(detalle, sizeof(detalle), "%s->%s %s", stateName(anterior), stateName(nuevo), porque);
  publishEvent("state_change", detalle);
  publishState(e);
}

void irAFalla(const char *motivo, const Entradas &e) {
  motorStop();
  enterState(ST_FALLA, motivo, e);
  publishFault(motivo);
  buzzerPlay(PATRON_FALLA);
}

void iniciarApertura(const char *porque, const Entradas &e) {
  motorDrive(MOTOR_ABRIR);
  enterState(ST_ABRIENDO, porque, e);
  buzzerPlay(PATRON_ABRIR);   // aviso: el porton va a abrir
}

void iniciarCierre(const char *porque, const Entradas &e) {
  motorDrive(MOTOR_CERRAR);
  enterState(ST_CERRANDO, porque, e);
  buzzerPlay(PATRON_CERRAR);  // aviso: el porton va a cerrar
}

void detenerEn(PortonState nuevo, const char *porque, const Entradas &e) {
  motorStop();
  enterState(nuevo, porque, e);
}

void procesarComando(PortonCmd cmd, const Entradas &e) {
  if (cmd == CMD_NONE) return;

  if (cmd == CMD_STOP) {
    detenerEn(ST_DETENIDO, "stop", e);
    return;
  }

  if (cmd == CMD_RESET_FAULT) {
    if (estado == ST_FALLA) {
      if (e.limit_closed && e.limit_open) {
        publishFault("no_reset_limits_imposibles");
      } else {
        detenerEn(ST_DETENIDO, "reset_fault", e);
      }
    }
    return;
  }

  if (estado == ST_FALLA || estado == ST_OBSTRUIDO) return;

  if (cmd == CMD_TOGGLE) {
    cmd = (estado == ST_ABIERTO || estado == ST_ABRIENDO) ? CMD_CLOSE : CMD_OPEN;
  }

  if (cmd == CMD_OPEN) {
    if (!e.limit_open) iniciarApertura("cmd_open", e);
    else detenerEn(ST_ABIERTO, "ya_abierto", e);
  } else if (cmd == CMD_CLOSE) {
    if (!e.limit_closed) iniciarCierre("cmd_close", e);
    else detenerEn(ST_CERRADO, "ya_cerrado", e);
  }
}

void procesar(const Entradas &e) {
  if (e.limit_closed && e.limit_open) {
    irAFalla("limits_imposibles", e);
    return;
  }

  PortonCmd cmd = cmdPendiente;
  cmdPendiente = CMD_NONE;

  if (botonFuePresionado(botones[0])) cmd = CMD_OPEN;
  else if (botonFuePresionado(botones[1])) cmd = CMD_CLOSE;
  else if (botonFuePresionado(botones[2])) cmd = CMD_STOP;

  procesarComando(cmd, e);

  if (estado == ST_ABRIENDO && e.limit_open) {
    encoderReset(countsOpen);
    detenerEn(ST_ABIERTO, "limit_open", e);
    buzzerPlay(PATRON_LLEGADA);
  } else if (estado == ST_CERRANDO && e.limit_closed) {
    encoderReset(countsClosed);
    detenerEn(ST_CERRADO, "limit_closed", e);
    buzzerPlay(PATRON_LLEGADA);
  }

  if (e.ftc_blocked && estado == ST_CERRANDO) {
    motorStop();
    resumeAfterObstruction = ftcReverse;
    obstructionClearMs = 0;
    enterState(ST_OBSTRUIDO, ftcReverse ? "ftc_stop_reverse" : "ftc_stop_only", e);
  }

  if (estado == ST_OBSTRUIDO && !e.ftc_blocked) {
    if (obstructionClearMs == 0) obstructionClearMs = millis();
    if (resumeAfterObstruction && millis() - obstructionClearMs >= obstructionReverseDelayMs) {
      resumeAfterObstruction = false;
      iniciarApertura("ftc_reversa", e);
    }
  } else if (estado != ST_OBSTRUIDO) {
    obstructionClearMs = 0;
  }

  if ((estado == ST_ABRIENDO || estado == ST_CERRANDO)) {
    const int32_t actual = encoderGet();
    if (actual != lastEncCount) {
      lastEncCount = actual;
      lastEncMoveMs = millis();
    } else if (millis() - lastEncMoveMs > stallTimeoutMs) {
      irAFalla("encoder_atascado", e);
      return;
    }
    if (millis() - stateEnterMs > movementTimeoutMs) {
      irAFalla("timeout_movimiento", e);
      return;
    }
  }

  if (estado == ST_ABIERTO && autoCloseEnabled &&
      millis() - stateEnterMs >= autoCloseDelayMs) {
    iniciarCierre("auto_close", e);
  }
}

// ================= Salidas visuales =================
void actualizarLedEstado() {
  const uint32_t now = millis();
  bool on = false;

  switch (estado) {
    case ST_CERRADO:
      on = true;
      break;
    case ST_ABIERTO:
      on = (now / 1000) % 2 == 0;
      break;
    case ST_ABRIENDO:
    case ST_CERRANDO:
      on = (now / 250) % 2 == 0;
      break;
    case ST_FALLA:
    case ST_OBSTRUIDO:
      on = (now / 120) % 2 == 0;
      break;
    default:
      on = false;
      break;
  }
  digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
}

void refrescarDisplay(const Entradas &e) {
  char linea[26];
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  snprintf(linea, sizeof(linea), "PORTON: %s", stateName(estado));
  display.setCursor(0, 0);
  display.print(linea);

  snprintf(linea, sizeof(linea), "ENC: %ld", (long)encoderGet());
  display.setCursor(0, 16);
  display.print(linea);

  snprintf(linea, sizeof(linea), "LC:%d LA:%d FTC:%d",
           e.limit_closed, e.limit_open, e.ftc_blocked);
  display.setCursor(0, 32);
  display.print(linea);

  snprintf(linea, sizeof(linea), "MQTT:%s DUTY:%lu%%",
           mqttConectado ? "OK" : "--", (unsigned long)motorDutyPercent);
  display.setCursor(0, 48);
  display.print(linea);

  display.display();
}

// ================= Setup / Loop =================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(PIN_MOTOR_IN1, OUTPUT);
  pinMode(PIN_MOTOR_IN2, OUTPUT);
  pinMode(PIN_STATUS_LED, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(PIN_MOTOR_PWM, PWM_CHANNEL);
  motorStop();

  const uint8_t pinesEntrada[] = {
    PIN_LIMIT_CLOSED, PIN_LIMIT_OPEN, PIN_FTC_SENSOR,
    PIN_LOCAL_OPEN, PIN_LOCAL_CLOSE, PIN_LOCAL_STOP,
  };
  for (uint8_t p : pinesEntrada) {
    pinMode(p, INPUT_PULLUP);
  }

  for (Boton &b : botones) {
    b.estable = digitalRead(b.pin) == LOW;
    b.ultimoCambioMs = millis();
  }

  pinMode(PIN_ENCODER_A, INPUT_PULLUP);
  pinMode(PIN_ENCODER_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_A), encoderIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_B), encoderIsr, CHANGE);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED no encontrada (se continua sin display)");
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("PORTON INICIANDO");
  display.display();

  snprintf(TOPIC_CMD, sizeof(TOPIC_CMD), "porton/%s/cmd", DEVICE_ID);
  snprintf(TOPIC_STATE, sizeof(TOPIC_STATE), "porton/%s/state", DEVICE_ID);
  snprintf(TOPIC_EVENT, sizeof(TOPIC_EVENT), "porton/%s/event", DEVICE_ID);
  snprintf(TOPIC_FAULT, sizeof(TOPIC_FAULT), "porton/%s/fault", DEVICE_ID);
  snprintf(TOPIC_CONFIG_SET, sizeof(TOPIC_CONFIG_SET), "porton/%s/config/set", DEVICE_ID);

  WiFi.mode(WIFI_STA);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);

  Entradas e = leerEntradas();
  enterState(ST_DETENIDO, "init", e);

  Serial.println();
  Serial.println("PORTON TAREA 6 listo.");
  Serial.println("Serial: a=abrir c=cerrar s=stop r=reset t=toggle");
  Serial.println("MQTT topic base: porton/porton_wokwi_01");
}

void loop() {
  serviceNetwork();

  Entradas e = leerEntradas();
  procesar(e);

  while (Serial.available() > 0) {
    const char c = Serial.read();
    switch (c) {
      case 'a': cmdPendiente = CMD_OPEN; break;
      case 'c': cmdPendiente = CMD_CLOSE; break;
      case 's': cmdPendiente = CMD_STOP; break;
      case 't': cmdPendiente = CMD_TOGGLE; break;
      case 'r': cmdPendiente = CMD_RESET_FAULT; break;
      default: break;
    }
  }

  const bool moviendo = (motorDir != MOTOR_STOP);
  buzzerTick(moviendo);
  actualizarLedEstado();

  static uint32_t lastLcdMs = 0;
  if (millis() - lastLcdMs >= 250) {
    lastLcdMs = millis();
    refrescarDisplay(e);
  }

  static uint32_t lastPubMs = 0;
  if (millis() - lastPubMs >= 1000) {
    lastPubMs = millis();
    publishState(e);
  }
}
