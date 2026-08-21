#ifndef PORTON_H
#define PORTON_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 *  PORTON AUTOMATICO — LOGICA PORTATIL (C puro)
 *  Emulacion de consola del proyecto ESP32 / Wokwi
 * ================================================================ */

/* -------------------- Pines virtuales ---------------------- */
#define PIN_MOTOR_IN1       26
#define PIN_MOTOR_IN2       27
#define PIN_MOTOR_PWM       25
#define PIN_ENCODER_A       32
#define PIN_ENCODER_B       33
#define PIN_LIMIT_CLOSED    16
#define PIN_LIMIT_OPEN      17
#define PIN_FTC_SENSOR      14
#define PIN_LOCAL_OPEN      18
#define PIN_LOCAL_CLOSE     19
#define PIN_LOCAL_STOP      23
#define PIN_BUZZER          13
#define PIN_STATUS_LED       2

/* -------------------- PWM --------------------------------- */
#define PWM_CHANNEL          0
#define PWM_FREQ_HZ      20000
#define PWM_RES_BITS        10
#define PWM_MAX_DUTY     1023

/* -------------------- Tiempos (ms) ------------------------ */
#define DEBOUNCE_MS          30
#define BEEP_MOVE_PERIOD_MS 1000
#define BEEP_MOVE_ON_MS      60

/* -------------------- Estado del motor -------------------- */
typedef enum {
    MOTOR_STOP = 0,
    MOTOR_ABRIR,
    MOTOR_CERRAR
} MotorDir;

/* -------------------- Estado del porton ------------------- */
typedef enum {
    ST_INIT = 0,
    ST_DETENIDO,
    ST_CERRADO,
    ST_ABRIENDO,
    ST_ABIERTO,
    ST_CERRANDO,
    ST_OBSTRUIDO,
    ST_FALLA
} PortonState;

/* -------------------- Comandos ---------------------------- */
typedef enum {
    CMD_NONE = 0,
    CMD_OPEN,
    CMD_CLOSE,
    CMD_STOP,
    CMD_TOGGLE,
    CMD_RESET_FAULT
} PortonCmd;

/* -------------------- Patron de buzzer -------------------- */
typedef struct {
    uint16_t on_ms;
    uint16_t off_ms;
} BuzzStep;

/* -------------------- Boton con debounce ------------------ */
typedef struct {
    uint8_t  pin;
    bool     estable;
    uint32_t ultimoCambioMs;
} Boton;

/* -------------------- Entradas ---------------------------- */
typedef struct {
    bool limit_closed;
    bool limit_open;
    bool ftc_blocked;
} Entradas;

/* -------------------- Contexto del porton ----------------- */
typedef struct {
    /* Estado */
    PortonState estado;
    PortonCmd   cmdPendiente;
    uint32_t    stateEnterMs;
    int32_t     lastEncCount;
    uint32_t    lastEncMoveMs;
    bool        resumeAfterObstruction;
    uint32_t    obstructionClearMs;

    /* Motor */
    MotorDir motorDir;
    uint32_t motorDutyPercent;

    /* Encoder */
    int32_t encoderCount;
    bool    encoderInverted;

    /* Tiempos */
    uint32_t movementTimeoutMs;
    uint32_t stallTimeoutMs;
    uint32_t obstructionReverseDelayMs;
    bool     ftcReverse;
    bool     autoCloseEnabled;
    uint32_t autoCloseDelayMs;
    int32_t  countsClosed;
    int32_t  countsOpen;

    /* Buzzer */
    BuzzStep *buzzSeq;
    uint8_t  buzzIdx;
    uint32_t buzzStepStartMs;
    bool     buzzOn;

    /* GPIO virtual */
    bool gpio[64];          /* estado logico de cada pin */

    /* Botones locales */
    Boton botones[3];

    /* Simulacion de buzzing (para la capa de presentacion) */
    bool buzzerActive;

} PortonCtx;

/* ================================================================
 *  API — Funciones que implementa porton.c
 * ================================================================ */

/* Inicializar contexto */
void porton_init(PortonCtx *ctx);

/* GPIO virtual */
void gpio_write(PortonCtx *ctx, uint8_t pin, bool val);
bool gpio_read(PortonCtx *ctx, uint8_t pin);

/* Encoder */
int32_t encoderGet(PortonCtx *ctx);
void porton_encoderStep(PortonCtx *ctx, int delta);

/* Leer entradas (lee de gpio virtual) */
Entradas porton_leerEntradas(PortonCtx *ctx);

/* Procesar un ciclo completo */
void porton_procesar(PortonCtx *ctx, Entradas *e);

/* Procesar un ciclo con tiempo explicito (para simulacion) */
void porton_procesarConTiempo(PortonCtx *ctx, Entradas *e, uint32_t nowMs);

/* Manejar tecla serial */
void porton_serialCmd(PortonCtx *ctx, char c);

/* Maquina de estados (expuesta para debug) */
void porton_enterState(PortonCtx *ctx, PortonState nuevo, const char *porque, Entradas *e);

/* Nombre legible de cada estado */
const char *porton_stateName(PortonState s);

/* Actualizar led de estado (escribe en gpio virtual) */
void porton_actualizarLedEstado(PortonCtx *ctx);

/* Buzzer tick (actualiza estado del buzzer) */
void porton_buzzerTick(PortonCtx *ctx, bool moving);

/* Buzzer tick con tiempo explicito (para simulacion) */
void porton_buzzerTickMs(PortonCtx *ctx, bool moving, uint32_t nowMs);

/* Buzzer: silenciar */
void porton_buzzerSilence(PortonCtx *ctx);

/* Buzzer: reproducir patron */
void porton_buzzerPlay(PortonCtx *ctx, BuzzStep *seq);

#endif /* PORTON_H */
