#ifndef SSD1306_H
#define SSD1306_H

#include <stdint.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

// ---------------- Display configuration ----------------
#define ssd1306_width   128
#define ssd1306_height   64
#define ssd1306_page_height 8
#define ssd1306_n_pages (ssd1306_height / ssd1306_page_height)
#define ssd1306_buffer_length (ssd1306_width * ssd1306_n_pages)

// ---------------- I2C configuration ----------------
#ifndef SSD1306_I2C_INST
#define SSD1306_I2C_INST i2c1
#endif

#ifndef SSD1306_PIN_SDA
#define SSD1306_PIN_SDA 14
#endif

#ifndef SSD1306_PIN_SCL
#define SSD1306_PIN_SCL 15
#endif

#define ssd1306_i2c_address 0x3C
#define ssd1306_i2c_clock   400 // kHz

// ---------------- Global framebuffer ----------------
extern uint8_t ssd1306_buffer[ssd1306_buffer_length];

// ---------------- Render area struct ----------------
struct render_area {
    uint8_t start_column;
    uint8_t end_column;
    uint8_t start_page;
    uint8_t end_page;
    int buffer_length;
};

typedef struct {
  uint8_t width, height, pages, address;
  i2c_inst_t * i2c_port;
  bool external_vcc;
  uint8_t *ram_buffer;
  size_t bufsize;
  uint8_t port_buffer[2];
} ssd1306_t;

// ---------------- API ----------------
void ssd1306_init(void);
void calculate_render_area_buffer_length(struct render_area *area);
void render_on_display(uint8_t *buf, struct render_area *area);
void ssd1306_scroll(bool enable);

void ssd1306_send_command(uint8_t cmd);
void ssd1306_send_command_list(uint8_t *cmds, int number);
void ssd1306_send_buffer(uint8_t data[], int len);

void ssd1306_set_pixel(uint8_t *buf, int x, int y, bool on);
void ssd1306_clear(uint8_t *buf);
void ssd1306_draw_points(uint8_t *buf, int points[][2], int n_points);
void ssd1306_draw_line(uint8_t *buf, int x0, int y0, int x1, int y1, bool on);
void ssd1306_draw_char(uint8_t *ssd, int16_t x, int16_t y, uint8_t character);
void ssd1306_draw_string(uint8_t *ssd, int16_t x, int16_t y, char *string);
void ssd1306_command(ssd1306_t *ssd, uint8_t command);

#endif // SSD1306_H
