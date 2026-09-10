/* Production-source host regression, not in-game certification.
 * cc -std=c11 -Wall -Wextra -Werror -I include \
 *   tests/game_over_to_title_test.c -o /tmp/game_over_to_title_test
 */
#include <stdio.h>
#include <stdlib.h>

#define __MODDING_H__
#define __RECOMPCONFIG_H__
#define RECOMP_HOOK_RETURN(name)
unsigned long recomp_get_config_u32(const char *key);

static unsigned long config;
static int teardown_calls;
static int step_calls;
static int step_value;

unsigned long recomp_get_config_u32(const char *key)
{
    (void)key;
    return config;
}

void func_8000383C_443C(void)
{
    teardown_calls++;
}

void func_80003728_4328(int step)
{
    step_calls++;
    step_value = step;
}

#include "../src/game_over_to_title.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

int main(void)
{
    /* Enabled is the first enum entry, so its config value is zero. */
    config = 0;
    extra_options_game_over_to_title_hook();
    CHECK(teardown_calls == 1);
    CHECK(step_calls == 1);
    CHECK(step_value == 3);

    /* Disabled must not change the native game-over flow. */
    config = 1;
    teardown_calls = 0;
    step_calls = 0;
    step_value = 0;
    extra_options_game_over_to_title_hook();
    CHECK(teardown_calls == 0);
    CHECK(step_calls == 0);
    CHECK(step_value == 0);

    printf("game_over_to_title: ok\n");
    return 0;
}
