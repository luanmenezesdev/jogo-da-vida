#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "ssd1306.h"

// ---------- Configurações ----------

#define JOY_ADC_MAX 4095
#define JOY_CENTER (JOY_ADC_MAX / 2)
#define JOY_DEAD_ZONE 600
#define JOY_REPEAT_MS 150
#define JOY_X_ADC_CHANNEL 0
#define JOY_Y_ADC_CHANNEL 1
#define BTN_A_PIN 5
#define BTN_B_PIN 6

#define LIFE_RENDER_WIDTH ssd1306_width    // 128
#define LIFE_RENDER_HEIGHT ssd1306_height  // 64

#define LIFE_GRID_WIDTH 136   // tabuleiro maior que render
#define LIFE_GRID_HEIGHT 72

// ---------- Variáveis globais ----------

bool life_grid[LIFE_GRID_WIDTH][LIFE_GRID_HEIGHT];
bool life_grid_next[LIFE_GRID_WIDTH][LIFE_GRID_HEIGHT];

int cursor_x = 0;
int cursor_y = 0;

bool life_running = false;

// SSD1306 buffer
uint8_t ssd[ssd1306_buffer_length];
struct render_area frame_area;

// ---------- Joystick & Botões ----------

static inline int wrap_x(int v) { return (v + LIFE_GRID_WIDTH) % LIFE_GRID_WIDTH; }
static inline int wrap_y(int v) { return (v + LIFE_GRID_HEIGHT) % LIFE_GRID_HEIGHT; }

void gpio_callback(uint gpio, uint32_t events) {
    if (gpio == BTN_A_PIN && !life_running) {
        // Toggle célula
        life_grid[cursor_x][cursor_y] = !life_grid[cursor_x][cursor_y];
    } 
    else if (gpio == BTN_B_PIN) {
        if (!life_running) {
            // Começa o Jogo da Vida
            life_running = true;
        } else {
            // Reset: volta para desenho
            life_running = false;
            memset(life_grid, 0, sizeof(life_grid));
            cursor_x = 0;
            cursor_y = 0;
        }
    }
}

void handle_joystick(void) {
    static uint64_t last_move_ms = 0;
    uint64_t now_ms = to_ms_since_boot(get_absolute_time());
    if (now_ms - last_move_ms < JOY_REPEAT_MS) return;

    adc_select_input(JOY_X_ADC_CHANNEL);
    int16_t adc_x = adc_read();
    adc_select_input(JOY_Y_ADC_CHANNEL);
    int16_t adc_y = adc_read();

    int dx = 0, dy = 0;

    // Corrigir mapeamento
    if (adc_y < JOY_CENTER - JOY_DEAD_ZONE) dx = -1;  // up → x--
    else if (adc_y > JOY_CENTER + JOY_DEAD_ZONE) dx = +1; // down → x++

    if (adc_x > JOY_CENTER + JOY_DEAD_ZONE) dy = -1; // right → y--
    else if (adc_x < JOY_CENTER - JOY_DEAD_ZONE) dy = +1; // left → y++

    if (!dx && !dy) return;

    cursor_x = wrap_x(cursor_x + dx);
    cursor_y = wrap_y(cursor_y + dy);

    last_move_ms = now_ms;
}


// ---------- Jogo da Vida ----------

void update_life(void) {
    for (int x = 0; x < LIFE_GRID_WIDTH; x++) {
        for (int y = 0; y < LIFE_GRID_HEIGHT; y++) {
            int neighbors = 0;
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    if (dx == 0 && dy == 0) continue;
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

void render_life(void) {
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
    if (now_ms - last_blink_ms > 500) {
        blink = !blink;
        last_blink_ms = now_ms;
    }
    if (!life_running && blink) {
        if (cursor_x < LIFE_RENDER_WIDTH && cursor_y < LIFE_RENDER_HEIGHT)
            ssd1306_set_pixel(ssd, cursor_x, cursor_y, true);
    }

    render_on_display(ssd, &frame_area);
}

// ---------- Inicialização ----------

void init_hardware(void) {
    stdio_init_all();

    // Botões
    gpio_init(BTN_A_PIN); gpio_set_dir(BTN_A_PIN, GPIO_IN); gpio_pull_up(BTN_A_PIN);
    gpio_init(BTN_B_PIN); gpio_set_dir(BTN_B_PIN, GPIO_IN); gpio_pull_up(BTN_B_PIN);
    gpio_set_irq_enabled_with_callback(BTN_A_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled_with_callback(BTN_B_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    // ADC
    adc_init();
    adc_gpio_init(JOY_X_ADC_CHANNEL + 26);
    adc_gpio_init(JOY_Y_ADC_CHANNEL + 26);
}

void init_oled_display(void) {
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

int main() {
    init_hardware();
    init_oled_display();

    memset(life_grid, 0, sizeof(life_grid));

    while (true) {
        if (!life_running) handle_joystick();
        else update_life();

        render_life();
        sleep_ms(50);
    }

    return 0;
}
