#include <avr/io.h>
#include <avr/cpufunc.h>
#include <stdint.h>

#include "rtc.h"

// global variables for passing logged "door opened when machine was off" times
uint32_t m68k_d4, m68k_d5, m68k_d6, m68k_d7;

#pragma GCC push_options // save all current GCC compiler settings
#pragma GCC optimize ("O3") // enable highest optimization option for fastest code

// bitbang function for reading the R4543B RTC (am48.txt 0x56)
uint8_t rtc4543_read_bits(uint8_t bits_to_read)
{
    uint8_t data = 0;

    uint8_t bits = bits_to_read > 8 ? 8 : bits_to_read;

    for (uint8_t i = 0; i < bits; ++i)
    {
        PORTB |= (1 << RTC_CLK_PIN);

       _NOP(); _NOP(); _NOP(); _NOP();
       _NOP(); _NOP(); _NOP(); _NOP();
       _NOP(); _NOP(); _NOP(); _NOP();
       _NOP(); _NOP(); _NOP(); _NOP();

        data |= (PINC & (1 << RTC_DATA_PIN)) << i;

        PORTB &= ~(1 << RTC_CLK_PIN);

       _NOP(); _NOP(); _NOP(); _NOP();
       _NOP(); _NOP(); _NOP(); _NOP();
       _NOP(); _NOP(); _NOP(); _NOP();
       _NOP(); _NOP(); _NOP(); _NOP();
    }

    return data;
}
#pragma GCC pop_options // restore all previously saved GCC compiler settings

// gets the time on the R4543B RTC (inlined into update_door_open)
void rtc4543_read_date(rtc_time_s* time)
{
    DDRB |= (1 << RTC_CE_PIN);
    DDRB |= (1 << RTC_WR_PIN);
    DDRB |= (1 << RTC_CLK_PIN);
    PORTB |= (1 << RTC_CE_PIN);

    time->seconds   = BCD2BIN(rtc4543_read_bits(8)) & 0x7F;
    time->minutes   = BCD2BIN(rtc4543_read_bits(8)) & 0x7F;
    time->hours     = BCD2BIN(rtc4543_read_bits(8)) & 0x3F;
    time->weekday   = BCD2BIN(rtc4543_read_bits(4)) & 0x07;
    time->days      = BCD2BIN(rtc4543_read_bits(8)) & 0x3F;
    time->month     = BCD2BIN(rtc4543_read_bits(8)) & 0x1F;
    time->year      = BCD2BIN(rtc4543_read_bits(8));

    PORTB &= ~(1 << RTC_CE_PIN);
    DDRB &= ~(1 << RTC_CE_PIN);
    DDRB &= ~(1 << RTC_WR_PIN);
    DDRB &= ~(1 << RTC_CLK_PIN);
}

// log time when service door got opened when machine is turned off (am48.txt 0xbe)
void update_door_open()
{
    // door opened
    rtc_time_s time_s = {0};
    rtc4543_read_date(&time_s);

    // contains current minute + bit 4 of month + bit 0 of year
    uint8_t r0 = (time_s.minutes & 0x7F) | (((time_s.month >> 4) & 1) << 6) | ((time_s.year >> 0) & 1) << 7;

    // contains current hour + bit 2 & 3 of month
    uint8_t r1 = (time_s.hours & 0x7F) | (((time_s.month >> 2) & 1) << 6) | (((time_s.month >> 3) & 1) << 7);

    // contains current day + bit 0 & 1 of month
    uint8_t r2 = (time_s.days & 0x7F) | (((time_s.month >> 0) & 1) << 6) | (((time_s.month >> 1) & 1) << 7);

    // check against previous r2 & r1
    if (r2 == (uint8_t)(m68k_d5 >> 8) || r1 != (uint8_t)(m68k_d5))
        return;

    // door open status is grouped in 3 byte pairs, only 5 last ones are saved in the registers
    // this cluster fuck of byte shifting moves all 3 old ones one position backwards and adds the new on the front
    // D7 = current year | fifth pair (R2 + R1 + R0)
    // D6 = fourth pair (R2 + R1 + R0) | third pair (R2)
    // D5 = third pair (R1 + R0) | second pair (R2 + R1)
    // D4 = second pair (R0) | first pair (R2 + R1 + R0)
    m68k_d7 = (((uint32_t)time_s.year) << 24) | ((m68k_d6 >> 24) << 16) | ((m68k_d6 >> 16) << 8) | (m68k_d6 >> 8);
    m68k_d6 = (m68k_d6 << 24) | ((m68k_d5 >> 24) << 16) | ((m68k_d5 >> 16) << 8) | (m68k_d5 >> 8);
    m68k_d5 = (m68k_d5 << 24) | ((m68k_d4 >> 8) << 16) | ((m68k_d4 >> 16) << 8) | (m68k_d4 >> 24);
    m68k_d4 = (m68k_d4 << 24) | (((uint32_t)r2) << 16) | (((uint32_t)r1) << 8) | r0;
}
