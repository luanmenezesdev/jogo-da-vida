#include "ssd1306.h"

int main()
{
    stdio_init_all();
    ssd1306_init();

    // clear buffer
    ssd1306_clear(ssd1306_buffer);

    // some test pixels
    int dots[5][2] = {
        {10, 10}, {20, 20}, {30, 30}, {40, 40}, {50, 10}};

    // draw list of points
    ssd1306_draw_points(ssd1306_buffer, dots, 5);

    // render to screen
    struct render_area full_area = {
        .start_column = 0,
        .end_column = ssd1306_width - 1,
        .start_page = 0,
        .end_page = ssd1306_n_pages - 1,
        .buffer_length = ssd1306_buffer_length};

    render_on_display(ssd1306_buffer, &full_area);

    while (true)
    {
        tight_loop_contents();

        // Reduce CPU usage
        sleep_ms(10);
    }
}
