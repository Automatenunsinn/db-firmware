#include <avr/io.h>
#include <util/delay.h>
#include <stdbool.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>

#include "bdm.h"

#include "mc68331.h"

/*
 * PIN MAPPINGS
 *
 * TARGET (BDM Device):
 *   DSCLK: PC0, DSI: PC3, DSO: PD6
 */

volatile uint8_t g_state = 0;

// ============================================================================
// Target MC68331 initialization
// ============================================================================

uint8_t target_init(uint8_t mode)
{

    _delay_ms(20);

    return 1;
}

// ============================================================================
// Hardware initialization
// ============================================================================

void system_init()
{
    // SMCR: Sleep Enable=0, Sleep Mode=Power-save (011)
    // matching: in r24, 0x33; andi r24, 0xF1; ori r24, 0x06; out 0x33, r24
    SMCR = (SMCR & 0xF1) | 0x06;

    // PRR: Power Reduction Register (matching lds/ori/sts)
    PRR |= 0xAE;

    // Digital input disable
    DIDR0 = 0x2E;
    DIDR1 = 0x02;

    // Pin directions and initial states (matching am48.txt)
    DDRB = 0x10;
    PORTB = 0x10;
    DDRC = 0x20;
    PORTC = 0x20;
    DDRD = 0x10;
    PORTD = 0x10;

    // Timer1: stopped
    TCCR1A = 0;
    TCCR1B = 0;
    TCCR1C = 0;

    // Timer2: async mode, external 32.768 kHz crystal
    ASSR = (1 << EXCLK) | (1 << AS2);
    TCCR2A = 0x00;
    TCCR2B = (1 << CS22) | (1 << CS21);
    OCR2A = 30;
    TCNT2 = 0;

    // Wait for async registers to synchronize
    while (ASSR & ((1 << TCN2UB) | (1 << OCR2AUB) | (1 << TCR2BUB)))
        ;

    // Clear Timer2 Compare A flag and enable interrupt
    TIFR2 = (1 << OCF2A);
    TIMSK2 = (1 << OCIE2A);

    sei();
}

// ============================================================================
// Main entry point
// ============================================================================

int main()
{
    system_init();

    while (1)
    {
        if (g_state == 0)
        {
            // Sleep until Timer2 Compare A wakes the CPU
            set_sleep_mode(SLEEP_MODE_PWR_DOWN);
            sleep_mode();
            g_state = 1;
        }
        else if (g_state == 1)
        {
            if (target_init(0))
            {
                g_state = 2;
            }
            else
            {
                // Restore outputs and sleep
                PORTB = 0x10;
                PORTC = 0x20;
                PORTD = 0x10;
                g_state = 0;
            }
        }
        else if (g_state == 2)
        {
            target_init(1);
            g_state = 3;
        }
        else if (g_state == 3)
        {
            g_state = 0;
            // Long delay
            for (volatile uint32_t i = 1599999; i > 0; i--)
                ;
        }
        else
        {
            g_state = 0;
        }
    }
}

ISR(TIMER2_OVF_vect)
{
    TCNT2 = 0x1E;
}
