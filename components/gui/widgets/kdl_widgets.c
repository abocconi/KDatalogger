#include "kdl_widgets.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "kdl_text.h"
#include "kdl_theme.h"

static const char *TAG = "kdl_widgets";

/* Border of kdl_style_tile, which every widget root below carries. */
#define KDL_TILE_BORDER 1

#define KDL_WIDGET_VALUE_TEXT_LEN    8  /* "-1350" plus slack       */
#define KDL_WIDGET_EXTREMES_TEXT_LEN 32 /* "min -270 · max 1350"    */

/* Cylinder bank. Vertical positions inside a column are measured from the
 * column top; the column itself starts below the header. */
#define KDL_BANK_PAD_X          6
#define KDL_BANK_PAD_TOP        5
#define KDL_BANK_PAD_BOTTOM     6
#define KDL_BANK_HEADER_H       16
#define KDL_BANK_HEADER_BASE    12  /* Header text baseline, px from the top */
#define KDL_BANK_HEADER_GAP     4
#define KDL_BANK_AXIS_W         20
#define KDL_BANK_COL_GAP        4
#define KDL_BANK_SPREAD_W       30
#define KDL_BANK_DELTA_W        12
#define KDL_BANK_AXIS_MAX_LABELS 7
#define KDL_COL_VALUE_Y         3
#define KDL_COL_TRACK_Y         25
#define KDL_COL_TRACK_W         14
#define KDL_COL_LABEL_GAP       4
#define KDL_COL_PEAK_W          22
#define KDL_COL_PEAK_H          2
#define KDL_COL_MIN_TRACK_H     40

/* Thermocouple tile. */
#define KDL_TILE_PAD_X          7
#define KDL_TILE_PAD_TOP        4
#define KDL_TILE_PAD_BOTTOM     5
#define KDL_TILE_VALUE_Y        3
#define KDL_TILE_VALUE_W        60  /* Four tabular 32 px digits: 58.8 px */
#define KDL_TILE_UNIT_W         14
#define KDL_TILE_UNIT_GAP       2
#define KDL_TILE_TEXT_GAP       4
#define KDL_TILE_TRACK_H        5
#define KDL_TILE_MIN_TEXT_W     60

/* Analog cell. */
#define KDL_CELL_PAD_X          6
#define KDL_CELL_PAD_TOP        4
#define KDL_CELL_PAD_BOTTOM     4
#define KDL_CELL_VALUE_BOTTOM   4   /* Digits' baseline, px above the inner bottom */
#define KDL_CELL_UNIT_W         12
#define KDL_CELL_UNIT_GAP       2
#define KDL_CELL_MIN_VALUE_W    40  /* "3.30" in the 26 px num face */

typedef struct {
    kdl_level_t level;
    int32_t value; /**< Rounded reading; meaningless when level is FAULT */
    char text[KDL_WIDGET_VALUE_TEXT_LEN];
} kdl_reading_t;

void kdl_widget_set_text(lv_obj_t *label, const char *text)
{
    if (label == NULL || text == NULL)
    {
        return;
    }

    if (strcmp(lv_label_get_text(label), text) != 0)
    {
        lv_label_set_text(label, text);
    }
}

/* -- Building blocks ------------------------------------------------------- */

/** Height from a label's top edge to its baseline. The generated faces have
 *  tight line boxes, so this is what aligns a caption with a reading. */
static int32_t kdl_font_ascent(const lv_font_t *font)
{
    return lv_font_get_line_height(font) - font->base_line;
}

static lv_obj_t *kdl_widget_container(lv_obj_t *parent, int32_t x, int32_t y,
                                      int32_t width, int32_t height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_add_style(obj, &kdl_style_panel, 0);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *kdl_widget_box(lv_obj_t *parent, int32_t x, int32_t y,
                                int32_t width, int32_t height, lv_color_t color)
{
    lv_obj_t *obj = kdl_widget_container(parent, x, y, width, height);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    return obj;
}

static lv_obj_t *kdl_widget_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                                  int32_t x, int32_t y, int32_t width, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, lv_font_get_line_height(font));
    lv_label_set_text(label, "");
    return label;
}

/** Root of a bordered tile, with its padding. */
static lv_obj_t *kdl_widget_tile_root(lv_obj_t *parent, int32_t width, int32_t height,
                                      int32_t pad_x, int32_t pad_top, int32_t pad_bottom)
{
    lv_obj_t *root = kdl_widget_container(parent, 0, 0, width, height);
    lv_obj_add_style(root, &kdl_style_tile, 0);
    lv_obj_set_style_pad_left(root, pad_x, 0);
    lv_obj_set_style_pad_right(root, pad_x, 0);
    lv_obj_set_style_pad_top(root, pad_top, 0);
    lv_obj_set_style_pad_bottom(root, pad_bottom, 0);
    return root;
}

/* -- Readings -------------------------------------------------------------- */

static bool kdl_desc_is_consistent(const kdl_thermocouple_desc_t *desc)
{
    return desc != NULL && desc->channel != NULL && desc->full_scale > 0.0f
           && desc->warn_threshold <= desc->alarm_threshold
           && desc->alarm_threshold <= desc->full_scale;
}

/** @p value mapped onto @p span pixels of a 0..full_scale bar, clamped. */
static int32_t kdl_scale_px(float value, float full_scale, int32_t span)
{
    if (value <= 0.0f)
    {
        return 0;
    }
    const float px = value / full_scale * (float)span;
    return (px >= (float)span) ? span : (int32_t)lroundf(px);
}

/** Fault text for a channel the MAX31855 could not read, or NULL when the
 *  reading is usable. Open-circuit is shown as an unplugged probe rather
 *  than an error, since that is what it means in the field. */
static const char *kdl_probe_fault_text(const kdl_sensor_sample_t *sample, uint8_t channel)
{
    const uint16_t bit = (uint16_t)(1U << channel);

    if ((sample->thermocouple_valid_mask & bit) != 0U)
    {
        return NULL;
    }
    if ((sample->thermocouple_oc_mask & bit) != 0U)
    {
        return KDL_TXT_VALUE_OPEN;
    }
    if ((sample->thermocouple_scg_mask & bit) != 0U)
    {
        return "SCG";
    }
    if ((sample->thermocouple_scv_mask & bit) != 0U)
    {
        return "SCV";
    }
    return "ERR";
}

static void kdl_widget_read(const kdl_thermocouple_desc_t *desc, const kdl_sensor_sample_t *sample,
                            uint8_t channel, kdl_reading_t *out)
{
    const char *fault = kdl_probe_fault_text(sample, channel);
    if (fault != NULL)
    {
        out->level = KDL_LEVEL_FAULT;
        out->value = 0;
        snprintf(out->text, sizeof(out->text), "%s", fault);
        return;
    }

    const float value = sample->thermocouples_c[channel];
    out->value = (int32_t)lroundf(value);
    snprintf(out->text, sizeof(out->text), "%d", (int)out->value);

    if (value >= desc->alarm_threshold)
    {
        out->level = KDL_LEVEL_ALARM;
    }
    else if (value >= desc->warn_threshold)
    {
        out->level = KDL_LEVEL_WARN;
    }
    else
    {
        out->level = KDL_LEVEL_OK;
    }
}

static lv_color_t kdl_level_ink(kdl_level_t level)
{
    switch (level)
    {
    case KDL_LEVEL_WARN:
        return KDL_COLOR_WARM;
    case KDL_LEVEL_ALARM:
        return KDL_COLOR_HOT;
    case KDL_LEVEL_FAULT:
        return KDL_COLOR_INK_FAINT;
    default:
        return KDL_COLOR_INK;
    }
}

static bool kdl_extremes_valid(const kdl_sensor_extremes_t *extremes, uint8_t channel)
{
    return extremes != NULL && (extremes->valid_mask & (uint16_t)(1U << channel)) != 0U;
}

/* -- Cylinder bank --------------------------------------------------------- */

/**
 * Label the shared scale: full scale, zero and both thresholds first, then
 * the halves and quarters wherever they do not collide with a label already
 * placed. Driven by the descriptor, so re-tuned thresholds relabel the axis.
 */
static void kdl_cyl_bank_build_axis(lv_obj_t *root, const kdl_thermocouple_desc_t *desc,
                                    int32_t track_top, int32_t track_h)
{
    const lv_font_t *font = KDL_FONT_MICRO;
    const int32_t line_h = lv_font_get_line_height(font);
    const float fs = desc->full_scale;
    const float candidates[KDL_BANK_AXIS_MAX_LABELS] = {
        fs, 0.0f, desc->alarm_threshold, desc->warn_threshold, fs * 0.5f, fs * 0.25f, fs * 0.75f,
    };
    int32_t placed_y[KDL_BANK_AXIS_MAX_LABELS];
    uint8_t placed = 0;

    for (uint8_t index = 0; index < KDL_BANK_AXIS_MAX_LABELS; ++index)
    {
        const float value = candidates[index];
        const int32_t y = track_top + track_h - kdl_scale_px(value, fs, track_h) - line_h / 2;

        bool collides = false;
        for (uint8_t other = 0; other < placed; ++other)
        {
            if (LV_ABS(y - placed_y[other]) < line_h)
            {
                collides = true;
                break;
            }
        }
        if (collides)
        {
            continue;
        }
        placed_y[placed++] = y;

        lv_color_t color = KDL_COLOR_INK_MUTED;
        if (index == 2)
        {
            color = KDL_COLOR_HOT;
        }
        else if (index == 3)
        {
            color = KDL_COLOR_WARM;
        }

        char text[KDL_WIDGET_VALUE_TEXT_LEN];
        snprintf(text, sizeof(text), "%d", (int)lroundf(value));
        lv_obj_t *label = kdl_widget_label(root, font, color, 0, y, KDL_BANK_AXIS_W,
                                           LV_TEXT_ALIGN_RIGHT);
        lv_label_set_text(label, text);
    }
}

static void kdl_cyl_column_create(kdl_cyl_column_t *col, lv_obj_t *parent,
                                  const kdl_thermocouple_desc_t *desc, uint8_t channel,
                                  int32_t x, int32_t y, int32_t width, int32_t height,
                                  int32_t track_h)
{
    col->desc = desc;
    col->channel = channel;
    col->level = KDL_LEVEL_UNSET;
    col->fill_h = -1;
    col->peak_y = -1;

    col->root = kdl_widget_container(parent, x, y, width, height);
    lv_obj_set_style_radius(col->root, 3, 0);
    lv_obj_set_style_bg_opa(col->root, LV_OPA_TRANSP, 0);

    col->value = kdl_widget_label(col->root, KDL_FONT_NUM_M, KDL_COLOR_INK_FAINT, 0,
                                  KDL_COL_VALUE_Y, width, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(col->value, "---");

    /* Zone bands are drawn once, top down: alarm band, warning band, and
     * the track colour below the warning threshold. */
    lv_obj_t *track = kdl_widget_box(col->root, (width - KDL_COL_TRACK_W) / 2, KDL_COL_TRACK_Y,
                                     KDL_COL_TRACK_W, track_h, KDL_COLOR_ZONE_OK);
    const int32_t warn_px = kdl_scale_px(desc->warn_threshold, desc->full_scale, track_h);
    const int32_t alarm_px = kdl_scale_px(desc->alarm_threshold, desc->full_scale, track_h);
    if (track_h - alarm_px > 0)
    {
        kdl_widget_box(track, 0, 0, KDL_COL_TRACK_W, track_h - alarm_px, KDL_COLOR_ZONE_HOT);
    }
    if (alarm_px - warn_px > 0)
    {
        kdl_widget_box(track, 0, track_h - alarm_px, KDL_COL_TRACK_W, alarm_px - warn_px,
                       KDL_COLOR_ZONE_WARM);
    }
    col->fill = kdl_widget_box(track, 0, track_h, KDL_COL_TRACK_W, 0, KDL_COLOR_INK);

    col->peak = kdl_widget_box(col->root, (width - KDL_COL_PEAK_W) / 2, KDL_COL_TRACK_Y,
                               KDL_COL_PEAK_W, KDL_COL_PEAK_H, KDL_COLOR_INK);
    lv_obj_add_flag(col->peak, LV_OBJ_FLAG_HIDDEN);

    const int32_t label_h = lv_font_get_line_height(KDL_FONT_BODY);
    lv_obj_t *name = kdl_widget_label(col->root, KDL_FONT_BODY, KDL_COLOR_INK_DIM, 0,
                                      height - label_h, width, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(name, desc->channel->name);
}

esp_err_t kdl_cyl_bank_create(kdl_cyl_bank_t *bank, lv_obj_t *parent, uint8_t first_channel,
                              int32_t width, int32_t height)
{
    if (bank == NULL || parent == NULL
        || (uint32_t)first_channel + KDL_CYL_BANK_COUNT > KDL_THERMOCOUPLE_DISPLAY_COUNT)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const kdl_thermocouple_desc_t *descs[KDL_CYL_BANK_COUNT];
    for (uint8_t index = 0; index < KDL_CYL_BANK_COUNT; ++index)
    {
        descs[index] = kdl_channels_thermocouple((uint8_t)(first_channel + index));
        if (!kdl_desc_is_consistent(descs[index]))
        {
            return ESP_ERR_INVALID_ARG;
        }
        if (descs[index]->full_scale != descs[0]->full_scale
            || descs[index]->warn_threshold != descs[0]->warn_threshold
            || descs[index]->alarm_threshold != descs[0]->alarm_threshold)
        {
            /* Still usable -- each column draws its own zones -- but the axis
             * labels describe the first channel only. */
            ESP_LOGW(TAG, "%s scale differs from %s: bank axis shows the latter",
                     descs[index]->channel->id, descs[0]->channel->id);
        }
    }

    const int32_t inner_w = width - 2 * KDL_TILE_BORDER - 2 * KDL_BANK_PAD_X;
    const int32_t inner_h = height - 2 * KDL_TILE_BORDER - KDL_BANK_PAD_TOP - KDL_BANK_PAD_BOTTOM;
    const int32_t columns = (int32_t)KDL_CYL_BANK_COUNT;
    const int32_t col_w = (inner_w - KDL_BANK_AXIS_W - columns * KDL_BANK_COL_GAP) / columns;
    const int32_t col_y = KDL_BANK_HEADER_H + KDL_BANK_HEADER_GAP;
    const int32_t col_h = inner_h - col_y;
    const int32_t track_h = col_h - KDL_COL_TRACK_Y - KDL_COL_LABEL_GAP
                            - lv_font_get_line_height(KDL_FONT_BODY);
    if (col_w < KDL_COL_PEAK_W || track_h < KDL_COL_MIN_TRACK_H)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(bank, 0, sizeof(*bank));
    bank->track_h = track_h;
    bank->root = kdl_widget_tile_root(parent, width, height, KDL_BANK_PAD_X, KDL_BANK_PAD_TOP,
                                      KDL_BANK_PAD_BOTTOM);

    lv_obj_t *heading = kdl_widget_label(bank->root, KDL_FONT_MICRO, KDL_COLOR_INK_DIM, 0,
                                         KDL_BANK_HEADER_BASE - kdl_font_ascent(KDL_FONT_MICRO),
                                         inner_w / 2, LV_TEXT_ALIGN_LEFT);
    lv_obj_set_style_text_letter_space(heading, 1, 0);
    lv_label_set_text(heading, KDL_TXT_MAIN_EXHAUST);

    lv_obj_t *delta = kdl_widget_label(bank->root, KDL_FONT_MICRO, KDL_COLOR_INK_MUTED,
                                       inner_w - KDL_BANK_SPREAD_W - KDL_BANK_DELTA_W,
                                       KDL_BANK_HEADER_BASE - kdl_font_ascent(KDL_FONT_MICRO),
                                       KDL_BANK_DELTA_W, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(delta, KDL_TXT_MAIN_SPREAD);

    bank->spread = kdl_widget_label(bank->root, KDL_FONT_NUM_S, KDL_COLOR_INK,
                                    inner_w - KDL_BANK_SPREAD_W,
                                    KDL_BANK_HEADER_BASE - kdl_font_ascent(KDL_FONT_NUM_S),
                                    KDL_BANK_SPREAD_W, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text(bank->spread, "--");

    kdl_cyl_bank_build_axis(bank->root, descs[0], col_y + KDL_COL_TRACK_Y, track_h);

    for (uint8_t index = 0; index < KDL_CYL_BANK_COUNT; ++index)
    {
        const int32_t x = KDL_BANK_AXIS_W + KDL_BANK_COL_GAP
                          + (int32_t)index * (col_w + KDL_BANK_COL_GAP);
        kdl_cyl_column_create(&bank->columns[index], bank->root, descs[index],
                              (uint8_t)(first_channel + index), x, col_y, col_w, col_h, track_h);
    }

    return ESP_OK;
}

static void kdl_cyl_column_update(kdl_cyl_column_t *col, int32_t track_h,
                                  const kdl_reading_t *reading,
                                  const kdl_sensor_extremes_t *extremes)
{
    kdl_widget_set_text(col->value, reading->text);

    if (reading->level != col->level)
    {
        col->level = reading->level;
        const lv_color_t ink = kdl_level_ink(reading->level);
        lv_obj_set_style_text_color(col->value, ink, 0);
        lv_obj_set_style_bg_color(col->fill, ink, 0);

        if (reading->level == KDL_LEVEL_ALARM || reading->level == KDL_LEVEL_WARN)
        {
            lv_obj_set_style_bg_color(col->root, reading->level == KDL_LEVEL_ALARM
                                                 ? KDL_COLOR_TINT_HOT : KDL_COLOR_TINT_WARM, 0);
            lv_obj_set_style_bg_opa(col->root, LV_OPA_COVER, 0);
        }
        else
        {
            lv_obj_set_style_bg_opa(col->root, LV_OPA_TRANSP, 0);
        }
    }

    const float full_scale = col->desc->full_scale;
    const int32_t fill_h = (reading->level == KDL_LEVEL_FAULT)
                           ? 0 : kdl_scale_px((float)reading->value, full_scale, track_h);
    if (fill_h != col->fill_h)
    {
        col->fill_h = fill_h;
        lv_obj_set_y(col->fill, track_h - fill_h);
        lv_obj_set_height(col->fill, fill_h);
    }

    int32_t peak_y = -1;
    if (kdl_extremes_valid(extremes, col->channel))
    {
        const int32_t px = kdl_scale_px(extremes->thermocouple_max_c[col->channel],
                                        col->desc->full_scale, track_h);
        peak_y = KDL_COL_TRACK_Y + track_h - px - KDL_COL_PEAK_H / 2;
    }
    if (peak_y != col->peak_y)
    {
        if (peak_y < 0)
        {
            lv_obj_add_flag(col->peak, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_set_y(col->peak, peak_y);
            lv_obj_remove_flag(col->peak, LV_OBJ_FLAG_HIDDEN);
        }
        col->peak_y = peak_y;
    }
}

void kdl_cyl_bank_update(kdl_cyl_bank_t *bank, const kdl_sensor_sample_t *sample,
                         const kdl_sensor_extremes_t *extremes)
{
    if (bank == NULL || bank->root == NULL || sample == NULL)
    {
        return;
    }

    int32_t hottest = INT32_MIN;
    int32_t coolest = INT32_MAX;
    uint8_t readable = 0;

    for (uint8_t index = 0; index < KDL_CYL_BANK_COUNT; ++index)
    {
        kdl_cyl_column_t *col = &bank->columns[index];
        kdl_reading_t reading;
        kdl_widget_read(col->desc, sample, col->channel, &reading);
        kdl_cyl_column_update(col, bank->track_h, &reading, extremes);

        if (reading.level != KDL_LEVEL_FAULT)
        {
            hottest = LV_MAX(hottest, reading.value);
            coolest = LV_MIN(coolest, reading.value);
            readable++;
        }
    }

    /* A spread needs two cylinders to compare; with a probe out it is taken
     * over the ones still reading rather than hidden. */
    char text[KDL_WIDGET_VALUE_TEXT_LEN];
    if (readable >= 2U)
    {
        snprintf(text, sizeof(text), "%d", (int)(hottest - coolest));
    }
    else
    {
        snprintf(text, sizeof(text), "--");
    }
    kdl_widget_set_text(bank->spread, text);
}

/* -- Thermocouple tile ----------------------------------------------------- */

esp_err_t kdl_fluid_tile_create(kdl_fluid_tile_t *tile, lv_obj_t *parent, uint8_t channel,
                                int32_t width, int32_t height)
{
    if (tile == NULL || parent == NULL || channel >= KDL_THERMOCOUPLE_DISPLAY_COUNT)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const kdl_thermocouple_desc_t *desc = kdl_channels_thermocouple(channel);
    if (!kdl_desc_is_consistent(desc))
    {
        return ESP_ERR_INVALID_ARG;
    }

    const int32_t inner_w = width - 2 * KDL_TILE_BORDER - 2 * KDL_TILE_PAD_X;
    const int32_t inner_h = height - 2 * KDL_TILE_BORDER - KDL_TILE_PAD_TOP - KDL_TILE_PAD_BOTTOM;
    const int32_t value_x = inner_w - KDL_TILE_UNIT_W - KDL_TILE_UNIT_GAP - KDL_TILE_VALUE_W;
    const int32_t text_w = value_x - KDL_TILE_TEXT_GAP;
    const int32_t name_h = lv_font_get_line_height(KDL_FONT_KEY);
    const int32_t track_y = inner_h - KDL_TILE_TRACK_H;
    if (text_w < KDL_TILE_MIN_TEXT_W
        || KDL_TILE_VALUE_Y + lv_font_get_line_height(KDL_FONT_NUM_XL) > track_y)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(tile, 0, sizeof(*tile));
    tile->desc = desc;
    tile->channel = channel;
    tile->level = KDL_LEVEL_UNSET;
    tile->track_w = inner_w;
    tile->fill_w = -1;

    tile->root = kdl_widget_tile_root(parent, width, height, KDL_TILE_PAD_X, KDL_TILE_PAD_TOP,
                                      KDL_TILE_PAD_BOTTOM);

    lv_obj_t *name = kdl_widget_label(tile->root, KDL_FONT_KEY, KDL_COLOR_INK, 0, 0, text_w,
                                      LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(name, desc->channel->name);

    tile->extremes = kdl_widget_label(tile->root, KDL_FONT_MICRO, KDL_COLOR_INK_MUTED, 0,
                                      name_h + 2, text_w, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(tile->extremes, KDL_TXT_EXTREMES_NONE);

    tile->value = kdl_widget_label(tile->root, KDL_FONT_NUM_XL, KDL_COLOR_INK_FAINT, value_x,
                                   KDL_TILE_VALUE_Y, KDL_TILE_VALUE_W, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text(tile->value, "---");

    /* Unit set as a superscript against the top of the digits. */
    lv_obj_t *unit = kdl_widget_label(tile->root, KDL_FONT_MICRO, KDL_COLOR_INK_MUTED,
                                      inner_w - KDL_TILE_UNIT_W, KDL_TILE_VALUE_Y,
                                      KDL_TILE_UNIT_W, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(unit, desc->channel->unit);

    lv_obj_t *track = kdl_widget_box(tile->root, 0, track_y, inner_w, KDL_TILE_TRACK_H,
                                     KDL_COLOR_ZONE_OK);
    const int32_t warn_px = kdl_scale_px(desc->warn_threshold, desc->full_scale, inner_w);
    const int32_t alarm_px = kdl_scale_px(desc->alarm_threshold, desc->full_scale, inner_w);
    if (alarm_px - warn_px > 0)
    {
        kdl_widget_box(track, warn_px, 0, alarm_px - warn_px, KDL_TILE_TRACK_H,
                       KDL_COLOR_ZONE_WARM);
    }
    if (inner_w - alarm_px > 0)
    {
        kdl_widget_box(track, alarm_px, 0, inner_w - alarm_px, KDL_TILE_TRACK_H,
                       KDL_COLOR_ZONE_HOT);
    }
    tile->fill = kdl_widget_box(track, 0, 0, 0, KDL_TILE_TRACK_H, KDL_COLOR_INK);

    return ESP_OK;
}

void kdl_fluid_tile_update(kdl_fluid_tile_t *tile, const kdl_sensor_sample_t *sample,
                           const kdl_sensor_extremes_t *extremes)
{
    if (tile == NULL || tile->root == NULL || sample == NULL)
    {
        return;
    }

    kdl_reading_t reading;
    kdl_widget_read(tile->desc, sample, tile->channel, &reading);
    kdl_widget_set_text(tile->value, reading.text);

    if (reading.level != tile->level)
    {
        tile->level = reading.level;
        const lv_color_t ink = kdl_level_ink(reading.level);
        lv_obj_set_style_text_color(tile->value, ink, 0);
        lv_obj_set_style_bg_color(tile->fill, ink, 0);

        lv_color_t fill = KDL_COLOR_CARD;
        lv_color_t border = KDL_COLOR_BORDER;
        if (reading.level == KDL_LEVEL_ALARM)
        {
            fill = KDL_COLOR_TINT_HOT;
            border = KDL_COLOR_HOT;
        }
        else if (reading.level == KDL_LEVEL_WARN)
        {
            fill = KDL_COLOR_TINT_WARM;
            border = KDL_COLOR_BORDER_WARM;
        }
        else if (reading.level == KDL_LEVEL_FAULT)
        {
            fill = KDL_COLOR_SURFACE;
        }
        lv_obj_set_style_bg_color(tile->root, fill, 0);
        lv_obj_set_style_border_color(tile->root, border, 0);
    }

    const int32_t fill_w = (reading.level == KDL_LEVEL_FAULT)
                           ? 0 : kdl_scale_px((float)reading.value, tile->desc->full_scale,
                                              tile->track_w);
    if (fill_w != tile->fill_w)
    {
        tile->fill_w = fill_w;
        lv_obj_set_width(tile->fill, fill_w);
    }

    char text[KDL_WIDGET_EXTREMES_TEXT_LEN];
    if ((sample->thermocouple_oc_mask & (uint16_t)(1U << tile->channel)) != 0U)
    {
        snprintf(text, sizeof(text), "%s", KDL_TXT_PROBE_OPEN);
    }
    else if (kdl_extremes_valid(extremes, tile->channel))
    {
        snprintf(text, sizeof(text), KDL_TXT_EXTREMES_FMT,
                 (int)lroundf(extremes->thermocouple_min_c[tile->channel]),
                 (int)lroundf(extremes->thermocouple_max_c[tile->channel]));
    }
    else
    {
        snprintf(text, sizeof(text), "%s", KDL_TXT_EXTREMES_NONE);
    }
    kdl_widget_set_text(tile->extremes, text);
}

/* -- Analog cell ----------------------------------------------------------- */

esp_err_t kdl_analog_cell_create(kdl_analog_cell_t *cell, lv_obj_t *parent,
                                 const kdl_analog_desc_t *desc, int32_t width, int32_t height)
{
    if (cell == NULL || parent == NULL || desc == NULL || desc->channel == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const int32_t inner_w = width - 2 * KDL_TILE_BORDER - 2 * KDL_CELL_PAD_X;
    const int32_t inner_h = height - 2 * KDL_TILE_BORDER - KDL_CELL_PAD_TOP - KDL_CELL_PAD_BOTTOM;
    const int32_t value_w = inner_w - KDL_CELL_UNIT_W - KDL_CELL_UNIT_GAP;
    const int32_t baseline = inner_h - KDL_CELL_VALUE_BOTTOM;
    if (value_w < KDL_CELL_MIN_VALUE_W
        || baseline - kdl_font_ascent(KDL_FONT_NUM_L) < lv_font_get_line_height(KDL_FONT_MICRO))
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(cell, 0, sizeof(*cell));
    cell->desc = desc;
    cell->valid = -1;

    cell->root = kdl_widget_tile_root(parent, width, height, KDL_CELL_PAD_X, KDL_CELL_PAD_TOP,
                                      KDL_CELL_PAD_BOTTOM);

    lv_obj_t *name = kdl_widget_label(cell->root, KDL_FONT_MICRO, KDL_COLOR_INK_MUTED, 0, 0,
                                      inner_w, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(name, desc->channel->name);

    cell->value = kdl_widget_label(cell->root, KDL_FONT_NUM_L, KDL_COLOR_INK_FAINT, 0,
                                   baseline - kdl_font_ascent(KDL_FONT_NUM_L), value_w,
                                   LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(cell->value, "---");

    lv_obj_t *unit = kdl_widget_label(cell->root, KDL_FONT_MICRO, KDL_COLOR_INK_MUTED,
                                      value_w + KDL_CELL_UNIT_GAP,
                                      baseline - kdl_font_ascent(KDL_FONT_MICRO),
                                      KDL_CELL_UNIT_W, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(unit, desc->channel->unit);

    return ESP_OK;
}

void kdl_analog_cell_update(kdl_analog_cell_t *cell, const kdl_sensor_sample_t *sample)
{
    if (cell == NULL || cell->root == NULL || sample == NULL)
    {
        return;
    }

    char text[KDL_WIDGET_VALUE_TEXT_LEN];
    const bool valid = (sample->analog_valid_mask & (1U << cell->desc->input_index)) != 0U;

    if (valid)
    {
        snprintf(text, sizeof(text), "%.*f", (int)cell->desc->decimals,
                 (double)sample->analog_inputs[cell->desc->input_index]);
    }
    else
    {
        snprintf(text, sizeof(text), "---");
    }

    kdl_widget_set_text(cell->value, text);
    if ((int8_t)valid != cell->valid)
    {
        cell->valid = (int8_t)valid;
        lv_obj_set_style_text_color(cell->value, valid ? KDL_COLOR_INK : KDL_COLOR_INK_FAINT, 0);
    }
}
