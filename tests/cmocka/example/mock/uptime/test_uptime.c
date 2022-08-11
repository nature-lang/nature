/*
 * Copyright 2018 Andreas Scheider <asn@cryptomilk.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#define UNIT_TESTING 1
#include "uptime.c"

#define UNUSED(x) (void)(x)

/*
 * This is a mocked object!
 *
 * It is a reimplementation of the uptime() function you can find in
 * proc_uptime.c.
 *
 * This function can be instrumeted by the test. We can tell it what
 * we expect or should return.
 */
int __wrap_uptime(const char *uptime_path,
                  double *uptime_secs,
                  double *idle_secs);
int __wrap_uptime(const char *uptime_path,
                  double *uptime_secs,
                  double *idle_secs)
{
    double up;
    double idle;

    /* Verify the passed value of the argument is correct */
    check_expected_ptr(uptime_path);

    /* Assign the return values */
    up = mock_type(double);
    idle = mock_type(double);

    if (uptime_secs != NULL) {
        *uptime_secs = up;
    }
    if (idle_secs != NULL) {
        *idle_secs = idle;
    }

    return (int)up;
}

static void test_calc_uptime_minutes(void **state)
{
    char *uptime_str = NULL;

    UNUSED(state);

    /* Make sure the passed 'in' argument is correct */
    expect_string(__wrap_uptime, uptime_path, "/proc/uptime");

    /* We tell the uptime function what values it should return */
    will_return(__wrap_uptime, 508.16);
    will_return(__wrap_uptime, 72.23);

    /* We call the function like we would do it normally */
    uptime_str = calc_uptime();

    /* Now lets check if the result is what we expect it to be */
    assert_non_null(uptime_str);
    assert_string_equal(uptime_str, "up 8 minutes");

    free(uptime_str);
}

static void test_calc_uptime_hour_minute(void **state)
{
    char *uptime_str = NULL;

    UNUSED(state);

    /* Make sure the passed 'in' argument is correct */
    expect_string(__wrap_uptime, uptime_path, "/proc/uptime");

    /* We tell the uptime function what values it should return */
    will_return(__wrap_uptime, 3699.16);
    will_return(__wrap_uptime, 4069.23);

    /* We call the function like we would do it normally */
    uptime_str = calc_uptime();

    /* Now lets check if the result is what we expect it to be */
    assert_non_null(uptime_str);
    assert_string_equal(uptime_str, "up 1 hour, 1 minute");

    free(uptime_str);
}

static void test_calc_uptime_days_minutes(void **state)
{
    char *uptime_str = NULL;

    UNUSED(state);

    /* Make sure the passed 'in' argument is correct */
    expect_string(__wrap_uptime, uptime_path, "/proc/uptime");

    /* We tell the uptime function what values it should return */
    will_return(__wrap_uptime, 259415.14);
    will_return(__wrap_uptime, 262446.29);

    /* We call the function like we would do it normally */
    uptime_str = calc_uptime();

    /* Now lets check if the result is what we expect it to be */
    assert_non_null(uptime_str);
    assert_string_equal(uptime_str, "up 3 days, 3 minutes");

    free(uptime_str);
}

static void test_calc_uptime_days_hours_minutes(void **state)
{
    char *uptime_str = NULL;

    UNUSED(state);

    /* Make sure the passed 'in' argument is correct */
    expect_string(__wrap_uptime, uptime_path, "/proc/uptime");

    /* We tell the uptime function what values it should return */
    will_return(__wrap_uptime, 359415.14);
    will_return(__wrap_uptime, 362446.29);

    /* We call the function like we would do it normally */
    uptime_str = calc_uptime();

    /* Now lets check if the result is what we expect it to be */
    assert_non_null(uptime_str);
    assert_string_equal(uptime_str, "up 4 days, 3 hours, 50 minutes");

    free(uptime_str);
}

static void test_calc_uptime_null(void **state)
{
    char *uptime_str = NULL;

    UNUSED(state);

    /* Make sure the passed 'in' argument is correct */
    expect_string(__wrap_uptime, uptime_path, "/proc/uptime");

    will_return(__wrap_uptime, -0.0);
    will_return(__wrap_uptime, 0.1);

    uptime_str = calc_uptime();
    assert_null(uptime_str);

    free(uptime_str);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_calc_uptime_minutes),
        cmocka_unit_test(test_calc_uptime_hour_minute),
        cmocka_unit_test(test_calc_uptime_days_minutes),
        cmocka_unit_test(test_calc_uptime_days_hours_minutes),
        cmocka_unit_test(test_calc_uptime_null),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
