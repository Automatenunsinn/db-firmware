#include <avr/io.h>
#include <util/delay.h>
#include <stdbool.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>

#include "rtc.h"
#include "bdm.h"

#include "mc68331.h"

/* 
 * PIN MAPPINGS
 * ----------------------------------
 * RTC4543SB:
 *   CLK:  PB5, DATA: PC4, CE: PB1, WR: PB3
 *
 * TARGET (BDM Device):
 *   DSCLK: PC0, DSI: PC3, DSO: PD6
 *
 * INTRUSION:
 *   PROBE: PB1 (Pulsed), SENSE: PD5 (T1 Input)
 */

volatile uint8_t g_state = 0;

// ============================================================================
// RTC and Intrusion Logic
// ============================================================================

bool check_door_intrusion() {
    // PD2 is the door sensor/intrusion pin
    return (PIND & (1 << PD2)) ? true : false;
}

// ============================================================================
// ADC and Verification
// ============================================================================

uint8_t __attribute__((noinline)) read_adc(void) {
    ADMUX = 0xE2;                        // ADC2, 1.1V ref
    ADCSRA |= (1 << ADSC);
    while (!(ADCSRA & (1 << ADIF)));
    return ADCH;
}

uint8_t verify_voltages(uint8_t mode) {
    uint8_t bat = read_adc();
    if (mode == 0) {
        return (bat >= 180) ? 1 : 0;
    }

    DDRD |= (1 << 0);
    PORTD |= (1 << 0);

    ADMUX = 0xC7;
    ADCSRA |= (1 << ADSC);
    while (!(ADCSRA & (1 << ADIF)));
    uint16_t adc7 = ADC;

    PORTD &= ~(1 << 0);
    DDRD &= ~(1 << 0);

    if (bat < 180) return 0;

    adc7 -= 660;
    if (adc7 >= 249) {
        return 0;
    }

    return 1;
}

// ============================================================================
// Target MC68331 initialization
// ============================================================================

uint8_t target_init(uint8_t mode) {
    if (!verify_voltages(mode)) {
        return 0;
    }
    
    if (mode == 0) return 1;

    // Full initialization
    // Initialize CPU32 core registers
    bdm_write_word(SIMCR,   0x40CF);

    bdm_write_word(SYNCR,   0x7F03);
    bdm_write_word(SYPCR,   0x004C);
    
    // Configure chip selects
    bdm_write_word(CSPAR0,  0x3FFF);
    bdm_write_word(CSPAR1,  0x03FD);
    bdm_write_word(CSBARBT, 0x0007);
    bdm_write_word(CSORBT,  0x6C70);
    bdm_write_word(CSBAR0,  0x0007);
    bdm_write_word(CSOR0,   0x7470);
    bdm_write_word(CSBAR1,  0x0007);
    bdm_write_word(CSOR1,   0x7070);
    bdm_write_word(CSBAR2,  0x0007);
    bdm_write_word(CSOR2,   0x7070);
    
    // Initial vectors
    bdm_write_long(INITIAL_STACK_POINTER, 0x00000010);
    bdm_write_long(RESET_VECTOR, 0x00000408);
    for (uint32_t addr = 0x08; addr < 0x20; addr += 4) {
        bdm_write_long(addr, 0x00000FFE);
    }

    _delay_ms(20);
    
    // Load EEPROM contents to target RAM
    // Target address 0x3C0 is where it expects the code
    for (uint16_t addr = 0x0000; addr < 0x0040; addr += 2) {
        while (EECR & (1 << EEPE));
        uint16_t data = eeprom_read_word((uint16_t*)addr);
        uint16_t swapped = (data << 8) | (data >> 8);
        bdm_write_word(0x03C0 + addr, swapped);
    }
    
    // Final Reset Vector
    bdm_write_long(RESET_VECTOR, 0x00000FC0);

    // ------------------------------------------------------------------
    // Read back the freshly loaded RAM and checksum it to confirm the
    // transfer succeeded. Seed with a single BDM_READ,
    // then stream the rest with the block DUMP command.
    // ------------------------------------------------------------------
    uint16_t checksum = bdm_read_word(0x03C0);
    for (uint16_t addr = 0x03C2; addr < 0x0400; addr += 2) {
        checksum += bdm_dump_word();
    }
    if (checksum == 0) {
        return 0;                               // nothing loaded -> fail
    }

    // ------------------------------------------------------------------
    // Push the recorded "door opened while powered off" history into the
    // target CPU32 registers so the MC68331 firmware can read it back.
    // Two fixed setup longs, then D2..D7 via WDREG.
    // ------------------------------------------------------------------
    bdm_write_long(CHK_INSTRUCTION_VECTOR, 0x00450303);
    bdm_write_long(TRAPV_INSTRUCTION_VECTOR, 0xC0CAC01A);

    bdm_write_reg(BDM_WDREG | 2, 0x5F72D920);   // D2 (marker / constant)
    bdm_write_reg(BDM_WDREG | 3, 0xD27B7159);   // D3 (marker / constant)
    bdm_write_reg(BDM_WDREG | 4, m68k_d4);      // D4 = history slot
    bdm_write_reg(BDM_WDREG | 5, m68k_d5);      // D5 = history slot
    bdm_write_reg(BDM_WDREG | 6, m68k_d6);      // D6 = history slot
    bdm_write_reg(BDM_WDREG | 7, m68k_d7);      // D7 = history slot

    // ------------------------------------------------------------------
    // Program the CPU32 system registers, then resume the target
    // ------------------------------------------------------------------
    bdm_write_sysreg(BDM_RPC, 0x00000408);      // program counter
    bdm_write_sysreg(BDMPCC,  0x00000000);      // PC correction cache
    bdm_write_sysreg(RSR,     0x00000000);      // return stack
    bdm_write_sysreg(SSP,     0x00000010);      // supervisor stack pointer
    bdm_write_sysreg(SFC,     0x00000005);      // source function code
    bdm_write_sysreg(DFC,     0x00000005);      // destination function code

    bdm_go();                                   // resume MC68331 execution

    return 1;
}

// ============================================================================
// Hardware initialization
// ============================================================================

void system_init() {
    // Disable watchdog (WDTCSR = WDCE|WDE, then 0)
    cli();
    __asm__ __volatile__("wdr");
    WDTCSR |= (1 << WDCE) | (1 << WDE);
    WDTCSR = 0x00;

    // SMCR: Power-save mode (SM1|SM0 = 0x06), preserve other bits
    SMCR = (SMCR & 0xF1) | (1 << SM1) | (1 << SM0);

    // TWCR: disable TWI
    TWCR &= ~(1 << TWEN);

    // ACSR: Analog Comparator Disable
    ACSR |= (1 << ACD);

    // Digital input disable
    DIDR0 = 0x2E;
    DIDR1 = 0x02;

    // PRR: Power Reduction
    PRR |= 0xAE;

    // ADCSRA: enable ADC, prescaler 64 (0x86)
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1);

    // Pin directions and initial states
    DDRB = 0x10; PORTB = 0x10;
    DDRC = 0x20; PORTC = 0x20;
    DDRD = 0x10; PORTD = 0x10;

    // Timer2: async mode, external crystal
    ASSR = (1 << EXCLK);
    ASSR |= (1 << AS2);
    TCCR2A = 0x00;
    TCCR2B = (1 << CS22) | (1 << CS21); // clk/256 (0x06)
    OCR2A = 0;
    TCNT2 = 0;
    while (ASSR & 0x15); // Wait for synchronization

    TIFR2 = 0x00;
    TIMSK2 = (1 << TOIE2);

    sei();
}

// ============================================================================
// Main entry point
// ============================================================================

int main() {
    system_init();
    
    g_state = 0;
    
    while (1) {
        if (g_state == 0) {
            // Power-save sleep
            SMCR |= (1 << SE);
            sleep_cpu();
            SMCR &= ~(1 << SE);
            
            // Wake up logic
            cli();
            TIMSK2 &= ~(1 << OCIE2A); // Disable T2 interrupt while checking
            sei();
            
            if (check_door_intrusion()) {
                // Door open/intrusion: wait for close?
                while (check_door_intrusion());
                g_state = 1;
            } else {
                // Door closed: perform periodic RTC check
                update_door_open();
            }
        }
        else if (g_state == 1) {
            if (target_init(0)) {
                g_state = 2;
            } else {
                // Back to sleep if battery low
                PORTB = 0x10; PORTC = 0x20; PORTD = 0x10;
                g_state = 0;
            }
        }
        else if (g_state == 2) {
            target_init(1);
            g_state = 3;
        }
        else if (g_state == 3) {
            // Runtime monitor: target is running, watch for exit / poll its state
            if (!verify_voltages(1)) {
                g_state = 0;                    // battery dropped -> power down
            } else if (!(PINC & (1 << PC0))) {
                g_state = 0;                    // DSCLK gone -> target lost
            } else if (PINB & (1 << PB2)) {
                // Host request: read the two CPU32 status registers back over
                // BDM, then re-arm the target.
                (void)bdm_read_reg(0x2581);     // RSREG (status / SR)
                (void)bdm_read_reg(0x2180);     // RDREG / RAREG snapshot
                g_state = 2;
            }
        }
        else {
            g_state = 0;
        }
    }
}

ISR(TIMER2_OVF_vect) {
    TCNT2 = 0x1E;
}
