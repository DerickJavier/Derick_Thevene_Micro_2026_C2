/* ================================================================
 *  porton.c — Logica del porton automatico (C puro, portable)
 *
 *  Portado del proyecto ESP32 / Arduino original.
 *  Toda la logica de negocio vive aqui.  La capa de presentacion
 *  (consola, MQTT, display fisico) queda en la capa de simulacion.
 * ================================================================ */
#include "porton.h"
#include <stdio.h>
#include <string.h>

/* ----------------------------------------------------------------
 *  Patron de buzzer: 2 pitidos
 * ---------------------------------------------------------------- */
static BuzzStep PATRON_ABRIR[]   = {
    {90, 110}, {90, 110}, {90, 0}, {0, 0}
};
/* 3 pitidos */
static BuzzStep PATRON_CERRAR[]  = {
    {90, 90}, {90, 90}, {90, 90}, {90, 0}, {0, 0}
};
/* pitido largo al llegar */
static BuzzStep PATRON_LLEGADA[] = {
    {350, 0}, {0, 0}
};
/* serie de falla */
static BuzzStep PATRON_FALLA[]   = {
    {60, 160}, {60, 160}, {60, 160}, {60, 0}, {0, 0}
};

/* ================================================================
 *  GPIO virtual — lectura / escritura
 * ================================================================ */
void gpio_write(PortonCtx *ctx, uint8_t pin, bool val) {
    if (pin < 64) ctx->gpio[pin] = val;
}

bool gpio_read(PortonCtx *ctx, uint8_t pin) {
    if (pin < 64) return ctx->gpio[pin];
    return false;
}

/* ================================================================
 *  Encoder
 * ================================================================ */
static void encoderReset(PortonCtx *ctx, int32_t value) {
    ctx->encoderCount = value;
}

int32_t encoderGet(PortonCtx *ctx) {
    return ctx->encoderCount;
}

void porton_encoderStep(PortonCtx *ctx, int delta) {
    if (ctx->encoderInverted) delta = -delta;
    ctx->encoderCount += delta;
}

/* ================================================================
 *  Motor
 * ================================================================ */
static void motorStop(PortonCtx *ctx) {
    ctx->motorDir = MOTOR_STOP;
    gpio_write(ctx, PIN_MOTOR_IN1, false);
    gpio_write(ctx, PIN_MOTOR_IN2, false);
    /* PWM = 0 */
}

static void motorDrive(PortonCtx *ctx, MotorDir dir) {
    if (dir == MOTOR_STOP) { motorStop(ctx); return; }
    ctx->motorDir = dir;
    gpio_write(ctx, PIN_MOTOR_IN1, dir == MOTOR_ABRIR);
    gpio_write(ctx, PIN_MOTOR_IN2, dir == MOTOR_CERRAR);
    /* PWM duty se refleja en gpio virtual como indicador */
}

/* ================================================================
 *  Buzzer
 * ================================================================ */
void porton_buzzerSilence(PortonCtx *ctx) {
    ctx->buzzSeq      = NULL;
    ctx->buzzIdx      = 0;
    ctx->buzzOn       = false;
    ctx->buzzerActive = false;
    gpio_write(ctx, PIN_BUZZER, false);
}

void porton_buzzerPlay(PortonCtx *ctx, BuzzStep *seq) {
    ctx->buzzSeq      = seq;
    ctx->buzzIdx      = 0;
    ctx->buzzStepStartMs = 0;   /* se ajusta al primer tick */
    ctx->buzzOn       = true;
    ctx->buzzerActive = true;
    gpio_write(ctx, PIN_BUZZER, true);
}

void porton_buzzerTick(PortonCtx *ctx, bool moving) {
    const uint32_t now = ctx->stateEnterMs;  /* placeholder real */

    /* --- Patron programado --- */
    if (ctx->buzzSeq != NULL) {
        const BuzzStep *st = &ctx->buzzSeq[ctx->buzzIdx];
        if (st->on_ms == 0 && st->off_ms == 0) {
            porton_buzzerSilence(ctx);
            return;
        }
        /* La logica de tiempo real se resuelve en la capa de simulacion.
           Aqui solo avanzamos pasos cuando la capa de simulacion llama
           a porton_buzzerAdvance() o delegamos completamente. */
        return;
    }

    /* --- Beep periodico mientras se mueve --- */
    if (moving) {
        /* La capa de simulacion gestiona el temporizado real */
        ctx->buzzerActive = true;
    } else {
        ctx->buzzerActive = false;
        gpio_write(ctx, PIN_BUZZER, false);
    }
}

/* Version simplificada para tickPeriodico (llamada desde sim) */
void porton_buzzerTickMs(PortonCtx *ctx, bool moving, uint32_t nowMs) {
    /* --- Patron programado --- */
    if (ctx->buzzSeq != NULL) {
        const BuzzStep *st = &ctx->buzzSeq[ctx->buzzIdx];
        if (st->on_ms == 0 && st->off_ms == 0) {
            porton_buzzerSilence(ctx);
            return;
        }

        if (ctx->buzzStepStartMs == 0) {
            ctx->buzzStepStartMs = nowMs;
            gpio_write(ctx, PIN_BUZZER, true);
            ctx->buzzOn = true;
        }

        uint32_t elapsed = nowMs - ctx->buzzStepStartMs;

        if (ctx->buzzOn) {
            if (elapsed >= st->on_ms) {
                ctx->buzzOn = false;
                gpio_write(ctx, PIN_BUZZER, false);
                ctx->buzzStepStartMs = nowMs;
            }
        } else {
            if (elapsed >= st->off_ms) {
                ctx->buzzIdx++;
                ctx->buzzStepStartMs = nowMs;
                const BuzzStep *nx = &ctx->buzzSeq[ctx->buzzIdx];
                if (nx->on_ms == 0 && nx->off_ms == 0) {
                    porton_buzzerSilence(ctx);
                    return;
                }
                ctx->buzzOn = true;
                gpio_write(ctx, PIN_BUZZER, true);
            }
        }
        return;
    }

    /* --- Beep periodico mientras se mueve --- */
    static uint32_t moveBeepT0 = 0;
    static bool     moveBeepPhase = false;

    if (moving) {
        if (!moveBeepPhase && (nowMs - moveBeepT0 >= BEEP_MOVE_PERIOD_MS - BEEP_MOVE_ON_MS)) {
            moveBeepPhase = true;
            moveBeepT0    = nowMs;
            gpio_write(ctx, PIN_BUZZER, true);
            ctx->buzzerActive = true;
        } else if (moveBeepPhase && (nowMs - moveBeepT0 >= BEEP_MOVE_ON_MS)) {
            moveBeepPhase = false;
            gpio_write(ctx, PIN_BUZZER, false);
            ctx->buzzerActive = false;
        }
    } else {
        moveBeepPhase = false;
        gpio_write(ctx, PIN_BUZZER, false);
        ctx->buzzerActive = false;
    }
}

/* ================================================================
 *  Botones — debounce
 * ================================================================ */
static bool botonFuePresionado(PortonCtx *ctx, Boton *b, uint32_t nowMs) {
    bool leido = gpio_read(ctx, b->pin);  /* true = presionado (LOW fisico) */

    if (leido != b->estable && (nowMs - b->ultimoCambioMs > DEBOUNCE_MS)) {
        b->estable = leido;
        b->ultimoCambioMs = nowMs;
        if (leido) return true;
    }
    return false;
}

/* ================================================================
 *  Entradas
 * ================================================================ */
Entradas porton_leerEntradas(PortonCtx *ctx) {
    Entradas e;
    e.limit_closed = gpio_read(ctx, PIN_LIMIT_CLOSED);
    e.limit_open   = gpio_read(ctx, PIN_LIMIT_OPEN);
    e.ftc_blocked  = gpio_read(ctx, PIN_FTC_SENSOR);
    return e;
}

/* ================================================================
 *  Nombre legible de estados
 * ================================================================ */
const char *porton_stateName(PortonState s) {
    switch (s) {
        case ST_INIT:      return "INIT";
        case ST_DETENIDO:  return "DETENIDO";
        case ST_CERRADO:   return "CERRADO";
        case ST_ABRIENDO:  return "ABRIENDO";
        case ST_ABIERTO:   return "ABIERTO";
        case ST_CERRANDO:  return "CERRANDO";
        case ST_OBSTRUIDO: return "OBSTRUIDO";
        case ST_FALLA:     return "FALLA";
    }
    return "?";
}

/* ================================================================
 *  Transicion de estado
 * ================================================================ */
void porton_enterState(PortonCtx *ctx, PortonState nuevo,
                       const char *porque, Entradas *e)
{
    if (ctx->estado == nuevo) return;
    PortonState anterior = ctx->estado;
    ctx->estado = nuevo;
    ctx->stateEnterMs = 0;   /* la capa de simulacion pone el tiempo real */
    ctx->lastEncCount = encoderGet(ctx);
    ctx->lastEncMoveMs = 0;

    printf("[STATE] %s -> %s  (%s)\n",
           porton_stateName(anterior), porton_stateName(nuevo), porque);
}

/* ================================================================
 *  Funciones de accion
 * ================================================================ */
static void irAFalla(PortonCtx *ctx, const char *motivo, Entradas *e) {
    motorStop(ctx);
    porton_enterState(ctx, ST_FALLA, motivo, e);
    porton_buzzerPlay(ctx, PATRON_FALLA);
    printf("[FAULT] %s\n", motivo);
}

static void iniciarApertura(PortonCtx *ctx, const char *porque, Entradas *e) {
    motorDrive(ctx, MOTOR_ABRIR);
    porton_enterState(ctx, ST_ABRIENDO, porque, e);
    porton_buzzerPlay(ctx, PATRON_ABRIR);
}

static void iniciarCierre(PortonCtx *ctx, const char *porque, Entradas *e) {
    motorDrive(ctx, MOTOR_CERRAR);
    porton_enterState(ctx, ST_CERRANDO, porque, e);
    porton_buzzerPlay(ctx, PATRON_CERRAR);
}

static void detenerEn(PortonCtx *ctx, PortonState nuevo,
                      const char *porque, Entradas *e)
{
    motorStop(ctx);
    porton_enterState(ctx, nuevo, porque, e);
}

/* ================================================================
 *  Procesamiento de comandos
 * ================================================================ */
static void procesarComando(PortonCtx *ctx, PortonCmd cmd, Entradas *e) {
    if (cmd == CMD_NONE) return;

    if (cmd == CMD_STOP) {
        detenerEn(ctx, ST_DETENIDO, "stop", e);
        return;
    }

    if (cmd == CMD_RESET_FAULT) {
        if (ctx->estado == ST_FALLA) {
            if (e->limit_closed && e->limit_open) {
                printf("[FAULT] no_reset_limits_imposibles\n");
            } else {
                detenerEn(ctx, ST_DETENIDO, "reset_fault", e);
            }
        }
        return;
    }

    if (ctx->estado == ST_FALLA || ctx->estado == ST_OBSTRUIDO) return;

    if (cmd == CMD_TOGGLE) {
        cmd = (ctx->estado == ST_ABIERTO || ctx->estado == ST_ABRIENDO)
              ? CMD_CLOSE : CMD_OPEN;
    }

    if (cmd == CMD_OPEN) {
        if (!e->limit_open)
            iniciarApertura(ctx, "cmd_open", e);
        else
            detenerEn(ctx, ST_ABIERTO, "ya_abierto", e);
    } else if (cmd == CMD_CLOSE) {
        if (!e->limit_closed)
            iniciarCierre(ctx, "cmd_close", e);
        else
            detenerEn(ctx, ST_CERRADO, "ya_cerrado", e);
    }
}

/* ================================================================
 *  Procesamiento principal (un ciclo)
 *  nowMs: tiempo actual en milisegundos (proporcionado por sim)
 * ================================================================ */
void porton_procesarConTiempo(PortonCtx *ctx, Entradas *e, uint32_t nowMs) {

    /* Limite imposible */
    if (e->limit_closed && e->limit_open) {
        irAFalla(ctx, "limits_imposibles", e);
        return;
    }

    /* Tomar comando pendiente */
    PortonCmd cmd = ctx->cmdPendiente;
    ctx->cmdPendiente = CMD_NONE;

    /* Botones locales */
    for (int i = 0; i < 3; i++) {
        if (botonFuePresionado(ctx, &ctx->botones[i], nowMs)) {
            if (i == 0)      cmd = CMD_OPEN;
            else if (i == 1) cmd = CMD_CLOSE;
            else if (i == 2) cmd = CMD_STOP;
        }
    }

    procesarComando(ctx, cmd, e);

    /* Llegada a limite */
    if (ctx->estado == ST_ABRIENDO && e->limit_open) {
        encoderReset(ctx, ctx->countsOpen);
        detenerEn(ctx, ST_ABIERTO, "limit_open", e);
        porton_buzzerPlay(ctx, PATRON_LLEGADA);
    } else if (ctx->estado == ST_CERRANDO && e->limit_closed) {
        encoderReset(ctx, ctx->countsClosed);
        detenerEn(ctx, ST_CERRADO, "limit_closed", e);
        porton_buzzerPlay(ctx, PATRON_LLEGADA);
    }

    /* FTC — obstaculo */
    if (e->ftc_blocked && ctx->estado == ST_CERRANDO) {
        motorStop(ctx);
        ctx->resumeAfterObstruction = ctx->ftcReverse;
        ctx->obstructionClearMs = 0;
        porton_enterState(ctx, ST_OBSTRUIDO,
                          ctx->ftcReverse ? "ftc_stop_reverse" : "ftc_stop_only", e);
    }

    if (ctx->estado == ST_OBSTRUIDO && !e->ftc_blocked) {
        if (ctx->obstructionClearMs == 0)
            ctx->obstructionClearMs = nowMs;
        if (ctx->resumeAfterObstruction &&
            (nowMs - ctx->obstructionClearMs >= ctx->obstructionReverseDelayMs))
        {
            ctx->resumeAfterObstruction = false;
            iniciarApertura(ctx, "ftc_reversa", e);
        }
    } else if (ctx->estado != ST_OBSTRUIDO) {
        ctx->obstructionClearMs = 0;
    }

    /* Atascado / timeout */
    if (ctx->estado == ST_ABRIENDO || ctx->estado == ST_CERRANDO) {
        int32_t actual = encoderGet(ctx);
        if (actual != ctx->lastEncCount) {
            ctx->lastEncCount = actual;
            ctx->lastEncMoveMs = nowMs;
        } else if (ctx->lastEncMoveMs != 0 &&
                   (nowMs - ctx->lastEncMoveMs > ctx->stallTimeoutMs))
        {
            irAFalla(ctx, "encoder_atascado", e);
            return;
        }
        if (ctx->stateEnterMs != 0 &&
            (nowMs - ctx->stateEnterMs > ctx->movementTimeoutMs))
        {
            irAFalla(ctx, "timeout_movimiento", e);
            return;
        }
    }

    /* Cierre automatico */
    if (ctx->estado == ST_ABIERTO && ctx->autoCloseEnabled &&
        ctx->stateEnterMs != 0 &&
        (nowMs - ctx->stateEnterMs >= ctx->autoCloseDelayMs))
    {
        iniciarCierre(ctx, "auto_close", e);
    }
}

/* Wrapper sin tiempo (usa stateEnterMs como referencia) */
void porton_procesar(PortonCtx *ctx, Entradas *e) {
    porton_procesarConTiempo(ctx, e, ctx->stateEnterMs);
}

/* ================================================================
 *  Comandos por serial
 * ================================================================ */
void porton_serialCmd(PortonCtx *ctx, char c) {
    switch (c) {
        case 'a': ctx->cmdPendiente = CMD_OPEN;         break;
        case 'c': ctx->cmdPendiente = CMD_CLOSE;        break;
        case 's': ctx->cmdPendiente = CMD_STOP;         break;
        case 't': ctx->cmdPendiente = CMD_TOGGLE;       break;
        case 'r': ctx->cmdPendiente = CMD_RESET_FAULT;  break;
        default: break;
    }
}

/* ================================================================
 *  LED de estado (refleja en gpio virtual)
 * ================================================================ */
void porton_actualizarLedEstado(PortonCtx *ctx) {
    bool on = false;
    switch (ctx->estado) {
        case ST_CERRADO:   on = true;  break;
        case ST_ABIERTO:   on = ((ctx->stateEnterMs / 1000) % 2 == 0); break;
        case ST_ABRIENDO:
        case ST_CERRANDO:  on = ((ctx->stateEnterMs / 250) % 2 == 0);  break;
        case ST_FALLA:
        case ST_OBSTRUIDO: on = ((ctx->stateEnterMs / 120) % 2 == 0);  break;
        default: break;
    }
    gpio_write(ctx, PIN_STATUS_LED, on);
}

/* ================================================================
 *  Inicializacion
 * ================================================================ */
void porton_init(PortonCtx *ctx) {
    memset(ctx, 0, sizeof(*ctx));

    ctx->estado       = ST_INIT;
    ctx->cmdPendiente = CMD_NONE;

    /* Configuracion por defecto */
    ctx->motorDutyPercent          = 70;
    ctx->movementTimeoutMs         = 20000;
    ctx->stallTimeoutMs            = 3000;
    ctx->obstructionReverseDelayMs = 800;
    ctx->ftcReverse                = true;
    ctx->autoCloseEnabled          = false;
    ctx->autoCloseDelayMs          = 10000;
    ctx->countsClosed              = 0;
    ctx->countsOpen                = 2000;

    /* Botones (activos en LOW) */
    ctx->botones[0].pin    = PIN_LOCAL_OPEN;
    ctx->botones[0].estable = true;
    ctx->botones[1].pin    = PIN_LOCAL_CLOSE;
    ctx->botones[1].estable = true;
    ctx->botones[2].pin    = PIN_LOCAL_STOP;
    ctx->botones[2].estable = true;

    /* Pines de entrada en HIGH por defecto (pull-up) */
    ctx->gpio[PIN_LIMIT_CLOSED] = true;
    ctx->gpio[PIN_LIMIT_OPEN]   = true;
    ctx->gpio[PIN_FTC_SENSOR]   = true;
    ctx->gpio[PIN_LOCAL_OPEN]   = true;
    ctx->gpio[PIN_LOCAL_CLOSE]  = true;
    ctx->gpio[PIN_LOCAL_STOP]   = true;

    printf("[INIT] Porton C puro — simulacion de consola\n");
    printf("[INIT] Serial: a=abrir c=cerrar s=stop t=toggle r=reset q=salir\n");
    printf("[INIT] Simulacion: 1=toggle LIM CERRADO  2=toggle LIM ABIERTO  3=toggle OBSTACULO\n");
    printf("[INIT] Encoder: +/- para girar\n\n");

    Entradas e = porton_leerEntradas(ctx);
    porton_enterState(ctx, ST_DETENIDO, "init", &e);
}
