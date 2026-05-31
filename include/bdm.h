#ifndef BDM_H
#define BDM_H

#include <stdint.h>

// BDM pin mappings
#define BDM_DSCLK_PIN 0 // PC0
#define BDM_DSI_PIN 3   // PC3
#define BDM_DSO_PIN 6   // PD6

// Operand Size
#define SIZE_BYTE 0b00000000
#define SIZE_WORD 0b01000000
#define SIZE_LONG 0b10000000
#define SIZE_RESERVED 0b11000000

// Read/Write Memory Location commands
#define BDM_READ 0b0001100100000000
#define BDM_WRITE 0b0001100000000000

// Specific forms:
//  Word write: BDM_WRITE|SIZE_WORD
//  Word read:  BDM_READ |SIZE_WORD
//  Long  write: BDM_WRITE|SIZE_LONG
//  Long  read:  BDM_READ |SIZE_LONG

// Other BDM commands
#define BDM_RAREG 0b0010000110000000 // Read A Register 
#define BDM_RDREG 0b0010000110000000 // Read D Register
#define BDM_WAREG 0b0010000010000000 // Write A Register
#define BDM_WDREG 0b0010000010000000 // Write D Register
#define BDM_RSREG 0b0010010010000000 // Read System Register
#define BDM_WSREG 0b0010010010000000 // Write System Register
#define BDM_DUMP 0b0001110100000000 // Dump Memory Block
#define BDM_FILL 0b0001110000000000 // Fill Memory Block 
#define BDM_GO 0b0000110000000000 // Resume Execution
#define BDM_CALL 0b0000100000000000 // Patch User Code 
#define BDM_RST 0b0000010000000000 // Reset Peripherals
#define BDM_NOP 0b0000000000000000 // No Operation

// System register selects (used with RSREG/WSREG)
#define BDM_RPC 0b0000
#define BDMPCC 0b0001
#define RSR 0b1011
#define USP 0b1100
#define SSP 0b1101
#define SFC 0b1110
#define DFC 0b1111
#define ATEMP 0b1000
#define FAR 0b1001
#define VBR 0b1010

// High-level BDM memory operations
void bdm_write_word(uint32_t addr, uint16_t data);
void bdm_write_long(uint32_t addr, uint32_t data);

// Write a CPU32 data/address register over BDM (cmd = BDM_WXREG | reg)
void bdm_write_reg(uint16_t cmd, uint32_t data);

// Read target memory (bdm_read_word seeds the address, bdm_dump_word continues)
uint16_t bdm_read_word(uint32_t addr);
uint16_t bdm_dump_word(void);

// Write a CPU32 system register (sel = BDM_RPC / BDMPCC / RSR / SSP / SFC / DFC)
void bdm_write_sysreg(uint8_t sel, uint32_t data);

// Read a CPU32 register over BDM (cmd = BDM_RDREG/RAREG/RSREG | sel)
uint32_t bdm_read_reg(uint16_t cmd);

// Resume target execution (NOP + GO)
void bdm_go(void);

#endif // BDM_H
