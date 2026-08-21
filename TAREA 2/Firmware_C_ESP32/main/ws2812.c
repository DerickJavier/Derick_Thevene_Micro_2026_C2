#include "ws2812.h"

#include "driver/rmt_tx.h"

#define WS2812_RESOLUTION_HZ 10000000

static rmt_channel_handle_t s_chan;
static rmt_encoder_handle_t s_enc;

esp_err_t ws2812_init(gpio_num_t gpio)
{
    rmt_tx_channel_config_t ch_cfg = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = WS2812_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };

    esp_err_t err = rmt_new_tx_channel(&ch_cfg, &s_chan);
    if (err != ESP_OK) {
        return err;
    }

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = { .duration0 = 3, .level0 = 1, .duration1 = 9, .level1 = 0 },
        .bit1 = { .duration0 = 6, .level0 = 1, .duration1 = 6, .level1 = 0 },
        .flags.msb_first = 1,
    };

    err = rmt_new_bytes_encoder(&enc_cfg, &s_enc);
    if (err != ESP_OK) {
        return err;
    }

    return rmt_enable(s_chan);
}

esp_err_t ws2812_write(rgb_t color)
{
    uint8_t grb[3] = { color.g, color.r, color.b };

    rmt_transmit_config_t tx_cfg = {
        .loop_count = 0,
    };

    esp_err_t err = rmt_transmit(s_chan, s_enc, grb, sizeof(grb), &tx_cfg);
    if (err != ESP_OK) {
        return err;
    }

    return rmt_tx_wait_all_done(s_chan, 100);
}
