# Wrote Documents\esp_mqtt5_project\main\main.c
/*
 * ============================================================================
 * ESP32 MQTT5 - Maquina de estados de 2 estados
 * ============================================================================
 *
 * Descripcion:
 *   Proyecto ESP-IDF que implementa una maquina de estados con 2 estados
 *   (LED ON / LED OFF). El estado se cambia presionando el boton fisico
 *   del ESP32 o enviando comandos MQTT5 desde una app movil/PC.
 *
 * Hardware:
 *   - LED:     GPIO 2 (LED integrado del ESP32)
 *   - Boton:   GPIO 0 (Boton BOOT del ESP32)
 *
 * Topicos MQTT:
 *   - esp32/comando  (suscripcion) : Recibe comandos ON, OFF, TOGGLE, STATUS
 *   - esp32/estado   (publicacion) : Publica el estado actual (ON/OFF)
 *   - esp32/log      (publicacion) : Mensajes de conexion/desconexion
 *
 * Broker publico: mqtt://broker.hivemq.com:1883 (sin autenticacion)
 *
 * Autor: Proyecto ITLA
 * ============================================================================
 */

/* ======================== INCLUDES ======================== */
#include <stdio.h>      /* Funciones de entrada/salida estandar */
#include <stdint.h>     /* Tipos de enteros fijos (uint32_t, etc.) */
#include <stddef.h>     /* Definiciones generales (NULL, size_t) */
#include <string.h>     /* Funciones de cadena (strcmp, strlen, memcpy) */

/* FreeRTOS - Sistema operativo en tiempo real del ESP32 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"     /* Creacion y manejo de tareas */
#include "freertos/queue.h"    /* Colas para comunicacion entre tareas */

/* Componentes ESP-IDF */
#include "esp_system.h"        /* Funciones del sistema ESP32 */
#include "nvs_flash.h"         /* Almacenamiento no-volatil (NVS) para WiFi */
#include "esp_event.h"         /* Manejo de eventos del sistema */
#include "esp_netif.h"         /* Interfaz de red (WiFi, TCP/IP) */
#include "esp_log.h"           /* Sistema de logs (ESP_LOGI, ESP_LOGW, etc.) */
#include "driver/gpio.h"       /* Control de pines GPIO */
#include "mqtt_client.h"       /* Cliente MQTT de ESP-IDF (soporta MQTT5) */

/* Componente auxiliar de ESP-IDF para conexion WiFi automatica */
/* Nota: esta en esp-idf/examples/protocols/mqtt/ */
#include "protocol_examples_common.h"

/* ======================== CONSTANTES ======================== */

/* Tag para los logs del serial (aparece en el monitor) */
static const char *TAG = "MQTT5_APP";

/* ======================== PINES GPIO ======================== */

/*
 * LED_GPIO - Pin del LED que se enciende/apaga
 *   GPIO 2 es el LED integrado en la mayoria de placas ESP32
 *
 * BUTTON_GPIO - Pin del boton fisico
 *   GPIO 0 es el boton BOOT del ESP32, tiene pull-up interno
 */
#define LED_GPIO        GPIO_NUM_2
#define BUTTON_GPIO     GPIO_NUM_0

/* ======================== MAQUINA DE ESTADOS ======================== */

/*
 * state_t - Enumeracion de los 2 estados posibles
 *
 *   STATE_LED_OFF (0) = LED apagado
 *   STATE_LED_ON  (1) = LED encendido
 *
 * La maquina solo puede estar en uno de estos dos estados.
 * Cada vez que se presiona el boton o se recibe un comando MQTT,
 * la maquina cambia al otro estado (transicion).
 */
typedef enum {
    STATE_LED_OFF = 0,   /* Estado: LED apagado */
    STATE_LED_ON  = 1    /* Estado: LED encendido */
} state_t;

/*
 * current_state - Variable que almacena el estado actual de la maquina
 *
 * volatile: se usa porque puede ser modificada desde una interrupcion
 *           (boton) o desde el handler MQTT (otra tarea)
 */
static volatile state_t current_state = STATE_LED_OFF;

/* ======================== VARIABLES GLOBALES ======================== */

/*
 * button_event_queue - Cola para enviar eventos del boton desde la
 *   interrupcion (ISR) hacia la tarea de la maquina de estados.
 *   FreeRTOS usa colas para que las ISR puedan comunicarse con tareas.
 */
static QueueHandle_t button_event_queue = NULL;

/*
 * mqtt_client_handle - Manejador del cliente MQTT5.
 *   Se usa para publicar mensajes y verificar si esta conectado.
 */
static esp_mqtt_client_handle_t mqtt_client_handle = NULL;

/* ======================== TOPICOS MQTT ======================== */

/*
 * TOPIC_CMD - Topico donde el ESP32 recibe comandos
 *   El celular/PC publica aqui: "ON", "OFF", "TOGGLE", "STATUS"
 *
 * TOPIC_STATE - Topico donde el ESP32 publica su estado actual
 *   El celular/PC se suscribe aqui para ver si el LED esta ON/OFF
 *
 * TOPIC_LOG - Topico para mensajes de log
 *   "ESP32 conectado" cuando se conecta
 *   "ESP32 desconectado" si se cae (last will)
 */
#define TOPIC_CMD       "esp32/comando"
#define TOPIC_STATE     "esp32/estado"
#define TOPIC_LOG       "esp32/log"

/* ======================== FUNCIONES DE ESTADO ======================== */

/*
 * state_name() - Convierte un estado a su nombre en texto
 *
 * Parametros:
 *   s - Estado actual (STATE_LED_ON o STATE_LED_OFF)
 *
 * Retorna:
 *   "ON" si el estado es STATE_LED_ON
 *   "OFF" si el estado es STATE_LED_OFF
 *
 * Se usa para mostrar el nombre del estado en logs y publicar en MQTT.
 */
static const char* state_name(state_t s) {
    return (s == STATE_LED_ON) ? "ON" : "OFF";
}

/*
 * apply_state() - Aplica un estado al hardware (enciende/apaga LED)
 *
 * Parametros:
 *   s - Estado a aplicar (STATE_LED_ON o STATE_LED_OFF)
 *
 * Funcionamiento:
 *   1. GPIO 2 = 1 si el estado es ON
 *   2. GPIO 2 = 0 si el estado es OFF
 *   3. Muestra el estado en el monitor serial
 *
 * Esta funcion es la que realmente cambia el LED fisicamente.
 */
static void apply_state(state_t s) {
    gpio_set_level(LED_GPIO, (s == STATE_LED_ON) ? 1 : 0);
    ESP_LOGI(TAG, "Estado: %s (GPIO%d=%d)", state_name(s), LED_GPIO, (int)s);
}

/*
 * transition_to() - Realiza una transicion de estado completa
 *
 * Parametros:
 *   new_state - Estado al que se quiere transicionar
 *
 * Funcionamiento:
 *   1. Guarda el estado anterior
 *   2. Actualiza current_state al nuevo estado
 *   3. Aplica el nuevo estado al hardware (LED)
 *   4. Publica el nuevo estado en MQTT (si esta conectado)
 *   5. Muestra la transicion en el monitor serial
 *
 * Esta es la funcion principal de la maquina de estados.
 * Se llama cada vez que hay un cambio de estado (boton o MQTT).
 */
static void transition_to(state_t new_state) {
    /* Guardar el estado anterior para el log */
    state_t old_state = current_state;

    /* Actualizar el estado global */
    current_state = new_state;

    /* Aplicar el nuevo estado al LED fisico */
    apply_state(new_state);

    /* Publicar el nuevo estado en MQTT si el cliente esta conectado */
    if (mqtt_client_handle != NULL) {
        esp_mqtt_client_publish(mqtt_client_handle, TOPIC_STATE,
                                state_name(new_state), 0, 1, 1);
        ESP_LOGI(TAG, "Transicion: %s -> %s",
                 state_name(old_state), state_name(new_state));
    }
}

/* ======================== LED - GPIO 2 ======================== */

/*
 * led_init() - Configura el pin del LED como salida digital
 *
 * Funcionamiento:
 *   1. Configura GPIO 2 como salida
 *   2. Sin pull-up ni pull-down (no necesario para LED)
 *   3. Sin interrupciones (solo es salida)
 *   4. Inicia el LED apagado (nivel 0)
 *
 * Se llama una sola vez al inicio del programa en app_main().
 */
static void led_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),  /* Mascarra de pin: GPIO 2 */
        .mode = GPIO_MODE_OUTPUT,            /* Modo: salida digital */
        .pull_up_en = GPIO_PULLUP_DISABLE,   /* Sin pull-up interno */
        .pull_down_en = GPIO_PULLDOWN_DISABLE, /* Sin pull-down */
        .intr_type = GPIO_INTR_DISABLE,      /* Sin interrupciones */
    };
    gpio_config(&io_conf);       /* Aplicar configuracion al hardware */
    gpio_set_level(LED_GPIO, 0); /* Iniciar LED apagado */
}

/* ======================== BOTON - GPIO 0 ======================== */

/*
 * button_isr_handler() - Manejador de interrupcion del boton
 *
 * Parametros:
 *   arg - Puntero al numero de GPIO (en este caso GPIO 0)
 *
 * Funcionamiento:
 *   Esta funcion se ejecuta automaticamente cuando se presiona el boton
 *   (flanco descendente: de HIGH a LOW). Se ejecuta en contexto de
 *   interrupcion (ISR), por lo que debe ser rapida y no puede usar
 *   funciones bloqueantes.
 *
 *   La funcion envia el numero de GPIO a la cola button_event_queue
 *   para que la tarea de la maquina de estados lo procese.
 *
 * IRAM_ATTR: indica que esta funcion debe estar en RAM interna
 *   para que pueda ejecutarse rapidamente desde una interrupcion.
 */
static void IRAM_ATTR button_isr_handler(void *arg) {
    uint32_t gpio_num = (uint32_t)arg;
    xQueueSendFromISR(button_event_queue, &gpio_num, NULL);
}

/*
 * button_init() - Configura el pin del boton como entrada con interrupcion
 *
 * Funcionamiento:
 *   1. Configura GPIO 0 como entrada
 *   2. Activa pull-up interno ( boton normalmente en HIGH)
 *   3. Configura interrupcion por flanco descendente (HIGH -> LOW)
 *   4. Instala el servicio de interrupciones de GPIO
 *   5. Registra el manejador de interrupcion para GPIO 0
 *
 * Cuando se presiona el boton, GPIO 0 pasa de HIGH a LOW,
 * lo que genera una interrupcion que ejecuta button_isr_handler().
 */
static void button_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),  /* Mascarra: GPIO 0 */
        .mode = GPIO_MODE_INPUT,                /* Modo: entrada digital */
        .pull_up_en = GPIO_PULLUP_ENABLE,       /* Pull-up interno ON */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  /* Sin pull-down */
        .intr_type = GPIO_INTR_NEGEDGE,         /* Interrupcion: flanco descendente */
    };
    gpio_config(&io_conf);                      /* Aplicar configuracion */
    gpio_install_isr_service(0);                /* Instalar servicio ISR */
    gpio_isr_handler_add(BUTTON_GPIO,           /* Agregar manejador para GPIO 0 */
                         button_isr_handler,
                         (void *)BUTTON_GPIO);
}

/* ======================== TAREA MAQUINA DE ESTADOS ======================== */

/*
 * state_machine_task() - Tarea FreeRTOS que ejecuta la maquina de estados
 *
 * Parametros:
 *   arg - Puntero a argumentos (no usado, NULL)
 *
 * Funcionamiento:
 *   1. Aplica el estado inicial (LED apagado)
 *   2. Entra en un bucle infinito esperando eventos
 *   3. Cuando recibe un evento de la cola (boton presionado):
 *      - Calcula el siguiente estado (cambia al otro estado)
 *      - Ejecuta la transicion de estado
 *   4. Repite paso 2
 *
 * Esta tarea corre en paralelo con el WiFi y MQTT.
 * La cola button_event_queue sincroniza la interrupcion del boton
 * con esta tarea.
 *
 * stack_size: 4096 bytes (suficiente para esta tarea)
 * priority: 5 (media-alta)
 */
static void state_machine_task(void *arg) {
    uint32_t io_num;     /* Numero de GPIO que genero el evento */
    state_t next_state;  /* Siguiente estado calculado */

    /* Aplicar el estado inicial (LED apagado) */
    apply_state(current_state);

    /* Bucle principal de la maquina de estados */
    while (1) {
        /* Esperar un evento de la cola (bloquea hasta que llegue algo) */
        if (xQueueReceive(button_event_queue, &io_num, portMAX_DELAY)) {
            /* Calcular el siguiente estado: cambiar al otro estado */
            next_state = (current_state == STATE_LED_OFF)
                         ? STATE_LED_ON
                         : STATE_LED_OFF;

            /* Ejecutar la transicion de estado */
            transition_to(next_state);
        }
    }
}

/* ======================== MQTT5 EVENT HANDLER ======================== */

/*
 * mqtt5_event_handler() - Manejador de eventos del cliente MQTT5
 *
 * Parametros:
 *   handler_args - Argumentos del manejador (no usado)
 *   base         - Base del evento (MQTT_EVENT_BASE)
 *   event_id     - Identificador del evento que ocurrio
 *   event_data   - Puntero a los datos del evento
 *
 * Eventos que maneja:
 *   MQTT_EVENT_CONNECTED:
 *     - Se suscribe al topico de comandos (esp32/comando)
 *     - Publica el estado actual en esp32/estado
 *     - Publica "ESP32 conectado" en esp32/log
 *
 *   MQTT_EVENT_DATA:
 *     - Verifica que el mensaje sea para esp32/comando
 *     - Parsea el comando recibido (ON/OFF/TOGGLE/STATUS)
 *     - Ejecuta la accion correspondiente
 *
 *   MQTT_EVENT_DISCONNECTED:
 *     - Muestra warning en el monitor
 *
 *   MQTT_EVENT_ERROR:
 *     - Muestra error en el monitor
 */
static void mqtt5_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) {

    /* --- Conexion exitosa al broker MQTT --- */
    case MQTT_EVENT_CONNECTED:
        /* Suscribirse al topico de comandos con QoS 1 */
        msg_id = esp_mqtt_client_subscribe(client, TOPIC_CMD, 1);
        ESP_LOGI(TAG, "Suscrito a %s (msg_id=%d)", TOPIC_CMD, msg_id);

        /* Publicar estado actual (retain=1 para que nuevos suscriptores lo reciban) */
        esp_mqtt_client_publish(client, TOPIC_STATE,
                                state_name(current_state), 0, 1, 1);

        /* Publicar mensaje de conexion en el log */
        esp_mqtt_client_publish(client, TOPIC_LOG,
                                "ESP32 conectado", 0, 0, 0);
        break;

    /* --- Mensaje MQTT recibido --- */
    case MQTT_EVENT_DATA:
        /* Verificar que el mensaje sea para el topico esp32/comando */
        if (event->topic_len == strlen(TOPIC_CMD) &&
            strncmp(event->topic, TOPIC_CMD, event->topic_len) == 0) {

            /* Copiar el payload del mensaje a un buffer seguro */
            char data[32] = {0};
            int len = event->data_len < 31 ? event->data_len : 31;
            memcpy(data, event->data, len);

            ESP_LOGI(TAG, "Comando recibido: %s", data);

            /* Parsear y ejecutar el comando recibido */
            if (strcmp(data, "ON") == 0 || strcmp(data, "1") == 0) {
                /* Comando ON: encender LED */
                transition_to(STATE_LED_ON);
            } else if (strcmp(data, "OFF") == 0 || strcmp(data, "0") == 0) {
                /* Comando OFF: apagar LED */
                transition_to(STATE_LED_OFF);
            } else if (strcmp(data, "TOGGLE") == 0) {
                /* Comando TOGGLE: cambiar al estado opuesto */
                transition_to((current_state == STATE_LED_OFF)
                              ? STATE_LED_ON : STATE_LED_OFF);
            } else if (strcmp(data, "STATUS") == 0) {
                /* Comando STATUS: publicar estado actual sin cambiar nada */
                esp_mqtt_client_publish(client, TOPIC_STATE,
                                        state_name(current_state), 0, 1, 0);
            }
        }
        break;

    /* --- Desconexion del broker --- */
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT desconectado");
        break;

    /* --- Error en la conexion MQTT --- */
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

/* ======================== MQTT5 START ======================== */

/*
 * mqtt5_app_start() - Inicia el cliente MQTT5 y lo conecta al broker
 *
 * Funcionamiento:
 *   1. Configura el cliente MQTT con los siguientes parametros:
 *      - Broker: broker.hivemq.com (publico, sin autenticacion)
 *      - Protocolo: MQTT v5
 *      - Auto-reconexion: habilitada
 *      - Last Will: "ESP32 desconectado" en esp32/log si se cae
 *   2. Inicializa el cliente MQTT con la configuracion
 *   3. Registra el manejador de eventos mqtt5_event_handler
 *   4. Inicia el cliente MQTT (se conecta al broker)
 *
 * MQTT5 vs MQTT 3.1.1:
 *   - MQTT5 soporta propiedades adicionales
 *   - Permite mensajes de error mas detallados
 *   - Soporta session expiry, topic aliases, etc.
 */
static void mqtt5_app_start(void) {
    /* Configuracion del cliente MQTT5 */
    esp_mqtt_client_config_t mqtt5_cfg = {
        .broker.address.uri = "mqtt://broker.hivemq.com",  /* Broker publico */
        .session.protocol_ver = MQTT_PROTOCOL_V_5,         /* Protocolo MQTT v5 */
        .network.disable_auto_reconnect = false,            /* Auto-reconexion ON */
        .session.last_will.topic = TOPIC_LOG,              /* Topico del Last Will */
        .session.last_will.msg = "ESP32 desconectado",     /* Mensaje si se cae */
        .session.last_will.qos = 1,                        /* QoS del Last Will */
    };

    /* Inicializar el cliente MQTT con la configuracion */
    mqtt_client_handle = esp_mqtt_client_init(&mqtt5_cfg);

    /* Registrar el manejador de eventos para todos los eventos MQTT */
    esp_mqtt_client_register_event(mqtt_client_handle,
                                   ESP_EVENT_ANY_ID,
                                   mqtt5_event_handler,
                                   NULL);

    /* Iniciar el cliente MQTT (intenta conectarse al broker) */
    esp_mqtt_client_start(mqtt_client_handle);
    ESP_LOGI(TAG, "MQTT5 cliente iniciado -> %s", mqtt5_cfg.broker.address.uri);
}

/* ======================== APP_MAIN ======================== */

/*
 * app_main() - Punto de entrada principal del programa ESP32
 *
 * Funcionamiento:
 *   1. Inicializa NVS (almacenamiento no-volatil para credenciales WiFi)
 *   2. Inicializa la red (netif + event loop)
 *   3. Configura el LED y el boton
 *   4. Crea la cola de eventos del boton
 *   5. Crea la tarea de la maquina de estados
 *   6. Conecta al WiFi (usa protocol_examples_common)
 *   7. Inicia el cliente MQTT5
 *
 * Orden de inicializacion importante:
 *   NVS -> Red -> GPIO -> Cola -> Tarea -> WiFi -> MQTT
 *
 * NVS se inicializa primero porque WiFi necesita almacenar
 * las credenciales en NVS. Si NVS esta corrupta, se borra
 * y se reinicia.
 */
void app_main(void) {
    /* --- Paso 1: Inicializar NVS (necesario para WiFi) --- */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* Si NVS esta corrupta o lleno, borrar y reinicializar */
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* --- Paso 2: Inicializar la red (TCP/IP + event loop) --- */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* --- Paso 3: Configurar LED y Boton --- */
    led_init();                              /* GPIO 2 como salida (LED) */
    button_event_queue = xQueueCreate(10, sizeof(uint32_t)); /* Cola de 10 eventos */
    button_init();                           /* GPIO 0 como entrada + interrupcion */

    /* --- Paso 4: Crear tarea de la maquina de estados --- */
    xTaskCreate(state_machine_task,   /* Funcion de la tarea */
                "state_machine",      /* Nombre (para depuracion) */
                4096,                 /* Stack size en bytes */
                NULL,                 /* Argumentos (ninguno) */
                5,                    /* Prioridad (0=minima, configMAX_PRIORITIES-1=maxima) */
                NULL);                /* Handle (no lo necesitamos) */

    /* --- Paso 5: Conectar al WiFi --- */
    /* example_connect() es una funcion auxiliar de ESP-IDF que:
       - Configura el WiFi en modo STA (estacion)
       - Usa las credenciales de sdkconfig.defaults
       - Bloquea hasta que se conecte */
    ESP_ERROR_CHECK(example_connect());

    /* --- Paso 6: Iniciar cliente MQTT5 --- */
    mqtt5_app_start();
}

+ Thought: 699ms
$ Get-Content "C:\Users\TIGER\Documents\esp_mqtt5_project\main\main.c" | Measure-Object -Line
Lines Words Characters Property
----- ----- ---------- --------
  461
+ Thought: 654ms
Codigo organizado con 461 lineas. Cada funcion tiene su comentario explicando:
Seccion	Funciones	Que hace
Estado	state_name(), apply_state(), transition_to()	Convierte estados a texto, aplica al LED, ejecuta transiciones
LED	led_init()	Configura GPIO 2 como salida
Boton	button_isr_handler(), button_init()	Interrupcion + configura GPIO 0 como entrada
Tarea	state_machine_task()	Bucle que espera eventos del boton y cambia estado
MQTT5	mqtt5_event_handler(), mqtt5_app_start()	Maneja conexion, comandos ON/OFF/TOGGLE/STATUS
Main	app_main()	Inicializa NVS, red, GPIO, WiFi, MQTT5 en orden
Archivos del proyecto en C:\Users\TIGER\Documents\esp_mqtt5_project\:
- main/main.c - Codigo comentado
- CMakeLists.txt - Configuracion del proyecto
- sdkconfig.defaults - WiFi + MQTT5
- partitions.csv - Tabla de particiones
- README.md - Instrucciones app movil