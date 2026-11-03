/* advisor.c — rule-based advisor stub (prototype chooseCompanionGoal). */
#include "advisor.h"

/* stateless PRNG: advisor must not mutate the tank */
static float frand(const tank_t *t, int fish_idx) {
    uint32_t x = t->rng ^ (0x9E3779B9u * (uint32_t)(fish_idx + 1))
                        ^ (uint32_t)(uint64_t)(t->clock * 977.0);
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return (float)(x & 0xffffff) / 16777215.0f;
}

goal_t advisor_rules(const tank_t *t, int fish_idx, bool request) {
    const fish_t *f = &t->fish[fish_idx];
    if (!request) return f->goal;            /* rules are synchronous: polls no-op */
    float food_d;
    int food_i = tank_nearest_food(t, f, &food_d);
    float friend_d;
    tank_nearest_friend(t, fish_idx, &friend_d);
    float r = frand(t, fish_idx);

    goal_t g = f->goal;                      /* default: keep current */
    g.confidence = 1.0f; g.runner_up = GOAL_COUNT;   /* rules are never torn */
    if (f->goal_age < 2.5f && f->goal.id != GOAL_FLEE_SHADOW) return g;

    if (f->energy < 2.5f || f->stress > 7.5f ||
               (f->lazy > 0.55f && f->hunger < 5 && r < 0.35f) ||
               (t->night && f->hunger < 6 && r < 0.5f)) {
        g.id = GOAL_REST; g.urgency = f->energy < 1.5f ? 7 : 4;
    } else if (f->hunger > 6.3f && food_i >= 0) {
        g.id = GOAL_SEEK_FOOD; g.urgency = f->hunger > 8.5f ? 9 : 6;
    } else if (f->sociable > 0.62f && friend_d > 60 && r < 0.36f) {
        g.id = GOAL_FOLLOW_FRIEND; g.urgency = 4;
    } else if (f->curiosity > 6 && r < 0.32f) {
        g.id = (r < 0.16f) ? GOAL_VISIT_BUBBLES : GOAL_INSPECT_REEF; g.urgency = 3;
    } else if (f->bold > 0.75f && f->energy > 6 && r < 0.2f) {
        g.id = GOAL_DART_PLAY; g.urgency = 5;
    } else if (f->goal_age > 5.2f + r * 4.8f) {
        g.id = GOAL_EXPLORE; g.urgency = 3;
    }
    return g;
}
