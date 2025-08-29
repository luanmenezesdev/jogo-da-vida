#include "ssd1306.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "ssd1306_font.h"

uint8_t ssd1306_buffer[ssd1306_buffer_length];

// ---------- Low-level I2C helpers ----------
void ssd1306_send_command(uint8_t cmd) {
    uint8_t pkt[2] = { 0x00, cmd }; // 0x00 = control byte for "command"
    i2c_write_blocking(SSD1306_I2C_INST, ssd1306_i2c_address, pkt, 2, false);
}

void ssd1306_send_command_list(uint8_t *cmds, int number) {
    for (int i = 0; i < number; i++) ssd1306_send_command(cmds[i]);
}

void ssd1306_send_buffer(uint8_t data[], int len) {
    // Send in small chunks: [0x40, <up to N data bytes>]
    const int CHUNK = 16;
    uint8_t pkt[1 + CHUNK];
    pkt[0] = 0x40; // 0x40 = control byte for "data"

    int sent = 0;
    while (sent < len) {
        int n = (len - sent > CHUNK) ? CHUNK : (len - sent);
        memcpy(&pkt[1], &data[sent], n);
        i2c_write_blocking(SSD1306_I2C_INST, ssd1306_i2c_address, pkt, 1 + n, false);
        sent += n;
    }
}

// Adquire os pixels para um caractere (de acordo com ssd1306_font.h)
inline int ssd1306_get_font(uint8_t character)
{
  if (character >= 'A' && character <= 'Z') {
    return character - 'A' + 1;
  }
  else if (character >= '0' && character <= '9') {
    return character - '0' + 27;
  }
  else
    return 0;
}

// Desenha um único caractere no display
void ssd1306_draw_char(uint8_t *ssd, int16_t x, int16_t y, uint8_t character) {
    if (x > ssd1306_width - 8 || y > ssd1306_height - 8) {
        return;
    }

    y = y / 8;

    character = toupper(character);
    int idx = ssd1306_get_font(character);
    int fb_idx = y * 128 + x;

    for (int i = 0; i < 8; i++) {
        ssd[fb_idx++] = font[idx * 8 + i];
    }
}

// Desenha uma string, chamando a função de desenhar caractere várias vezes
void ssd1306_draw_string(uint8_t *ssd, int16_t x, int16_t y, char *string) {
    if (x > ssd1306_width - 8 || y > ssd1306_height - 8) {
        return;
    }

    while (*string) {
        ssd1306_draw_char(ssd, x, y, *string++);
        x += 8;
    }
}

// Comando de configuração com base na estrutura ssd1306_t
void ssd1306_command(ssd1306_t *ssd, uint8_t command) {
  ssd->port_buffer[1] = command;
  i2c_write_blocking(
	ssd->i2c_port, ssd->address, ssd->port_buffer, 2, false );
}

// ---------- Address window helper ----------
void calculate_render_area_buffer_length(struct render_area *area) {
    int cols  = (int)area->end_column - (int)area->start_column + 1;
    int pages = (int)area->end_page   - (int)area->start_page   + 1;
    area->buffer_length = cols * pages;
}

// ---------- Init & render ----------
void ssd1306_init(void) {
    // I2C pins & init
    i2c_init(SSD1306_I2C_INST, ssd1306_i2c_clock * 1000);
    gpio_set_function(SSD1306_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(SSD1306_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(SSD1306_PIN_SDA);
    gpio_pull_up(SSD1306_PIN_SCL);
    sleep_ms(10);

    // SSD1306 init sequence (internal charge pump, 128x64)
    uint8_t init_cmds[] = {
        0xAE,               // display off
        0x20, 0x00,         // memory mode: horizontal
        0xB0,               // page start (0)
        0xC8,               // COM scan dec
        0x00,               // low column start
        0x10,               // high column start
        0x40,               // start line = 0
        0x81, 0x7F,         // contrast
        0xA1,               // segment remap
        0xA6,               // normal display
        0xA8, 0x3F,         // multiplex (64-1)
        0xD3, 0x00,         // display offset = 0
        0xD5, 0x80,         // clock divide
        0xD9, 0xF1,         // pre-charge
        0xDA, 0x12,         // COM pins config for 128x64
        0xDB, 0x40,         // VCOM detect
        0x8D, 0x14,         // charge pump on
        0xA4,               // display follows RAM
        0xAF                // display on
    };
    ssd1306_send_command_list(init_cmds, (int)sizeof(init_cmds));
    sleep_ms(10);
}

void render_on_display(uint8_t *buf, struct render_area *area) {
    // Column address
    ssd1306_send_command(0x21); // SET COLUMN ADDRESS
    ssd1306_send_command(area->start_column);
    ssd1306_send_command(area->end_column);

    // Page address
    ssd1306_send_command(0x22); // SET PAGE ADDRESS
    ssd1306_send_command(area->start_page);
    ssd1306_send_command(area->end_page);

    // Data
    ssd1306_send_buffer(buf, area->buffer_length);
}

// ---------- Optional: start/stop a simple horizontal scroll ----------
void ssd1306_scroll(bool enable) {
    if (!enable) {
        ssd1306_send_command(0x2E); // Deactivate scroll
        return;
    }
    uint8_t cmds[] = {
        0x2E,       // Deactivate scroll (safety)
        0x26,       // Right horizontal scroll
        0x00,       // dummy
        0x00,       // start page
        0x07,       // frame interval
        0x07,       // end page
        0x00, 0xFF, // dummy
        0x2F        // Activate scroll
    };
    ssd1306_send_command_list(cmds, (int)sizeof(cmds));
}

// ---------- Pixel helpers ----------
void ssd1306_set_pixel(uint8_t *buf, int x, int y, bool on) {
    if (x < 0 || x >= ssd1306_width || y < 0 || y >= ssd1306_height) return;
    int page  = y / 8;
    int bit   = y % 8;
    int index = x + (page * ssd1306_width);
    if (on) buf[index] |=  (1u << bit);
    else    buf[index] &= ~(1u << bit);
}

void ssd1306_clear(uint8_t *buf) {
    memset(buf, 0x00, ssd1306_buffer_length);
}

void ssd1306_draw_points(uint8_t *buf, int points[][2], int n_points) {
    for (int i = 0; i < n_points; i++) {
        ssd1306_set_pixel(buf, points[i][0], points[i][1], true);
    }
}

// Optional: Bresenham line
void ssd1306_draw_line(uint8_t *buf, int x0, int y0, int x1, int y1, bool on) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        ssd1306_set_pixel(buf, x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
