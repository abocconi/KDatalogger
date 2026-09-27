#include "unity.h"

#include "pressure_scaling.h"

#define INPUT_MAX_V 5.0f

static pressure_sensor_cfg_t s_cfg;

void setUp(void)
{
    /* 0-10 bar, 0.5-4.5 V: the reference sensor. */
    s_cfg = (pressure_sensor_cfg_t){
        .enabled = true, .v_min = 0.5f, .v_max = 4.5f, .p_min = 0.0f, .p_max = 10.0f,
    };
}

void tearDown(void)
{
}

static void test_valid_config_is_accepted(void)
{
    TEST_ASSERT_TRUE(pressure_scaling_cfg_is_valid(&s_cfg, INPUT_MAX_V));
}

static void test_zero_to_ten_volt_sensor_is_rejected_on_five_volt_input(void)
{
    s_cfg.v_min = 0.0f;
    s_cfg.v_max = 10.0f;
    TEST_ASSERT_FALSE(pressure_scaling_cfg_is_valid(&s_cfg, INPUT_MAX_V));
}

static void test_inverted_or_narrow_ranges_are_rejected(void)
{
    pressure_sensor_cfg_t cfg = s_cfg;
    cfg.p_max = cfg.p_min;
    TEST_ASSERT_FALSE(pressure_scaling_cfg_is_valid(&cfg, INPUT_MAX_V));

    cfg = s_cfg;
    cfg.v_max = cfg.v_min + 0.4f;
    TEST_ASSERT_FALSE(pressure_scaling_cfg_is_valid(&cfg, INPUT_MAX_V));

    cfg = s_cfg;
    cfg.v_min = -0.1f;
    TEST_ASSERT_FALSE(pressure_scaling_cfg_is_valid(&cfg, INPUT_MAX_V));
}

static void test_mid_scale_converts_linearly(void)
{
    float bar = -1.0f;
    TEST_ASSERT_EQUAL(PRESSURE_STATUS_OK, pressure_scaling_convert(&s_cfg, 2.5f, 0.0f, false, &bar));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 5.0f, bar);
}

static void test_open_wire_is_a_low_fault(void)
{
    float bar = 0.0f;
    TEST_ASSERT_EQUAL(PRESSURE_STATUS_FAULT_LOW,
                      pressure_scaling_convert(&s_cfg, 0.1f, 0.0f, false, &bar));
}

static void test_noise_below_live_zero_is_clamped_to_p_min(void)
{
    float bar = -1.0f;
    TEST_ASSERT_EQUAL(PRESSURE_STATUS_OK, pressure_scaling_convert(&s_cfg, 0.3f, 0.0f, false, &bar));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bar);
}

static void test_over_range_extrapolates_then_faults(void)
{
    float bar = 0.0f;
    TEST_ASSERT_EQUAL(PRESSURE_STATUS_OK, pressure_scaling_convert(&s_cfg, 4.7f, 0.0f, false, &bar));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.5f, bar);
    TEST_ASSERT_EQUAL(PRESSURE_STATUS_FAULT_HIGH,
                      pressure_scaling_convert(&s_cfg, 4.8f, 0.0f, false, &bar));
}

static void test_saturated_input_is_a_high_fault(void)
{
    float bar = 0.0f;
    TEST_ASSERT_EQUAL(PRESSURE_STATUS_FAULT_HIGH,
                      pressure_scaling_convert(&s_cfg, 2.5f, 0.0f, true, &bar));
}

static void test_zero_based_sensor_has_no_low_fault(void)
{
    s_cfg.v_min = 0.0f;
    s_cfg.v_max = 5.0f;
    float bar = -1.0f;
    TEST_ASSERT_EQUAL(PRESSURE_STATUS_OK, pressure_scaling_convert(&s_cfg, 0.0f, 0.0f, false, &bar));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, bar);
}

static void test_disabled_input_reports_disabled(void)
{
    s_cfg.enabled = false;
    float bar = 0.0f;
    TEST_ASSERT_EQUAL(PRESSURE_STATUS_DISABLED,
                      pressure_scaling_convert(&s_cfg, 2.5f, 0.0f, false, &bar));
}

static void test_zero_removes_a_small_offset(void)
{
    float zero_v = 0.0f;
    TEST_ASSERT_TRUE(pressure_scaling_compute_zero(&s_cfg, 0.55f, &zero_v));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.05f, zero_v);

    float bar = -1.0f;
    TEST_ASSERT_EQUAL(PRESSURE_STATUS_OK,
                      pressure_scaling_convert(&s_cfg, 2.55f, zero_v, false, &bar));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 5.0f, bar);
}

static void test_zero_under_pressure_is_refused(void)
{
    float zero_v = 123.0f;
    /* 1.0 V is 1.25 bar: a 0.5 V correction, beyond the 0.2 V window. */
    TEST_ASSERT_FALSE(pressure_scaling_compute_zero(&s_cfg, 1.0f, &zero_v));
    TEST_ASSERT_EQUAL_FLOAT(123.0f, zero_v);
}

static void test_zero_refused_on_fault_or_when_zero_is_off_range(void)
{
    float zero_v = 0.0f;
    TEST_ASSERT_FALSE(pressure_scaling_compute_zero(&s_cfg, 0.1f, &zero_v));

    s_cfg.p_min = 1.0f;
    s_cfg.p_max = 5.0f;
    TEST_ASSERT_FALSE(pressure_scaling_compute_zero(&s_cfg, 0.5f, &zero_v));
}

static void test_display_decimals_keep_four_characters(void)
{
    TEST_ASSERT_EQUAL(2, pressure_scaling_display_decimals(2.345f));
    TEST_ASSERT_EQUAL(1, pressure_scaling_display_decimals(12.3f));
    TEST_ASSERT_EQUAL(0, pressure_scaling_display_decimals(250.0f));
    TEST_ASSERT_EQUAL(1, pressure_scaling_display_decimals(-0.85f));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_config_is_accepted);
    RUN_TEST(test_zero_to_ten_volt_sensor_is_rejected_on_five_volt_input);
    RUN_TEST(test_inverted_or_narrow_ranges_are_rejected);
    RUN_TEST(test_mid_scale_converts_linearly);
    RUN_TEST(test_open_wire_is_a_low_fault);
    RUN_TEST(test_noise_below_live_zero_is_clamped_to_p_min);
    RUN_TEST(test_over_range_extrapolates_then_faults);
    RUN_TEST(test_saturated_input_is_a_high_fault);
    RUN_TEST(test_zero_based_sensor_has_no_low_fault);
    RUN_TEST(test_disabled_input_reports_disabled);
    RUN_TEST(test_zero_removes_a_small_offset);
    RUN_TEST(test_zero_under_pressure_is_refused);
    RUN_TEST(test_zero_refused_on_fault_or_when_zero_is_off_range);
    RUN_TEST(test_display_decimals_keep_four_characters);
    return UNITY_END();
}
