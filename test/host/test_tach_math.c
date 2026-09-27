#include "unity.h"

#include "tach_math.h"

#define RES_HZ 80000000U

static tach_cfg_t s_cfg;

void setUp(void)
{
    s_cfg = (tach_cfg_t){ .enabled = true, .pulses_per_rev = 1.0f, .rpm_max = 6000U };
}

void tearDown(void)
{
}

static uint64_t ticks_for_ms(uint32_t ms)
{
    return (uint64_t)ms * (RES_HZ / 1000U);
}

static void test_limits_follow_the_configuration(void)
{
    TEST_ASSERT_TRUE(tach_math_cfg_is_valid(&s_cfg));
    /* 6000 rpm at 1 ppr = 10 ms period; glitches are shorter than half of it. */
    TEST_ASSERT_EQUAL_UINT32(5000U, tach_math_min_period_us(&s_cfg));
    /* 100 rpm = 600 ms period. */
    TEST_ASSERT_EQUAL_UINT32(600000U, tach_math_timeout_us(&s_cfg));
}

static void test_invalid_configurations_are_rejected(void)
{
    tach_cfg_t cfg = s_cfg;
    cfg.pulses_per_rev = 0.0f;
    TEST_ASSERT_FALSE(tach_math_cfg_is_valid(&cfg));

    cfg = s_cfg;
    cfg.rpm_max = 500U;
    TEST_ASSERT_FALSE(tach_math_cfg_is_valid(&cfg));

    /* 200 ppr at 20000 rpm = 66.7 kHz, past the ISR budget. */
    cfg = s_cfg;
    cfg.pulses_per_rev = 200.0f;
    cfg.rpm_max = 20000U;
    TEST_ASSERT_FALSE(tach_math_cfg_is_valid(&cfg));
}

static void test_mean_period_gives_the_speed(void)
{
    /* Ten 20 ms periods = 3000 rpm. */
    const float rpm = tach_math_update(&s_cfg, 0.0f, 10U, 10U * ticks_for_ms(20U), RES_HZ, true,
                                       1000);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 3000.0f, rpm);
}

static void test_pulses_per_rev_divide_the_rate(void)
{
    s_cfg.pulses_per_rev = 2.0f;
    const float rpm = tach_math_update(&s_cfg, 0.0f, 20U, 20U * ticks_for_ms(10U), RES_HZ, true,
                                       1000);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 3000.0f, rpm);
}

static void test_no_new_period_keeps_the_previous_value(void)
{
    const float rpm = tach_math_update(&s_cfg, 3000.0f, 0U, 0U, RES_HZ, true, 5000);
    TEST_ASSERT_EQUAL_FLOAT(3000.0f, rpm);
}

static void test_speed_winds_down_while_pulses_are_missing(void)
{
    /* 100 ms without a pulse: at most 600 rpm. */
    const float rpm = tach_math_update(&s_cfg, 3000.0f, 0U, 0U, RES_HZ, true, 100000);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 600.0f, rpm);
}

static void test_timeout_or_no_pulse_reads_zero(void)
{
    TEST_ASSERT_EQUAL_FLOAT(0.0f, tach_math_update(&s_cfg, 3000.0f, 0U, 0U, RES_HZ, true, 600000));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, tach_math_update(&s_cfg, 0.0f, 0U, 0U, RES_HZ, false, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_limits_follow_the_configuration);
    RUN_TEST(test_invalid_configurations_are_rejected);
    RUN_TEST(test_mean_period_gives_the_speed);
    RUN_TEST(test_pulses_per_rev_divide_the_rate);
    RUN_TEST(test_no_new_period_keeps_the_previous_value);
    RUN_TEST(test_speed_winds_down_while_pulses_are_missing);
    RUN_TEST(test_timeout_or_no_pulse_reads_zero);
    return UNITY_END();
}
