#include "hyper_enemies.h"

/*
 * Impact-boss Hyper cadence.
 *
 * Kashiwagi, Taisamba 2, Balberra and D'Etoile each own several replay loops
 * (root AI, owned children, projectile motion, auras).  Running every loop a
 * flat three extra times produced 4x.  This helper gives each caller its own
 * clock that alternates one and two extra ticks per native frame, so the
 * average is 1.5 extra ticks: the same 2.5x rate Dharumanyo and Tsurami use.
 *
 * Clocks are keyed by id.  A clock only re-rolls its phase when the frame it
 * is asked about differs from the frame it last served, so a loop that runs
 * more than once inside one native frame keeps the same budget and never
 * over-advances the boss.
 */

#define HYPER_IMPACT_MIN_EXTRA_TICKS 1u
#define HYPER_IMPACT_MAX_EXTRA_TICKS 2u

typedef struct
{
    unsigned int frame;
    unsigned int extra_ticks;
    unsigned char phase;
    unsigned char frame_valid;
} HyperImpactClock;

static HyperImpactClock s_impact_clocks[EXTRA_OPTIONS_HYPER_IMPACT_CLOCKS];

void extra_options_hyper_impact_cadence_begin(
    unsigned int clock_id, unsigned int frame)
{
    HyperImpactClock *clock;

    if (clock_id >= EXTRA_OPTIONS_HYPER_IMPACT_CLOCKS)
        return;

    clock = &s_impact_clocks[clock_id];
    if (clock->frame_valid && clock->frame == frame)
        return;

    clock->frame = frame;
    clock->frame_valid = 1;
    clock->extra_ticks = clock->phase ? HYPER_IMPACT_MAX_EXTRA_TICKS
                                      : HYPER_IMPACT_MIN_EXTRA_TICKS;
    clock->phase = clock->phase ? 0 : 1;
}

unsigned int extra_options_hyper_impact_extra_ticks(unsigned int clock_id)
{
    if (clock_id >= EXTRA_OPTIONS_HYPER_IMPACT_CLOCKS)
        return HYPER_IMPACT_MIN_EXTRA_TICKS;
    return s_impact_clocks[clock_id].extra_ticks;
}
