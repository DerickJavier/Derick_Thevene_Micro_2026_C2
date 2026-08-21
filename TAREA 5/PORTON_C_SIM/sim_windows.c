/* ================================================================
 *  sim_windows.c — Simulacion en consola para Windows (C puro)
 *
 *  Compilar: gcc -o porton.exe porton.c sim_windows.c -lwinmm
 *  Ejecutar: porton.exe
 *
 *  Controles:
 *    a = abrir          c = cerrar        s = stop
 *    t = toggle         r = reset falla   q = salir
 *    1 = toggle LIM CERRADO   2 = toggle LIM ABIERTO
 *    3 = toggle OBSTACULO
 *    + = encoder +1     - = encoder -1
 * ================================================================ */
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "porton.h"

/* Tiempo en milisegundos */
static DWORD nowMs(void) {
    return GetTickCount();
}

/* ---------------------------------------------------------------
 *  Consola — modo sin espera de enter
 * --------------------------------------------------------------- */
static HANDLE hConsole;
static DWORD  oldMode;

static void console_init(void) {
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    /* Ocultar cursor */
    CONSOLE_CURSOR_INFO ci = { 1, FALSE };
    SetConsoleCursorInfo(hConsole, &ci);
}

static void console_cleanup(void) {
    /* Restaurar cursor */
    CONSOLE_CURSOR_INFO ci = { 1, TRUE };
    SetConsoleCursorInfo(hConsole, &ci);
}

/* ---------------------------------------------------------------
 *  Dibujar pantalla completa
 * --------------------------------------------------------------- */
static void drawScreen(const PortonCtx *ctx, Entradas *e, DWORD elapsed) {
    /* Mover cursor a origen */
    COORD orig = { 0, 0 };
    SetConsoleCursorPosition(hConsole, orig);

    printf("=============================================================\n");
    printf("  PORTON AUTOMATICO  -  Simulacion C puro (Windows)\n");
    printf("=============================================================\n");
    printf("\n");

    /* ---- Estado ---- */
    printf("  ESTADO:    %-12s\n", porton_stateName(ctx->estado));
    printf("  ENCODER:   %ld\n", (long)encoderGet(ctx));
    printf("\n");

    /* ---- Sensores ---- */
    printf("  LIM CERRADO: [%s]   ", e->limit_closed ? "ON " : "OFF");
    printf("  LIM ABIERTO: [%s]   ", e->limit_open   ? "ON " : "OFF");
    printf("  OBSTACULO:   [%s]\n", e->ftc_blocked  ? "ON " : "OFF");
    printf("\n");

    /* ---- Motor / LEDs ---- */
    const char *motorState = "---";
    if (ctx->motorDir == MOTOR_ABRIR)  motorState = "ABRE (rojo)";
    if (ctx->motorDir == MOTOR_CERRAR) motorState = "CIERRA (amarillo)";
    if (ctx->motorDir == MOTOR_STOP)   motorState = "STOP";

    printf("  MOTOR:     %-16s   DUTY: %lu%%\n", motorState, (unsigned long)ctx->motorDutyPercent);

    /* LED status */
    bool ledOn = gpio_read(ctx, PIN_STATUS_LED);
    printf("  LED STATUS: [%s]\n", ledOn ? "ON " : "OFF");

    /* Buzzer */
    printf("  BUZZER:    [%s]\n", ctx->buzzerActive ? "ON " : "OFF");

    printf("\n");

    /* ---- Config ---- */
    printf("  AUTO CLOSE: %s", ctx->autoCloseEnabled ? "ON " : "OFF");
    if (ctx->autoCloseEnabled)
        printf("  (delay: %lu ms)", (unsigned long)ctx->autoCloseDelayMs);
    printf("\n");
    printf("  FTC REVERSE: %s\n", ctx->ftcReverse ? "YES" : "NO");
    printf("\n");

    /* ---- Barras de progreso ---- */
    int32_t enc = encoderGet(ctx);
    int32_t min_enc = ctx->countsClosed;
    int32_t max_enc = ctx->countsOpen;
    if (max_enc <= min_enc) max_enc = min_enc + 1;

    float pct = (float)(enc - min_enc) / (float)(max_enc - min_enc) * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;

    int barLen = 40;
    int filled = (int)(pct / 100.0f * barLen + 0.5f);

    printf("  POSICION:  [");
    for (int i = 0; i < barLen; i++) {
        printf(i < filled ? "#" : "-");
    }
    printf("] %3.0f%%\n", pct);
    printf("             CERRADO");
    for (int i = 0; i < barLen - 12; i++) printf(" ");
    printf("ABIERTO\n");
    printf("\n");

    /* ---- Ayuda ---- */
    printf("-------------------------------------------------------------\n");
    printf("  CONTROLES:\n");
    printf("    a=abrir  c=cerrar  s=stop  t=toggle  r=reset  q=salir\n");
    printf("    1=LIM_CERRADO  2=LIM_ABIERTO  3=OBSTACULO  +/-=encoder\n");
    printf("-------------------------------------------------------------\n");

    /* Limpiar linea extra */
    printf("                                                            \n");
    printf("                                                            \n");
}

/* ---------------------------------------------------------------
 *  Lectura de entrada (no bloqueante)
 * --------------------------------------------------------------- */
static int readKey(void) {
    if (_kbhit()) {
        int ch = _getch();
        /* Teclas de flecha: 224 + flecha */
        if (ch == 224 || ch == 0) {
            if (_kbhit()) {
                int ext = _getch();
                if (ext == 72) return '+';   /* flecha arriba  = encoder + */
                if (ext == 80) return '-';   /* flecha abajo   = encoder - */
                if (ext == 75) return 'a';   /* izquierda = abrir */
                if (ext == 77) return 'c';   /* derecha = cerrar */
            }
            return 0;
        }
        return ch;
    }
    return 0;
}

/* ---------------------------------------------------------------
 *  MAIN
 * --------------------------------------------------------------- */
int main(void) {
    PortonCtx ctx;
    Entradas  e;

    console_init();
    porton_init(&ctx);

    DWORD lastTick = nowMs();
    DWORD lastRedraw = 0;
    int running = 1;

    while (running) {
        DWORD now = nowMs();
        DWORD dt = now - lastTick;
        lastTick = now;

        /* Leer entradas */
        e = porton_leerEntradas(&ctx);

        /* Procesar logica */
        porton_procesarConTiempo(&ctx, &e, now);

        /* Buzzer tick */
        porton_buzzerTickMs(&ctx, ctx.motorDir != MOTOR_STOP, now);

        /* LED estado */
        porton_actualizarLedEstado(&ctx);

        /* Redibujar cada 100 ms */
        if (now - lastRedraw >= 100) {
            lastRedraw = now;
            drawScreen(&ctx, &e, now);
        }

        /* Teclado */
        int ch = readKey();
        if (ch != 0) {
            switch (ch) {
                case 'q': case 'Q':
                    running = 0;
                    break;
                case '1': {
                    /* Toggle LIM CERRADO */
                    bool val = !gpio_read(&ctx, PIN_LIMIT_CLOSED);
                    gpio_write(&ctx, PIN_LIMIT_CLOSED, val);
                    printf("\n  >> LIM CERRADO -> %s\n", val ? "ON" : "OFF");
                    break;
                }
                case '2': {
                    /* Toggle LIM ABIERTO */
                    bool val = !gpio_read(&ctx, PIN_LIMIT_OPEN);
                    gpio_write(&ctx, PIN_LIMIT_OPEN, val);
                    printf("\n  >> LIM ABIERTO -> %s\n", val ? "ON" : "OFF");
                    break;
                }
                case '3': {
                    /* Toggle OBSTACULO */
                    bool val = !gpio_read(&ctx, PIN_FTC_SENSOR);
                    gpio_write(&ctx, PIN_FTC_SENSOR, val);
                    printf("\n  >> OBSTACULO -> %s\n", val ? "ON" : "OFF");
                    break;
                }
                case '+':
                    porton_encoderStep(&ctx, 10);
                    printf("\n  >> ENCODER +%ld\n", (long)10);
                    break;
                case '-':
                    porton_encoderStep(&ctx, -10);
                    printf("\n  >> ENCODER %ld\n", (long)-10);
                    break;
                default:
                    porton_serialCmd(&ctx, (char)ch);
                    break;
            }
        }

        /* No saturar CPU */
        Sleep(10);
    }

    console_cleanup();
    printf("\n  Simulacion terminada.\n");
    return 0;
}
