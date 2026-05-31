#ifndef RTC_H
#define RTC_H

#include <stdint.h>

// RTC RICOH R4543B pins
#define RTC_CLK_PIN     5   // PB5
#define RTC_DATA_PIN    4   // PC4
#define RTC_CE_PIN      1   // PB1
#define RTC_WR_PIN      3   // PB3

#define BIN2BCD(x) ((((x) / 10) << 4) | ((x) % 10))
#define BCD2BIN(x) ((((x) >> 4) * 10) + ((x) & 0x0F))

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t weekday;
    uint8_t days;
    uint8_t month;
    uint8_t year;
} rtc_time_s;

// global variables for passing logged "door opened when machine was off" times
extern uint32_t m68k_d4, m68k_d5, m68k_d6, m68k_d7;

uint8_t rtc4543_read_bits(uint8_t bits_to_read);
void rtc4543_read_date(rtc_time_s* time);

void update_door_open(void);

#endif // RTC_H
