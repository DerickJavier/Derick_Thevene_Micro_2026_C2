/*
 * JUEGO DE RAPIDEZ - Logica principal en C estructurado.
 * Maquina de estados + filtro anti-rebote, sin clases ni objetos.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "app.h"

/* ===== Constantes de tiempo ===== */
#define REBOTE_US       2000u      /* 2 ms de anti-rebote  */
#define RETARDO_MIN_US  1500000u   /* 1.5 s                */
#define RETARDO_MAX_US  4500000u   /* 4.5 s                */

/* ===== Estados del juego ===== */
typedef enum {
    EST_ESPERANDO_INICIO,
    EST_MANTENIENDO_BTN1,
    EST_LED1_ESPERA_SOLTAR,
    EST_ESPERANDO_BTN2,
    EST_RESULTADOS
} EstadoJuego;

/* ===== Filtro anti-rebote por boton ===== */
typedef struct {
    uint32_t t_ultimo_cambio;
    bool     lectura_anterior;
    bool     estable;
    bool     estable_previo;
} Boton;

/* ===== Estado completo del juego ===== */
typedef struct {
    EstadoJuego estado;
    Boton       btn1;
    Boton       btn2;
    uint32_t    t_encendido_led1;
    uint32_t    t_soltado_btn1;
    uint32_t    t_inicio_presionado;
    uint32_t    reaccion1_ms;
    uint32_t    reaccion2_ms;
} Juego;

static Juego juego = {
    .estado = EST_ESPERANDO_INICIO
};

/* ===== Operaciones sobre un boton ===== */
static void boton_actualizar(Boton *b, bool lectura, uint32_t ahora)
{
    if (lectura != b->lectura_anterior) {
        b->t_ultimo_cambio = ahora;
        b->lectura_anterior = lectura;
    }
    if ((uint32_t)(ahora - b->t_ultimo_cambio) > REBOTE_US) {
        b->estable = lectura;
    }
}

static bool boton_fue_presionado(const Boton *b)   /* flanco de bajada  */
{
    return b->estable && !b->estable_previo;
}

static bool boton_fue_soltado(const Boton *b)      /* flanco de subida  */
{
    return !b->estable && b->estable_previo;
}

static void boton_guardar_flanco(Boton *b)
{
    b->estable_previo = b->estable;
}

/* ===== Publicacion de resultados por MQTT ===== */
static void publicar_resultados(void)
{
    char json[96];
    char msg[96];

    snprintf(json, sizeof(json),
             "{\"reaction1_ms\":%lu,\"reaction2_ms\":%lu}",
             (unsigned long)juego.reaccion1_ms,
             (unsigned long)juego.reaccion2_ms);
    net_publicar("reaction_game/times", json);

    snprintf(msg, sizeof(msg),
             "Reaccion 1: %lu ms | Reaccion 2: %lu ms",
             (unsigned long)juego.reaccion1_ms,
             (unsigned long)juego.reaccion2_ms);
    net_publicar("topic/qos0", msg);

    hal_log("-> Datos publicados en MQTT!");
}

/* ===== Maquina de estados ===== */
static void maquina_de_estados(uint32_t ahora)
{
    switch (juego.estado) {

    case EST_ESPERANDO_INICIO:
        hal_escribir_led(LED1_PIN, false);
        hal_escribir_led(LED2_PIN, false);
        juego.t_inicio_presionado = 0;
        if (boton_fue_presionado(&juego.btn1)) {
            hal_log("-> Boton 1 PRESIONADO. Esperando tiempo aleatorio...");
            juego.estado = EST_MANTENIENDO_BTN1;
        }
        break;

    case EST_MANTENIENDO_BTN1:
        if (boton_fue_soltado(&juego.btn1)) {
            hal_log("-> Boton 1 soltado muy rapido. Reiniciando...");
            juego.t_inicio_presionado = 0;
            juego.estado = EST_ESPERANDO_INICIO;
            break;
        }

        if (juego.t_inicio_presionado == 0) {
            juego.t_inicio_presionado = ahora;
        }

        if (ahora - juego.t_inicio_presionado > hal_aleatorio(RETARDO_MIN_US, RETARDO_MAX_US)) {
            hal_escribir_led(LED1_PIN, true);
            juego.t_encendido_led1 = ahora;
            hal_log("-> LED 1 ENCENDIDO! Suelta el Boton 1!");
            juego.estado = EST_LED1_ESPERA_SOLTAR;
            juego.t_inicio_presionado = 0;
        }
        break;

    case EST_LED1_ESPERA_SOLTAR:
        if (boton_fue_soltado(&juego.btn1)) {
            juego.t_soltado_btn1 = ahora;
            juego.reaccion1_ms = (juego.t_soltado_btn1 - juego.t_encendido_led1) / 1000u;
            hal_log_formato("-> Reaccion 1: %lu ms. Presiona Boton 2 (GPIO13)...",
                            (unsigned long)juego.reaccion1_ms);
            hal_escribir_led(LED1_PIN, false);
            juego.estado = EST_ESPERANDO_BTN2;
        }
        break;

    case EST_ESPERANDO_BTN2:
        if (boton_fue_presionado(&juego.btn2)) {
            juego.reaccion2_ms = (ahora - juego.t_soltado_btn1) / 1000u;
            hal_log_formato("-> Reaccion 2: %lu ms.",
                            (unsigned long)juego.reaccion2_ms);
            hal_escribir_led(LED2_PIN, true);
            juego.estado = EST_RESULTADOS;
        }
        break;

    case EST_RESULTADOS:
        publicar_resultados();
        hal_esperar_ms(2000);
        hal_escribir_led(LED2_PIN, false);
        juego.estado = EST_ESPERANDO_INICIO;
        break;
    }
}

/* ===== API del juego ===== */
void juego_iniciar(void)
{
    juego.estado             = EST_ESPERANDO_INICIO;
    juego.t_encendido_led1   = 0;
    juego.t_soltado_btn1     = 0;
    juego.t_inicio_presionado = 0;
    juego.reaccion1_ms       = 0;
    juego.reaccion2_ms       = 0;
}

void juego_iterar(void)
{
    uint32_t ahora = hal_micros();

    /* Lectura y anti-rebote de ambos botones */
    boton_actualizar(&juego.btn1, hal_leer_boton(BTN1_PIN), ahora);
    boton_actualizar(&juego.btn2, hal_leer_boton(BTN2_PIN), ahora);

    /* Transiciones de la maquina de estados */
    maquina_de_estados(ahora);

    /* Guardar flancos para la proxima iteracion */
    boton_guardar_flanco(&juego.btn1);
    boton_guardar_flanco(&juego.btn2);
}
