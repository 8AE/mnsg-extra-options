#ifndef EXTRA_OPTIONS_H
#define EXTRA_OPTIONS_H

extern unsigned char D_8015C608_15D208[];

#define EXTRA_OPTIONS_SAVE_HP_MAX_OFFSET (-0x28)

static inline int extra_options_save_is_loaded(void)
{
    return *(volatile signed int *)(
               (char *)D_8015C608_15D208 +
               EXTRA_OPTIONS_SAVE_HP_MAX_OFFSET) > 0;
}

#endif
