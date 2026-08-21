# PORTON C SIM

Simulacion en consola del porton automatico, escrita en **C puro** para Windows.

## Compilar

Requisitos: `gcc` (MinGW, MSYS2 o TCC) en el PATH.

```bash
# Opcion 1: gcc (MinGW)
gcc -Wall -Wextra -O2 -o porton.exe porton.c sim_windows.c

# Opcion 2: usar el script
build.bat
```

Si no tienes gcc, puedes instalar **MinGW-w64** desde:
https://www.mingw-w64.org/

O usar **TCC** (Tiny C Compiler), que es un solo `.exe`:
https://bellard.org/tcc/

## Ejecutar

```
porton.exe
```

## Controles

| Tecla | Accion |
|-------|--------|
| `a` | Abrir porton |
| `c` | Cerrar porton |
| `s` | Stop |
| `t` | Toggle (abrir si esta cerrado, cerrar si esta abierto) |
| `r` | Reset falla |
| `1` | Toggle sensor LIM CERRADO |
| `2` | Toggle sensor LIM ABIERTO |
| `3` | Toggle sensor OBSTACULO (FTC) |
| `+` / flecha arriba | Encoder +10 |
| `-` / flecha abajo | Encoder -10 |
| `q` | Salir |

## Archivos

| Archivo | Descripcion |
|---------|-------------|
| `porton.h` | Tipos, enums y declaraciones de la logica |
| `porton.c` | Logica del porton (state machine, motor, encoder, buzzer, serial) |
| `sim_windows.c` | Simulacion de consola Windows (main loop, dibujo, entrada) |
| `build.bat` | Script de compilacion |

## Correspondencia con el original Arduino

| Funcionalidad | Arduino / Wokwi | C puro (sim) |
|---------------|-----------------|--------------|
| GPIO | `digitalWrite` / `digitalRead` | Array `gpio[64]` virtual |
| PWM | `ledcWrite` | Reflejado en state del motor |
| Encoder | ISR + `attachInterrupt` | `porton_encoderStep()` desde teclado |
| Buzzer | Patron de pitidos | Patron en consola (ON/OFF) |
| Display OLED | Adafruit SSD1306 | Texto en consola |
| Serial | `Serial.read()` | `_kbhit()` / `_getch()` |
| WiFi/MQTT | PubSubClient | No simulado (se puede agregar) |
| Botones fisicos | Push buttons | Teclas de teclado |
