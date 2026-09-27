#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "sensor_config_parse.h"

#define INPUT_MAX_V 5.0f

static sensor_config_parser_t s_parser;

void setUp(void)
{
    sensor_config_parser_begin(&s_parser);
}

void tearDown(void)
{
}

/** Feed a NUL-separated list of lines, numbered from 1. */
static void feed(const char *const *lines, size_t count)
{
    char buffer[160];
    for (size_t index = 0; index < count; ++index) {
        snprintf(buffer, sizeof(buffer), "%s", lines[index]);
        sensor_config_parser_feed(&s_parser, buffer, (uint32_t)(index + 1U));
    }
    sensor_config_parser_end(&s_parser, INPUT_MAX_V);
}

static void test_defaults_disable_pressures_and_enable_tachometer(void)
{
    sensor_config_t config;
    sensor_config_defaults(&config);
    for (size_t index = 0; index < DATA_MODEL_ANALOG_INPUT_COUNT; ++index) {
        TEST_ASSERT_FALSE(config.pressure[index].enabled);
        TEST_ASSERT_TRUE(pressure_scaling_cfg_is_valid(&config.pressure[index], INPUT_MAX_V));
    }
    TEST_ASSERT_TRUE(config.tach.enabled);
    TEST_ASSERT_TRUE(tach_math_cfg_is_valid(&config.tach));
}

static void test_notepad_style_file_is_read(void)
{
    static const char *const lines[] = {
        "\xEF\xBB\xBF; comment\r\n",
        "\r\n",
        "[in4]\r\n",
        "attivo = s\xC3\xAC\r\n",
        "tensione_min = 1\r\n",
        "tensione_max = 5,0 V   ; unit and comment\r\n",
        "pressione_min = 0\r\n",
        "Pressione_Max = 6.5 bar\r\n",
        "[GIRI]\n",
        "impulsi_giro = 2,5\n",
        "giri_max = 4500\n",
    };
    feed(lines, sizeof(lines) / sizeof(lines[0]));

    TEST_ASSERT_EQUAL_UINT32(0U, s_parser.error_count);
    const pressure_sensor_cfg_t *cfg = &s_parser.config.pressure[1];
    TEST_ASSERT_TRUE(cfg->enabled);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, cfg->v_min);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, cfg->v_max);
    TEST_ASSERT_EQUAL_FLOAT(6.5f, cfg->p_max);
    TEST_ASSERT_FALSE(s_parser.config.pressure[0].enabled);
    TEST_ASSERT_TRUE(s_parser.config.tach.enabled);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, s_parser.config.tach.pulses_per_rev);
    TEST_ASSERT_EQUAL_UINT32(4500U, s_parser.config.tach.rpm_max);
}

static void test_bad_value_disables_its_section_only(void)
{
    static const char *const lines[] = {
        "[IN3]",
        "attivo = si",
        "tensione_max = quattro",
        "[IN5]",
        "attivo = si",
    };
    feed(lines, sizeof(lines) / sizeof(lines[0]));

    TEST_ASSERT_EQUAL_UINT32(1U, s_parser.error_count);
    TEST_ASSERT_EQUAL_UINT32(3U, s_parser.first_error_line);
    TEST_ASSERT_FALSE(s_parser.config.pressure[0].enabled);
    TEST_ASSERT_TRUE(s_parser.config.pressure[2].enabled);
}

static void test_out_of_range_sensor_is_reported_on_its_header(void)
{
    static const char *const lines[] = {
        "; 0-10 V sensor on a 0-5 V input",
        "[IN6]",
        "attivo = si",
        "tensione_min = 0",
        "tensione_max = 10",
    };
    feed(lines, sizeof(lines) / sizeof(lines[0]));

    TEST_ASSERT_EQUAL_UINT32(1U, s_parser.error_count);
    TEST_ASSERT_EQUAL_UINT32(2U, s_parser.first_error_line);
    TEST_ASSERT_FALSE(s_parser.config.pressure[3].enabled);
}

static void test_unknown_and_repeated_sections_are_errors(void)
{
    static const char *const lines[] = {
        "[IN9]",
        "attivo = si",
        "[GIRI]",
        "[GIRI]",
        "chiave = 1",
    };
    feed(lines, sizeof(lines) / sizeof(lines[0]));

    TEST_ASSERT_EQUAL_UINT32(1U, s_parser.first_error_line);
    TEST_ASSERT_EQUAL_UINT32(3U, s_parser.error_count);
    TEST_ASSERT_FALSE(s_parser.config.tach.enabled);
}

static void test_template_reads_back_as_the_defaults(void)
{
    sensor_config_t defaults;
    sensor_config_defaults(&defaults);

    FILE *file = tmpfile();
    TEST_ASSERT_NOT_NULL(file);
    TEST_ASSERT_EQUAL(0, sensor_config_write_template(file, &defaults, INPUT_MAX_V));
    rewind(file);

    char line[160];
    uint32_t line_no = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        sensor_config_parser_feed(&s_parser, line, ++line_no);
    }
    fclose(file);
    sensor_config_parser_end(&s_parser, INPUT_MAX_V);

    TEST_ASSERT_EQUAL_UINT32(0U, s_parser.error_count);
    TEST_ASSERT_EQUAL_MEMORY(&defaults, &s_parser.config, sizeof(defaults));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_disable_pressures_and_enable_tachometer);
    RUN_TEST(test_notepad_style_file_is_read);
    RUN_TEST(test_bad_value_disables_its_section_only);
    RUN_TEST(test_out_of_range_sensor_is_reported_on_its_header);
    RUN_TEST(test_unknown_and_repeated_sections_are_errors);
    RUN_TEST(test_template_reads_back_as_the_defaults);
    return UNITY_END();
}
