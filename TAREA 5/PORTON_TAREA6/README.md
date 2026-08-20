# PORTON_TAREA6

Porton automatico con ESP32, buzzer de aviso y simulacion Wokwi en VS Code + PlatformIO.

## Novedad: buzzer de aviso (GPIO13)

- **2 pitidos** cuando el porton empieza a **abrir**
- **3 pitidos** cuando el porton empieza a **cerrar**
- **Pitido corto cada 1 s** mientras el motor esta en movimiento
- **Pitido largo** al llegar al limite (totalmente abierto o cerrado)
- **Serie de pitidos rapidos** ante una falla

## Como emularlo en VS Code con PlatformIO + Wokwi

1. Instala en VS Code las extensiones:
   - `PlatformIO IDE` (platformio.platformio-id)
   - `Wokwi Simulator` (wokwi.wokwi-vscode) — es el emulador de ESP32; es gratis para uso personal
2. Abre esta carpeta (`PORTON_TAREA6`) en VS Code.
3. Espera a que PlatformIO descargue el framework y las librerias (esquina inferior derecha).
4. Compila una vez: icono de PlatformIO -> Build (o `Ctrl+Alt+B`). Esto genera el firmware que Wokwi carga.
5. Inicia la simulacion: `Ctrl+Shift+P` -> **Wokwi: Start Simulator** (o el icono del engranaje naranja arriba a la derecha del editor).
6. En la pestaña del navegador/simulador que abre Wokwi puedes interactuar.

## Controles en la simulacion

| Elemento | Accion |
|---|---|
| Boton ABRIR | Manda comando abrir (2 pitidos y arranca el motor) |
| Boton CERRAR | Manda comando cerrar (3 pitidos y arranca el motor) |
| Boton STOP | Detiene el porton |
| LIM CERRADO / LIM ABIERTO | Simulan los fines de carrera (manten presionado al llegar) |
| OBSTACULO | Sensor FTC: si lo presionas mientras cierra, detiene y revierte |
| Encoder giratorio | Mueve el conteo ENC del display OLED |
| LED rojo ABRE / amarillo CIERRA / verde PWM | Estado del motor |

Tambien por monitor serie (115200): `a`=abrir, `c`=cerrar, `s`=stop, `t`=toggle, `r`=reset falla.

## MQTT

Broker: `broker.emqx.io:1883` (Wokwi lo alcanza con su gateway WiFi `Wokwi-GUEST`).

Topic base: `porton/porton_wokwi_01`

| Topic | Uso |
|---|---|
| `porton/porton_wokwi_01/cmd` | Publicar `{"cmd":"open"}` / `close` / `stop` / `toggle` / `reset` |
| `porton/porton_wokwi_01/config/set` | `{"duty":70,"auto_close_enabled":true,"auto_close_delay_ms":10000,"ftc_behavior":"STOP_AND_REVERSE"}` |
| `porton/porton_wokwi_01/state` | Telemetria JSON retenida |
| `porton/porton_wokwi_01/event` | Cambios de estado |
| `porton/porton_wokwi_01/fault` | Fallas |

Para probar sin instalar nada: entra a https://www.mqtt-dashboard.com/ , conecta a `broker.emqx.io`
y publica/suscribete a esos topics.

## Grabar en hardware real

1. Comenta `WIFI_SSID = "Wokwi-GUEST"` y pon tu SSID/password reales.
2. Descomenta `upload_port = COM5` en `platformio.ini`.
3. `Ctrl+Alt+U` (Upload) y monitor serie a 115200.
