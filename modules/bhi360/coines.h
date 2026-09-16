#ifndef EVRT_BHI360_COINES_COMPAT_H
#define EVRT_BHI360_COINES_COMPAT_H

#define COINES_APP30_LED_G 0
#define COINES_APP31_LED_G 1
#define COINES_PIN_DIRECTION_OUT 0
#define COINES_PIN_VALUE_LOW 0
#define COINES_PIN_VALUE_HIGH 1

static inline int coines_set_pin_config(int pin, int direction, int value)
{
    (void)pin;
    (void)direction;
    (void)value;
    return 0;
}

#endif
