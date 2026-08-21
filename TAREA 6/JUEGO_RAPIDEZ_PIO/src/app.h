#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Pines ===== */
#define BTN1_PIN 4   /* GPIO4  = Boton 1 (activo LOW, pull-up) */
#define BTN2_PIN 13  /* GPIO13 = Boton 2 (activo LOW, pull-up) */
#define LED1_PIN 2   /* GPIO2  = LED integrado / LED 1         */
#define LED2_PIN 15  /* GPIO15 = LED 2                          */

/* ===== Capa de hardware (implementada en puente_arduino.cpp) ===== */
void     hal_configurar_pines(void);
uint32_t hal_micros(void);
void     hal_esperar_ms(uint32_t ms);
uint32_t hal_aleatorio(uint32_t minimo, uint32_t maximo);
bool     hal_leer_boton(uint8_t pin);            /* true si esta presionado */
void     hal_escribir_led(uint8_t pin, bool encendido);
void     hal_log(const char *texto);
void     hal_log_formato(const char *formato, ...);

/* ===== Capa de red WiFi + MQTT (implementada en puente_arduino.cpp) ===== */
void net_iniciar(void);
bool net_conectado(void);
void net_procesar(void);
void net_reconectar(void);
bool net_publicar(const char *topico, const char *mensaje);

/* ===== Logica del juego (implementada en main.c) ===== */
void juego_iniciar(void);
void juego_iterar(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
