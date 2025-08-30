#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "ssd1306.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"

// ---------- Configurações ----------

#define JOY_ADC_MAX 4095
#define JOY_CENTER (JOY_ADC_MAX / 2)
#define JOY_DEAD_ZONE 600
#define JOY_REPEAT_MS 150
#define JOY_X_ADC_CHANNEL 0
#define JOY_Y_ADC_CHANNEL 1
#define BTN_A_PIN 5
#define BTN_B_PIN 6
#define LED_R_PIN 13
#define LED_G_PIN 11

#define LIFE_RENDER_WIDTH ssd1306_width   // 128
#define LIFE_RENDER_HEIGHT ssd1306_height // 64

#define LIFE_GRID_WIDTH 136 // tabuleiro maior que render
#define LIFE_GRID_HEIGHT 72

// --- Config WiFi + MQTT ---

#define WIFI_SSID "brisa-4370576"
#define WIFI_PASSWORD "mmy6opmr"
#define MQTT_BROKER "52.57.135.186"
#define MQTT_TOPIC "pico/life"

// ---------- Variáveis globais ----------

bool life_grid[LIFE_GRID_WIDTH][LIFE_GRID_HEIGHT];
bool life_grid_next[LIFE_GRID_WIDTH][LIFE_GRID_HEIGHT];

int cursor_x = 0;
int cursor_y = 0;

bool life_running = false;

// SSD1306 buffer
uint8_t ssd[ssd1306_buffer_length];
struct render_area frame_area;

volatile uint64_t last_press_time_a = 0;
volatile uint64_t last_press_time_b = 0;

// ---------- Joystick & Botões ----------

static inline int wrap_x(int v) { return (v + LIFE_GRID_WIDTH) % LIFE_GRID_WIDTH; }
static inline int wrap_y(int v) { return (v + LIFE_GRID_HEIGHT) % LIFE_GRID_HEIGHT; }

// Cliente MQTT
mqtt_client_t *mqtt_client;

// Info MQTT
static const struct mqtt_connect_client_info_t mqtt_client_info = {
    .client_id = "PicoLife",
    .keep_alive = 60,
};

void gpio_callback(uint gpio, uint32_t events)
{
    uint64_t current_time = to_ms_since_boot(get_absolute_time());

    if (gpio == BTN_A_PIN && current_time - last_press_time_a > 200) // 200ms debounce
    {
        last_press_time_a = current_time;

        if (!life_running)
        {
            // Toggle célula
            life_grid[cursor_x][cursor_y] = !life_grid[cursor_x][cursor_y];
        }
    }
    else if (gpio == BTN_B_PIN && current_time - last_press_time_b > 200) // 200ms debounce
    {
        last_press_time_b = current_time;

        if (!life_running)
        {
            // Começa o Jogo da Vida
            life_running = true;
            gpio_put(LED_R_PIN, 0);
            gpio_put(LED_G_PIN, 0);
        }
        else
        {
            // Reset: volta para desenho
            life_running = false;
            memset(life_grid, 0, sizeof(life_grid));
            cursor_x = 0;
            cursor_y = 0;
            gpio_put(LED_R_PIN, 0);
            gpio_put(LED_G_PIN, 1);
        }
    }
}

void handle_joystick(void)
{
    static uint64_t last_move_ms = 0;
    uint64_t now_ms = to_ms_since_boot(get_absolute_time());
    if (now_ms - last_move_ms < JOY_REPEAT_MS)
        return;

    adc_select_input(JOY_X_ADC_CHANNEL);
    int16_t adc_x = adc_read();
    adc_select_input(JOY_Y_ADC_CHANNEL);
    int16_t adc_y = adc_read();

    int dx = 0, dy = 0;

    // Corrigir mapeamento
    if (adc_y < JOY_CENTER - JOY_DEAD_ZONE)
        dx = -1; // up → x--
    else if (adc_y > JOY_CENTER + JOY_DEAD_ZONE)
        dx = +1; // down → x++

    if (adc_x > JOY_CENTER + JOY_DEAD_ZONE)
        dy = -1; // right → y--
    else if (adc_x < JOY_CENTER - JOY_DEAD_ZONE)
        dy = +1; // left → y++

    if (!dx && !dy)
        return;

    cursor_x = wrap_x(cursor_x + dx);
    cursor_y = wrap_y(cursor_y + dy);

    last_move_ms = now_ms;
}

// ---------- Jogo da Vida ----------

void update_life(void)
{
    for (int x = 0; x < LIFE_GRID_WIDTH; x++)
    {
        for (int y = 0; y < LIFE_GRID_HEIGHT; y++)
        {
            int neighbors = 0;
            for (int dx = -1; dx <= 1; dx++)
            {
                for (int dy = -1; dy <= 1; dy++)
                {
                    if (dx == 0 && dy == 0)
                        continue;
                    int nx = x + dx;
                    int ny = y + dy;
                    if (nx >= 0 && nx < LIFE_GRID_WIDTH && ny >= 0 && ny < LIFE_GRID_HEIGHT)
                        neighbors += life_grid[nx][ny] ? 1 : 0;
                }
            }
            if (life_grid[x][y])
                life_grid_next[x][y] = (neighbors == 2 || neighbors == 3);
            else
                life_grid_next[x][y] = (neighbors == 3);
        }
    }
    memcpy(life_grid, life_grid_next, sizeof(life_grid));
}

// ---------- Renderização ----------

void render_life(void)
{
    ssd1306_clear(ssd);

    // Render apenas o retângulo visível do tabuleiro
    for (int x = 0; x < LIFE_RENDER_WIDTH; x++)
        for (int y = 0; y < LIFE_RENDER_HEIGHT; y++)
            if (life_grid[x][y])
                ssd1306_set_pixel(ssd, x, y, true);

    // Cursor piscante se não estiver rodando
    static bool blink = false;
    static uint64_t last_blink_ms = 0;
    uint64_t now_ms = to_ms_since_boot(get_absolute_time());
    if (now_ms - last_blink_ms > 500)
    {
        blink = !blink;
        last_blink_ms = now_ms;
    }
    if (!life_running && blink)
    {
        if (cursor_x < LIFE_RENDER_WIDTH && cursor_y < LIFE_RENDER_HEIGHT)
            ssd1306_set_pixel(ssd, cursor_x, cursor_y, true);
    }

    render_on_display(ssd, &frame_area);
}

// ---------- Inicialização ----------

void init_hardware(void)
{
    stdio_init_all();

    // Botões
    gpio_init(BTN_A_PIN);
    gpio_set_dir(BTN_A_PIN, GPIO_IN);
    gpio_pull_up(BTN_A_PIN);
    gpio_init(BTN_B_PIN);
    gpio_set_dir(BTN_B_PIN, GPIO_IN);
    gpio_pull_up(BTN_B_PIN);
    gpio_set_irq_enabled_with_callback(BTN_A_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled_with_callback(BTN_B_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    // LED
    gpio_init(LED_R_PIN);
    gpio_set_dir(LED_R_PIN, GPIO_OUT);
    gpio_init(LED_G_PIN);
    gpio_set_dir(LED_G_PIN, GPIO_OUT);

    // ADC
    adc_init();
    adc_gpio_init(JOY_X_ADC_CHANNEL + 26);
    adc_gpio_init(JOY_Y_ADC_CHANNEL + 26);
}

// -------- MQTT: Publish estado --------
void mqtt_send_message(mqtt_client_t *client, const char *message)
{
    char formatted_message[256];
    snprintf(formatted_message, sizeof(formatted_message), "pico: %s", message);

    err_t result = mqtt_publish(client, MQTT_TOPIC, formatted_message,
                                strlen(formatted_message), 0, 0, NULL, NULL);
    if (result == ERR_OK)
    {
        printf("MQTT published: %s\n", formatted_message);
        gpio_put(LED_R_PIN, 1); // blink red LED on publish
        sleep_ms(100);
        gpio_put(LED_R_PIN, 0);
    }
    else
    {
        printf("MQTT publish failed. Error: %d\n", result);
        gpio_put(LED_R_PIN, 1); // keep red ON if fail
    }
}

// -------- Callback de subscription --------
void mqtt_subscription_cb(void *arg, err_t err)
{
    if (err == ERR_OK)
        printf("✅ Subscribed to topic: %s\n", MQTT_TOPIC);
    else
        printf("❌ Subscription failed. Error: %d\n", err);
}

// -------- Callback de conexão --------
void mqtt_connection_cb(mqtt_client_t *client, void *arg,
                        mqtt_connection_status_t status)
{
    if (status == MQTT_CONNECT_ACCEPTED)
    {
        printf("✅ MQTT connected.\n");

        // Mensagem de boas-vindas
        mqtt_send_message(client, "Game of Life Pico W connected!");

        // Se inscreve para receber updates
        mqtt_subscribe(client, MQTT_TOPIC, 1, mqtt_subscription_cb, NULL);

        gpio_put(LED_G_PIN, 1); // green = connected
        gpio_put(LED_R_PIN, 0);
    }
    else
    {
        printf("❌ MQTT connection failed, status: %d\n", status);
        gpio_put(LED_G_PIN, 0);
        gpio_put(LED_R_PIN, 1);
    }
}

// -------- Mensagem chegando --------
void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t total_length)
{
    printf("📩 Incoming message on topic: %s, length: %d\n", topic, total_length);
}

// -------- Processar dados recebidos --------
void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    char incoming_message[256]; // buffer pequeno
    if (len >= sizeof(incoming_message)) len = sizeof(incoming_message) - 1;

    memcpy(incoming_message, data, len);
    incoming_message[len] = '\0';

    // processa cada chunk
    int x, y;
    char *ptr = incoming_message;
    while ((ptr = strchr(ptr, '[')) != NULL) {
        if (sscanf(ptr, "[%d,%d]", &x, &y) == 2) {
            if (x >= 0 && x < LIFE_GRID_WIDTH &&
                y >= 0 && y < LIFE_GRID_HEIGHT) {
                life_grid[x][y] = true;
            }
        }
        ptr++;
    }
}

// -------- Inicialização MQTT --------
void init_mqtt()
{
    ip_addr_t broker_ip;
    mqtt_client = mqtt_client_new();
    if (!mqtt_client)
    {
        printf("❌ Failed to create MQTT client\n");
        gpio_put(LED_G_PIN, 0);
        gpio_put(LED_R_PIN, 1);
        return;
    }

    if (!ip4addr_aton(MQTT_BROKER, &broker_ip))
    {
        printf("❌ Failed to resolve broker IP: %s\n", MQTT_BROKER);
        gpio_put(LED_G_PIN, 0);
        gpio_put(LED_R_PIN, 1);
        return;
    }

    err_t err = mqtt_client_connect(mqtt_client, &broker_ip, MQTT_PORT,
                                    mqtt_connection_cb, NULL, &mqtt_client_info);

    mqtt_set_inpub_callback(mqtt_client,
                            mqtt_incoming_publish_cb,
                            mqtt_incoming_data_cb, NULL);

    if (err != ERR_OK)
    {
        printf("❌ MQTT connect failed, code: %d\n", err);
        gpio_put(LED_G_PIN, 0);
        gpio_put(LED_R_PIN, 1);
        return;
    }

    printf("🔗 Connecting to MQTT broker %s:%d...\n", MQTT_BROKER, MQTT_PORT);
}

// Connect to Wi-Fi
void connect_to_wifi()
{
    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000))
    {
        printf("Failed to connect to Wi-Fi.\n");
        gpio_put(LED_G_PIN, 0); // Turn off green LED
        gpio_put(LED_R_PIN, 1); // Turn on red LED
        while (1)
            sleep_ms(1000);
    }
    printf("Connected to Wi-Fi.\n");
    gpio_put(LED_G_PIN, 1); // Turn on green LED
    gpio_put(LED_R_PIN, 0); // Turn off red LED
}

void init_oled_display(void)
{
    ssd1306_init();

    frame_area.start_column = 0;
    frame_area.end_column = ssd1306_width - 1;
    frame_area.start_page = 0;
    frame_area.end_page = ssd1306_n_pages - 1;
    calculate_render_area_buffer_length(&frame_area);

    memset(ssd, 0, ssd1306_buffer_length);
    render_on_display(ssd, &frame_area);
}

// ---------- Main ----------

int main()
{
    init_hardware();

    if (cyw43_arch_init())
    {
        printf("Erro ao inicializar WiFi chip\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    connect_to_wifi();
    init_mqtt();

    init_oled_display();

    memset(life_grid, 0, sizeof(life_grid));

    while (true)
    {
        cyw43_arch_poll(); // precisa para o WiFi/MQTT rodar
        if (!life_running)
            handle_joystick();
        else
            update_life();

        render_life();
        sleep_ms(50);
    }

    cyw43_arch_deinit();
    return 0;
}
