/*
 * Puente entre la logica en C (main.c) y el entorno Arduino (C++).
 * Aqui vive todo lo que obligatoriamente necesita clases:
 * Serial, WiFi y PubSubClient.
 */
#include <stdio.h>
#include <stdarg.h>
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

extern "C" {
#include "app.h"
}

/* ===== CONFIGURACION WIFI =====
 * En simulacion (Wokwi) se usa la red virtual "Wokwi-GUEST".
 * En hardware real se usan las credenciales de tu casa.
 */
#ifdef SIMULACION
static const char* ssid     = "Wokwi-GUEST";
static const char* password = "";
#else
static const char* ssid     = "Las Penas";
static const char* password = "Pena123321";
#endif

/* ===== MQTT BROKER ===== */
static const char* mqtt_server = "broker.emqx.io";
static const int   mqtt_port   = 1883;
static const char* mqtt_user   = "";
static const char* mqtt_pass   = "";

static WiFiClient   espClient;
static PubSubClient cliente(espClient);

/* ===== Helpers internos (C++) ===== */
static void conectar_wifi(void)
{
  Serial.print("Conectando a Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Conectado!");
}

static void reconectar_mqtt(void)
{
  while (!cliente.connected()) {
    Serial.print("Intentando conexion MQTT...");
    String clientId = "ESP32-Game-" + String(random(0xffff), HEX);
    if (cliente.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println(" Conectado a EMQX!");
      cliente.subscribe("reaction_game/control");
      cliente.publish("topic/qos0", "ESP32 listo para jugar");
    } else {
      Serial.print(" Fallo rc=");
      Serial.print(cliente.state());
      Serial.println(" Reintentando en 2s...");
      delay(2000);
    }
  }
}

/* ===== Implementacion de la capa de hardware ===== */
extern "C" void hal_configurar_pines(void)
{
  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  digitalWrite(LED1_PIN, LOW);
  digitalWrite(LED2_PIN, LOW);
}

extern "C" uint32_t hal_micros(void)
{
  return micros();
}

extern "C" void hal_esperar_ms(uint32_t ms)
{
  delay(ms);
}

extern "C" uint32_t hal_aleatorio(uint32_t minimo, uint32_t maximo)
{
  return (uint32_t)random((long)minimo, (long)maximo);
}

extern "C" bool hal_leer_boton(uint8_t pin)
{
  return digitalRead(pin) == LOW; /* true si esta presionado */
}

extern "C" void hal_escribir_led(uint8_t pin, bool encendido)
{
  digitalWrite(pin, encendido ? HIGH : LOW);
}

extern "C" void hal_log(const char *texto)
{
  Serial.println(texto);
}

extern "C" void hal_log_formato(const char *formato, ...)
{
  char buffer[128];
  va_list args;
  va_start(args, formato);
  vsnprintf(buffer, sizeof(buffer), formato, args);
  va_end(args);
  Serial.println(buffer);
}

/* ===== Implementacion de la capa de red ===== */
extern "C" void net_iniciar(void)
{
  conectar_wifi();
  randomSeed(esp_random());      /* semilla con hardware del ESP32 */
  cliente.setServer(mqtt_server, mqtt_port);
}

extern "C" bool net_conectado(void)
{
  return cliente.connected();
}

extern "C" void net_procesar(void)
{
  cliente.loop();
}

extern "C" void net_reconectar(void)
{
  reconectar_mqtt();
}

extern "C" bool net_publicar(const char *topico, const char *mensaje)
{
  return cliente.publish(topico, mensaje);
}

/* ===== Puntos de entrada Arduino ===== */
void setup()
{
  Serial.begin(115200);
  delay(1000); /* Tiempo para estabilizar el Monitor Serie */
  Serial.println("\n--- Iniciando ESP32 ---");

  hal_configurar_pines();
  net_iniciar();
  juego_iniciar();
}

void loop()
{
  if (!net_conectado()) net_reconectar();
  net_procesar();
  juego_iterar();
}
