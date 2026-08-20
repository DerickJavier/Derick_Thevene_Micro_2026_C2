# KAKATA RC-433 V1 - PlatformIO

Control remoto 433MHz + WiFi basado en **ESP32-S3**, convertido a estructura
**PlatformIO** para usarse en Visual Studio Code.

## Estructura del proyecto

```
KAKATA_RC433_V1_PlatformIO/
├── platformio.ini            # Configuracion de PlatformIO (ESP32-S3 + ESP-IDF)
├── sdkconfig.kakata_rc433    # Opciones del SDK (flash 16MB, PSRAM octal, WiFi, MQTT)
├── partitions.csv            # Tabla de particiones personalizada
└── src/                      # Codigo fuente completo
    ├── main.c                # Punto de entrada (app_main) y tareas FreeRTOS
    ├── pin_config.h          # Definicion de pines del hardware
    ├── input.c/.h            # Joysticks ADC, botones y bateria
    ├── mpu6050.c/.h          # Acelerometro/giroscopio por I2C
    ├── ssd1306.c/.h          # Driver pantalla OLED SSD1306
    ├── display_ui.c/.h       # Interfaz de usuario en la OLED
    ├── mqtt_app.c/.h         # WiFi + MQTT (publicacion de datos)
    └── calibration.c/.h      # Calibracion de joysticks guardada en NVS
```

## Requisitos

1. Instalar [Visual Studio Code](https://code.visualstudio.com/)
2. Instalar la extension **PlatformIO IDE** desde el marketplace de VS Code
   (la primera vez descargara automaticamente el core de PlatformIO)

## Como abrir y compilar

1. Abrir VS Code
2. Menu `File > Open Folder...` y seleccionar esta carpeta
   (`KAKATA_RC433_V1_PlatformIO`)
3. Esperar a que PlatformIO termine de inicializar el proyecto
4. Compilar con el icono de PlatformIO en la barra lateral -> `Build`
   (o `Ctrl+Alt+B`)

## Como cargar al dispositivo

1. Conectar el ESP32-S3 por USB
2. En el menu de PlatformIO: `Upload` (o `Ctrl+Alt+U`)
3. Ver el monitor serie: `Monitor` (o `Ctrl+Alt+M`, 115200 baudios)

## Configuracion WiFi / MQTT

Editar en `src/main.c`:

```c
#define WIFI_SSID           "TU_WIFI_SSID"
#define WIFI_PASSWORD       "TU_WIFI_PASSWORD"
#define MQTT_BROKER_IP      "192.168.1.100"
#define MQTT_BROKER_PORT    1883
```

## Notas tecnicas

- MCU: ESP32-S3, flash 16MB, PSRAM octal a 80MHz
- Framework: ESP-IDF (PlatformIO lo descarga automaticamente)
- Perifericos: I2C (OLED SSD1306 + MPU6050), ADC (2 joysticks + bateria),
  GPIO (6 LEDs), WiFi STA, MQTT
