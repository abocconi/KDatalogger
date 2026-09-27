#include "sensor_config_parse.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "data_model_channels.h"

#define SENSOR_CONFIG_SECTION_NONE    (-1)
#define SENSOR_CONFIG_SECTION_UNKNOWN (-2)
#define SENSOR_CONFIG_TACH_NAME       "GIRI"
#define SENSOR_CONFIG_EOL             "\r\n"
#define SENSOR_CONFIG_UTF8_BOM        "\xEF\xBB\xBF"

/* Factory scaling written for a disabled pressure input: the common
 * 0.5-4.5 V, 0-10 bar sensor, so enabling it is often the only edit. */
#define SENSOR_CONFIG_DEFAULT_V_MIN   0.5f
#define SENSOR_CONFIG_DEFAULT_V_MAX   4.5f
#define SENSOR_CONFIG_DEFAULT_P_MIN   0.0f
#define SENSOR_CONFIG_DEFAULT_P_MAX   10.0f
#define SENSOR_CONFIG_DEFAULT_PPR     1.0f
#define SENSOR_CONFIG_DEFAULT_RPM_MAX 6000U

void sensor_config_defaults(sensor_config_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));
    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        out->pressure[index] = (pressure_sensor_cfg_t){
            .enabled = false,
            .v_min = SENSOR_CONFIG_DEFAULT_V_MIN,
            .v_max = SENSOR_CONFIG_DEFAULT_V_MAX,
            .p_min = SENSOR_CONFIG_DEFAULT_P_MIN,
            .p_max = SENSOR_CONFIG_DEFAULT_P_MAX,
        };
    }
    out->tach = (tach_cfg_t){
        .enabled = true,
        .pulses_per_rev = SENSOR_CONFIG_DEFAULT_PPR,
        .rpm_max = SENSOR_CONFIG_DEFAULT_RPM_MAX,
    };
}

void sensor_config_parser_begin(sensor_config_parser_t *parser)
{
    if (parser == NULL) {
        return;
    }

    memset(parser, 0, sizeof(*parser));
    sensor_config_defaults(&parser->config);
    parser->section = SENSOR_CONFIG_SECTION_NONE;
}

void sensor_config_parser_error(sensor_config_parser_t *parser, uint32_t line_no)
{
    if (parser == NULL) {
        return;
    }

    parser->error_count++;
    if (parser->first_error_line == 0U) {
        parser->first_error_line = line_no;
    }
    if (parser->section >= 0) {
        parser->section_error[parser->section] = true;
    }
}

static char *sensor_config_trim(char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text)) {
        ++text;
    }
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return text;
}

/**
 * Number with a comma or a dot as decimal mark, optionally followed by a
 * unit made of letters only ("4,5 V", "10bar"). Anything else after the
 * number -- a second number, a stray sign -- is rejected rather than guessed.
 */
static bool sensor_config_parse_float(const char *text, float *out)
{
    char buffer[32];
    const size_t len = strlen(text);
    if (len == 0U || len >= sizeof(buffer)) {
        return false;
    }
    memcpy(buffer, text, len + 1U);
    for (char *cursor = buffer; *cursor != '\0'; ++cursor) {
        if (*cursor == ',') {
            *cursor = '.';
        }
    }

    char *end = NULL;
    const float value = strtof(buffer, &end);
    if (end == buffer || isfinite(value) == 0) {
        return false;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) {
        ++end;
    }
    while (*end != '\0') {
        if (!isalpha((unsigned char)*end)) {
            return false;
        }
        ++end;
    }

    *out = value;
    return true;
}

static bool sensor_config_parse_uint(const char *text, uint32_t *out)
{
    float value = 0.0f;
    if (!sensor_config_parse_float(text, &value) || value < 0.0f || value > 4.0e9f
        || value != floorf(value)) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool sensor_config_parse_bool(const char *text, bool *out)
{
    /* "sì" in UTF-8 and in Windows-1252, which is what Notepad may save as. */
    static const char *const yes[] = { "si", "s\xC3\xAC", "s\xEC", "1", "true", "on" };
    static const char *const no[] = { "no", "0", "false", "off" };

    for (size_t index = 0; index < sizeof(yes) / sizeof(yes[0]); ++index) {
        if (strcasecmp(text, yes[index]) == 0) {
            *out = true;
            return true;
        }
    }
    for (size_t index = 0; index < sizeof(no) / sizeof(no[0]); ++index) {
        if (strcasecmp(text, no[index]) == 0) {
            *out = false;
            return true;
        }
    }
    return false;
}

static int sensor_config_find_section(const char *name)
{
    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        if (strcasecmp(name, data_model_analog_channels[index].id) == 0) {
            return (int)index;
        }
    }
    if (strcasecmp(name, SENSOR_CONFIG_TACH_NAME) == 0) {
        return SENSOR_CONFIG_SECTION_TACH;
    }
    return SENSOR_CONFIG_SECTION_UNKNOWN;
}

static bool sensor_config_apply_pressure_key(pressure_sensor_cfg_t *cfg, const char *key,
                                             const char *value)
{
    if (strcasecmp(key, "attivo") == 0) {
        return sensor_config_parse_bool(value, &cfg->enabled);
    }
    if (strcasecmp(key, "tensione_min") == 0) {
        return sensor_config_parse_float(value, &cfg->v_min);
    }
    if (strcasecmp(key, "tensione_max") == 0) {
        return sensor_config_parse_float(value, &cfg->v_max);
    }
    if (strcasecmp(key, "pressione_min") == 0) {
        return sensor_config_parse_float(value, &cfg->p_min);
    }
    if (strcasecmp(key, "pressione_max") == 0) {
        return sensor_config_parse_float(value, &cfg->p_max);
    }
    return false;
}

static bool sensor_config_apply_tach_key(tach_cfg_t *cfg, const char *key, const char *value)
{
    if (strcasecmp(key, "attivo") == 0) {
        return sensor_config_parse_bool(value, &cfg->enabled);
    }
    if (strcasecmp(key, "impulsi_giro") == 0) {
        return sensor_config_parse_float(value, &cfg->pulses_per_rev);
    }
    if (strcasecmp(key, "giri_max") == 0) {
        return sensor_config_parse_uint(value, &cfg->rpm_max);
    }
    return false;
}

void sensor_config_parser_feed(sensor_config_parser_t *parser, char *line, uint32_t line_no)
{
    if (parser == NULL || line == NULL) {
        return;
    }

    if (line_no == 1U && strncmp(line, SENSOR_CONFIG_UTF8_BOM, 3) == 0) {
        line += 3;
    }
    char *comment = strpbrk(line, ";#");
    if (comment != NULL) {
        *comment = '\0';
    }
    line = sensor_config_trim(line);
    if (*line == '\0') {
        return;
    }

    if (*line == '[') {
        char *close = strchr(line, ']');
        if (close == NULL || close[1] != '\0') {
            parser->section = SENSOR_CONFIG_SECTION_UNKNOWN;
            sensor_config_parser_error(parser, line_no);
            return;
        }
        *close = '\0';
        parser->section = sensor_config_find_section(sensor_config_trim(line + 1));
        if (parser->section == SENSOR_CONFIG_SECTION_UNKNOWN) {
            sensor_config_parser_error(parser, line_no);
            return;
        }
        if (parser->section_line[parser->section] != 0U) {
            /* A repeated section would silently override the first one. */
            sensor_config_parser_error(parser, line_no);
            return;
        }
        parser->section_line[parser->section] = line_no;
        return;
    }

    char *equals = strchr(line, '=');
    if (equals == NULL || parser->section < 0) {
        /* Lines under an unknown section were already reported with it. */
        if (parser->section != SENSOR_CONFIG_SECTION_UNKNOWN) {
            sensor_config_parser_error(parser, line_no);
        }
        return;
    }
    *equals = '\0';
    const char *key = sensor_config_trim(line);
    const char *value = sensor_config_trim(equals + 1);

    bool accepted = false;
    if (parser->section == SENSOR_CONFIG_SECTION_TACH) {
        accepted = sensor_config_apply_tach_key(&parser->config.tach, key, value);
    } else {
        accepted = sensor_config_apply_pressure_key(&parser->config.pressure[parser->section],
                                                    key, value);
    }
    if (!accepted) {
        sensor_config_parser_error(parser, line_no);
    }
}

/** Record a section-level error against its header line. */
static void sensor_config_section_error(sensor_config_parser_t *parser, int section)
{
    const int saved = parser->section;
    parser->section = section;
    sensor_config_parser_error(parser, parser->section_line[section]);
    parser->section = saved;
}

void sensor_config_parser_end(sensor_config_parser_t *parser, float input_max_v)
{
    if (parser == NULL) {
        return;
    }

    for (int index = 0; index < (int)DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        pressure_sensor_cfg_t *cfg = &parser->config.pressure[index];
        if (cfg->enabled && !parser->section_error[index]
            && !pressure_scaling_cfg_is_valid(cfg, input_max_v)) {
            sensor_config_section_error(parser, index);
        }
        if (parser->section_error[index]) {
            cfg->enabled = false;
        }
    }

    tach_cfg_t *tach = &parser->config.tach;
    if (tach->enabled && !parser->section_error[SENSOR_CONFIG_SECTION_TACH]
        && !tach_math_cfg_is_valid(tach)) {
        sensor_config_section_error(parser, SENSOR_CONFIG_SECTION_TACH);
    }
    if (parser->section_error[SENSOR_CONFIG_SECTION_TACH]) {
        tach->enabled = false;
    }
}

/* -- Template ---------------------------------------------------------------- */

/** @p value with a decimal comma and no trailing zeros: 0,5 / 10 / 4,25. */
static void sensor_config_format_number(char *out, size_t len, float value)
{
    snprintf(out, len, "%.3f", (double)value);
    char *dot = strchr(out, '.');
    if (dot == NULL) {
        return;
    }
    char *end = out + strlen(out);
    while (end > dot + 1 && end[-1] == '0') {
        *--end = '\0';
    }
    if (end == dot + 1) {
        *dot = '\0';
    } else {
        *dot = ',';
    }
}

static int sensor_config_put_lines(FILE *file, const char *const *lines, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        if (fputs(lines[index], file) < 0 || fputs(SENSOR_CONFIG_EOL, file) < 0) {
            return -1;
        }
    }
    return 0;
}

int sensor_config_write_template(FILE *file, const sensor_config_t *config, float input_max_v)
{
    if (file == NULL || config == NULL) {
        return -1;
    }

    static const char *const intro[] = {
        SENSOR_CONFIG_UTF8_BOM "; KDatalogger - configurazione dei sensori",
        ";",
        "; Come modificarlo:",
        ";  1. cambiare i valori dopo il segno \"=\" e salvare il file;",
        ";  2. espellere il disco dal computer e uscire dalla modalità USB.",
        "; Il datalogger rilegge questo file a ogni accensione e a ogni uscita dalla",
        "; modalità USB. L'esito è indicato in fondo alla pagina Impostazioni.",
        "; Se il file viene cancellato, ne viene creato uno nuovo con i valori iniziali.",
        ";",
        "; Le righe che iniziano con \";\" sono commenti. I decimali si possono",
        "; scrivere con la virgola o con il punto.",
        "",
        "; -------------------------------------------------------------------------",
        "; SENSORI DI PRESSIONE",
        ";",
        ";   attivo         si = sensore collegato, no = ingresso non usato",
        ";   tensione_min   tensione in uscita alla pressione minima, in volt",
        ";   tensione_max   tensione in uscita alla pressione massima, in volt",
        ";   pressione_min  pressione minima del sensore, in bar",
        ";   pressione_max  pressione massima del sensore, in bar",
        ";",
        "; I valori si trovano nella scheda tecnica del sensore. Esempio, sensore",
        "; 0-10 bar con uscita 0,5-4,5 V:",
        ";   tensione_min = 0,5    tensione_max = 4,5",
        ";   pressione_min = 0     pressione_max = 10",
        ";",
        "; Con i sensori 0,5-4,5 V o 1-5 V un filo interrotto viene segnalato con ERR",
        "; sul display. Con i sensori 0-5 V non è possibile: un filo interrotto si",
        "; legge come 0 bar.",
    };
    static const char *const tach_intro[] = {
        "",
        "; -------------------------------------------------------------------------",
        "; CONTAGIRI",
        ";",
        ";   attivo         si = sensore collegato, no = ingresso non usato",
        ";   impulsi_giro   impulsi per giro del motore (1 = un riferimento sul volano",
        ";                  o sulla puleggia dell'albero motore; ammessi i decimali)",
        ";   giri_max       fondo scala della barra sul display, in giri/min",
    };

    if (sensor_config_put_lines(file, intro, sizeof(intro) / sizeof(intro[0])) != 0) {
        return -1;
    }

    char limit[16];
    sensor_config_format_number(limit, sizeof(limit), input_max_v);
    if (fprintf(file, ";%s; Tensione massima misurabile dal datalogger: %s V.%s", SENSOR_CONFIG_EOL,
                limit, SENSOR_CONFIG_EOL) < 0) {
        return -1;
    }

    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        const pressure_sensor_cfg_t *cfg = &config->pressure[index];
        const kdl_channel_info_t *channel = &data_model_analog_channels[index];
        char v_min[16];
        char v_max[16];
        char p_min[16];
        char p_max[16];
        sensor_config_format_number(v_min, sizeof(v_min), cfg->v_min);
        sensor_config_format_number(v_max, sizeof(v_max), cfg->v_max);
        sensor_config_format_number(p_min, sizeof(p_min), cfg->p_min);
        sensor_config_format_number(p_max, sizeof(p_max), cfg->p_max);

        const int written = fprintf(file,
                                    "%s[%s]%s; %s%s"
                                    "attivo = %s%s"
                                    "tensione_min = %s%s"
                                    "tensione_max = %s%s"
                                    "pressione_min = %s%s"
                                    "pressione_max = %s%s",
                                    SENSOR_CONFIG_EOL, channel->id, SENSOR_CONFIG_EOL,
                                    channel->name, SENSOR_CONFIG_EOL,
                                    cfg->enabled ? "si" : "no", SENSOR_CONFIG_EOL,
                                    v_min, SENSOR_CONFIG_EOL, v_max, SENSOR_CONFIG_EOL,
                                    p_min, SENSOR_CONFIG_EOL, p_max, SENSOR_CONFIG_EOL);
        if (written < 0) {
            return -1;
        }
    }

    if (sensor_config_put_lines(file, tach_intro, sizeof(tach_intro) / sizeof(tach_intro[0])) != 0) {
        return -1;
    }

    char ppr[16];
    sensor_config_format_number(ppr, sizeof(ppr), config->tach.pulses_per_rev);
    const int written = fprintf(file,
                                "%s[%s]%s"
                                "attivo = %s%s"
                                "impulsi_giro = %s%s"
                                "giri_max = %u%s",
                                SENSOR_CONFIG_EOL, SENSOR_CONFIG_TACH_NAME, SENSOR_CONFIG_EOL,
                                config->tach.enabled ? "si" : "no", SENSOR_CONFIG_EOL,
                                ppr, SENSOR_CONFIG_EOL,
                                (unsigned)config->tach.rpm_max, SENSOR_CONFIG_EOL);
    return (written < 0) ? -1 : 0;
}
