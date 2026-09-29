#ifndef EM_INDICATOR_H
#define EM_INDICATOR_H

enum indicator_pattern_e {
    INDICATOR_CONNECT,
    INDICATOR_WARN,
    INDICATOR_PRINT_START,
};

void indicator_init();
void indicator_beep(indicator_pattern_e pattern);
void indicator_led(indicator_pattern_e pattern);

#endif
