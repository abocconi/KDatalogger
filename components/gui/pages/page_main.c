#include "pages.h"

#include <stdint.h>

#include "esp_log.h"

#include "acquisition_service.h"
#include "data_model.h"
#include "kdl_channels.h"
#include "kdl_text.h"
#include "kdl_theme.h"
#include "kdl_widgets.h"
#include "sensor_config.h"

/*
 * Main page geometry, derived from the content box the page manager hands
 * over (394 x 294) rather than from percentages, so every widget lands on a
 * known pixel and a value change can never reflow the page.
 *
 *   inner box    = 394 x 294 minus PAD on each side = 382 x 282
 *   top band     = left column (186) + GAP (6) + tile column (190) = 382
 *   left column  = engine speed tile (52) + GAP (6) + cylinder bank (163) = 221
 *   tile column  = 4 tiles of 52 with three 4 px gaps = 220 of 221
 *   height       = top band (221) + ROW_GAP (5) + analog row (56) = 282
 *   analog row   = 5 cells of 73 with four 4 px gaps = 381
 *
 * Engine speed tops the left column, the first place the eye lands. Below
 * it, thermocouples 1-4 (cylinder exhaust) go to the bank, which puts them
 * on one scale side by side; 5-8 (intercooler, oil, coolant) get a tile
 * each. The pressures fill the bottom row.
 */
#define PAGE_MAIN_PAD            6
#define PAGE_MAIN_GAP            6
#define PAGE_MAIN_ROW_GAP        5
#define PAGE_MAIN_INNER_W        382
#define PAGE_MAIN_INNER_H        282
#define PAGE_MAIN_TOP_H          221
#define PAGE_MAIN_BANK_W         186
#define PAGE_MAIN_RPM_H          52
#define PAGE_MAIN_BANK_Y         (PAGE_MAIN_RPM_H + PAGE_MAIN_GAP)
#define PAGE_MAIN_BANK_H         (PAGE_MAIN_TOP_H - PAGE_MAIN_BANK_Y)
#define PAGE_MAIN_TILE_W         190
#define PAGE_MAIN_TILE_H         52
#define PAGE_MAIN_TILE_GAP       4
#define PAGE_MAIN_TILE_COUNT     4U
#define PAGE_MAIN_ANALOG_H       56
#define PAGE_MAIN_CELL_W         73
#define PAGE_MAIN_CELL_GAP       4

_Static_assert(PAGE_MAIN_BANK_W + PAGE_MAIN_GAP + PAGE_MAIN_TILE_W == PAGE_MAIN_INNER_W,
               "top band does not fill the content width exactly");
_Static_assert(PAGE_MAIN_RPM_H == PAGE_MAIN_TILE_H,
               "engine speed tile uses the fluid tile metrics");
_Static_assert(PAGE_MAIN_TILE_COUNT * PAGE_MAIN_TILE_H
                   + (PAGE_MAIN_TILE_COUNT - 1U) * PAGE_MAIN_TILE_GAP <= PAGE_MAIN_TOP_H,
               "tile column overflows the top band");
_Static_assert(PAGE_MAIN_TOP_H + PAGE_MAIN_ROW_GAP + PAGE_MAIN_ANALOG_H == PAGE_MAIN_INNER_H,
               "main page bands do not fill the content box exactly");
_Static_assert(KDL_THERMOCOUPLE_DISPLAY_COUNT == KDL_CYL_BANK_COUNT + PAGE_MAIN_TILE_COUNT,
               "thermocouples are laid out as one 4-cylinder bank plus 4 tiles");
_Static_assert(KDL_ANALOG_DISPLAY_COUNT == 5,
               "analog row is laid out as a single row of 5 cells");
_Static_assert(5 * PAGE_MAIN_CELL_W + 4 * PAGE_MAIN_CELL_GAP <= PAGE_MAIN_INNER_W,
               "five analog cells overflow the content width");

static const char *TAG = "page_main";

static kdl_rpm_tile_t s_rpm;
static kdl_cyl_bank_t s_bank;
static kdl_fluid_tile_t s_tiles[PAGE_MAIN_TILE_COUNT];
static kdl_analog_cell_t s_cells[KDL_ANALOG_DISPLAY_COUNT];
static uint64_t s_last_sample_uptime_ms = UINT64_MAX;

static void on_tick(void)
{
    kdl_sensor_sample_t sample;
    if (acquisition_service_get_latest_sample(&sample) != ESP_OK)
    {
        return;
    }

    /* The GUI ticks faster than the acquisition period, so most ticks carry
     * the sample already on screen. Redrawing it would repaint the whole
     * page for nothing. */
    if (sample.uptime_ms == s_last_sample_uptime_ms)
    {
        return;
    }
    s_last_sample_uptime_ms = sample.uptime_ms;

    kdl_sensor_extremes_t extremes;
    const bool have_extremes = acquisition_service_get_extremes(&extremes) == ESP_OK;

    const kdl_sensor_extremes_t *extremes_or_null = have_extremes ? &extremes : NULL;
    kdl_rpm_tile_update(&s_rpm, &sample, extremes_or_null);
    kdl_cyl_bank_update(&s_bank, &sample, extremes_or_null);
    for (uint8_t index = 0; index < PAGE_MAIN_TILE_COUNT; ++index)
    {
        kdl_fluid_tile_update(&s_tiles[index], &sample, extremes_or_null);
    }
    for (uint8_t index = 0; index < KDL_ANALOG_DISPLAY_COUNT; ++index)
    {
        kdl_analog_cell_update(&s_cells[index], &sample);
    }
}

static void on_show(lv_obj_t *content)
{
    lv_obj_set_style_pad_all(content, PAGE_MAIN_PAD, 0);

    s_last_sample_uptime_ms = UINT64_MAX;

    /* A widget that fails to build keeps a NULL root, which its _update
     * treats as absent: the rest of the page still works. */
    /* The configuration only changes on the way back from USB mode, which
     * rebuilds this page: the full scale can be read once here. */
    sensor_config_t config;
    sensor_config_get(&config);
    esp_err_t err = kdl_rpm_tile_create(&s_rpm, content, (float)config.tach.rpm_max,
                                        PAGE_MAIN_BANK_W, PAGE_MAIN_RPM_H);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "engine speed tile: %s", esp_err_to_name(err));
    }

    err = kdl_cyl_bank_create(&s_bank, content, 0, PAGE_MAIN_BANK_W, PAGE_MAIN_BANK_H);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "cylinder bank: %s", esp_err_to_name(err));
    }
    else
    {
        lv_obj_set_pos(s_bank.root, 0, PAGE_MAIN_BANK_Y);
    }

    for (uint8_t index = 0; index < PAGE_MAIN_TILE_COUNT; ++index)
    {
        err = kdl_fluid_tile_create(&s_tiles[index], content,
                                    (uint8_t)(KDL_CYL_BANK_COUNT + index),
                                    PAGE_MAIN_TILE_W, PAGE_MAIN_TILE_H);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "tile %u: %s", (unsigned)index, esp_err_to_name(err));
            continue;
        }
        lv_obj_set_pos(s_tiles[index].root, PAGE_MAIN_BANK_W + PAGE_MAIN_GAP,
                       (int32_t)index * (PAGE_MAIN_TILE_H + PAGE_MAIN_TILE_GAP));
    }

    for (uint8_t index = 0; index < KDL_ANALOG_DISPLAY_COUNT; ++index)
    {
        err = kdl_analog_cell_create(&s_cells[index], content, kdl_channels_analog(index),
                                     PAGE_MAIN_CELL_W, PAGE_MAIN_ANALOG_H);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "analog cell %u: %s", (unsigned)index, esp_err_to_name(err));
            continue;
        }
        lv_obj_set_pos(s_cells[index].root,
                       (int32_t)index * (PAGE_MAIN_CELL_W + PAGE_MAIN_CELL_GAP),
                       PAGE_MAIN_TOP_H + PAGE_MAIN_ROW_GAP);
    }

    /* Paint the latest sample now rather than on the next GUI tick, up to
     * GUI_TICK_PERIOD_MS away: until then the widgets would show their
     * skeleton state, and a tile in warning, alarm or fault would flash
     * white before taking its colour. */
    on_tick();
}

static void on_hide(void)
{
    /* The page manager destroys the content subtree; drop the dangling
     * handles so a late update cannot touch freed objects. */
    s_rpm.root = NULL;
    s_bank.root = NULL;
    for (uint8_t index = 0; index < PAGE_MAIN_TILE_COUNT; ++index)
    {
        s_tiles[index].root = NULL;
    }
    for (uint8_t index = 0; index < KDL_ANALOG_DISPLAY_COUNT; ++index)
    {
        s_cells[index].root = NULL;
    }
    s_last_sample_uptime_ms = UINT64_MAX;
}

static void on_button(uint8_t button_index)
{
    switch (button_index)
    {
    case 2:
        page_manager_switch_to(&page_graph);
        break;
    case 3:
        page_manager_switch_to(&page_settings);
        break;
    case 4:
        page_usb_enter();
        break;
    default:
        break;
    }
}

const gui_page_t page_main = {
    .name = "main",
    .title = KDL_TXT_MAIN_TITLE,
    .button_labels = {"", "", KDL_TXT_KEY_GRAPH, KDL_TXT_KEY_SETTINGS, KDL_TXT_KEY_USB},
    .on_show = on_show,
    .on_hide = on_hide,
    .on_tick = on_tick,
    .on_button = on_button,
};
