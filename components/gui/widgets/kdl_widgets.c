#include "kdl_widgets.h"

#include <stdio.h>
#include <string.h>

#include "kdl_theme.h"

#define KDL_WIDGET_HEADER_H 12
#define KDL_WIDGET_FOOTER_H 12
#define KDL_WIDGET_VALUE_H  34
#define KDL_WIDGET_BAR_H    3
#define KDL_WIDGET_ANALOG_VALUE_H 24

#define KDL_WIDGET_VALUE_TEXT_LEN 8   /* "-1350" plus slack        */
#define KDL_WIDGET_EXTREMES_TEXT_LEN 24

/* U+2022 BULLET, present in LVGL's built-in Montserrat faces. Separates the
 * session minimum from the maximum without spending width on "MIN"/"MAX"
 * captions, which do not fit at this card width in a proportional font. */
#define KDL_WIDGET_EXTREMES_SEPARATOR "\xE2\x80\xA2"

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

static lv_obj_t *kdl_widget_row(lv_obj_t *parent, int32_t width, int32_t height,
                                lv_flex_align_t cross_align)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_add_style(row, &kdl_style_panel, 0);
    lv_obj_set_size(row, width, height);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, cross_align, cross_align);
    lv_obj_set_style_pad_column(row, 3, 0);
    return row;
}

static lv_obj_t *kdl_widget_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, "");
    return label;
}

/** Inner width of a bordered card/cell: the 1 px stroke and the horizontal
 *  padding both come out of the box, and the children are sized in absolute
 *  pixels so they must agree with that arithmetic. */
static int32_t kdl_widget_inner_width(int32_t width)
{
    return width - 2 - 8;
}

void kdl_probe_card_create(kdl_probe_card_t *card, lv_obj_t *parent,
                           const kdl_thermocouple_desc_t *desc,
                           int32_t width, int32_t height)
{
    if (card == NULL || desc == NULL)
    {
        return;
    }

    const int32_t inner_w = kdl_widget_inner_width(width);

    card->desc = desc;
    card->bar_percent = -1;

    card->root = lv_obj_create(parent);
    lv_obj_add_style(card->root, &kdl_style_panel, 0);
    lv_obj_add_style(card->root, &kdl_style_card, 0);
    lv_obj_set_size(card->root, width, height);
    lv_obj_clear_flag(card->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card->root, LV_FLEX_FLOW_COLUMN);
    /* Same distribution the prototype uses: the four bands are pinned to the
     * top and bottom edges and the slack falls between them. */
    lv_obj_set_flex_align(card->root, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *header = kdl_widget_row(card->root, inner_w, KDL_WIDGET_HEADER_H,
                                      LV_FLEX_ALIGN_CENTER);
    lv_obj_t *id = kdl_widget_label(header, KDL_FONT_MICRO, KDL_COLOR_INK_FAINT);
    lv_label_set_text(id, desc->channel->id);
    lv_obj_t *name = kdl_widget_label(header, KDL_FONT_MICRO, KDL_COLOR_INK);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_text(name, desc->channel->name);
    /* No unit on the card. Measured against the built-in Montserrat faces, a
     * four-digit reading already takes 75 of the 82 inner pixels. Since every
     * thermocouple card carries the same unit, it is stated once in the
     * status bar caption instead (see page_main.c). */

    card->value = kdl_widget_label(card->root, KDL_FONT_VALUE, KDL_COLOR_INK);
    lv_obj_set_size(card->value, inner_w, KDL_WIDGET_VALUE_H);
    lv_label_set_text(card->value, "---");

    lv_obj_t *track = lv_obj_create(card->root);
    lv_obj_add_style(track, &kdl_style_panel, 0);
    lv_obj_set_size(track, inner_w, KDL_WIDGET_BAR_H);
    lv_obj_clear_flag(track, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(track, KDL_COLOR_TRACK, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);

    card->bar_fill = lv_obj_create(track);
    lv_obj_add_style(card->bar_fill, &kdl_style_panel, 0);
    lv_obj_set_size(card->bar_fill, 0, KDL_WIDGET_BAR_H);
    lv_obj_set_pos(card->bar_fill, 0, 0);
    lv_obj_clear_flag(card->bar_fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(card->bar_fill, KDL_COLOR_INK, 0);
    lv_obj_set_style_bg_opa(card->bar_fill, LV_OPA_COVER, 0);

    lv_obj_t *footer = kdl_widget_row(card->root, inner_w, KDL_WIDGET_FOOTER_H,
                                      LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_color(footer, KDL_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_width(footer, 1, 0);
    lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
    card->extremes = kdl_widget_label(footer, KDL_FONT_MICRO, KDL_COLOR_INK_DIM);
    lv_obj_set_width(card->extremes, inner_w);
    lv_label_set_text(card->extremes, "");
}

/** Fault text for a channel the MAX31855 could not read, or NULL when the
 *  reading is usable. Open-circuit is reported as an unplugged probe rather
 *  than an error, since that is what it means in the field. */
static const char *kdl_probe_fault_text(const kdl_sensor_sample_t *sample, uint8_t index)
{
    const uint16_t bit = (uint16_t)(1U << index);

    if ((sample->thermocouple_valid_mask & bit) != 0U)
    {
        return NULL;
    }
    if ((sample->thermocouple_oc_mask & bit) != 0U)
    {
        return "---";
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

void kdl_probe_card_update(kdl_probe_card_t *card,
                           const kdl_sensor_sample_t *sample,
                           const kdl_sensor_extremes_t *extremes,
                           uint8_t index)
{
    if (card == NULL || card->root == NULL || sample == NULL)
    {
        return;
    }

    const char *fault = kdl_probe_fault_text(sample, index);
    char text[KDL_WIDGET_VALUE_TEXT_LEN];
    lv_color_t ink = KDL_COLOR_INK_FAINT;
    int32_t percent = 0;

    if (fault != NULL)
    {
        kdl_widget_set_text(card->value, fault);
    }
    else
    {
        const float value = sample->thermocouples_c[index];
        snprintf(text, sizeof(text), "%d", (int)(value + (value < 0.0f ? -0.5f : 0.5f)));
        kdl_widget_set_text(card->value, text);

        if (value >= card->desc->alarm_threshold)
        {
            ink = KDL_COLOR_HOT;
        }
        else if (value >= card->desc->warn_threshold)
        {
            ink = KDL_COLOR_WARM;
        }
        else
        {
            ink = KDL_COLOR_INK;
        }

        if (card->desc->full_scale > 0.0f)
        {
            percent = (int32_t)((value / card->desc->full_scale) * 100.0f);
            percent = (percent < 0) ? 0 : ((percent > 100) ? 100 : percent);
        }
    }

    lv_obj_set_style_text_color(card->value, ink, 0);

    if (percent != card->bar_percent)
    {
        card->bar_percent = percent;
        lv_obj_set_width(card->bar_fill, lv_pct(percent));
        lv_obj_set_style_bg_color(card->bar_fill, ink, 0);
    }

    char extremes_text[KDL_WIDGET_EXTREMES_TEXT_LEN];
    if (extremes != NULL && (extremes->valid_mask & (uint16_t)(1U << index)) != 0U)
    {
        snprintf(extremes_text, sizeof(extremes_text), "%d  " KDL_WIDGET_EXTREMES_SEPARATOR "  %d",
                 (int)extremes->thermocouple_min_c[index],
                 (int)extremes->thermocouple_max_c[index]);
    }
    else
    {
        snprintf(extremes_text, sizeof(extremes_text), "--  " KDL_WIDGET_EXTREMES_SEPARATOR "  --");
    }
    kdl_widget_set_text(card->extremes, extremes_text);
}

void kdl_analog_cell_create(kdl_analog_cell_t *cell, lv_obj_t *parent,
                            const kdl_analog_desc_t *desc,
                            int32_t width, int32_t height)
{
    if (cell == NULL || desc == NULL)
    {
        return;
    }

    const int32_t inner_w = kdl_widget_inner_width(width);

    cell->desc = desc;

    cell->root = lv_obj_create(parent);
    lv_obj_add_style(cell->root, &kdl_style_panel, 0);
    lv_obj_add_style(cell->root, &kdl_style_cell, 0);
    lv_obj_set_size(cell->root, width, height);
    lv_obj_clear_flag(cell->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(cell->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cell->root, LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *header = kdl_widget_row(cell->root, inner_w, KDL_WIDGET_HEADER_H,
                                      LV_FLEX_ALIGN_CENTER);
    lv_obj_t *id = kdl_widget_label(header, KDL_FONT_MICRO, KDL_COLOR_INK_FAINT);
    lv_label_set_text(id, desc->channel->id);
    lv_obj_t *name = kdl_widget_label(header, KDL_FONT_MICRO, KDL_COLOR_INK);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_text(name, desc->channel->name);

    lv_obj_t *value_row = kdl_widget_row(cell->root, inner_w, KDL_WIDGET_ANALOG_VALUE_H,
                                         LV_FLEX_ALIGN_END);
    cell->value = kdl_widget_label(value_row, KDL_FONT_VALUE_SM, KDL_COLOR_INK);
    lv_obj_set_flex_grow(cell->value, 1);
    lv_label_set_text(cell->value, "---");
    lv_obj_t *unit = kdl_widget_label(value_row, KDL_FONT_MICRO, KDL_COLOR_INK_MUTED);
    lv_label_set_text(unit, desc->channel->unit);
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
    lv_obj_set_style_text_color(cell->value, valid ? KDL_COLOR_INK : KDL_COLOR_INK_FAINT, 0);
}
