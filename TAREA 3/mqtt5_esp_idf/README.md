# MQTT5 LED Control - ESP32 (ESP-IDF nativo)

Control del LED integrado (GPIO 2) via MQTT v5. Proyecto en C puro con estructura estandar de ESP-IDF.

## Configuracion
- WiFi: **Las Penas** / **Pena123321**
- Broker: **broker.hivemq.com** (publico)
- Topic: **/topic/led**

## Estructura del proyecto

    mqtt5_esp_idf/
    |- CMakeLists.txt           # CMake raiz
    |- sdkconfig.defaults       # Configuracion por defecto
    '- main/
       |- CMakeLists.txt        # Registro del componente
       |- Kconfig.projbuild     # Menu de configuracion (WiFi/Broker)
       '- main.c                # Codigo fuente principal

## Requisitos
- ESP-IDF v5.x instalado y exportado (export.bat / . $IDF_PATH/export.sh)

## Compilar y flashear

    idf.py set-target esp32
    idf.py build
    idf.py -p COM3 flash monitor

(Cambia COM3 por el puerto de tu placa)

## Cambiar WiFi/Broker

    idf.py menuconfig   -> MQTT5 LED Config

## Probar LED

    # Encender
    mosquitto_pub -h broker.hivemq.com -t "/topic/led" -m "ON"

    # Apagar
    mosquitto_pub -h broker.hivemq.com -t "/topic/led" -m "OFF"

    # O con 1/0
    mosquitto_pub -h broker.hivemq.com -t "/topic/led" -m "1"
    mosquitto_pub -h broker.hivemq.com -t "/topic/led" -m "0"
