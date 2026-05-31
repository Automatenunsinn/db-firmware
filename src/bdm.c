#include <avr/io.h>
#include <stdint.h>
#include <stdbool.h>

#include "bdm.h"

// ============================================================================
// BDM low-level bit-bang transport
// ============================================================================

#pragma GCC push_options
#pragma GCC optimize ("O3")

static uint16_t bdm_transfer(uint16_t data)
{
    // Shift register pre-aligned: start bit + 16 data bits sit at the top of a
    // 32-bit word and are clocked out MSB first
    uint32_t v = (uint32_t)data << 15;

    uint8_t i = 17;
    do
    {
        int32_t out = (int32_t)v >> 31;     // current MSB replicated -> DSI

        v <<= 1;
        if (PIND & (1 << BDM_DSO_PIN))      // sample DSO into LSB
            v |= 1;

        PORTC &= ~(1 << BDM_DSCLK_PIN);
        PORTC &= ~(1 << BDM_DSI_PIN);

        __asm__ __volatile__(
            "nop\n\tnop\n\tnop\n\tnop\n\t"
            "nop\n\tnop\n\tnop\n\tnop");

        if (out)
            PORTC |= (1 << BDM_DSI_PIN);
        PORTC |= (1 << BDM_DSCLK_PIN);

        __asm__ __volatile__("nop\n\tnop\n\tnop\n\tnop");
    } while (--i);

    return (uint16_t)(v >> 16);
}

#pragma GCC pop_options

// ============================================================================
// BDM high-level memory operations
// ============================================================================

// Clock out two consecutive 16-bit words
static void __attribute__((noinline)) bdm_send_long(uint16_t a, uint16_t b)
{
    bdm_transfer(a);
    bdm_transfer(b);
}

void bdm_write_long(uint32_t addr, uint32_t data)
{
    bdm_transfer(BDM_WRITE | SIZE_LONG);    // 0x1880
    bdm_send_long(addr >> 16, addr);
    bdm_send_long(data >> 16, data);
}

void bdm_write_word(uint32_t addr, uint16_t data)
{
    bdm_transfer(BDM_WRITE | SIZE_WORD);    // 0x1840
    bdm_send_long(addr >> 16, addr);
    bdm_transfer(data);
}

// Write one of the target's CPU32 registers (WXREG): clock out the command
// word, then the 32-bit value as two 16-bit words
void bdm_write_reg(uint16_t cmd, uint32_t data)
{
    bdm_transfer(cmd);
    bdm_send_long(data >> 16, data);
}

// Read a 16-bit word from target memory (0x1940 = BDM_READ|SIZE_WORD).
uint16_t bdm_read_word(uint32_t addr)
{
    bdm_transfer(BDM_READ | SIZE_WORD);
    bdm_send_long(addr >> 16, addr);
    return bdm_transfer(0x0000);
}

// Read the next sequential 16-bit word using the block DUMP command
// (0x1d40 = BDM_DUMP|SIZE_WORD). Must follow a BDM_READ to seed the address.
uint16_t bdm_dump_word(void)
{
    bdm_transfer(BDM_DUMP | SIZE_WORD);
    return bdm_transfer(0x0000);
}

// Write one of the CPU32 system registers (0x2480 | sel).
void bdm_write_sysreg(uint8_t sel, uint32_t data)
{
    bdm_transfer(BDM_WSREG | sel);
    bdm_send_long(data >> 16, data);
}

// Read one of the target's CPU32 registers (RDREG/RAREG/RSREG): clock out the
// command word, then read the 32-bit result back as two 16-bit words.
uint32_t bdm_read_reg(uint16_t cmd)
{
    bdm_transfer(cmd);
    uint16_t hi = bdm_transfer(0x0000);
    uint16_t lo = bdm_transfer(0x0000);
    return ((uint32_t)hi << 16) | lo;
}

// Bring the target out of debug mode and resume execution: NOP then GO.
void bdm_go(void)
{
    bdm_transfer(BDM_NOP);  // 0x0000
    bdm_transfer(BDM_GO);   // 0x0c00
}
