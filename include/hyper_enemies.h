#ifndef HYPER_ENEMIES_H
#define HYPER_ENEMIES_H

typedef int (*ExtraOptionsHyperTargetPredicate)(void *task);
typedef void (*ExtraOptionsHyperBeforeTick)(void *task);

/* Boss-specific post callbacks use the ordinary-enemy capture and replay
 * state so nested callbacks share the same recursion guard and seen cache. */
void extra_options_hyper_capture_from_post(
    void *task, ExtraOptionsHyperTargetPredicate target_is_live);
/* Returns the number of completed extra AI/post pairs (zero when gated).
 * before_tick, if present, runs under the shared replay guard before each
 * extra AI call, allowing a multipart boss to consume the preceding tick. */
unsigned int extra_options_hyper_run_captured_tick(
    ExtraOptionsHyperTargetPredicate target_is_live,
    ExtraOptionsHyperBeforeTick before_tick);
/* Exact constructor hooks call this before a task's first common post so a
 * recycled eight-bit generation cannot inherit an ancient seen-cache entry. */
void extra_options_hyper_forget_task(void *task);

/* Congo-owned flames use the common post callback, unlike their boss. */
int extra_options_hyper_congo_child_is_live(void *task);

/* Dharumanyo's travelling attack carrier also uses the common post callback.
 * Its dedicated implementation admits only children from the exact native
 * projectile constructor, leaving trails and impact effects at native speed. */
int extra_options_hyper_dharumanyo_projectile_is_live(void *task);

/* Tsurami's exact attack constructor emits every travelling shot used by
 * all three phases.  Impact and particle children are intentionally omitted. */
int extra_options_hyper_tsurami_projectile_is_live(void *task);
/* Consumes the live projectile's independent 1/2-tick cadence.  Returns one
 * and writes that frame's synthetic budget for Tsurami, or returns zero for
 * every other task. */
int extra_options_hyper_tsurami_take_projectile_extra_ticks(
    void *task, unsigned int *extra_ticks);

/* Koryuta's room-0x155 flight spawns two overlay-owned enemy children that
 * inherit the encounter actor ID.  Their exact constructors and recurring
 * callbacks are validated in the dedicated implementation. */
int extra_options_hyper_koryuta_enemy_is_live(void *task);

#endif
