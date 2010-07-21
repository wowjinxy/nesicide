//    NESICIDE - an IDE for the 8-bit NES.
//    Copyright (C) 2009  Christopher S. Pow

//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.

//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.

//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "cnes6502.h"
#include "cnesppu.h"
#include "cnesrom.h"
#include "cnesapu.h"
#include "cnesio.h"

#include "cnesmappers.h"

#include "cnesicidecommon.h"

#include <QColor>

// Instructions with special handling needs.
#define ROL_ABS_X   0x3E
#define CLI_IMPLIED 0x58
#define BRK_IMPLIED 0x00

// CPU Registers
static CBitfieldData* tblCPUPCBitfields [] =
{
   new CBitfieldData("Instruction Pointer", 0, 16, "%04X", 0),
};

static CBitfieldData* tblCPUABitfields [] =
{
   new CBitfieldData("Accumulator", 0, 8, "%02X", 0),
};

static CBitfieldData* tblCPUXBitfields [] =
{
   new CBitfieldData("X Index", 0, 8, "%02X", 0),
};

static CBitfieldData* tblCPUYBitfields [] =
{
   new CBitfieldData("Y Index", 0, 8, "%02X", 0),
};

static CBitfieldData* tblCPUSPBitfields [] =
{
   new CBitfieldData("Stack Pointer", 0, 12, "%03X", 0),
};

static CBitfieldData* tblCPUFBitfields [] =
{
   new CBitfieldData("Negative", 7, 1, "%X", 2, "No", "Yes"),
   new CBitfieldData("Overflow", 6, 1, "%X", 2, "No", "Yes"),
   new CBitfieldData("Break", 4, 1, "%X", 2, "No", "Yes"),
   new CBitfieldData("Decimal Mode", 3, 1, "%X", 2, "No", "Yes"),
   new CBitfieldData("Interrupt", 2, 1, "%X", 2, "No", "Yes"),
   new CBitfieldData("Zero", 1, 1, "%X", 2, "No", "Yes"),
   new CBitfieldData("Carry", 0, 1, "%X", 2, "No", "Yes")
};

static CRegisterData* tblCPURegisters [] =
{
   new CRegisterData(0, "CPUPC", 1, tblCPUPCBitfields),
   new CRegisterData(1, "CPUA", 1, tblCPUABitfields),
   new CRegisterData(2, "CPUX", 1, tblCPUXBitfields),
   new CRegisterData(3, "CPUY", 1, tblCPUYBitfields),
   new CRegisterData(4, "CPUSP", 1, tblCPUSPBitfields),
   new CRegisterData(5, "CPUF", 7, tblCPUFBitfields)
};

// PPU Event breakpoints
bool cpuUndocumentedEvent(BreakpointInfo* pBreakpoint,int data)
{
   // This breakpoint is checked in the right place
   // so if this breakpoint is enabled it should always fire when called.
   return true;
}

bool cpuUndocumentedExactEvent(BreakpointInfo* pBreakpoint,int data)
{
   // If opcode executing is one specified, break...
   if ( pBreakpoint->item1 == data )
   {
      return true;
   }
   return false;
}

bool cpuExecuteExactEvent(BreakpointInfo* pBreakpoint,int data)
{
   // If opcode executing is one specified, break...
   if ( pBreakpoint->item1 == data )
   {
      return true;
   }
   return false;
}

bool cpuResetEvent(BreakpointInfo* pBreakpoint,int data)
{
   // This breakpoint is checked in the right place
   // so if this breakpoint is enabled it should always fire when called.
   return true;
}

bool cpuIRQEvent(BreakpointInfo* pBreakpoint,int data)
{
   // This breakpoint is checked in the right place
   // so if this breakpoint is enabled it should always fire when called.
   return true;
}

bool cpuNMIEvent(BreakpointInfo* pBreakpoint,int data)
{
   // This breakpoint is checked in the right place
   // so if this breakpoint is enabled it should always fire when called.
   return true;
}

static CBreakpointEventInfo* tblCPUEvents [] =
{
   new CBreakpointEventInfo("Specific Instruction Execution", cpuExecuteExactEvent, 1, "Break if opcode $%02X is executed", 16, "Opcode:"),
   new CBreakpointEventInfo("Any Undocumented Instruction Execution", cpuUndocumentedEvent, 0, "Break if any undocumented opcode is executed", 16),
   new CBreakpointEventInfo("Specific Undocumented Instruction Execution", cpuUndocumentedExactEvent, 1, "Break if undocumented opcode $%02X is executed", 16, "Opcode:"),
   new CBreakpointEventInfo("Reset", cpuResetEvent, 0, "Break if CPU is reset", 10),
   new CBreakpointEventInfo("IRQ", cpuIRQEvent, 0, "Break if CPU IRQ fires", 10),
   new CBreakpointEventInfo("NMI", cpuNMIEvent, 0, "Break if CPU NMI fires", 10)
};

bool            C6502::m_killed = false;              // KIL opcode not executed
bool            C6502::m_irqAsserted = false;
bool            C6502::m_irqPending = false;
bool            C6502::m_nmiAsserted = false;
bool            C6502::m_nmiPending = false;
unsigned char   C6502::m_6502memory [] = { 0, };
unsigned char   C6502::m_RAMopcodeMask [ MEM_2KB ] = { 0, };
char*           C6502::m_RAMdisassembly [ MEM_2KB ] = { 0, };
unsigned short  C6502::m_RAMsloc2addr [ MEM_2KB ] = { 0, };
unsigned short  C6502::m_RAMaddr2sloc [ MEM_2KB ] = { 0, };
unsigned int    C6502::m_RAMsloc = 0;
unsigned char   C6502::m_a = 0x00;
unsigned char   C6502::m_x = 0x00;
unsigned char   C6502::m_y = 0x00;
unsigned char   C6502::m_f = FLAG_MISC;
unsigned short  C6502::m_pc = VECTOR_RESET;
unsigned int    C6502::m_ea = 0xFFFFFFFF;
UINT            C6502::m_pcGoto = 0xFFFFFFFF;
unsigned char   C6502::m_sp = 0x00;

CMarker         C6502::m_marker;

CCodeDataLogger C6502::m_logger ( MEM_32KB, MASK_32KB );
unsigned int    C6502::m_cycles = 0;
int             C6502::m_curCycles = 0;

int             C6502::amode;
unsigned char*  C6502::data = NULL;
unsigned char   C6502::opcodeData [ 4 ]; // 3 opcode bytes and 1 byte for operand return data [extra cycle]
struct _C6502_opcode* C6502::pOpcodeStruct = NULL;
int             C6502::opcodeSize;
TracerInfo*     C6502::pDisassemblySample = NULL;
bool            C6502::m_sync = false;
bool            C6502::m_write = false;
char            C6502::m_phase = 0;

CRegisterData** C6502::m_tblRegisters = tblCPURegisters;
int             C6502::m_numRegisters = NUM_CPU_REGISTERS;

CBreakpointEventInfo** C6502::m_tblBreakpointEvents = tblCPUEvents;
int                    C6502::m_numBreakpointEvents = NUM_CPU_EVENTS;

char*          C6502::m_pCodeDataLoggerInspectorTV = NULL;

char*          C6502::m_pExecutionVisualizerInspectorTV = NULL;

static int opcode_size [ NUM_ADDRESSING_MODES ] =
{
   1, // AM_IMPLIED
   2, // AM_IMMEDIATE
   3, // AM_ABSOLUTE
   2, // AM_ZEROPAGE
   1, // AM_ACCUMULATOR
   3, // AM_ABSOLUTE_INDEXED_X
   3, // AM_ABSOLUTE_INDEXED_Y
   2, // AM_ZEROPAGE_INDEXED_X
   2, // AM_ZEROPAGE_INDEXED_Y
   3, // AM_INDIRECT
   2, // AM_PREINDEXED_INDIRECT
   2, // AM_POSTINDEXED_INDIRECT
   2  // AM_RELATIVE
};

static const char* operandFmt [ NUM_ADDRESSING_MODES ] =
{
   "", // AM_IMPLIED
   " #$%02X", // AM_IMMEDIATE
   " $%02X%02X", // AM_ABSOLUTE
   " $%02X", // AM_ZEROPAGE
   "", // AM_ACCUMULATOR
   " $%02X%02X, X", // AM_ABSOLUTE_INDEXED_X
   " $%02X%02X, Y", // AM_ABSOLUTE_INDEXED_Y
   " $%02X, X", // AM_ZEROPAGE_INDEXED_X
   " $%02X, Y", // AM_ZEROPAGE_INDEXED_Y
   " ($%02X%02X)", // AM_INDIRECT
   " ($%02X, X)", // AM_PREINDEXED_INDIRECT
   " ($%02X), Y", // AM_POSTINDEXED_INDIRECT
   " $%02X"  // AM_RELATIVE
};

static C6502_opcode m_6502opcode [ 256 ] =
{
   { 0x00, "BRK", C6502::BRK, AM_IMPLIED, 7, true, true }, // BRK
   { 0x01, "ORA", C6502::ORA, AM_PREINDEXED_INDIRECT, 6, true, true }, // ORA - (Indirect,X)
   { 0x02, "KIL", C6502::KIL, AM_IMPLIED, 0, false, true }, // KIL - Implied (processor lock up!)
   { 0x03, "ASO", C6502::ASO, AM_PREINDEXED_INDIRECT, 8, false, true }, // ASO - (Indirect,X) (undocumented)
   { 0x04, "DOP", C6502::DOP, AM_ZEROPAGE, 3, false, true }, // DOP (undocumented)
   { 0x05, "ORA", C6502::ORA, AM_ZEROPAGE, 3, true, true }, // ORA - Zero Page
   { 0x06, "ASL", C6502::ASL, AM_ZEROPAGE, 5, true, true }, // ASL - Zero Page
   { 0x07, "ASO", C6502::ASO, AM_ZEROPAGE, 5, false, true }, // ASO - Zero Page (undocumented)
   { 0x08, "PHP", C6502::PHP, AM_IMPLIED, 3, true, true }, // PHP
   { 0x09, "ORA", C6502::ORA, AM_IMMEDIATE, 2, true, true }, // ORA - Immediate
   { 0x0A, "ASL", C6502::ASL, AM_ACCUMULATOR, 2, true, true }, // ASL - Accumulator
   { 0x0B, "ANC", C6502::ANC, AM_IMMEDIATE, 2, false, true }, // ANC - Immediate (undocumented)
   { 0x0C, "TOP", C6502::TOP, AM_ABSOLUTE, 4, false, true }, // TOP (undocumented)
   { 0x0D, "ORA", C6502::ORA, AM_ABSOLUTE, 4, true, true }, // ORA - Absolute
   { 0x0E, "ASL", C6502::ASL, AM_ABSOLUTE, 6, true, true }, // ASL - Absolute
   { 0x0F, "ASO", C6502::ASO, AM_ABSOLUTE, 6, false, true }, // ASO - Absolute (undocumented)
   { 0x10, "BPL", C6502::BPL, AM_RELATIVE, 2, true, true }, // BPL
   { 0x11, "ORA", C6502::ORA, AM_POSTINDEXED_INDIRECT, 5, true, false }, // ORA - (Indirect),Y
   { 0x12, "KIL", C6502::KIL, AM_IMPLIED, 0, false, true }, // KIL - Implied (processor lock up!)
   { 0x13, "ASO", C6502::ASO, AM_POSTINDEXED_INDIRECT, 8, false, true }, // ASO - (Indirect),Y (undocumented)
   { 0x14, "DOP", C6502::DOP, AM_ZEROPAGE_INDEXED_X, 4, false, true }, // DOP (undocumented)
   { 0x15, "ORA", C6502::ORA, AM_ZEROPAGE_INDEXED_X, 4, true, true }, // ORA - Zero Page,X
   { 0x16, "ASL", C6502::ASL, AM_ZEROPAGE_INDEXED_X, 6, true, true }, // ASL - Zero Page,X
   { 0x17, "ASO", C6502::ASO, AM_ZEROPAGE_INDEXED_X, 6, false, true }, // ASO - Zero Page,X (undocumented)
   { 0x18, "CLC", C6502::CLC, AM_IMPLIED, 2, true, true }, // CLC
   { 0x19, "ORA", C6502::ORA, AM_ABSOLUTE_INDEXED_Y, 4, true, false }, // ORA - Absolute,Y
   { 0x1A, "NOP", C6502::NOP, AM_IMPLIED, 2, false, true }, // NOP (undocumented)
   { 0x1B, "ASO", C6502::ASO, AM_ABSOLUTE_INDEXED_Y, 7, false, true }, // ASO - Absolute,Y (undocumented)
   { 0x1C, "TOP", C6502::TOP, AM_ABSOLUTE_INDEXED_X, 4, false, false }, // TOP (undocumented)
   { 0x1D, "ORA", C6502::ORA, AM_ABSOLUTE_INDEXED_X, 4, true, false }, // ORA - Absolute,X
   { 0x1E, "ASL", C6502::ASL, AM_ABSOLUTE_INDEXED_X, 7, true, true }, // ASL - Absolute,X
   { 0x1F, "ASO", C6502::ASO, AM_ABSOLUTE_INDEXED_X, 7, false, true }, // ASO - Absolute,X (undocumented)
   { 0x20, "JSR", C6502::JSR, AM_ABSOLUTE, 6, true, true }, // JSR
   { 0x21, "AND", C6502::AND, AM_PREINDEXED_INDIRECT, 6, true, true }, // AND - (Indirect,X)
   { 0x22, "KIL", C6502::KIL, AM_IMPLIED, 0, false, true }, // KIL - Implied (processor lock up!)
   { 0x23, "RLA", C6502::RLA, AM_PREINDEXED_INDIRECT, 8, false, true }, // RLA - (Indirect,X) (undocumented)
   { 0x24, "BIT", C6502::BIT, AM_ZEROPAGE, 3, true, true }, // BIT - Zero Page
   { 0x25, "AND", C6502::AND, AM_ZEROPAGE, 3, true, true }, // AND - Zero Page
   { 0x26, "ROL", C6502::ROL, AM_ZEROPAGE, 5, true, true }, // ROL - Zero Page
   { 0x27, "RLA", C6502::RLA, AM_ZEROPAGE, 5, false, true }, // RLA - Zero Page (undocumented)
   { 0x28, "PLP", C6502::PLP, AM_IMPLIED, 4, true, true }, // PLP
   { 0x29, "AND", C6502::AND, AM_IMMEDIATE, 2, true, true }, // AND - Immediate
   { 0x2A, "ROL", C6502::ROL, AM_ACCUMULATOR, 2, true, true }, // ROL - Accumulator
   { 0x2B, "ANC", C6502::ANC, AM_IMMEDIATE, 2, false, true }, // ANC - Immediate (undocumented)
   { 0x2C, "BIT", C6502::BIT, AM_ABSOLUTE, 4, true, true }, // BIT - Absolute
   { 0x2D, "AND", C6502::AND, AM_ABSOLUTE, 4, true, true }, // AND - Absolute
   { 0x2E, "ROL", C6502::ROL, AM_ABSOLUTE, 6, true, true }, // ROL - Absolute
   { 0x2F, "RLA", C6502::RLA, AM_ABSOLUTE, 6, false, true }, // RLA - Absolute (undocumented)
   { 0x30, "BMI", C6502::BMI, AM_RELATIVE, 2, true, true }, // BMI
   { 0x31, "AND", C6502::AND, AM_POSTINDEXED_INDIRECT, 5, true, false }, // AND - (Indirect),Y
   { 0x32, "KIL", C6502::KIL, AM_IMPLIED, 0, false, true }, // KIL - Implied (processor lock up!)
   { 0x33, "RLA", C6502::RLA, AM_POSTINDEXED_INDIRECT, 8, false, true }, // RLA - (Indirect),Y (undocumented)
   { 0x34, "DOP", C6502::DOP, AM_ZEROPAGE_INDEXED_X, 4, false, true }, // DOP (undocumented)
   { 0x35, "AND", C6502::AND, AM_ZEROPAGE_INDEXED_X, 4, true, true }, // AND - Zero Page,X
   { 0x36, "ROL", C6502::ROL, AM_ZEROPAGE_INDEXED_X, 6, true, true }, // ROL - Zero Page,X
   { 0x37, "RLA", C6502::RLA, AM_ZEROPAGE_INDEXED_X, 6, false, true }, // RLA - Zero Page,X (undocumented)
   { 0x38, "SEC", C6502::SEC, AM_IMPLIED, 2, true, true }, // SEC
   { 0x39, "AND", C6502::AND, AM_ABSOLUTE_INDEXED_Y, 4, true, false }, // AND - Absolute,Y
   { 0x3A, "NOP", C6502::NOP, AM_IMPLIED, 2, false, true }, // NOP (undocumented)
   { 0x3B, "RLA", C6502::RLA, AM_ABSOLUTE_INDEXED_Y, 7, false, true }, // RLA - Absolute,Y (undocumented)
   { 0x3C, "TOP", C6502::TOP, AM_ABSOLUTE_INDEXED_X, 4, false, false }, // TOP (undocumented)
   { 0x3D, "AND", C6502::AND, AM_ABSOLUTE_INDEXED_X, 4, true, false }, // AND - Absolute,X
   { 0x3E, "ROL", C6502::ROL, AM_ABSOLUTE_INDEXED_X, 7, true, false }, // ROL - Absolute,X
   { 0x3F, "RLA", C6502::RLA, AM_ABSOLUTE_INDEXED_X, 7, false, true }, // RLA - Absolute,X (undocumented)
   { 0x40, "RTI", C6502::RTI, AM_IMPLIED, 6, true, true }, // RTI
   { 0x41, "EOR", C6502::EOR, AM_PREINDEXED_INDIRECT, 6, true, true }, // EOR - (Indirect,X)
   { 0x42, "KIL", C6502::KIL, AM_IMPLIED, 0, false, true }, // KIL - Implied (processor lock up!)
   { 0x43, "LSE", C6502::LSE, AM_PREINDEXED_INDIRECT, 8, false, true }, // LSE - (Indirect,X) (undocumented)
   { 0x44, "DOP", C6502::DOP, AM_ZEROPAGE, 3, false, true }, // DOP (undocumented)
   { 0x45, "EOR", C6502::EOR, AM_ZEROPAGE, 3, true, true }, // EOR - Zero Page
   { 0x46, "LSR", C6502::LSR, AM_ZEROPAGE, 5, true, true }, // LSR - Zero Page
   { 0x47, "LSE", C6502::LSE, AM_ZEROPAGE, 5, false, true }, // LSE - Zero Page (undocumented)
   { 0x48, "PHA", C6502::PHA, AM_IMPLIED, 3, true, true }, // PHA
   { 0x49, "EOR", C6502::EOR, AM_IMMEDIATE, 2, true, true }, // EOR - Immediate
   { 0x4A, "LSR", C6502::LSR, AM_ACCUMULATOR, 2, true, true }, // LSR - Accumulator
   { 0x4B, "ALR", C6502::ALR, AM_IMMEDIATE, 2, false, true }, // ALR - Immediate (undocumented)
   { 0x4C, "JMP", C6502::JMP, AM_ABSOLUTE, 3, true, true }, // JMP - Absolute
   { 0x4D, "EOR", C6502::EOR, AM_ABSOLUTE, 4, true, true }, // EOR - Absolute
   { 0x4E, "LSR", C6502::LSR, AM_ABSOLUTE, 6, true, true }, // LSR - Absolute
   { 0x4F, "LSE", C6502::LSE, AM_ABSOLUTE, 6, false, true }, // LSE - Absolute (undocumented)
   { 0x50, "BVC", C6502::BVC, AM_RELATIVE, 2, true, true }, // BVC
   { 0x51, "EOR", C6502::EOR, AM_POSTINDEXED_INDIRECT, 5, true, false }, // EOR - (Indirect),Y
   { 0x52, "KIL", C6502::KIL, AM_IMPLIED, 0, false, true }, // KIL - Implied (processor lock up!)
   { 0x53, "LSE", C6502::LSE, AM_POSTINDEXED_INDIRECT, 8, false, true }, // LSE - (Indirect),Y
   { 0x54, "DOP", C6502::DOP, AM_ZEROPAGE_INDEXED_X, 4, false, true }, // DOP (undocumented)
   { 0x55, "EOR", C6502::EOR, AM_ZEROPAGE_INDEXED_X, 4, true, true }, // EOR - Zero Page,X
   { 0x56, "LSR", C6502::LSR, AM_ZEROPAGE_INDEXED_X, 6, true, true }, // LSR - Zero Page,X
   { 0x57, "LSE", C6502::LSE, AM_ZEROPAGE_INDEXED_X, 6, false, true }, // LSE - Zero Page,X (undocumented)
   { 0x58, "CLI", C6502::CLI, AM_IMPLIED, 2, true, true }, // CLI
   { 0x59, "EOR", C6502::EOR, AM_ABSOLUTE_INDEXED_Y, 4, true, false }, // EOR - Absolute,Y
   { 0x5A, "NOP", C6502::NOP, AM_IMPLIED, 2, false, true }, // NOP (undocumented)
   { 0x5B, "LSE", C6502::LSE, AM_ABSOLUTE_INDEXED_Y, 7, false, true }, // LSE - Absolute,Y (undocumented)
   { 0x5C, "TOP", C6502::TOP, AM_ABSOLUTE_INDEXED_X, 4, false, false }, // TOP (undocumented)
   { 0x5D, "EOR", C6502::EOR, AM_ABSOLUTE_INDEXED_X, 4, true, false }, // EOR - Absolute,X
   { 0x5E, "LSR", C6502::LSR, AM_ABSOLUTE_INDEXED_X, 7, true, true }, // LSR - Absolute,X
   { 0x5F, "LSE", C6502::LSE, AM_ABSOLUTE_INDEXED_X, 7, false, true }, // LSE - Absolute,X (undocumented)
   { 0x60, "RTS", C6502::RTS, AM_IMPLIED, 6, true, true }, // RTS
   { 0x61, "ADC", C6502::ADC, AM_PREINDEXED_INDIRECT, 6, true, true }, // ADC - (Indirect,X)
   { 0x62, "KIL", C6502::KIL, AM_IMPLIED, 0, false, true }, // KIL - Implied (processor lock up!)
   { 0x63, "RRA", C6502::RRA, AM_PREINDEXED_INDIRECT, 8, false, true }, // RRA - (Indirect,X) (undocumented)
   { 0x64, "DOP", C6502::DOP, AM_ZEROPAGE, 3, false, true }, // DOP (undocumented)
   { 0x65, "ADC", C6502::ADC, AM_ZEROPAGE, 3, true, true }, // ADC - Zero Page
   { 0x66, "ROR", C6502::ROR, AM_ZEROPAGE, 5, true, true }, // ROR - Zero Page
   { 0x67, "RRA", C6502::RRA, AM_ZEROPAGE, 5, false, true }, // RRA - Zero Page (undocumented)
   { 0x68, "PLA", C6502::PLA, AM_IMPLIED, 4, true, true }, // PLA
   { 0x69, "ADC", C6502::ADC, AM_IMMEDIATE, 2, true, true }, // ADC - Immediate
   { 0x6A, "ROR", C6502::ROR, AM_ACCUMULATOR, 2, true, true }, // ROR - Accumulator
   { 0x6B, "ARR", C6502::ARR, AM_IMMEDIATE, 2, false, true }, // ARR - Immediate (undocumented)
   { 0x6C, "JMP", C6502::JMP, AM_INDIRECT, 5, true, true }, // JMP - Indirect
   { 0x6D, "ADC", C6502::ADC, AM_ABSOLUTE, 4, true, true }, // ADC - Absolute
   { 0x6E, "ROR", C6502::ROR, AM_ABSOLUTE, 6, true, true }, // ROR - Absolute
   { 0x6F, "RRA", C6502::RRA, AM_ABSOLUTE, 6, false, true }, // RRA - Absolute (undocumented)
   { 0x70, "BVS", C6502::BVS, AM_RELATIVE, 2, true, true }, // BVS
   { 0x71, "ADC", C6502::ADC, AM_POSTINDEXED_INDIRECT, 5, true, false }, // ADC - (Indirect),Y
   { 0x72, "KIL", C6502::KIL, AM_IMPLIED, 0, false, true }, // KIL - Implied (processor lock up!)
   { 0x73, "RRA", C6502::RRA, AM_POSTINDEXED_INDIRECT, 8, false, true }, // RRA - (Indirect),Y (undocumented)
   { 0x74, "DOP", C6502::DOP, AM_ZEROPAGE_INDEXED_X, 4, false, true }, // DOP (undocumented)
   { 0x75, "ADC", C6502::ADC, AM_ZEROPAGE_INDEXED_X, 4, true, true }, // ADC - Zero Page,X
   { 0x76, "ROR", C6502::ROR, AM_ZEROPAGE_INDEXED_X, 6, true, true }, // ROR - Zero Page,X
   { 0x77, "RRA", C6502::RRA, AM_ZEROPAGE_INDEXED_X, 6, false, true }, // RRA - Zero Page,X (undocumented)
   { 0x78, "SEI", C6502::SEI, AM_IMPLIED, 2, true, true }, // SEI
   { 0x79, "ADC", C6502::ADC, AM_ABSOLUTE_INDEXED_Y, 4, true, false }, // ADC - Absolute,Y
   { 0x7A, "NOP", C6502::NOP, AM_IMPLIED, 2, false, true }, // NOP (undocumented)
   { 0x7B, "RRA", C6502::RRA, AM_ABSOLUTE_INDEXED_Y, 7, false, true }, // RRA - Absolute,Y (undocumented)
   { 0x7C, "TOP", C6502::TOP, AM_ABSOLUTE_INDEXED_X, 4, false, false }, // TOP (undocumented)
   { 0x7D, "ADC", C6502::ADC, AM_ABSOLUTE_INDEXED_X, 4, true, false }, // ADC - Absolute,X
   { 0x7E, "ROR", C6502::ROR, AM_ABSOLUTE_INDEXED_X, 7, true, true }, // ROR - Absolute,X
   { 0x7F, "RRA", C6502::RRA, AM_ABSOLUTE_INDEXED_X, 7, false, true }, // RRA - Absolute,X (undocumented)
   { 0x80, "DOP", C6502::DOP, AM_IMMEDIATE, 2, false, false }, // DOP (undocumented)
   { 0x81, "STA", C6502::STA, AM_PREINDEXED_INDIRECT, 6, true, true }, // STA - (Indirect,X)
   { 0x82, "DOP", C6502::DOP, AM_IMMEDIATE, 2, false, true }, // DOP (undocumented)
   { 0x83, "AXS", C6502::AXS, AM_PREINDEXED_INDIRECT, 6, false, true }, // AXS - (Indirect,X) (undocumented)
   { 0x84, "STY", C6502::STY, AM_ZEROPAGE, 3, true, true }, // STY - Zero Page
   { 0x85, "STA", C6502::STA, AM_ZEROPAGE, 3, true, true }, // STA - Zero Page
   { 0x86, "STX", C6502::STX, AM_ZEROPAGE, 3, true, true }, // STX - Zero Page
   { 0x87, "AXS", C6502::AXS, AM_ZEROPAGE, 3, false, true }, // AXS - Zero Page (undocumented)
   { 0x88, "DEY", C6502::DEY, AM_IMPLIED, 2, true, true }, // DEY
   { 0x89, "DOP", C6502::DOP, AM_IMMEDIATE, 2, false, true }, // DOP (undocumented)
   { 0x8A, "TXA", C6502::TXA, AM_IMPLIED, 2, true, true }, // TXA
   { 0x8B, "XAA", C6502::XAA, AM_IMMEDIATE, 2, false, true }, // XAA - Immediate (undocumented)
   { 0x8C, "STY", C6502::STY, AM_ABSOLUTE, 4, true, true }, // STY - Absolute
   { 0x8D, "STA", C6502::STA, AM_ABSOLUTE, 4, true, true }, // STA - Absolute
   { 0x8E, "STX", C6502::STX, AM_ABSOLUTE, 4, true, true }, // STX - Absolute
   { 0x8F, "AXS", C6502::AXS, AM_ABSOLUTE, 4, false, true }, // AXS - Absolulte (undocumented)
   { 0x90, "BCC", C6502::BCC, AM_RELATIVE, 2, true, true }, // BCC
   { 0x91, "STA", C6502::STA, AM_POSTINDEXED_INDIRECT, 6, true, true }, // STA - (Indirect),Y
   { 0x92, "KIL", C6502::KIL, AM_IMPLIED, 0, false, true }, // KIL - Implied (processor lock up!)
   { 0x93, "AXA", C6502::AXA, AM_POSTINDEXED_INDIRECT, 6, false, false }, // AXA - (Indirect),Y
   { 0x94, "STY", C6502::STY, AM_ZEROPAGE_INDEXED_X, 4, true, true }, // STY - Zero Page,X
   { 0x95, "STA", C6502::STA, AM_ZEROPAGE_INDEXED_X, 4, true, true }, // STA - Zero Page,X
   { 0x96, "STX", C6502::STX, AM_ZEROPAGE_INDEXED_Y, 4, true, true }, // STX - Zero Page,Y
   { 0x97, "AXS", C6502::AXS, AM_ZEROPAGE_INDEXED_Y, 4, false, true }, // AXS - Zero Page,Y
   { 0x98, "TYA", C6502::TYA, AM_IMPLIED, 2, true, true }, // TYA
   { 0x99, "STA", C6502::STA, AM_ABSOLUTE_INDEXED_Y, 5, true, true }, // STA - Absolute,Y
   { 0x9A, "TXS", C6502::TXS, AM_IMPLIED, 2, true, true }, // TXS
   { 0x9B, "TAS", C6502::TAS, AM_ABSOLUTE_INDEXED_Y, 5, false, true }, // TAS - Absolute,Y (undocumented)
   { 0x9C, "SAY", C6502::SAY, AM_ABSOLUTE_INDEXED_X, 5, false, true }, // SAY - Absolute,X (undocumented)
   { 0x9D, "STA", C6502::STA, AM_ABSOLUTE_INDEXED_X, 5, true, true }, // STA - Absolute,X
   { 0x9E, "XAS", C6502::XAS, AM_ABSOLUTE_INDEXED_Y, 5, false, true }, // XAS - Absolute,Y (undocumented)
   { 0x9F, "AXA", C6502::AXA, AM_ABSOLUTE_INDEXED_Y, 5, false, true }, // AXA - Absolute,Y (undocumented)
   { 0xA0, "LDY", C6502::LDY, AM_IMMEDIATE, 2, true, true }, // LDY - Immediate
   { 0xA1, "LDA", C6502::LDA, AM_PREINDEXED_INDIRECT, 6, true, true }, // LDA - (Indirect,X)
   { 0xA2, "LDX", C6502::LDX, AM_IMMEDIATE, 2, true, true }, // LDX - Immediate
   { 0xA3, "LAX", C6502::LAX, AM_PREINDEXED_INDIRECT, 6, false, true }, // LAX - (Indirect,X) (undocumented)
   { 0xA4, "LDY", C6502::LDY, AM_ZEROPAGE, 3, true, true }, // LDY - Zero Page
   { 0xA5, "LDA", C6502::LDA, AM_ZEROPAGE, 3, true, true }, // LDA - Zero Page
   { 0xA6, "LDX", C6502::LDX, AM_ZEROPAGE, 3, true, true }, // LDX - Zero Page
   { 0xA7, "LAX", C6502::LAX, AM_ZEROPAGE, 3, false, true }, // LAX - Zero Page (undocumented)
   { 0xA8, "TAY", C6502::TAY, AM_IMPLIED, 2, true, true }, // TAY
   { 0xA9, "LDA", C6502::LDA, AM_IMMEDIATE, 2, true, true }, // LDA - Immediate
   { 0xAA, "TAX", C6502::TAX, AM_IMPLIED, 2, true, true }, // TAX
   { 0xAB, "OAL", C6502::OAL, AM_IMMEDIATE, 2, false, true }, // OAL - Immediate
   { 0xAC, "LDY", C6502::LDY, AM_ABSOLUTE, 4, true, true }, // LDY - Absolute
   { 0xAD, "LDA", C6502::LDA, AM_ABSOLUTE, 4, true, true }, // LDA - Absolute
   { 0xAE, "LDX", C6502::LDX, AM_ABSOLUTE, 4, true, true }, // LDX - Absolute
   { 0xAF, "LAX", C6502::LAX, AM_ABSOLUTE, 4, false, true }, // LAX - Absolute (undocumented)
   { 0xB0, "BCS", C6502::BCS, AM_RELATIVE, 2, true, true }, // BCS
   { 0xB1, "LDA", C6502::LDA, AM_POSTINDEXED_INDIRECT, 5, true, false }, // LDA - (Indirect),Y
   { 0xB2, "KIL", C6502::KIL, AM_IMPLIED, 0, false, true }, // KIL - Implied (processor lock up!)
   { 0xB3, "LAX", C6502::LAX, AM_POSTINDEXED_INDIRECT, 5, false, false }, // LAX - (Indirect),Y (undocumented)
   { 0xB4, "LDY", C6502::LDY, AM_ZEROPAGE_INDEXED_X, 4, true, true }, // LDY - Zero Page,X
   { 0xB5, "LDA", C6502::LDA, AM_ZEROPAGE_INDEXED_X, 4, true, true }, // LDA - Zero Page,X
   { 0xB6, "LDX", C6502::LDX, AM_ZEROPAGE_INDEXED_Y, 4, true, true }, // LDX - Zero Page,Y
   { 0xB7, "LAX", C6502::LAX, AM_ZEROPAGE_INDEXED_Y, 4, false, true }, // LAX - Zero Page,X (undocumented)
   { 0xB8, "CLV", C6502::CLV, AM_IMPLIED, 2, true, true }, // CLV
   { 0xB9, "LDA", C6502::LDA, AM_ABSOLUTE_INDEXED_Y, 4, true, false }, // LDA - Absolute,Y
   { 0xBA, "TSX", C6502::TSX, AM_IMPLIED, 2, true, true }, // TSX
   { 0xBB, "LAS", C6502::LAS, AM_ABSOLUTE_INDEXED_Y, 4, false, false }, // LAS - Absolute,Y (undocumented)
   { 0xBC, "LDY", C6502::LDY, AM_ABSOLUTE_INDEXED_X, 4, true, false }, // LDY - Absolute,X
   { 0xBD, "LDA", C6502::LDA, AM_ABSOLUTE_INDEXED_X, 4, true, false }, // LDA - Absolute,X
   { 0xBE, "LDX", C6502::LDX, AM_ABSOLUTE_INDEXED_Y, 4, true, false }, // LDX - Absolute,Y
   { 0xBF, "LAX", C6502::LAX, AM_ABSOLUTE_INDEXED_Y, 4, false, false }, // LAX - Absolute,Y (undocumented)
   { 0xC0, "CPY", C6502::CPY, AM_IMMEDIATE, 2, true, true }, // CPY - Immediate
   { 0xC1, "CMP", C6502::CMP, AM_PREINDEXED_INDIRECT, 6, true, true }, // CMP - (Indirect,X)
   { 0xC2, "DOP", C6502::DOP, AM_IMMEDIATE, 2, false, true }, // DOP (undocumented)
   { 0xC3, "DCM", C6502::DCM, AM_PREINDEXED_INDIRECT, 8, false, true }, // DCM - (Indirect,X) (undocumented)
   { 0xC4, "CPY", C6502::CPY, AM_ZEROPAGE, 3, true, true }, // CPY - Zero Page
   { 0xC5, "CMP", C6502::CMP, AM_ZEROPAGE, 3, true, true }, // CMP - Zero Page
   { 0xC6, "DEC", C6502::DEC, AM_ZEROPAGE, 5, true, true }, // DEC - Zero Page
   { 0xC7, "DCM", C6502::DCM, AM_ZEROPAGE, 5, true, true }, // DCM - Zero Page (undocumented)
   { 0xC8, "INY", C6502::INY, AM_IMPLIED, 2, true, true }, // INY
   { 0xC9, "CMP", C6502::CMP, AM_IMMEDIATE, 2, true, true }, // CMP - Immediate
   { 0xCA, "DEX", C6502::DEX, AM_IMPLIED, 2, true, true }, // DEX
   { 0xCB, "SAX", C6502::SAX, AM_IMMEDIATE, 2, false, true }, // SAX - Immediate (undocumented)
   { 0xCC, "CPY", C6502::CPY, AM_ABSOLUTE, 4, true, true }, // CPY - Absolute
   { 0xCD, "CMP", C6502::CMP, AM_ABSOLUTE, 4, true, true }, // CMP - Absolute
   { 0xCE, "DEC", C6502::DEC, AM_ABSOLUTE, 6, true, true }, // DEC - Absolute
   { 0xCF, "DCM", C6502::DCM, AM_ABSOLUTE, 6, false, true }, // DCM - Absolute (undocumented)
   { 0xD0, "BNE", C6502::BNE, AM_RELATIVE, 2, true, true }, // BNE
   { 0xD1, "CMP", C6502::CMP, AM_POSTINDEXED_INDIRECT, 5, true, false }, // CMP   (Indirect),Y
   { 0xD2, "KIL", C6502::KIL, AM_IMPLIED, 0, false, true }, // KIL - Implied (processor lock up!)
   { 0xD3, "DCM", C6502::DCM, AM_POSTINDEXED_INDIRECT, 8, false, true }, // DCM - (Indirect),Y (undocumented)
   { 0xD4, "DOP", C6502::DOP, AM_ZEROPAGE_INDEXED_X, 4, false, true }, // DOP (undocumented)
   { 0xD5, "CMP", C6502::CMP, AM_ZEROPAGE_INDEXED_X, 4, true, true }, // CMP - Zero Page,X
   { 0xD6, "DEC", C6502::DEC, AM_ZEROPAGE_INDEXED_X, 6, true, true }, // DEC - Zero Page,X
   { 0xD7, "DCM", C6502::DCM, AM_ZEROPAGE_INDEXED_X, 6, false, true }, // DCM - Zero Page,X (undocumented)
   { 0xD8, "CLD", C6502::CLD, AM_IMPLIED, 2, true, true }, // CLD
   { 0xD9, "CMP", C6502::CMP, AM_ABSOLUTE_INDEXED_Y, 4, true, false }, // CMP - Absolute,Y
   { 0xDA, "NOP", C6502::NOP, AM_IMPLIED, 2, false, true }, // NOP (undocumented)
   { 0xDB, "DCM", C6502::DCM, AM_ABSOLUTE_INDEXED_Y, 7, false, true }, // DCM - Absolute,Y (undocumented)
   { 0xDC, "TOP", C6502::TOP, AM_ABSOLUTE_INDEXED_X, 4, false, false }, // TOP (undocumented)
   { 0xDD, "CMP", C6502::CMP, AM_ABSOLUTE_INDEXED_X, 4, true, false }, // CMP - Absolute,X
   { 0xDE, "DEC", C6502::DEC, AM_ABSOLUTE_INDEXED_X, 7, true, true }, // DEC - Absolute,X
   { 0xDF, "DCM", C6502::DCM, AM_ABSOLUTE_INDEXED_X, 7, false, true }, // DCM - Absolute,X (undocumented)
   { 0xE0, "CPX", C6502::CPX, AM_IMMEDIATE, 2, true, true }, // CPX - Immediate
   { 0xE1, "SBC", C6502::SBC, AM_PREINDEXED_INDIRECT, 6, true, true }, // SBC - (Indirect,X)
   { 0xE2, "DOP", C6502::DOP, AM_IMMEDIATE, 2, false, true }, // DOP (undocumented)
   { 0xE3, "INS", C6502::INS, AM_PREINDEXED_INDIRECT, 8, false, true }, // INS - (Indirect,X) (undocumented)
   { 0xE4, "CPX", C6502::CPX, AM_ZEROPAGE, 3, true, true }, // CPX - Zero Page
   { 0xE5, "SBC", C6502::SBC, AM_ZEROPAGE, 3, true, true }, // SBC - Zero Page
   { 0xE6, "INC", C6502::INC, AM_ZEROPAGE, 5, true, true }, // INC - Zero Page
   { 0xE7, "INS", C6502::INS, AM_ZEROPAGE, 5, false, true }, // INS - Zero Page (undocumented)
   { 0xE8, "INX", C6502::INX, AM_IMPLIED, 2, true, true }, // INX
   { 0xE9, "SBC", C6502::SBC, AM_IMMEDIATE, 2, true, true }, // SBC - Immediate
   { 0xEA, "NOP", C6502::NOP, AM_IMPLIED, 2, true, true }, // NOP
   { 0xEB, "SBC", C6502::SBC, AM_IMMEDIATE, 2, false, true }, // SBC - Immediate (undocumented)
   { 0xEC, "CPX", C6502::CPX, AM_ABSOLUTE, 4, true, true }, // CPX - Absolute
   { 0xED, "SBC", C6502::SBC, AM_ABSOLUTE, 4, true, true }, // SBC - Absolute
   { 0xEE, "INC", C6502::INC, AM_ABSOLUTE, 6, true, true }, // INC - Absolute
   { 0xEF, "INS", C6502::INS, AM_ABSOLUTE, 6, false, true }, // INS - Absolute (undocumented)
   { 0xF0, "BEQ", C6502::BEQ, AM_RELATIVE, 2, true, true }, // BEQ
   { 0xF1, "SBC", C6502::SBC, AM_POSTINDEXED_INDIRECT, 5, true, false }, // SBC - (Indirect),Y
   { 0xF2, "KIL", C6502::KIL, AM_IMPLIED, 0, false, true }, // KIL - Implied (processor lock up!)
   { 0xF3, "INS", C6502::INS, AM_POSTINDEXED_INDIRECT, 8, false, true }, // INS - (Indirect),Y (undocumented)
   { 0xF4, "DOP", C6502::DOP, AM_ZEROPAGE_INDEXED_X, 4, false, true }, // DOP (undocumented)
   { 0xF5, "SBC", C6502::SBC, AM_ZEROPAGE_INDEXED_X, 4, true, true }, // SBC - Zero Page,X
   { 0xF6, "INC", C6502::INC, AM_ZEROPAGE_INDEXED_X, 6, true, true }, // INC - Zero Page,X
   { 0xF7, "INS", C6502::INS, AM_ZEROPAGE_INDEXED_X, 6, false, true }, // INS - Zero Page,X (undocumented)
   { 0xF8, "SED", C6502::SED, AM_IMPLIED, 2, true, true }, // SED
   { 0xF9, "SBC", C6502::SBC, AM_ABSOLUTE_INDEXED_Y, 4, true, false }, // SBC - Absolute,Y
   { 0xFA, "NOP", C6502::NOP, AM_IMPLIED, 2, false, true }, // NOP (undocumented)
   { 0xFB, "INS", C6502::INS, AM_ABSOLUTE_INDEXED_Y, 7, false, true }, // INS - Absolute,Y (undocumented)
   { 0xFC, "TOP", C6502::TOP, AM_ABSOLUTE_INDEXED_X, 4, false, false }, // TOP (undocumented)
   { 0xFD, "SBC", C6502::SBC, AM_ABSOLUTE_INDEXED_X, 4, true, false }, // SBC - Absolute,X
   { 0xFE, "INC", C6502::INC, AM_ABSOLUTE_INDEXED_X, 7, true, true }, // INC - Absolute,X
   { 0xFF, "INS", C6502::INS, AM_ABSOLUTE_INDEXED_X, 7, false, true }  // INS - Absolute,X (undocumented)
};

#define FLAG_EFFECT(__n,__z,__c,__i,__d,__v) \
   "Flag effect:" \
   "<table border=\"1\" cellpadding=\"2\" cellspacing=\"0\" bgcolor=\"#FFFFFF\">" \
   "<tr bgcolor=\"#B0B0B0\"><th>N</th><th>Z</th><th>C</th><th>I</th><th>D</th><th>V</th></tr>" \
   "<tr><td>" __n \
   "</td><td>" __z \
   "</td><td>" __c \
   "</td><td>" __i \
   "</td><td>" __d \
   "</td><td>" __v \
   "</td></tr>" \
   "</table><br>"

#define VARIANT_HEADER \
   "Variants:" \
   "<table border=\"1\" cellpadding=\"2\" cellspacing=\"0\" bgcolor=\"#FFFFFF\">" \
   "<tr bgcolor=\"#B0B0B0\"><th></th><th>Addressing Mode</th><th>Assembly Language Form</th><th>Opcode</th><th># Bytes</th><th># Cycles</th></tr>"

#define VARIANT_SELECTED \
   "<tr bgcolor=\"#E8E8E8\"><td>&gt;</td>"

#define VARIANT \
   "<tr><td></td>"

#define VARIANT_INFO(__amode,__mnemonic,__langform,__opcode,__bytes,__cycles) \
   "<td>" __amode \
   "</td><td>" __mnemonic " " __langform \
   "</td><td>" __opcode \
   "</td><td>" __bytes \
   "</td><td>" __cycles \
   "</td></tr>"

#define VARIANT_IMPLIED(__mnemonic,__opcode,__bytes,__cycles) \
   VARIANT_INFO("Implied",__mnemonic,"",__opcode,__bytes,__cycles)

#define VARIANT_IMMEDIATE(__mnemonic,__opcode,__bytes,__cycles) \
   VARIANT_INFO("Immediate",__mnemonic,"#Operand",__opcode,__bytes,__cycles)

#define VARIANT_ACCUMULATOR(__mnemonic,__opcode,__bytes,__cycles) \
   VARIANT_INFO("Accumulator",__mnemonic,"A",__opcode,__bytes,__cycles)

#define VARIANT_ZEROPAGE(__mnemonic,__opcode,__bytes,__cycles) \
   VARIANT_INFO("Zero Page",__mnemonic,"Operand",__opcode,__bytes,__cycles)

#define VARIANT_ZEROPAGE_X(__mnemonic,__opcode,__bytes,__cycles) \
   VARIANT_INFO("Zero Page, indexed by X",__mnemonic,"Operand,X",__opcode,__bytes,__cycles)

#define VARIANT_ZEROPAGE_Y(__mnemonic,__opcode,__bytes,__cycles) \
   VARIANT_INFO("Zero Page, indexed by Y",__mnemonic,"Operand,Y",__opcode,__bytes,__cycles)

#define VARIANT_ABSOLUTE(__mnemonic,__opcode,__bytes,__cycles) \
   VARIANT_INFO("Absolute",__mnemonic,"Operand",__opcode,__bytes,__cycles)

#define VARIANT_ABSOLUTE_X(__mnemonic,__opcode,__bytes,__cycles) \
   VARIANT_INFO("Absolute, indexed by X",__mnemonic,"Operand,X",__opcode,__bytes,__cycles)

#define VARIANT_ABSOLUTE_Y(__mnemonic,__opcode,__bytes,__cycles) \
   VARIANT_INFO("Absolute, indexed by Y",__mnemonic,"Operand,Y",__opcode,__bytes,__cycles)

#define VARIANT_INDIRECT(__mnemonic,__opcode,__bytes,__cycles) \
   VARIANT_INFO("Indirect",__mnemonic,"(Operand)",__opcode,__bytes,__cycles)

#define VARIANT_INDIRECT_X(__mnemonic,__opcode,__bytes,__cycles) \
   VARIANT_INFO("Pre-indexed Indirect",__mnemonic,"(Operand,X)",__opcode,__bytes,__cycles)

#define VARIANT_INDIRECT_Y(__mnemonic,__opcode,__bytes,__cycles) \
   VARIANT_INFO("Post-indexed Indirect",__mnemonic,"(Operand),Y",__opcode,__bytes,__cycles)

#define VARIANT_RELATIVE(__mnemonic,__opcode,__bytes,__cycles) \
   VARIANT_INFO("PC relative",__mnemonic,"Operand",__opcode,__bytes,__cycles)

#define VARIANT_FOOTER \
   "</table>"

static const char* m_6502opcodeInfo [ 256 ] =
{
   // BRK
   "BRK: Force Break<br>"
   "Operation:  Forced Interrupt, PC + 2 to S, P to S<br>"
   FLAG_EFFECT("","","","1","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("BRK","00","1","7")
   VARIANT_FOOTER
   "1. A BRK command cannot be masked by setting I.",

   // ORA - (Indirect,X)
   "ORA: \"OR\" memory with accumulator<br>"
   "Operation: A \\/ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ORA","09","2","2")
   VARIANT VARIANT_ZEROPAGE("ORA","05","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ORA","15","2","4")
   VARIANT VARIANT_ABSOLUTE("ORA","0D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ORA","1D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ORA","19","3","4*")
   VARIANT_SELECTED VARIANT_INDIRECT_X("ORA","01","2","6")
   VARIANT VARIANT_INDIRECT_Y("ORA","11","2","5*")
   VARIANT_FOOTER
   "* Add 1 on page crossing",

   "", // KIL - Implied (processor lock up!)

   "", // ASO - (Indirect,X) (undocumented)

   "", // DOP (undocumented)

   // ORA - Zero Page
   "ORA: \"OR\" memory with accumulator<br>"
   "Operation: A \\/ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ORA","09","2","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("ORA","05","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ORA","15","2","4")
   VARIANT VARIANT_ABSOLUTE("ORA","0D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ORA","1D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ORA","19","3","4*")
   VARIANT VARIANT_INDIRECT_X("ORA","01","2","6")
   VARIANT VARIANT_INDIRECT_Y("ORA","11","2","5*")
   VARIANT_FOOTER
   "* Add 1 on page crossing",

   // ASL - Zero Page
   "ASL: Shift Left One Bit (Memory or Accumulator)<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   "Operation:  C &lt;- |7|6|5|4|3|2|1|0| &lt;- 0<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("ASL","0A","1","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("ASL","06","2","5")
   VARIANT VARIANT_ZEROPAGE_X("ASL","16","2","6")
   VARIANT VARIANT_ABSOLUTE("ASL","0E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("ASL","1E","3","7")
   VARIANT_FOOTER,

   "", // ASO - Zero Page (undocumented)

   // PHP
   "PHP: Push processor status on stack<br>"
   "Operation:  P to S<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("PHP","08","1","3")
   VARIANT_FOOTER,

   // ORA - Immediate
   "ORA: \"OR\" memory with accumulator<br>"
   "Operation: A \\/ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMMEDIATE("ORA","09","2","2")
   VARIANT VARIANT_ZEROPAGE("ORA","05","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ORA","15","2","4")
   VARIANT VARIANT_ABSOLUTE("ORA","0D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ORA","1D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ORA","19","3","4*")
   VARIANT VARIANT_INDIRECT_X("ORA","01","2","6")
   VARIANT VARIANT_INDIRECT_Y("ORA","11","2","5*")
   VARIANT_FOOTER
   "* Add 1 on page crossing",

   // ASL - Accumulator
   "ASL: Shift Left One Bit (Memory or Accumulator)<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   "Operation:  C &lt;- |7|6|5|4|3|2|1|0| &lt;- 0<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_ACCUMULATOR("ASL","0A","1","2")
   VARIANT VARIANT_ZEROPAGE("ASL","06","2","5")
   VARIANT VARIANT_ZEROPAGE_X("ASL","16","2","6")
   VARIANT VARIANT_ABSOLUTE("ASL","0E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("ASL","1E","3","7")
   VARIANT_FOOTER,

   "", // ANC - Immediate (undocumented)

   "", // TOP (undocumented)

   // ORA - Absolute
   "ORA: \"OR\" memory with accumulator<br>"
   "Operation: A \\/ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ORA","09","2","2")
   VARIANT VARIANT_ZEROPAGE("ORA","05","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ORA","15","2","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE("ORA","0D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ORA","1D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ORA","19","3","4*")
   VARIANT VARIANT_INDIRECT_X("ORA","01","2","6")
   VARIANT VARIANT_INDIRECT_Y("ORA","11","2","5*")
   VARIANT_FOOTER
   "* Add 1 on page crossing",

   // ASL - Absolute
   "ASL: Shift Left One Bit (Memory or Accumulator)<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   "Operation:  C &lt;- |7|6|5|4|3|2|1|0| &lt;- 0<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("ASL","0A","1","2")
   VARIANT VARIANT_ZEROPAGE("ASL","06","2","5")
   VARIANT VARIANT_ZEROPAGE_X("ASL","16","2","6")
   VARIANT_SELECTED VARIANT_ABSOLUTE("ASL","0E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("ASL","1E","3","7")
   VARIANT_FOOTER,

   "", // ASO - Absolute (undocumented)

   // BPL
   "BPL: Branch on result plus<br>"
   "Operation:  Branch on N = 0<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_RELATIVE("BPL","10","2","2*")
   VARIANT_FOOTER
   "* Add 1 if branch occurs to same page.<br>"
   "* Add 2 if branch occurs to different page.",

   // ORA - (Indirect),Y
   "ORA: \"OR\" memory with accumulator<br>"
   "Operation: A \\/ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ORA","09","2","2")
   VARIANT VARIANT_ZEROPAGE("ORA","05","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ORA","15","2","4")
   VARIANT VARIANT_ABSOLUTE("ORA","0D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ORA","1D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ORA","19","3","4*")
   VARIANT VARIANT_INDIRECT_X("ORA","01","2","6")
   VARIANT_SELECTED VARIANT_INDIRECT_Y("ORA","11","2","5*")
   VARIANT_FOOTER
   "* Add 1 on page crossing",

   "", // KIL - Implied (processor lock up!)

   "", // ASO - (Indirect),Y (undocumented)

   "", // DOP (undocumented)

   // ORA - Zero Page,X
   "ORA: \"OR\" memory with accumulator<br>"
   "Operation: A \\/ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ORA","09","2","2")
   VARIANT VARIANT_ZEROPAGE("ORA","05","2","3")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("ORA","15","2","4")
   VARIANT VARIANT_ABSOLUTE("ORA","0D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ORA","1D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ORA","19","3","4*")
   VARIANT VARIANT_INDIRECT_X("ORA","01","2","6")
   VARIANT VARIANT_INDIRECT_Y("ORA","11","2","5*")
   VARIANT_FOOTER
   "* Add 1 on page crossing",

   // ASL - Zero Page,X
   "ASL: Shift Left One Bit (Memory or Accumulator)<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   "Operation:  C &lt;- |7|6|5|4|3|2|1|0| &lt;- 0<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("ASL","0A","1","2")
   VARIANT VARIANT_ZEROPAGE("ASL","06","2","5")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("ASL","16","2","6")
   VARIANT VARIANT_ABSOLUTE("ASL","0E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("ASL","1E","3","7")
   VARIANT_FOOTER,

   "", // ASO - Zero Page,X (undocumented)

   // CLC
   "CLC: Clear carry flag<br>"
   "Operation:  0 -&gt; C<br>"
   FLAG_EFFECT("","","0","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("CLC","18","1","2")
   VARIANT_FOOTER,

   // ORA - Absolute,Y
   "ORA: \"OR\" memory with accumulator<br>"
   "Operation: A \\/ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ORA","09","2","2")
   VARIANT VARIANT_ZEROPAGE("ORA","05","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ORA","15","2","4")
   VARIANT VARIANT_ABSOLUTE("ORA","0D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ORA","1D","3","4*")
   VARIANT_SELECTED VARIANT_ABSOLUTE_Y("ORA","19","3","4*")
   VARIANT VARIANT_INDIRECT_X("ORA","01","2","6")
   VARIANT VARIANT_INDIRECT_Y("ORA","11","2","5*")
   VARIANT_FOOTER
   "* Add 1 on page crossing",

   "", // NOP (undocumented)

   "", // ASO - Absolute,Y (undocumented)

   "", // TOP (undocumented)

   // ORA - Absolute,X
   "ORA: \"OR\" memory with accumulator<br>"
   "Operation: A \\/ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ORA","09","2","2")
   VARIANT VARIANT_ZEROPAGE("ORA","05","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ORA","15","2","4")
   VARIANT VARIANT_ABSOLUTE("ORA","0D","3","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("ORA","1D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ORA","19","3","4*")
   VARIANT VARIANT_INDIRECT_X("ORA","01","2","6")
   VARIANT VARIANT_INDIRECT_Y("ORA","11","2","5*")
   VARIANT_FOOTER
   "* Add 1 on page crossing",

   // ASL - Absolute,X
   "ASL: Shift Left One Bit (Memory or Accumulator)<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   "Operation:  C &lt;- |7|6|5|4|3|2|1|0| &lt;- 0<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("ASL","0A","1","2")
   VARIANT VARIANT_ZEROPAGE("ASL","06","2","5")
   VARIANT VARIANT_ZEROPAGE_X("ASL","16","2","6")
   VARIANT VARIANT_ABSOLUTE("ASL","0E","3","6")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("ASL","1E","3","7")
   VARIANT_FOOTER,

   "", // ASO - Absolute,X (undocumented)

   // JSR
   "JSR: Jump to new location saving return address<br>"
   "Operation:  PC + 2 toS, (PC + 1) -&gt; PCL<br>"
   "                        (PC + 2) -&gt; PCH<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_ABSOLUTE("JSR","20","3","6")
   VARIANT_FOOTER,

   // AND - (Indirect,X)
   "AND: \"AND\" memory with accumulator<br>"
   "Operation:  A /\\ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("AND","29","2","2")
   VARIANT VARIANT_ZEROPAGE("AND","25","2","3")
   VARIANT VARIANT_ZEROPAGE_X("AND","35","2","4")
   VARIANT VARIANT_ABSOLUTE("AND","2D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("AND","3D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("AND","39","3","4*")
   VARIANT_SELECTED VARIANT_INDIRECT_X("AND","21","2","6")
   VARIANT VARIANT_INDIRECT_Y("AND","31","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // KIL - Implied (processor lock up!)

   "", // RLA - (Indirect,X) (undocumented)

   // BIT - Zero Page
   "BIT: Test bits in memory with accumulator<br>"
   "Operation:  A /\\ M, M7 -&gt; N, M6 -&gt; V<br>"
   "Bit 6 and 7 are transferred to the status register.<br>"
   "If the result of A /\\ M is zero then Z = 1, otherwise<br>"
   "Z = 0<br>"
   FLAG_EFFECT("M7","/","","","","M6")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_ZEROPAGE("BIT","24","2","3")
   VARIANT VARIANT_ABSOLUTE("BIT","24","3","4")
   VARIANT_FOOTER,

   // AND - Zero Page
   "AND: \"AND\" memory with accumulator<br>"
   "Operation:  A /\\ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("AND","29","2","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("AND","25","2","3")
   VARIANT VARIANT_ZEROPAGE_X("AND","35","2","4")
   VARIANT VARIANT_ABSOLUTE("AND","2D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("AND","3D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("AND","39","3","4*")
   VARIANT VARIANT_INDIRECT_X("AND","21","2","6")
   VARIANT VARIANT_INDIRECT_Y("AND","31","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // ROL - Zero Page
   "ROL: Rotate one bit left (memory or accumulator)<br>"
   "             +------------------------------+<br>"
   "             |         M or A               |<br>"
   "             |   +-+-+-+-+-+-+-+-+    +-+   |<br>"
   "Operation:   +-&lt; |7|6|5|4|3|2|1|0| &lt;- |C| &lt;-+<br>"
   "                 +-+-+-+-+-+-+-+-+    +-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("ROL","2A","1","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("ROL","26","2","5")
   VARIANT VARIANT_ZEROPAGE_X("ROL","36","2","6")
   VARIANT VARIANT_ABSOLUTE("ROL","2E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("ROL","3E","3","7")
   VARIANT_FOOTER,

   "", // RLA - Zero Page (undocumented)

   // PLP
   "PLP: Pull processor status from stack<br>"
   "Operation:  P from S<br>"
   FLAG_EFFECT("from S","from S","from S","from S","from S","from S")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("PLP","28","1","4")
   VARIANT_FOOTER,

   // AND - Immediate
   "AND: \"AND\" memory with accumulator<br>"
   "Operation:  A /\\ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMMEDIATE("AND","29","2","2")
   VARIANT VARIANT_ZEROPAGE("AND","25","2","3")
   VARIANT VARIANT_ZEROPAGE_X("AND","35","2","4")
   VARIANT VARIANT_ABSOLUTE("AND","2D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("AND","3D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("AND","39","3","4*")
   VARIANT VARIANT_INDIRECT_X("AND","21","2","6")
   VARIANT VARIANT_INDIRECT_Y("AND","31","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // ROL - Accumulator
   "ROL: Rotate one bit left (memory or accumulator)<br>"
   "             +------------------------------+<br>"
   "             |         M or A               |<br>"
   "             |   +-+-+-+-+-+-+-+-+    +-+   |<br>"
   "Operation:   +-&lt; |7|6|5|4|3|2|1|0| &lt;- |C| &lt;-+<br>"
   "                 +-+-+-+-+-+-+-+-+    +-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_ACCUMULATOR("ROL","2A","1","2")
   VARIANT VARIANT_ZEROPAGE("ROL","26","2","5")
   VARIANT VARIANT_ZEROPAGE_X("ROL","36","2","6")
   VARIANT VARIANT_ABSOLUTE("ROL","2E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("ROL","3E","3","7")
   VARIANT_FOOTER,

   "", // ANC - Immediate (undocumented)

   // BIT - Absolute
   "BIT: Test bits in memory with accumulator<br>"
   "Operation:  A /\\ M, M7 -&gt; N, M6 -&gt; V<br>"
   "Bit 6 and 7 are transferred to the status register.<br>"
   "If the result of A /\\ M is zero then Z = 1, otherwise<br>"
   "Z = 0<br>"
   FLAG_EFFECT("M7","/","","","","M6")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("BIT","24","2","3")
   VARIANT_SELECTED VARIANT_ABSOLUTE("BIT","24","3","4")
   VARIANT_FOOTER,

   // AND - Absolute
   "AND: \"AND\" memory with accumulator<br>"
   "Operation:  A /\\ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("AND","29","2","2")
   VARIANT VARIANT_ZEROPAGE("AND","25","2","3")
   VARIANT VARIANT_ZEROPAGE_X("AND","35","2","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE("AND","2D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("AND","3D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("AND","39","3","4*")
   VARIANT VARIANT_INDIRECT_X("AND","21","2","6")
   VARIANT VARIANT_INDIRECT_Y("AND","31","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // ROL - Absolute
   "ROL: Rotate one bit left (memory or accumulator)<br>"
   "             +------------------------------+<br>"
   "             |         M or A               |<br>"
   "             |   +-+-+-+-+-+-+-+-+    +-+   |<br>"
   "Operation:   +-&lt; |7|6|5|4|3|2|1|0| &lt;- |C| &lt;-+<br>"
   "                 +-+-+-+-+-+-+-+-+    +-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("ROL","2A","1","2")
   VARIANT VARIANT_ZEROPAGE("ROL","26","2","5")
   VARIANT VARIANT_ZEROPAGE_X("ROL","36","2","6")
   VARIANT_SELECTED VARIANT_ABSOLUTE("ROL","2E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("ROL","3E","3","7")
   VARIANT_FOOTER,

   "", // RLA - Absolute (undocumented)

   // BMI
   "BMI: Branch on result minus<br>"
   "Operation:  Branch on N = 1<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_RELATIVE("BMI","30","2","2*")
   VARIANT_FOOTER
   "* Add 1 if branch occurs to same page.<br>"
   "* Add 2 if branch occurs to different page.",

   // AND - (Indirect),Y
   "AND: \"AND\" memory with accumulator<br>"
   "Operation:  A /\\ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("AND","29","2","2")
   VARIANT VARIANT_ZEROPAGE("AND","25","2","3")
   VARIANT VARIANT_ZEROPAGE_X("AND","35","2","4")
   VARIANT VARIANT_ABSOLUTE("AND","2D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("AND","3D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("AND","39","3","4*")
   VARIANT VARIANT_INDIRECT_X("AND","21","2","6")
   VARIANT_SELECTED VARIANT_INDIRECT_Y("AND","31","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // KIL - Implied (processor lock up!)

   "", // RLA - (Indirect),Y (undocumented)

   "", // DOP (undocumented)

   // AND - Zero Page,X
   "AND: \"AND\" memory with accumulator<br>"
   "Operation:  A /\\ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("AND","29","2","2")
   VARIANT VARIANT_ZEROPAGE("AND","25","2","3")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("AND","35","2","4")
   VARIANT VARIANT_ABSOLUTE("AND","2D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("AND","3D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("AND","39","3","4*")
   VARIANT VARIANT_INDIRECT_X("AND","21","2","6")
   VARIANT VARIANT_INDIRECT_Y("AND","31","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // ROL - Zero Page,X
   "ROL: Rotate one bit left (memory or accumulator)<br>"
   "             +------------------------------+<br>"
   "             |         M or A               |<br>"
   "             |   +-+-+-+-+-+-+-+-+    +-+   |<br>"
   "Operation:   +-&lt; |7|6|5|4|3|2|1|0| &lt;- |C| &lt;-+<br>"
   "                 +-+-+-+-+-+-+-+-+    +-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("ROL","2A","1","2")
   VARIANT VARIANT_ZEROPAGE("ROL","26","2","5")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("ROL","36","2","6")
   VARIANT VARIANT_ABSOLUTE("ROL","2E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("ROL","3E","3","7")
   VARIANT_FOOTER,

   "", // RLA - Zero Page,X (undocumented)

   // SEC
   "SEC: Set carry flag<br>"
   "Operation:  1 -&gt; C<br>"
   FLAG_EFFECT("","","1","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("SEC","38","1","2")
   VARIANT_FOOTER,

   // AND - Absolute,Y
   "AND: \"AND\" memory with accumulator<br>"
   "Operation:  A /\\ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("AND","29","2","2")
   VARIANT VARIANT_ZEROPAGE("AND","25","2","3")
   VARIANT VARIANT_ZEROPAGE_X("AND","35","2","4")
   VARIANT VARIANT_ABSOLUTE("AND","2D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("AND","3D","3","4*")
   VARIANT_SELECTED VARIANT_ABSOLUTE_Y("AND","39","3","4*")
   VARIANT VARIANT_INDIRECT_X("AND","21","2","6")
   VARIANT VARIANT_INDIRECT_Y("AND","31","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // NOP (undocumented)

   "", // RLA - Absolute,Y (undocumented)

   "", // TOP (undocumented)

   // AND - Absolute,X
   "AND: \"AND\" memory with accumulator<br>"
   "Operation:  A /\\ M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("AND","29","2","2")
   VARIANT VARIANT_ZEROPAGE("AND","25","2","3")
   VARIANT VARIANT_ZEROPAGE_X("AND","35","2","4")
   VARIANT VARIANT_ABSOLUTE("AND","2D","3","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("AND","3D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("AND","39","3","4*")
   VARIANT VARIANT_INDIRECT_X("AND","21","2","6")
   VARIANT VARIANT_INDIRECT_Y("AND","31","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // ROL - Absolute,X
   "ROL: Rotate one bit left (memory or accumulator)<br>"
   "             +------------------------------+<br>"
   "             |         M or A               |<br>"
   "             |   +-+-+-+-+-+-+-+-+    +-+   |<br>"
   "Operation:   +-&lt; |7|6|5|4|3|2|1|0| &lt;- |C| &lt;-+<br>"
   "                 +-+-+-+-+-+-+-+-+    +-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("ROL","2A","1","2")
   VARIANT VARIANT_ZEROPAGE("ROL","26","2","5")
   VARIANT VARIANT_ZEROPAGE_X("ROL","36","2","6")
   VARIANT VARIANT_ABSOLUTE("ROL","2E","3","6")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("ROL","3E","3","7")
   VARIANT_FOOTER,

   "", // RLA - Absolute,X (undocumented)

   // RTI
   "RTI: Return from interrupt<br>"
   "Operation:  P from S, PC from S<br>"
   FLAG_EFFECT("from S","from S","from S","from S","from S","from S")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("RTI","4D","1","6")
   VARIANT_FOOTER,

   // EOR - (Indirect,X)
   "EOR: \"Exclusive-Or\" memory with accumulator<br>"
   "Operation:  A EOR M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("EOR","49","2","2")
   VARIANT VARIANT_ZEROPAGE("EOR","45","2","3")
   VARIANT VARIANT_ZEROPAGE_X("EOR","55","2","4")
   VARIANT VARIANT_ABSOLUTE("EOR","4D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("EOR","5D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("EOR","59","3","4*")
   VARIANT_SELECTED VARIANT_INDIRECT_X("EOR","41","2","6")
   VARIANT VARIANT_INDIRECT_Y("EOR","51","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // KIL - Implied (processor lock up!)

   "", // LSE - (Indirect,X) (undocumented)

   "", // DOP (undocumented)

   // EOR - Zero Page
   "EOR: \"Exclusive-Or\" memory with accumulator<br>"
   "Operation:  A EOR M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("EOR","49","2","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("EOR","45","2","3")
   VARIANT VARIANT_ZEROPAGE_X("EOR","55","2","4")
   VARIANT VARIANT_ABSOLUTE("EOR","4D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("EOR","5D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("EOR","59","3","4*")
   VARIANT VARIANT_INDIRECT_X("EOR","41","2","6")
   VARIANT VARIANT_INDIRECT_Y("EOR","51","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LSR - Zero Page
   "LSR: Shift right one bit (memory or accumulator)<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   "Operation:  0 -&gt; |7|6|5|4|3|2|1|0| -&gt; C<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("0","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("LSR","4A","1","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("LSR","46","2","5")
   VARIANT VARIANT_ZEROPAGE_X("LSR","56","2","6")
   VARIANT VARIANT_ABSOLUTE("LSR","4E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("LSR","5E","3","7")
   VARIANT_FOOTER,

   "", // LSE - Zero Page (undocumented)

   // PHA
   "PHA: Push accumulator on stack<br>"
   "Operation:  A to S<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("PHA","48","1","3")
   VARIANT_FOOTER,

   // EOR - Immediate
   "EOR: \"Exclusive-Or\" memory with accumulator<br>"
   "Operation:  A EOR M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMMEDIATE("EOR","49","2","2")
   VARIANT VARIANT_ZEROPAGE("EOR","45","2","3")
   VARIANT VARIANT_ZEROPAGE_X("EOR","55","2","4")
   VARIANT VARIANT_ABSOLUTE("EOR","4D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("EOR","5D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("EOR","59","3","4*")
   VARIANT VARIANT_INDIRECT_X("EOR","41","2","6")
   VARIANT VARIANT_INDIRECT_Y("EOR","51","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LSR - Accumulator
   "LSR: Shift right one bit (memory or accumulator)<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   "Operation:  0 -&gt; |7|6|5|4|3|2|1|0| -&gt; C<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("0","/","/","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_ACCUMULATOR("LSR","4A","1","2")
   VARIANT VARIANT_ZEROPAGE("LSR","46","2","5")
   VARIANT VARIANT_ZEROPAGE_X("LSR","56","2","6")
   VARIANT VARIANT_ABSOLUTE("LSR","4E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("LSR","5E","3","7")
   VARIANT_FOOTER,

   "", // ALR - Immediate (undocumented)

   // JMP - Absolute
   "JMP: Jump to new location<br>"
   "Operation:  (PC + 1) -&gt; PCL<br>"
   "            (PC + 2) -&gt; PCH<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_ABSOLUTE("JMP","4C","3","3")
   VARIANT VARIANT_INDIRECT("JMP","6C","3","5")
   VARIANT_FOOTER,

   // EOR - Absolute
   "EOR: \"Exclusive-Or\" memory with accumulator<br>"
   "Operation:  A EOR M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("EOR","49","2","2")
   VARIANT VARIANT_ZEROPAGE("EOR","45","2","3")
   VARIANT VARIANT_ZEROPAGE_X("EOR","55","2","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE("EOR","4D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("EOR","5D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("EOR","59","3","4*")
   VARIANT VARIANT_INDIRECT_X("EOR","41","2","6")
   VARIANT VARIANT_INDIRECT_Y("EOR","51","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LSR - Absolute
   "LSR: Shift right one bit (memory or accumulator)<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   "Operation:  0 -&gt; |7|6|5|4|3|2|1|0| -&gt; C<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("0","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("LSR","4A","1","2")
   VARIANT VARIANT_ZEROPAGE("LSR","46","2","5")
   VARIANT VARIANT_ZEROPAGE_X("LSR","56","2","6")
   VARIANT_SELECTED VARIANT_ABSOLUTE("LSR","4E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("LSR","5E","3","7")
   VARIANT_FOOTER,

   "", // LSE - Absolute (undocumented)

   // BVC
   "BVC: Branch on overflow clear<br>"
   "Operation:  Branch on V = 0<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_RELATIVE("BVC","50","2","2*")
   VARIANT_FOOTER
   "* Add 1 if branch occurs to same page.<br>"
   "* Add 2 if branch occurs to different page.",

   // EOR - (Indirect),Y
   "EOR: \"Exclusive-Or\" memory with accumulator<br>"
   "Operation:  A EOR M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("EOR","49","2","2")
   VARIANT VARIANT_ZEROPAGE("EOR","45","2","3")
   VARIANT VARIANT_ZEROPAGE_X("EOR","55","2","4")
   VARIANT VARIANT_ABSOLUTE("EOR","4D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("EOR","5D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("EOR","59","3","4*")
   VARIANT VARIANT_INDIRECT_X("EOR","41","2","6")
   VARIANT_SELECTED VARIANT_INDIRECT_Y("EOR","51","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // KIL - Implied (processor lock up!)

   "", // LSE - (Indirect),Y

   "", // DOP (undocumented)

   // EOR - Zero Page,X
   "EOR: \"Exclusive-Or\" memory with accumulator<br>"
   "Operation:  A EOR M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("EOR","49","2","2")
   VARIANT VARIANT_ZEROPAGE("EOR","45","2","3")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("EOR","55","2","4")
   VARIANT VARIANT_ABSOLUTE("EOR","4D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("EOR","5D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("EOR","59","3","4*")
   VARIANT VARIANT_INDIRECT_X("EOR","41","2","6")
   VARIANT VARIANT_INDIRECT_Y("EOR","51","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LSR - Zero Page,X
   "LSR: Shift right one bit (memory or accumulator)<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   "Operation:  0 -&gt; |7|6|5|4|3|2|1|0| -&gt; C<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("0","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("LSR","4A","1","2")
   VARIANT VARIANT_ZEROPAGE("LSR","46","2","5")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("LSR","56","2","6")
   VARIANT VARIANT_ABSOLUTE("LSR","4E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("LSR","5E","3","7")
   VARIANT_FOOTER,

   "", // LSE - Zero Page,X (undocumented)

   // CLI
   "CLI: Clear interrupt disable bit<br>"
   "Operation:  0 -&gt; I<br>"
   FLAG_EFFECT("","","","0","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("CLI","58","1","2")
   VARIANT_FOOTER,

   // EOR - Absolute,Y
   "EOR: \"Exclusive-Or\" memory with accumulator<br>"
   "Operation:  A EOR M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("EOR","49","2","2")
   VARIANT VARIANT_ZEROPAGE("EOR","45","2","3")
   VARIANT VARIANT_ZEROPAGE_X("EOR","55","2","4")
   VARIANT VARIANT_ABSOLUTE("EOR","4D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("EOR","5D","3","4*")
   VARIANT_SELECTED VARIANT_ABSOLUTE_Y("EOR","59","3","4*")
   VARIANT VARIANT_INDIRECT_X("EOR","41","2","6")
   VARIANT VARIANT_INDIRECT_Y("EOR","51","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // NOP (undocumented)

   "", // LSE - Absolute,Y (undocumented)

   "", // TOP (undocumented)

   // EOR - Absolute,X
   "EOR: \"Exclusive-Or\" memory with accumulator<br>"
   "Operation:  A EOR M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("EOR","49","2","2")
   VARIANT VARIANT_ZEROPAGE("EOR","45","2","3")
   VARIANT VARIANT_ZEROPAGE_X("EOR","55","2","4")
   VARIANT VARIANT_ABSOLUTE("EOR","4D","3","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("EOR","5D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("EOR","59","3","4*")
   VARIANT VARIANT_INDIRECT_X("EOR","41","2","6")
   VARIANT VARIANT_INDIRECT_Y("EOR","51","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LSR - Absolute,X
   "LSR: Shift right one bit (memory or accumulator)<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   "Operation:  0 -&gt; |7|6|5|4|3|2|1|0| -&gt; C<br>"
   "                 +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("0","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("LSR","4A","1","2")
   VARIANT VARIANT_ZEROPAGE("LSR","46","2","5")
   VARIANT VARIANT_ZEROPAGE_X("LSR","56","2","6")
   VARIANT VARIANT_ABSOLUTE("LSR","4E","3","6")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("LSR","5E","3","7")
   VARIANT_FOOTER,

   "", // LSE - Absolute,X (undocumented)

   // RTS
   "RTS: Return from subroutine<br>"
   "Operation:  PC from S, PC + 1 -&gt; PC<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("RTS","60","1","6")
   VARIANT_FOOTER,

   // ADC - (Indirect,X)
   "ADC: Add memory to accumulator with carry<br>"
   "Operation:  A + M + C -&gt; A, C<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ADC","69","2","2")
   VARIANT VARIANT_ZEROPAGE("ADC","65","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ADC","75","2","4")
   VARIANT VARIANT_ABSOLUTE("ADC","6D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ADC","7D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ADC","79","3","4*")
   VARIANT_SELECTED VARIANT_INDIRECT_X("ADC","61","2","6")
   VARIANT VARIANT_INDIRECT_Y("ADC","71","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // KIL - Implied (processor lock up!)

   "", // RRA - (Indirect,X) (undocumented)

   "", // DOP (undocumented)

   // ADC - Zero Page
   "ADC: Add memory to accumulator with carry<br>"
   "Operation:  A + M + C -&gt; A, C<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ADC","69","2","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("ADC","65","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ADC","75","2","4")
   VARIANT VARIANT_ABSOLUTE("ADC","6D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ADC","7D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ADC","79","3","4*")
   VARIANT VARIANT_INDIRECT_X("ADC","61","2","6")
   VARIANT VARIANT_INDIRECT_Y("ADC","71","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // ROR - Zero Page
   "ROR: Rotate one bit right (memory or accumulator)<br>"
   "             +------------------------------+<br>"
   "             |                              |<br>"
   "             |   +-+    +-+-+-+-+-+-+-+-+   |<br>"
   "Operation:   +-&gt; |C| -&gt; |7|6|5|4|3|2|1|0| &gt;-+<br>"
   "                 +-+    +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("ROR","6A","1","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("ROR","66","2","5")
   VARIANT VARIANT_ZEROPAGE_X("ROR","76","2","6")
   VARIANT VARIANT_ABSOLUTE("ROR","6E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("ROR","7E","3","7")
   VARIANT_FOOTER,

   "", // RRA - Zero Page (undocumented)

   // PLA
   "PLA: Pull accumulator from stack<br>"
   "Operation:  A from S<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("PLA","68","1","4")
   VARIANT_FOOTER,

   // ADC - Immediate
   "ADC: Add memory to accumulator with carry<br>"
   "Operation:  A + M + C -&gt; A, C<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMMEDIATE("ADC","69","2","2")
   VARIANT VARIANT_ZEROPAGE("ADC","65","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ADC","75","2","4")
   VARIANT VARIANT_ABSOLUTE("ADC","6D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ADC","7D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ADC","79","3","4*")
   VARIANT VARIANT_INDIRECT_X("ADC","61","2","6")
   VARIANT VARIANT_INDIRECT_Y("ADC","71","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // ROR - Accumulator
   "ROR: Rotate one bit right (memory or accumulator)<br>"
   "             +------------------------------+<br>"
   "             |                              |<br>"
   "             |   +-+    +-+-+-+-+-+-+-+-+   |<br>"
   "Operation:   +-&gt; |C| -&gt; |7|6|5|4|3|2|1|0| &gt;-+<br>"
   "                 +-+    +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_ACCUMULATOR("ROR","6A","1","2")
   VARIANT VARIANT_ZEROPAGE("ROR","66","2","5")
   VARIANT VARIANT_ZEROPAGE_X("ROR","76","2","6")
   VARIANT VARIANT_ABSOLUTE("ROR","6E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("ROR","7E","3","7")
   VARIANT_FOOTER,

   "", // ARR - Immediate (undocumented)

   // JMP - Indirect
   "JMP: Jump to new location<br>"
   "Operation:  (PC + 1) -&gt; PCL<br>"
   "            (PC + 2) -&gt; PCH<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ABSOLUTE("JMP","4C","3","3")
   VARIANT_SELECTED VARIANT_INDIRECT("JMP","6C","3","5")
   VARIANT_FOOTER,

   // ADC - Absolute
   "ADC: Add memory to accumulator with carry<br>"
   "Operation:  A + M + C -&gt; A, C<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ADC","69","2","2")
   VARIANT VARIANT_ZEROPAGE("ADC","65","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ADC","75","2","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE("ADC","6D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ADC","7D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ADC","79","3","4*")
   VARIANT VARIANT_INDIRECT_X("ADC","61","2","6")
   VARIANT VARIANT_INDIRECT_Y("ADC","71","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // ROR - Absolute
   "ROR: Rotate one bit right (memory or accumulator)<br>"
   "             +------------------------------+<br>"
   "             |                              |<br>"
   "             |   +-+    +-+-+-+-+-+-+-+-+   |<br>"
   "Operation:   +-&gt; |C| -&gt; |7|6|5|4|3|2|1|0| &gt;-+<br>"
   "                 +-+    +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("ROR","6A","1","2")
   VARIANT VARIANT_ZEROPAGE("ROR","66","2","5")
   VARIANT VARIANT_ZEROPAGE_X("ROR","76","2","6")
   VARIANT_SELECTED VARIANT_ABSOLUTE("ROR","6E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("ROR","7E","3","7")
   VARIANT_FOOTER,

   "", // RRA - Absolute (undocumented)

   // BVS
   "BVS: Branch on overflow set<br>"
   "Operation:  Branch on V = 1<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_RELATIVE("BVS","70","2","2*")
   VARIANT_FOOTER
   "* Add 1 if branch occurs to same page.<br>"
   "* Add 2 if branch occurs to different page.",

   // ADC - (Indirect),Y
   "ADC: Add memory to accumulator with carry<br>"
   "Operation:  A + M + C -&gt; A, C<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ADC","69","2","2")
   VARIANT VARIANT_ZEROPAGE("ADC","65","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ADC","75","2","4")
   VARIANT VARIANT_ABSOLUTE("ADC","6D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ADC","7D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ADC","79","3","4*")
   VARIANT VARIANT_INDIRECT_X("ADC","61","2","6")
   VARIANT_SELECTED VARIANT_INDIRECT_Y("ADC","71","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // KIL - Implied (processor lock up!)

   "", // RRA - (Indirect),Y (undocumented)

   "", // DOP (undocumented)

   // ADC - Zero Page,X
   "ADC: Add memory to accumulator with carry<br>"
   "Operation:  A + M + C -&gt; A, C<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ADC","69","2","2")
   VARIANT VARIANT_ZEROPAGE("ADC","65","2","3")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("ADC","75","2","4")
   VARIANT VARIANT_ABSOLUTE("ADC","6D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ADC","7D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ADC","79","3","4*")
   VARIANT VARIANT_INDIRECT_X("ADC","61","2","6")
   VARIANT VARIANT_INDIRECT_Y("ADC","71","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // ROR - Zero Page,X
   "ROR: Rotate one bit right (memory or accumulator)<br>"
   "             +------------------------------+<br>"
   "             |                              |<br>"
   "             |   +-+    +-+-+-+-+-+-+-+-+   |<br>"
   "Operation:   +-&gt; |C| -&gt; |7|6|5|4|3|2|1|0| &gt;-+<br>"
   "                 +-+    +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("ROR","6A","1","2")
   VARIANT VARIANT_ZEROPAGE("ROR","66","2","5")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("ROR","76","2","6")
   VARIANT VARIANT_ABSOLUTE("ROR","6E","3","6")
   VARIANT VARIANT_ABSOLUTE_X("ROR","7E","3","7")
   VARIANT_FOOTER,

   "", // RRA - Zero Page,X (undocumented)

   // SEI
   "SEI: Set interrupt disable status<br>"
   "Operation:  1 -&gt; I<br>"
   FLAG_EFFECT("","","","1","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("SEI","78","1","2")
   VARIANT_FOOTER,

   // ADC - Absolute,Y
   "ADC: Add memory to accumulator with carry<br>"
   "Operation:  A + M + C -&gt; A, C<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ADC","69","2","2")
   VARIANT VARIANT_ZEROPAGE("ADC","65","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ADC","75","2","4")
   VARIANT VARIANT_ABSOLUTE("ADC","6D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("ADC","7D","3","4*")
   VARIANT_SELECTED VARIANT_ABSOLUTE_Y("ADC","79","3","4*")
   VARIANT VARIANT_INDIRECT_X("ADC","61","2","6")
   VARIANT VARIANT_INDIRECT_Y("ADC","71","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // NOP (undocumented)

   "", // RRA - Absolute,Y (undocumented)

   "", // TOP (undocumented)

   // ADC - Absolute,X
   "ADC: Add memory to accumulator with carry<br>"
   "Operation:  A + M + C -&gt; A, C<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("ADC","69","2","2")
   VARIANT VARIANT_ZEROPAGE("ADC","65","2","3")
   VARIANT VARIANT_ZEROPAGE_X("ADC","75","2","4")
   VARIANT VARIANT_ABSOLUTE("ADC","6D","3","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("ADC","7D","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("ADC","79","3","4*")
   VARIANT VARIANT_INDIRECT_X("ADC","61","2","6")
   VARIANT VARIANT_INDIRECT_Y("ADC","71","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // ROR - Absolute,X
   "ROR: Rotate one bit right (memory or accumulator)<br>"
   "             +------------------------------+<br>"
   "             |                              |<br>"
   "             |   +-+    +-+-+-+-+-+-+-+-+   |<br>"
   "Operation:   +-&gt; |C| -&gt; |7|6|5|4|3|2|1|0| &gt;-+<br>"
   "                 +-+    +-+-+-+-+-+-+-+-+<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ACCUMULATOR("ROR","6A","1","2")
   VARIANT VARIANT_ZEROPAGE("ROR","66","2","5")
   VARIANT VARIANT_ZEROPAGE_X("ROR","76","2","6")
   VARIANT VARIANT_ABSOLUTE("ROR","6E","3","6")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("ROR","7E","3","7")
   VARIANT_FOOTER,

   "", // RRA - Absolute,X (undocumented)

   "", // DOP (undocumented)

   // STA - (Indirect,X)
   "STA: Store accumulator in memory<br>"
   "Operation:  A -&gt; M<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("STA","85","2","3")
   VARIANT VARIANT_ZEROPAGE_X("STA","95","2","4")
   VARIANT VARIANT_ABSOLUTE("STA","8D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("STA","9D","3","5")
   VARIANT VARIANT_ABSOLUTE_Y("STA","99","3","5")
   VARIANT_SELECTED VARIANT_INDIRECT_X("STA","81","2","6")
   VARIANT VARIANT_INDIRECT_Y("STA","91","2","6")
   VARIANT_FOOTER,

   "", // DOP (undocumented)

   "", // AXS - (Indirect,X) (undocumented)

   // STY - Zero Page
   "STY: Store index Y in memory<br>"
   "Operation: Y -&gt; M<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_ZEROPAGE("STY","84","2","3")
   VARIANT VARIANT_ZEROPAGE_X("STY","94","2","4")
   VARIANT VARIANT_ABSOLUTE("STY","8C","3","4")
   VARIANT_FOOTER,

   // STA - Zero Page
   "STA: Store accumulator in memory<br>"
   "Operation:  A -&gt; M<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_ZEROPAGE("STA","85","2","3")
   VARIANT VARIANT_ZEROPAGE_X("STA","95","2","4")
   VARIANT VARIANT_ABSOLUTE("STA","8D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("STA","9D","3","5")
   VARIANT VARIANT_ABSOLUTE_Y("STA","99","3","5")
   VARIANT VARIANT_INDIRECT_X("STA","81","2","6")
   VARIANT VARIANT_INDIRECT_Y("STA","91","2","6")
   VARIANT_FOOTER,

   // STX - Zero Page
   "STX: Store index X in memory<br>"
   "Operation: X -&gt; M<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_ZEROPAGE("STX","86","2","3")
   VARIANT VARIANT_ZEROPAGE_X("STX","96","2","4")
   VARIANT VARIANT_ABSOLUTE("STX","8E","3","4")
   VARIANT_FOOTER,

   "", // AXS - Zero Page (undocumented)

   // DEY
   "DEY: Decrement index Y by one<br>"
   "Operation:  Y - 1 -&gt; Y<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("DEY","88","1","2")
   VARIANT_FOOTER,

   "", // DOP (undocumented)

   // TXA
   "TXA: Transfer index X to accumulator<br>"
   "Operation:  X -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("TXA","8A","1","2")
   VARIANT_FOOTER,

   "", // XAA - Immediate (undocumented)

   // STY - Absolute
   "STY: Store index Y in memory<br>"
   "Operation: Y -&gt; M<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("STY","84","2","3")
   VARIANT VARIANT_ZEROPAGE_X("STY","94","2","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE("STY","8C","3","4")
   VARIANT_FOOTER,

   // STA - Absolute
   "STA: Store accumulator in memory<br>"
   "Operation:  A -&gt; M<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("STA","85","2","3")
   VARIANT VARIANT_ZEROPAGE_X("STA","95","2","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE("STA","8D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("STA","9D","3","5")
   VARIANT VARIANT_ABSOLUTE_Y("STA","99","3","5")
   VARIANT VARIANT_INDIRECT_X("STA","81","2","6")
   VARIANT VARIANT_INDIRECT_Y("STA","91","2","6")
   VARIANT_FOOTER,

   // STX - Absolute
   "STX: Store index X in memory<br>"
   "Operation: X -&gt; M<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("STX","86","2","3")
   VARIANT VARIANT_ZEROPAGE_X("STX","96","2","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE("STX","8E","3","4")
   VARIANT_FOOTER,

   "", // AXS - Absolulte (undocumented)

   // BCC
   "BCC: Branch on carry clear<br>"
   "Operation:  Branch on C = 0<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_RELATIVE("BCC","90","2","2*")
   VARIANT_FOOTER
   "* Add 1 if branch occurs to same page.<br>"
   "* Add 2 if branch occurs to different page.",

   // STA - (Indirect),Y
   "STA: Store accumulator in memory<br>"
   "Operation:  A -&gt; M<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("STA","85","2","3")
   VARIANT VARIANT_ZEROPAGE_X("STA","95","2","4")
   VARIANT VARIANT_ABSOLUTE("STA","8D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("STA","9D","3","5")
   VARIANT VARIANT_ABSOLUTE_Y("STA","99","3","5")
   VARIANT VARIANT_INDIRECT_X("STA","81","2","6")
   VARIANT_SELECTED VARIANT_INDIRECT_Y("STA","91","2","6")
   VARIANT_FOOTER,

   "", // KIL - Implied (processor lock up!)

   "", // AXA - (Indirect),Y

   // STY - Zero Page,X
   "STY: Store index Y in memory<br>"
   "Operation: Y -&gt; M<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("STY","84","2","3")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("STY","94","2","4")
   VARIANT VARIANT_ABSOLUTE("STY","8C","3","4")
   VARIANT_FOOTER,

   // STA - Zero Page,X
   "STA: Store accumulator in memory<br>"
   "Operation:  A -&gt; M<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("STA","85","2","3")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("STA","95","2","4")
   VARIANT VARIANT_ABSOLUTE("STA","8D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("STA","9D","3","5")
   VARIANT VARIANT_ABSOLUTE_Y("STA","99","3","5")
   VARIANT VARIANT_INDIRECT_X("STA","81","2","6")
   VARIANT VARIANT_INDIRECT_Y("STA","91","2","6")
   VARIANT_FOOTER,

   // STX - Zero Page,Y
   "STX: Store index X in memory<br>"
   "Operation: X -&gt; M<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("STX","86","2","3")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("STX","96","2","4")
   VARIANT VARIANT_ABSOLUTE("STX","8E","3","4")
   VARIANT_FOOTER,

   "", // AXS - Zero Page,Y

   // TYA
   "TYA: Transfer index Y to accumulator<br>"
   "Operation:  Y -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("TYA","98","1","2")
   VARIANT_FOOTER,

   // STA - Absolute,Y
   "STA: Store accumulator in memory<br>"
   "Operation:  A -&gt; M<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("STA","85","2","3")
   VARIANT VARIANT_ZEROPAGE_X("STA","95","2","4")
   VARIANT VARIANT_ABSOLUTE("STA","8D","3","4")
   VARIANT VARIANT_ABSOLUTE_X("STA","9D","3","5")
   VARIANT_SELECTED VARIANT_ABSOLUTE_Y("STA","99","3","5")
   VARIANT VARIANT_INDIRECT_X("STA","81","2","6")
   VARIANT VARIANT_INDIRECT_Y("STA","91","2","6")
   VARIANT_FOOTER,

   // TXS
   "TXS: Transfer index X to stack pointer<br>"
   "Operation:  X -&gt; S<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("TXS","9A","1","2")
   VARIANT_FOOTER,

   "", // TAS - Absolute,Y (undocumented)

   "", // SAY - Absolute,X (undocumented)

   // STA - Absolute,X
   "STA: Store accumulator in memory<br>"
   "Operation:  A -&gt; M<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("STA","85","2","3")
   VARIANT VARIANT_ZEROPAGE_X("STA","95","2","4")
   VARIANT VARIANT_ABSOLUTE("STA","8D","3","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("STA","9D","3","5")
   VARIANT VARIANT_ABSOLUTE_Y("STA","99","3","5")
   VARIANT VARIANT_INDIRECT_X("STA","81","2","6")
   VARIANT VARIANT_INDIRECT_Y("STA","91","2","6")
   VARIANT_FOOTER,

   "", // XAS - Absolute,Y (undocumented)

   "", // AXA - Absolute,Y (undocumented)

   // LDY - Immediate
   "LDY: Load index Y with memory<br>"
   "Operation:  M -&gt; Y<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMMEDIATE("LDY","A0","2","2")
   VARIANT VARIANT_ZEROPAGE("LDY","A4","2","3")
   VARIANT VARIANT_ZEROPAGE_X("LDY","B4","2","4")
   VARIANT VARIANT_ABSOLUTE("LDY","AC","3","4")
   VARIANT VARIANT_ABSOLUTE_X("LDY","BC","3","4*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LDA - (Indirect,X)
   "LDA: Load accumulator with memory<br>"
   "Operation:  M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDA","A9","2","2")
   VARIANT VARIANT_ZEROPAGE("LDA","A5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("LDA","B5","2","4")
   VARIANT VARIANT_ABSOLUTE("LDA","AD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("LDA","BD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("LDA","B9","3","4*")
   VARIANT_SELECTED VARIANT_INDIRECT_X("LDA","A1","2","6")
   VARIANT VARIANT_INDIRECT_Y("LDA","B1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LDX - Immediate
   "LDX: Load index X with memory<br>"
   "Operation:  M -&gt; X<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMMEDIATE("LDX","A2","2","2")
   VARIANT VARIANT_ZEROPAGE("LDX","A6","2","3")
   VARIANT VARIANT_ZEROPAGE_Y("LDX","B6","2","4")
   VARIANT VARIANT_ABSOLUTE("LDX","AE","3","4")
   VARIANT VARIANT_ABSOLUTE_Y("LDX","BE","3","4*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // LAX - (Indirect,X) (undocumented)

   // LDY - Zero Page
   "LDY: Load index Y with memory<br>"
   "Operation:  M -&gt; Y<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDY","A0","2","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("LDY","A4","2","3")
   VARIANT VARIANT_ZEROPAGE_X("LDY","B4","2","4")
   VARIANT VARIANT_ABSOLUTE("LDY","AC","3","4")
   VARIANT VARIANT_ABSOLUTE_X("LDY","BC","3","4*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LDA - Zero Page
   "LDA: Load accumulator with memory<br>"
   "Operation:  M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDA","A9","2","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("LDA","A5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("LDA","B5","2","4")
   VARIANT VARIANT_ABSOLUTE("LDA","AD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("LDA","BD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("LDA","B9","3","4*")
   VARIANT VARIANT_INDIRECT_X("LDA","A1","2","6")
   VARIANT VARIANT_INDIRECT_Y("LDA","B1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LDX - Zero Page
   "LDX: Load index X with memory<br>"
   "Operation:  M -&gt; X<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDX","A2","2","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("LDX","A6","2","3")
   VARIANT VARIANT_ZEROPAGE_Y("LDX","B6","2","4")
   VARIANT VARIANT_ABSOLUTE("LDX","AE","3","4")
   VARIANT VARIANT_ABSOLUTE_Y("LDX","BE","3","4*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // LAX - Zero Page (undocumented)

   // TAY
   "TAY: Transfer accumulator to index Y<br>"
   "Operation:  A -&gt; Y<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("TAY","A8","1","2")
   VARIANT_FOOTER,

   // LDA - Immediate
   "LDA: Load accumulator with memory<br>"
   "Operation:  M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMMEDIATE("LDA","A9","2","2")
   VARIANT VARIANT_ZEROPAGE("LDA","A5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("LDA","B5","2","4")
   VARIANT VARIANT_ABSOLUTE("LDA","AD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("LDA","BD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("LDA","B9","3","4*")
   VARIANT VARIANT_INDIRECT_X("LDA","A1","2","6")
   VARIANT VARIANT_INDIRECT_Y("LDA","B1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // TAX
   "TAX: Transfer accumulator to index X<br>"
   "Operation:  A -&gt; X<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("TAX","AA","1","2")
   VARIANT_FOOTER,

   "", // OAL - Immediate (undocumented)

   // LDY - Absolute
   "LDY: Load index Y with memory<br>"
   "Operation:  M -&gt; Y<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDY","A0","2","2")
   VARIANT VARIANT_ZEROPAGE("LDY","A4","2","3")
   VARIANT VARIANT_ZEROPAGE_X("LDY","B4","2","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE("LDY","AC","3","4")
   VARIANT VARIANT_ABSOLUTE_X("LDY","BC","3","4*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LDA - Absolute
   "LDA: Load accumulator with memory<br>"
   "Operation:  M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDA","A9","2","2")
   VARIANT VARIANT_ZEROPAGE("LDA","A5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("LDA","B5","2","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE("LDA","AD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("LDA","BD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("LDA","B9","3","4*")
   VARIANT VARIANT_INDIRECT_X("LDA","A1","2","6")
   VARIANT VARIANT_INDIRECT_Y("LDA","B1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LDX - Absolute
   "LDX: Load index X with memory<br>"
   "Operation:  M -&gt; X<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDX","A2","2","2")
   VARIANT VARIANT_ZEROPAGE("LDX","A6","2","3")
   VARIANT VARIANT_ZEROPAGE_Y("LDX","B6","2","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE("LDX","AE","3","4")
   VARIANT VARIANT_ABSOLUTE_Y("LDX","BE","3","4*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // LAX - Absolute (undocumented)

   // BCS
   "BCS: Branch on carry set<br>"
   "Operation:  Branch on C = 1<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_RELATIVE("BCS","B0","2","2*")
   VARIANT_FOOTER
   "* Add 1 if branch occurs to same page.<br>"
   "* Add 2 if branch occurs to different page.",

   // LDA - (Indirect),Y
   "LDA: Load accumulator with memory<br>"
   "Operation:  M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDA","A9","2","2")
   VARIANT VARIANT_ZEROPAGE("LDA","A5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("LDA","B5","2","4")
   VARIANT VARIANT_ABSOLUTE("LDA","AD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("LDA","BD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("LDA","B9","3","4*")
   VARIANT VARIANT_INDIRECT_X("LDA","A1","2","6")
   VARIANT_SELECTED VARIANT_INDIRECT_Y("LDA","B1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // KIL - Implied (processor lock up!)

   "", // LAX - (Indirect),Y (undocumented)

   // LDY - Zero Page,X
   "LDY: Load index Y with memory<br>"
   "Operation:  M -&gt; Y<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDY","A0","2","2")
   VARIANT VARIANT_ZEROPAGE("LDY","A4","2","3")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("LDY","B4","2","4")
   VARIANT VARIANT_ABSOLUTE("LDY","AC","3","4")
   VARIANT VARIANT_ABSOLUTE_X("LDY","BC","3","4*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LDA - Zero Page,X
   "LDA: Load accumulator with memory<br>"
   "Operation:  M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDA","A9","2","2")
   VARIANT VARIANT_ZEROPAGE("LDA","A5","2","3")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("LDA","B5","2","4")
   VARIANT VARIANT_ABSOLUTE("LDA","AD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("LDA","BD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("LDA","B9","3","4*")
   VARIANT VARIANT_INDIRECT_X("LDA","A1","2","6")
   VARIANT VARIANT_INDIRECT_Y("LDA","B1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LDX - Zero Page,Y
   "LDX: Load index X with memory<br>"
   "Operation:  M -&gt; X<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDX","A2","2","2")
   VARIANT VARIANT_ZEROPAGE("LDX","A6","2","3")
   VARIANT_SELECTED VARIANT_ZEROPAGE_Y("LDX","B6","2","4")
   VARIANT VARIANT_ABSOLUTE("LDX","AE","3","4")
   VARIANT VARIANT_ABSOLUTE_Y("LDX","BE","3","4*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // LAX - Zero Page,X (undocumented)

   // CLV
   "CLV: Clear overflow flag<br>"
   "Operation:  0 -&gt; V<br>"
   FLAG_EFFECT("","","","","","0")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("CLV","B8","1","2")
   VARIANT_FOOTER,

   // LDA - Absolute,Y
   "LDA: Load accumulator with memory<br>"
   "Operation:  M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDA","A9","2","2")
   VARIANT VARIANT_ZEROPAGE("LDA","A5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("LDA","B5","2","4")
   VARIANT VARIANT_ABSOLUTE("LDA","AD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("LDA","BD","3","4*")
   VARIANT_SELECTED VARIANT_ABSOLUTE_Y("LDA","B9","3","4*")
   VARIANT VARIANT_INDIRECT_X("LDA","A1","2","6")
   VARIANT VARIANT_INDIRECT_Y("LDA","B1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // TSX
   "TSX: Transfer stack pointer to index X<br>"
   "Operation:  S -&gt; X<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("TSX","BA","1","2")
   VARIANT_FOOTER,

   "", // LAS - Absolute,Y (undocumented)

   // LDY - Absolute,X
   "LDY: Load index Y with memory<br>"
   "Operation:  M -&gt; Y<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDY","A0","2","2")
   VARIANT VARIANT_ZEROPAGE("LDY","A4","2","3")
   VARIANT VARIANT_ZEROPAGE_X("LDY","B4","2","4")
   VARIANT VARIANT_ABSOLUTE("LDY","AC","3","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("LDY","BC","3","4*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LDA - Absolute,X
   "LDA: Load accumulator with memory<br>"
   "Operation:  M -&gt; A<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDA","A9","2","2")
   VARIANT VARIANT_ZEROPAGE("LDA","A5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("LDA","B5","2","4")
   VARIANT VARIANT_ABSOLUTE("LDA","AD","3","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("LDA","BD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("LDA","B9","3","4*")
   VARIANT VARIANT_INDIRECT_X("LDA","A1","2","6")
   VARIANT VARIANT_INDIRECT_Y("LDA","B1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // LDX - Absolute,Y
   "LDX: Load index X with memory<br>"
   "Operation:  M -&gt; X<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("LDX","A2","2","2")
   VARIANT VARIANT_ZEROPAGE("LDX","A6","2","3")
   VARIANT VARIANT_ZEROPAGE_Y("LDX","B6","2","4")
   VARIANT VARIANT_ABSOLUTE("LDX","AE","3","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE_Y("LDX","BE","3","4*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // LAX - Absolute,Y (undocumented)

   // CPY - Immediate
   "CPY: Compare memory and index Y<br>"
   "Operation:  Y - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMMEDIATE("CPY","C0","2","2")
   VARIANT VARIANT_ZEROPAGE("CPY","C4","2","3")
   VARIANT VARIANT_ABSOLUTE("CPY","CC","3","4")
   VARIANT_FOOTER,

   // CMP - (Indirect,X)
   "CMP: Compare memory and accumulator<br>"
   "Operation:  A - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("CMP","C9","2","2")
   VARIANT VARIANT_ZEROPAGE("CMP","C5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("CMP","D5","2","4")
   VARIANT VARIANT_ABSOLUTE("CMP","CD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("CMP","DD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("CMP","D9","3","4*")
   VARIANT_SELECTED VARIANT_INDIRECT_X("CMP","C1","2","6")
   VARIANT VARIANT_INDIRECT_Y("CMP","D1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // DOP (undocumented)

   "", // DCM - (Indirect,X) (undocumented)

   // CPY - Zero Page
   "CPY: Compare memory and index Y<br>"
   "Operation:  Y - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("CPY","C0","2","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("CPY","C4","2","3")
   VARIANT VARIANT_ABSOLUTE("CPY","CC","3","4")
   VARIANT_FOOTER,

   // CMP - Zero Page
   "CMP: Compare memory and accumulator<br>"
   "Operation:  A - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("CMP","C9","2","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("CMP","C5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("CMP","D5","2","4")
   VARIANT VARIANT_ABSOLUTE("CMP","CD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("CMP","DD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("CMP","D9","3","4*")
   VARIANT VARIANT_INDIRECT_X("CMP","C1","2","6")
   VARIANT VARIANT_INDIRECT_Y("CMP","D1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // DEC - Zero Page
   "DEC: Decrement memory by one<br>"
   "Operation:  M - 1 -&gt; M<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_ZEROPAGE("DEC","C6","2","5")
   VARIANT VARIANT_ZEROPAGE_X("DEC","D6","2","6")
   VARIANT VARIANT_ABSOLUTE("DEC","CE","3","6")
   VARIANT VARIANT_ABSOLUTE_X("DEC","DE","3","7")
   VARIANT_FOOTER,

   "", // DCM - Zero Page (undocumented)

   // INY
   "INY: Increment Index Y by one<br>"
   "Operation:  Y + 1 -&gt; Y<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("INY","C8","1","2")
   VARIANT_FOOTER,

   // CMP - Immediate
   "CMP: Compare memory and accumulator<br>"
   "Operation:  A - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMMEDIATE("CMP","C9","2","2")
   VARIANT VARIANT_ZEROPAGE("CMP","C5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("CMP","D5","2","4")
   VARIANT VARIANT_ABSOLUTE("CMP","CD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("CMP","DD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("CMP","D9","3","4*")
   VARIANT VARIANT_INDIRECT_X("CMP","C1","2","6")
   VARIANT VARIANT_INDIRECT_Y("CMP","D1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // DEX
   "DEX: DEX Decrement index X by one<br>"
   "Operation:  X - 1 -&gt; X<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("DEX","CA","1","2")
   VARIANT_FOOTER,

   "", // SAX - Immediate (undocumented)

   // CPY - Absolute
   "CPY: Compare memory and index Y<br>"
   "Operation:  Y - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("CPY","C0","2","2")
   VARIANT VARIANT_ZEROPAGE("CPY","C4","2","3")
   VARIANT_SELECTED VARIANT_ABSOLUTE("CPY","CC","3","4")
   VARIANT_FOOTER,

   // CMP - Absolute
   "CMP: Compare memory and accumulator<br>"
   "Operation:  A - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("CMP","C9","2","2")
   VARIANT VARIANT_ZEROPAGE("CMP","C5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("CMP","D5","2","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE("CMP","CD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("CMP","DD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("CMP","D9","3","4*")
   VARIANT VARIANT_INDIRECT_X("CMP","C1","2","6")
   VARIANT VARIANT_INDIRECT_Y("CMP","D1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // DEC - Absolute
   "DEC: Decrement memory by one<br>"
   "Operation:  M - 1 -&gt; M<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("DEC","C6","2","5")
   VARIANT VARIANT_ZEROPAGE_X("DEC","D6","2","6")
   VARIANT_SELECTED VARIANT_ABSOLUTE("DEC","CE","3","6")
   VARIANT VARIANT_ABSOLUTE_X("DEC","DE","3","7")
   VARIANT_FOOTER,

   "", // DCM - Absolute (undocumented)

   // BNE
   "BNE: Branch on result not zero<br>"
   "Operation:  Branch on Z = 0<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_RELATIVE("BNE","D0","2","2*")
   VARIANT_FOOTER
   "* Add 1 if branch occurs to same page.<br>"
   "* Add 2 if branch occurs to different page.",

   // CMP   (Indirect),Y
   "CMP: Compare memory and accumulator<br>"
   "Operation:  A - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("CMP","C9","2","2")
   VARIANT VARIANT_ZEROPAGE("CMP","C5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("CMP","D5","2","4")
   VARIANT VARIANT_ABSOLUTE("CMP","CD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("CMP","DD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("CMP","D9","3","4*")
   VARIANT VARIANT_INDIRECT_X("CMP","C1","2","6")
   VARIANT_SELECTED VARIANT_INDIRECT_Y("CMP","D1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // KIL - Implied (processor lock up!)

   "", // DCM - (Indirect),Y (undocumented)

   "", // DOP (undocumented)

   // CMP - Zero Page,X
   "CMP: Compare memory and accumulator<br>"
   "Operation:  A - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("CMP","C9","2","2")
   VARIANT VARIANT_ZEROPAGE("CMP","C5","2","3")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("CMP","D5","2","4")
   VARIANT VARIANT_ABSOLUTE("CMP","CD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("CMP","DD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("CMP","D9","3","4*")
   VARIANT VARIANT_INDIRECT_X("CMP","C1","2","6")
   VARIANT VARIANT_INDIRECT_Y("CMP","D1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // DEC - Zero Page,X
   "DEC: Decrement memory by one<br>"
   "Operation:  M - 1 -&gt; M<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("DEC","C6","2","5")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("DEC","D6","2","6")
   VARIANT VARIANT_ABSOLUTE("DEC","CE","3","6")
   VARIANT VARIANT_ABSOLUTE_X("DEC","DE","3","7")
   VARIANT_FOOTER,

   "", // DCM - Zero Page,X (undocumented)

   // CLD
   "CLD: Clear decimal mode<br>"
   "Operation:  0 -&gt; D<br>"
   FLAG_EFFECT("","","","","0","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("CLD","D8","1","2")
   VARIANT_FOOTER,

   // CMP - Absolute,Y
   "CMP: Compare memory and accumulator<br>"
   "Operation:  A - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("CMP","C9","2","2")
   VARIANT VARIANT_ZEROPAGE("CMP","C5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("CMP","D5","2","4")
   VARIANT VARIANT_ABSOLUTE("CMP","CD","3","4")
   VARIANT VARIANT_ABSOLUTE_X("CMP","DD","3","4*")
   VARIANT_SELECTED VARIANT_ABSOLUTE_Y("CMP","D9","3","4*")
   VARIANT VARIANT_INDIRECT_X("CMP","C1","2","6")
   VARIANT VARIANT_INDIRECT_Y("CMP","D1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // NOP (undocumented)

   "", // DCM - Absolute,Y (undocumented)

   "", // TOP (undocumented)

   // CMP - Absolute,X
   "CMP: Compare memory and accumulator<br>"
   "Operation:  A - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("CMP","C9","2","2")
   VARIANT VARIANT_ZEROPAGE("CMP","C5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("CMP","D5","2","4")
   VARIANT VARIANT_ABSOLUTE("CMP","CD","3","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("CMP","DD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("CMP","D9","3","4*")
   VARIANT VARIANT_INDIRECT_X("CMP","C1","2","6")
   VARIANT VARIANT_INDIRECT_Y("CMP","D1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // DEC - Absolute,X
   "DEC: Decrement memory by one<br>"
   "Operation:  M - 1 -&gt; M<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("DEC","C6","2","5")
   VARIANT VARIANT_ZEROPAGE_X("DEC","D6","2","6")
   VARIANT VARIANT_ABSOLUTE("DEC","CE","3","6")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("DEC","DE","3","7")
   VARIANT_FOOTER,

   "", // DCM - Absolute,X (undocumented)

   // CPX - Immediate
   "CPX: Compare memory and index X<br>"
   "Operation:  X - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMMEDIATE("CPX","E0","2","2")
   VARIANT VARIANT_ZEROPAGE("CPX","E4","2","3")
   VARIANT VARIANT_ABSOLUTE("CPX","EC","3","4")
   VARIANT_FOOTER,

   // SBC - (Indirect,X)
   "SBC: Subtract memory from accumulator with borrow<br>"
   "Operation:  A - M - (1-C) -&gt; A<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("SBC","E9","2","2")
   VARIANT VARIANT_ZEROPAGE("SBC","E5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("SBC","F5","2","4")
   VARIANT VARIANT_ABSOLUTE("SBC","ED","3","4")
   VARIANT VARIANT_ABSOLUTE_X("SBC","FD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("SBC","F9","3","4*")
   VARIANT_SELECTED VARIANT_INDIRECT_X("SBC","E1","2","6")
   VARIANT VARIANT_INDIRECT_Y("SBC","F1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // DOP (undocumented)

   "", // INS - (Indirect,X) (undocumented)

   // CPX - Zero Page
   "CPX: Compare memory and index X<br>"
   "Operation:  X - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("CPX","E0","2","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("CPX","E4","2","3")
   VARIANT VARIANT_ABSOLUTE("CPX","EC","3","4")
   VARIANT_FOOTER,

   // SBC - Zero Page
   "SBC: Subtract memory from accumulator with borrow<br>"
   "Operation:  A - M - (1-C) -&gt; A<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("SBC","E9","2","2")
   VARIANT_SELECTED VARIANT_ZEROPAGE("SBC","E5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("SBC","F5","2","4")
   VARIANT VARIANT_ABSOLUTE("SBC","ED","3","4")
   VARIANT VARIANT_ABSOLUTE_X("SBC","FD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("SBC","F9","3","4*")
   VARIANT VARIANT_INDIRECT_X("SBC","E1","2","6")
   VARIANT VARIANT_INDIRECT_Y("SBC","F1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // INC - Zero Page
   "INC: Increment memory by one<br>"
   "Operation:  M + 1 -&gt; M<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_ZEROPAGE("INC","E6","2","5")
   VARIANT VARIANT_ZEROPAGE_X("INC","F6","2","6")
   VARIANT VARIANT_ABSOLUTE("INC","EE","3","6")
   VARIANT VARIANT_ABSOLUTE_X("INC","FE","3","7")
   VARIANT_FOOTER,

   "", // INS - Zero Page (undocumented)

   // INX
   "INX: Increment Index X by one<br>"
   "Operation:  X + 1 -&gt; X<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("INX","E8","1","2")
   VARIANT_FOOTER,

   // SBC - Immediate
   "SBC: Subtract memory from accumulator with borrow<br>"
   "Operation:  A - M - (1-C) -&gt; A<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMMEDIATE("SBC","E9","2","2")
   VARIANT VARIANT_ZEROPAGE("SBC","E5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("SBC","F5","2","4")
   VARIANT VARIANT_ABSOLUTE("SBC","ED","3","4")
   VARIANT VARIANT_ABSOLUTE_X("SBC","FD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("SBC","F9","3","4*")
   VARIANT VARIANT_INDIRECT_X("SBC","E1","2","6")
   VARIANT VARIANT_INDIRECT_Y("SBC","F1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // NOP

   // SBC - Immediate (undocumented)
   "SBC: Subtract memory from accumulator with borrow<br>"
   "Operation:  A - M - (1-C) -&gt; A<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMMEDIATE("SBC","E9","2","2")
   VARIANT VARIANT_ZEROPAGE("SBC","E5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("SBC","F5","2","4")
   VARIANT VARIANT_ABSOLUTE("SBC","ED","3","4")
   VARIANT VARIANT_ABSOLUTE_X("SBC","FD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("SBC","F9","3","4*")
   VARIANT VARIANT_INDIRECT_X("SBC","E1","2","6")
   VARIANT VARIANT_INDIRECT_Y("SBC","F1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // CPX - Absolute
   "CPX: Compare memory and index X<br>"
   "Operation:  X - M<br>"
   FLAG_EFFECT("/","/","/","","","")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("CPX","E0","2","2")
   VARIANT VARIANT_ZEROPAGE("CPX","E4","2","3")
   VARIANT_SELECTED VARIANT_ABSOLUTE("CPX","EC","3","4")
   VARIANT_FOOTER,

   // SBC - Absolute
   "SBC: Subtract memory from accumulator with borrow<br>"
   "Operation:  A - M - (1-C) -&gt; A<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("SBC","E9","2","2")
   VARIANT VARIANT_ZEROPAGE("SBC","E5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("SBC","F5","2","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE("SBC","ED","3","4")
   VARIANT VARIANT_ABSOLUTE_X("SBC","FD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("SBC","F9","3","4*")
   VARIANT VARIANT_INDIRECT_X("SBC","E1","2","6")
   VARIANT VARIANT_INDIRECT_Y("SBC","F1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // INC - Absolute
   "INC: Increment memory by one<br>"
   "Operation:  M + 1 -&gt; M<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("INC","E6","2","5")
   VARIANT VARIANT_ZEROPAGE_X("INC","F6","2","6")
   VARIANT_SELECTED VARIANT_ABSOLUTE("INC","EE","3","6")
   VARIANT VARIANT_ABSOLUTE_X("INC","FE","3","7")
   VARIANT_FOOTER,

   "", // INS - Absolute (undocumented)

   // BEQ
   "BEQ: Branch on result zero<br>"
   "Operation:  Branch on Z = 1<br>"
   FLAG_EFFECT("","","","","","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_RELATIVE("BEQ","F0","2","2*")
   VARIANT_FOOTER
   "* Add 1 if branch occurs to same page.<br>"
   "* Add 2 if branch occurs to different page.",

   // SBC - (Indirect),Y
   "SBC: Subtract memory from accumulator with borrow<br>"
   "Operation:  A - M - (1-C) -&gt; A<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("SBC","E9","2","2")
   VARIANT VARIANT_ZEROPAGE("SBC","E5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("SBC","F5","2","4")
   VARIANT VARIANT_ABSOLUTE("SBC","ED","3","4")
   VARIANT VARIANT_ABSOLUTE_X("SBC","FD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("SBC","F9","3","4*")
   VARIANT VARIANT_INDIRECT_X("SBC","E1","2","6")
   VARIANT_SELECTED VARIANT_INDIRECT_Y("SBC","F1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // KIL - Implied (processor lock up!)

   "", // INS - (Indirect),Y (undocumented)

   "", // DOP (undocumented)

   // SBC - Zero Page,X
   "SBC: Subtract memory from accumulator with borrow<br>"
   "Operation:  A - M - (1-C) -&gt; A<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("SBC","E9","2","2")
   VARIANT VARIANT_ZEROPAGE("SBC","E5","2","3")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("SBC","F5","2","4")
   VARIANT VARIANT_ABSOLUTE("SBC","ED","3","4")
   VARIANT VARIANT_ABSOLUTE_X("SBC","FD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("SBC","F9","3","4*")
   VARIANT VARIANT_INDIRECT_X("SBC","E1","2","6")
   VARIANT VARIANT_INDIRECT_Y("SBC","F1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // INC - Zero Page,X
   "INC: Increment memory by one<br>"
   "Operation:  M + 1 -&gt; M<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("INC","E6","2","5")
   VARIANT_SELECTED VARIANT_ZEROPAGE_X("INC","F6","2","6")
   VARIANT VARIANT_ABSOLUTE("INC","EE","3","6")
   VARIANT VARIANT_ABSOLUTE_X("INC","FE","3","7")
   VARIANT_FOOTER,

   "", // INS - Zero Page,X (undocumented)

   // SED
   "SED: Set decimal mode<br>"
   "Operation:  1 -&gt; D<br>"
   FLAG_EFFECT("","","","","1","")
   VARIANT_HEADER
   VARIANT_SELECTED VARIANT_IMPLIED("SED","F8","1","2")
   VARIANT_FOOTER,

   // SBC - Absolute,Y
   "SBC: Subtract memory from accumulator with borrow<br>"
   "Operation:  A - M - (1-C) -&gt; A<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("SBC","E9","2","2")
   VARIANT VARIANT_ZEROPAGE("SBC","E5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("SBC","F5","2","4")
   VARIANT VARIANT_ABSOLUTE("SBC","ED","3","4")
   VARIANT VARIANT_ABSOLUTE_X("SBC","FD","3","4*")
   VARIANT_SELECTED VARIANT_ABSOLUTE_Y("SBC","F9","3","4*")
   VARIANT VARIANT_INDIRECT_X("SBC","E1","2","6")
   VARIANT VARIANT_INDIRECT_Y("SBC","F1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   "", // NOP (undocumented)

   "", // INS - Absolute,Y (undocumented)

   "", // TOP (undocumented)

   // SBC - Absolute,X
   "SBC: Subtract memory from accumulator with borrow<br>"
   "Operation:  A - M - (1-C) -&gt; A<br>"
   FLAG_EFFECT("/","/","/","","","/")
   VARIANT_HEADER
   VARIANT VARIANT_IMMEDIATE("SBC","E9","2","2")
   VARIANT VARIANT_ZEROPAGE("SBC","E5","2","3")
   VARIANT VARIANT_ZEROPAGE_X("SBC","F5","2","4")
   VARIANT VARIANT_ABSOLUTE("SBC","ED","3","4")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("SBC","FD","3","4*")
   VARIANT VARIANT_ABSOLUTE_Y("SBC","F9","3","4*")
   VARIANT VARIANT_INDIRECT_X("SBC","E1","2","6")
   VARIANT VARIANT_INDIRECT_Y("SBC","F1","2","5*")
   VARIANT_FOOTER
   "* Add 1 if page boundary is crossed.",

   // INC - Absolute,X
   "INC: Increment memory by one<br>"
   "Operation:  M + 1 -&gt; M<br>"
   FLAG_EFFECT("/","/","","","","")
   VARIANT_HEADER
   VARIANT VARIANT_ZEROPAGE("INC","E6","2","5")
   VARIANT VARIANT_ZEROPAGE_X("INC","F6","2","6")
   VARIANT VARIANT_ABSOLUTE("INC","EE","3","6")
   VARIANT_SELECTED VARIANT_ABSOLUTE_X("INC","FE","3","7")
   VARIANT_FOOTER,

   ""  // INS - Absolute,X (undocumented)
};

const char* OPCODEINFO(unsigned char op)
{
   return *(m_6502opcodeInfo+op);
}

unsigned char OPCODESIZE ( unsigned char op )
{
   C6502_opcode* pOp;

   pOp = m_6502opcode+op;
   return *(opcode_size+pOp->amode);
}

static QColor color [] =
{
   QColor(255,0,0),
   QColor(0,255,0),
   QColor(0,0,255),
   QColor(255,255,0)
};

static QColor dmaColor [] =
{
   QColor(0,0,0),
   QColor(255,0,255),
   QColor(0,255,255),
   QColor(0,0,0)
};

static C6502 __init __attribute((unused));

C6502::C6502()
{
   int addr;
   for ( addr = 0; addr < MEM_2KB; addr++ )
   {
      m_RAMdisassembly[addr] = new char [ 16 ];
   }
}

C6502::~C6502()
{
   int addr;

   for ( addr = 0; addr < MEM_2KB; addr++ )
   {
      delete [] m_RAMdisassembly[addr];
   }
}

void C6502::RENDERCODEDATALOGGER ( void )
{
   unsigned int idxx, idxy;
   UINT cycleDiff;
   UINT curCycle = CCodeDataLogger::GetCurCycle ();
   QColor lcolor;
   CCodeDataLogger* pLogger;
   LoggerInfo* pLogEntry;

   // Clearly...
   memset ( m_pCodeDataLoggerInspectorTV, 255, 196608 );

   // Show CPU RAM...
   pLogger = &m_logger;
   for ( idxx = 0; idxx < MEM_2KB; idxx++ )
   {
      pLogEntry = pLogger->GetLogEntry(idxx);
      cycleDiff = (curCycle-pLogEntry->cycle)/17800;
      if ( cycleDiff > 199 ) cycleDiff = 199;

      if ( pLogEntry->count )
      {
         if ( pLogEntry->type == eLogger_DMA )
         {
            lcolor = dmaColor[pLogEntry->source];
         }
         else
         {
            lcolor = color[pLogEntry->type];
         }
         if ( !lcolor.red() )
         {
            lcolor.setRed(cycleDiff);
         }
         if ( !lcolor.green() )
         {
            lcolor.setGreen(cycleDiff);
         }
         if ( !lcolor.blue() )
         {
            lcolor.setBlue(cycleDiff);
         }
         m_pCodeDataLoggerInspectorTV[(idxx * 3) + 0] = lcolor.red();
         m_pCodeDataLoggerInspectorTV[(idxx * 3) + 1] = lcolor.green();
         m_pCodeDataLoggerInspectorTV[(idxx * 3) + 2] = lcolor.blue();
      }
   }

   // Show I/O region...
   pLogger = &m_logger;
   for ( idxx = MEM_8KB; idxx < 0x5C00; idxx++ )
   {
      pLogEntry = pLogger->GetLogEntry(idxx);
      cycleDiff = (curCycle-pLogEntry->cycle)/17800;
      if ( cycleDiff > 199 ) cycleDiff = 199;

      if ( pLogEntry->count )
      {
         if ( pLogEntry->type == eLogger_DMA )
         {
            lcolor = dmaColor[pLogEntry->source];
         }
         else
         {
            lcolor = color[pLogEntry->type];
         }
         if ( !lcolor.red() )
         {
            lcolor.setRed(cycleDiff);
         }
         if ( !lcolor.green() )
         {
            lcolor.setGreen(cycleDiff);
         }
         if ( !lcolor.blue() )
         {
            lcolor.setBlue(cycleDiff);
         }
         m_pCodeDataLoggerInspectorTV[(idxx * 3) + 0] = lcolor.red();
         m_pCodeDataLoggerInspectorTV[(idxx * 3) + 1] = lcolor.green();
         m_pCodeDataLoggerInspectorTV[(idxx * 3) + 2] = lcolor.blue();
      }
   }

   // Show cartrige EXRAM memory...
   pLogger = CROM::EXRAMLOGGER();
   for ( idxx = 0; idxx < MEM_1KB; idxx++ )
   {
      pLogEntry = pLogger->GetLogEntry(idxx);
      cycleDiff = (curCycle-pLogEntry->cycle)/17800;
      if ( cycleDiff > 199 ) cycleDiff = 199;

      if ( pLogEntry->count )
      {
         if ( pLogEntry->type == eLogger_DMA )
         {
            lcolor = dmaColor[pLogEntry->source];
         }
         else
         {
            lcolor = color[pLogEntry->type];
         }
         if ( !lcolor.red() )
         {
            lcolor.setRed(cycleDiff);
         }
         if ( !lcolor.green() )
         {
            lcolor.setGreen(cycleDiff);
         }
         if ( !lcolor.blue() )
         {
            lcolor.setBlue(cycleDiff);
         }
         m_pCodeDataLoggerInspectorTV[((0x5C00+idxx) * 3) + 0] = lcolor.red();
         m_pCodeDataLoggerInspectorTV[((0x5C00+idxx) * 3) + 1] = lcolor.green();
         m_pCodeDataLoggerInspectorTV[((0x5C00+idxx) * 3) + 2] = lcolor.blue();
      }
   }

   // Show cartrige SRAM memory...
   for ( idxx = 0; idxx < MEM_8KB; idxx++ )
   {
      pLogger = CROM::SRAMLOGGER(0x6000);

      pLogEntry = pLogger->GetLogEntry(idxx);
      cycleDiff = (curCycle-pLogEntry->cycle)/17800;
      if ( cycleDiff > 199 ) cycleDiff = 199;

      if ( pLogEntry->count )
      {
         if ( pLogEntry->type == eLogger_DMA )
         {
            lcolor = dmaColor[pLogEntry->source];
         }
         else
         {
            lcolor = color[pLogEntry->type];
         }
         if ( !lcolor.red() )
         {
            lcolor.setRed(cycleDiff);
         }
         if ( !lcolor.green() )
         {
            lcolor.setGreen(cycleDiff);
         }
         if ( !lcolor.blue() )
         {
            lcolor.setBlue(cycleDiff);
         }
         m_pCodeDataLoggerInspectorTV[((0x6000+idxx) * 3) + 0] = lcolor.red();
         m_pCodeDataLoggerInspectorTV[((0x6000+idxx) * 3) + 1] = lcolor.green();
         m_pCodeDataLoggerInspectorTV[((0x6000+idxx) * 3) + 2] = lcolor.blue();
      }
   }

   // Show cartrige PRG-ROM memory...
   for ( idxy = 0; idxy < 4; idxy++ )
   {
      for ( idxx = 0; idxx < MEM_8KB; idxx++ )
      {
         pLogger = CROM::LOGGER(MEM_32KB+(idxy*MEM_8KB)+idxx);

         pLogEntry = pLogger->GetLogEntry(idxx);
         cycleDiff = (curCycle-pLogEntry->cycle)/17800;
         if ( cycleDiff > 199 ) cycleDiff = 199;

         if ( pLogEntry->count )
         {
            if ( pLogEntry->type == eLogger_DMA )
            {
               lcolor = dmaColor[pLogEntry->source];
            }
            else
            {
               lcolor = color[pLogEntry->type];
            }
            if ( !lcolor.red() )
            {
               lcolor.setRed(cycleDiff);
            }
            if ( !lcolor.green() )
            {
               lcolor.setGreen(cycleDiff);
            }
            if ( !lcolor.blue() )
            {
               lcolor.setBlue(cycleDiff);
            }
            m_pCodeDataLoggerInspectorTV[((MEM_32KB+(idxy*MEM_8KB)+idxx) * 3) + 0] = lcolor.red();
            m_pCodeDataLoggerInspectorTV[((MEM_32KB+(idxy*MEM_8KB)+idxx) * 3) + 1] = lcolor.green();
            m_pCodeDataLoggerInspectorTV[((MEM_32KB+(idxy*MEM_8KB)+idxx) * 3) + 2] = lcolor.blue();
         }
      }
   }
}

void C6502::RENDEREXECUTIONVISUALIZER ( void )
{
   unsigned int idxx, idxy;
   MarkerSetInfo* pMarker;
   int marker;
   int frameDiff;
   char marked;
   unsigned int numScanlines = CPPU::SCANLINES();

   for ( idxy = 0; idxy < 512; idxy++ )
   {
      for ( idxx = 0; idxx < 512; idxx++ )
      {
         if ( (idxx >= PPU_CYCLES_PER_SCANLINE) || (idxy >= numScanlines) )
         {
            // Black otherwise...
            m_pExecutionVisualizerInspectorTV[((idxy<<9) * 3) + (idxx * 3) + 0] = 0;
            m_pExecutionVisualizerInspectorTV[((idxy<<9) * 3) + (idxx * 3) + 1] = 0;
            m_pExecutionVisualizerInspectorTV[((idxy<<9) * 3) + (idxx * 3) + 2] = 0;
         }

         if ( (idxx < PPU_CYCLES_PER_SCANLINE) && (idxy < numScanlines) )
         {
            marked = 0;
            for ( marker = 0; marker < m_marker.GetNumMarkers(); marker++ )
            {
               pMarker = m_marker.GetMarker(marker);
               frameDiff = pMarker->endFrame-pMarker->startFrame;
               if ( ((pMarker->state == eMarkerSet_Started) ||
                    (pMarker->state == eMarkerSet_Complete)) &&
                    (pMarker->endCycle != MARKER_NOT_MARKED) &&
                    (((frameDiff == 0) &&
                    (VISY_VISX_TO_CYCLE(idxy,idxx) >= pMarker->startCycle) &&
                    (VISY_VISX_TO_CYCLE(idxy,idxx) <= pMarker->endCycle)) ||
                    ((frameDiff == 1) &&
                    ((pMarker->startCycle > pMarker->endCycle) &&
                    ((VISY_VISX_TO_CYCLE(idxy,idxx) >= pMarker->startCycle) ||
                    (VISY_VISX_TO_CYCLE(idxy,idxx) <= pMarker->endCycle)))) ||
                    (frameDiff > 1)) )
               {
                  // Marker color!
                  m_pExecutionVisualizerInspectorTV[((idxy<<9) * 3) + (idxx * 3) + 0] = pMarker->red;
                  m_pExecutionVisualizerInspectorTV[((idxy<<9) * 3) + (idxx * 3) + 1] = pMarker->green;
                  m_pExecutionVisualizerInspectorTV[((idxy<<9) * 3) + (idxx * 3) + 2] = pMarker->blue;
                  marked++;
               }
            }
            if ( !marked  )
            {
               if ( (idxx < 256) && (idxy < 240) )
               {
                  // Screen...
                  m_pExecutionVisualizerInspectorTV[((idxy<<9) * 3) + (idxx * 3) + 0] = CPPU::TV()[((idxy<<8) * 3) + (idxx * 3) + 0];
                  m_pExecutionVisualizerInspectorTV[((idxy<<9) * 3) + (idxx * 3) + 1] = CPPU::TV()[((idxy<<8) * 3) + (idxx * 3) + 1];
                  m_pExecutionVisualizerInspectorTV[((idxy<<9) * 3) + (idxx * 3) + 2] = CPPU::TV()[((idxy<<8) * 3) + (idxx * 3) + 2];
               }
               else
               {
                  // Darker gray backdrop PPU-off time outline...
                  m_pExecutionVisualizerInspectorTV[((idxy<<9) * 3) + (idxx * 3) + 0] = 105;
                  m_pExecutionVisualizerInspectorTV[((idxy<<9) * 3) + (idxx * 3) + 1] = 105;
                  m_pExecutionVisualizerInspectorTV[((idxy<<9) * 3) + (idxx * 3) + 2] = 105;
               }
            }
         }
      }
   }
}

void C6502::EMULATE ( int cycles )
{
   m_curCycles += cycles;

   if ( !m_killed )
   {
      do
      {
         if ( m_curCycles > 0 )
         {
            STEP();
            if ( rPC() == m_pcGoto )
            {
               CNES::FORCEBREAKPOINT();
               m_pcGoto = 0xFFFFFFFF;
            }
         }
         if ( m_killed )
         {
            CNES::FORCEBREAKPOINT();
         }
      } while ( (!m_killed) && (m_curCycles > 0) );
   }
}

void C6502::STEALCYCLES ( int cycles )
{
// CPTODO: complete implementation of cycle stealing that affects $4016 reads
   if ( (!m_sync) && m_write )
   {
      for ( int i = 0; i < cycles; i++ ) ADVANCE();
   }
   else if ( (!m_sync) && (!m_write) )
   {
      // Read from effective address...
      for ( int i = 0; i < cycles; i++ ) MEM ( m_ea );
   }
   else
   {
      for ( int i = 0; i < cycles; i++ ) ADVANCE();
   }
}

void C6502::ADVANCE ( void )
{
   // Run APU for one cycle...
   CAPU::EMULATE ( 1 );

   // Increment running cycle counter...
   m_cycles++;

   // Decrement cycles available counter...
   m_curCycles--;
}

void C6502::STEP ( void )
{
   if ( m_phase == 0 )
   {
      // Reset effective address...
      wEA ( 0xFFFFFFFF );

      // Indicate opcode fetch...
      m_sync = true;
      m_write = false;

      // Fetch
      (*opcodeData) = FETCH ( rPC() );
      (*(opcodeData+3)) = 0; // no extra cycle yet

      // Check for KIL opcodes...
      if ( (((*opcodeData) == 0x02) ||
           ((*opcodeData) == 0x12) ||
           ((*opcodeData) == 0x22) ||
           ((*opcodeData) == 0x32) ||
           ((*opcodeData) == 0x42) ||
           ((*opcodeData) == 0x52) ||
           ((*opcodeData) == 0x62) ||
           ((*opcodeData) == 0x72) ||
           ((*opcodeData) == 0x92) ||
           ((*opcodeData) == 0xB2) ||
           ((*opcodeData) == 0xD2) ||
           ((*opcodeData) == 0xF2)) )
      {
         // KIL opcodes halt PC dead!  Force break...
         CNES::FORCEBREAKPOINT ();
      }

      // Indicate NOT opcode fetch...
      m_sync = false;

      INCPC ();

      // Get information about current opcode...
      pOpcodeStruct = m_6502opcode+(*opcodeData);
      opcodeSize = (*(opcode_size+(pOpcodeStruct->amode)));

      // Check for undocumented breakpoint...
      if ( !pOpcodeStruct->documented )
      {
         CNES::CHECKBREAKPOINT ( eBreakInCPU, eBreakOnCPUEvent, 0, CPU_EVENT_UNDOCUMENTED );
         CNES::CHECKBREAKPOINT ( eBreakInCPU, eBreakOnCPUEvent, (*opcodeData), CPU_EVENT_UNDOCUMENTED_EXACT );
      }
      else
      {
         CNES::CHECKBREAKPOINT ( eBreakInCPU, eBreakOnCPUEvent, (*opcodeData), CPU_EVENT_EXECUTE_EXACT );
      }

      // Set up class data so we don't need to pass it down to each func...
      amode = pOpcodeStruct->amode;
      data = opcodeData+1;

      // Go to next phase...
      m_phase++;
   }
   else if ( m_phase == 1 )
   {
      // Check for dummy-read needed for single-byte instructions...
      if ( opcodeSize == 1 )
      {
         // Perform additional fetch...
         (*(opcodeData+1)) = EXTRAFETCH ( rPC() );

         // Cause instruction execution...
         m_phase = -1;
      }
      else
      {
         (*(opcodeData+1)) = FETCH ( rPC() );
         INCPC ();

         if ( opcodeSize == 2 )
         {
            // Cause instruction execution...
            m_phase = -1;
         }
         else
         {
            // Go to next phase...
            m_phase++;
         }
      }
   }
   else if ( m_phase == 2 )
   {
      (*(opcodeData+2)) = FETCH ( rPC() );
      INCPC ();

      // Cause instruction execution...
      m_phase = -1;
   }
   else if (  m_phase == -1 )
   {
      // Update Tracer
      CNES::TRACER()->SetDisassembly ( pDisassemblySample, opcodeData );
      CNES::TRACER()->SetRegisters ( pDisassemblySample, rA(), rX(), rY(), rSP(), rF() );

      // Execute
      if ( !(m_nmiAsserted && ((*opcodeData) == BRK_IMPLIED)) )
      {
         pOpcodeStruct->pFn ();
      }

      // Adjust cycles for extra...
      for ( int i = 0; i < (*(opcodeData+3)); i++ )
      {
         ADVANCE ();
      }

      // Update Tracer
      CNES::TRACER()->SetEffectiveAddress ( pDisassemblySample, rEA() );

      if ( m_irqAsserted )
      {
         m_irqPending = true;
      }
      if ( m_nmiAsserted )
      {
         m_nmiPending = true;
         m_nmiAsserted = false;
         m_irqPending = false;
      }

      // Check for NMI assertion...
      if ( m_nmiPending )
      {
         // Execute NMI handler...
         NMI();
         m_nmiPending = false;
      }

      // Check for IRQ assertion...
      // CLI requires one cycle latency on interrupt.
      else if ( ((*opcodeData) != CLI_IMPLIED) && (m_irqPending) )
      {
         // Execute IRQ handler...
         IRQ();
         m_irqPending = false;
      }

      // Go back to fetch phases...
      m_phase = 0;
   }
}

void C6502::KIL ( void )
{
   m_killed = true;
   return;
}

// AAC (ANC) [ANC]
// ~~~~~~~~~~~~~~~
// AND byte with accumulator. If result is negative then carry is
// set. Status flags: N,Z,C
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Immediate   |AAC #arg   |$0B| 2 | 2
// Immediate   |AAC #arg   |$2B| 2 | 2
void C6502::ANC ( void )
{
   wA ( rA()&(*data) );
   wN ( rA()&0x80 );
   wC ( rA()&0x80 );
   wZ ( !rA() );

   return;
}

// ASR (ASR) [ALR]
// ~~~~~~~~~~~~~~~
// AND byte with accumulator, then shift right one bit in accumu-
// lator. Status flags: N,Z,C
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Immediate   |ASR #arg   |$4B| 2 | 2
void C6502::ALR ( void )
{
   wA ( (rA()&(*data)) );
   wC ( rA()&0x01 );
   wA ( rA()>>1 );
   wN ( rA()&0x80 );
   wZ ( !rA() );

   return;
}

// ARR (ARR) [ARR]
// ~~~~~~~~~~~~~~~
// AND byte with accumulator, then rotate one bit right in accu-
// mulator and check bit 5 and 6:
// If both bits are 1: set C, clear V.
// If both bits are 0: clear C and V.
// If only bit 5 is 1: set V, clear C.
// If only bit 6 is 1: set C and V.
// Status flags: N,V,Z,C
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Immediate   |ARR #arg   |$6B| 2 | 2
void C6502::ARR ( void )
{
   wA ( (rA()&(*data))>>1 );
   wN ( rA()&0x80 );
   wZ ( !rA() );

   if ( (rA()&0x60) == 0x60 )
   {
      sC();
      cV();
   }
   else if ( (rA()&0x60) == 0x00 )
   {
      cC();
      cV();
   }
   else if ( (rA()&0x60) == 0x20 )
   {
      cC();
      sV();
   }
   else if ( (rA()&0x60) == 0x40 )
   {
      sC();
      sV();
   }

   return;
}

// XAA (ANE) [XAA]
// ~~~~~~~~~~~~~~~
// Exact operation unknown. Read the referenced documents for
// more information and observations.
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Immediate   |XAA #arg   |$8B| 2 | 2
void C6502::XAA ( void )
{
   return;
}

// AXA (SHA) [AXA]
// ~~~~~~~~~~~~~~~
// AND X register with accumulator then AND result with 7 and
// store in memory. Status flags: -
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Absolute,Y  |AXA arg,Y  |$9F| 3 | 5
// (Indirect),Y|AXA arg    |$93| 2 | 6
void C6502::AXA ( void )
{
   unsigned short addr;
   unsigned char  val;

   addr = MAKEADDR ( amode, data );
   val = (rX()&rA())&7;
   MEM ( addr, val );

   if ( amode != AM_ABSOLUTE_INDEXED_Y )
   {
      // A missing memory cycle here?
      // Synchronize CPU and APU...
      ADVANCE ();
   }

   return;
}

// XAS (SHS) [TAS]
// ~~~~~~~~~~~~~~~
// AND X register with accumulator and store result in stack
// pointer, then AND stack pointer with the high byte of the
// target address of the argument + 1. Store result in memory.
//
// S = X AND A, M = S AND HIGH(arg) + 1
//
// Status flags: -
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Absolute,Y  |XAS arg,Y  |$9B| 3 | 5
void C6502::TAS ( void )
{
   unsigned short addr;
   unsigned char  val;

   wSP ( rX()&rA() );
   addr = MAKEADDR ( amode, data );
   val = (rSP()&((*(data+1))+1));
   MEM ( addr, val );

   return;
}

// SYA (SHY) [SAY]
// ~~~~~~~~~~~~~~~
// AND Y register with the high byte of the target address of the
// argument + 1. Store the result in memory.
//
// M = Y AND HIGH(arg) + 1
//
// Status flags: -
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Absolute,X  |SYA arg,X  |$9C| 3 | 5
void C6502::SAY ( void )
{
   unsigned short addr;
   unsigned char  val;

   addr = MAKEADDR ( amode, data );
   val = (rY()&((addr>>8)+1));
   MEM ( addr, val );

   return;
}

// SXA (SHX) [XAS]
// ~~~~~~~~~~~~~~~
// AND X register with the high byte of the target address of the
// argument + 1. Store the result in memory.
//
// M = X AND HIGH(arg) + 1
//
// Status flags: -
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Absolute,Y  |SXA arg,Y  |$9E| 3 | 5
void C6502::XAS ( void )
{
   unsigned short addr;
   unsigned char  val;

   addr = MAKEADDR ( amode, data );
   val = (rX()&((addr>>8)+1));
   MEM ( addr, val );

   return;
}

// ATX (LXA) [OAL]
// ~~~~~~~~~~~~~~~
// AND byte with accumulator, then transfer accumulator to X
// register. Status flags: N,Z
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Immediate   |ATX #arg   |$AB| 2 | 2
void C6502::OAL ( void )
{
   wA ( rA()&(*data) );
   wX ( rA() );
   wN ( rA()&0x80 );
   wZ ( !rA() );

   return;
}

// LAR (LAE) [LAS]
// ~~~~~~~~~~~~~~~
// AND memory with stack pointer, transfer result to accumulator,
// X register and stack pointer.
// Status flags: N,Z
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Absolute,Y  |LAR arg,Y  |$BB| 3 | 4 *
void C6502::LAS ( void )
{
   unsigned short addr;

   addr = MAKEADDR ( amode, data );
   wA ( rSP()&MEM(addr) );
   wX ( rA() );
   wSP ( rA() );
   wN ( rA()&0x80 );
   wZ ( !rA() );

   return;
}

// AXS (SBX) [SAX]
// ~~~~~~~~~~~~~~~
// AND X register with accumulator and store result in X regis-
// ter, then subtract byte from X register (without borrow).
// Status flags: N,Z,C
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Immediate   |AXS #arg   |$CB| 2 | 2
void C6502::SAX ( void )
{
   short val;

   wX ( rA()&rX() );
   val = rX()-(*data);
   wX ( val&0xFF );
   wN ( rA()&0x80 );
   wZ ( !rA() );
   wC ( val<0 );

   return;
}

//  ORA                 ORA "OR" memory with accumulator                  ORA
//
//  Operation: A V M -> A                                 N Z C I D V
//                                                        / / _ _ _ _
//                               (Ref: 2.2.3.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Immediate     |   ORA #Oper           |    09   |    2    |    2     |
//  |  Zero Page     |   ORA Oper            |    05   |    2    |    3     |
//  |  Zero Page,X   |   ORA Oper,X          |    15   |    2    |    4     |
//  |  Absolute      |   ORA Oper            |    0D   |    3    |    4     |
//  |  Absolute,X    |   ORA Oper,X          |    1D   |    3    |    4*    |
//  |  Absolute,Y    |   ORA Oper,Y          |    19   |    3    |    4*    |
//  |  (Indirect,X)  |   ORA (Oper,X)        |    01   |    2    |    6     |
//  |  (Indirect),Y  |   ORA (Oper),Y        |    11   |    2    |    5*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 on page crossing
void C6502::ORA ( void )
{
   unsigned short addr;

   if ( amode == AM_IMMEDIATE )
   {
      wA ( rA()|(*data) );
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      wA ( rA()|MEM(addr) );
   }
   wN ( rA()&0x80 );
   wZ ( !rA() );

   return;
}

//  ASL          ASL Shift Left One Bit (Memory or Accumulator)           ASL
//                   +-+-+-+-+-+-+-+-+
//  Operation:  C <- |7|6|5|4|3|2|1|0| <- 0
//                   +-+-+-+-+-+-+-+-+                    N Z C I D V
//                                                        / / / _ _ _
//                                 (Ref: 10.2)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Accumulator   |   ASL A               |    0A   |    1    |    2     |
//  |  Zero Page     |   ASL Oper            |    06   |    2    |    5     |
//  |  Zero Page,X   |   ASL Oper,X          |    16   |    2    |    6     |
//  |  Absolute      |   ASL Oper            |    0E   |    3    |    6     |
//  |  Absolute, X   |   ASL Oper,X          |    1E   |    3    |    7     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::ASL ( void )
{
   unsigned short addr = 0x0000;
   unsigned short val;

   if ( amode == AM_ACCUMULATOR )
   {
      val = rA ();
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      val = MEM ( addr );

      // dummy write
      MEM ( addr, val );
   }
   val <<= 1;
   wC ( val&0x100 );
   val &= 0xFF;
   wN ( val&0x80 );
   wZ ( !val );
   if ( amode == AM_ACCUMULATOR )
   {
      wA ( (unsigned char)val );
   }
   else
   {
      MEM ( addr, (unsigned char)val );
   }

   return;
}

// SLO (SLO) [ASO]
// ~~~~~~~~~~~~~~~
// Shift left one bit in memory, then OR accumulator with memory.
//
// Status flags: N,Z,C
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Zero Page   |SLO arg    |$07| 2 | 5
// Zero Page,X |SLO arg,X  |$17| 2 | 6
// Absolute    |SLO arg    |$0F| 3 | 6
// Absolute,X  |SLO arg,X  |$1F| 3 | 7
// Absolute,Y  |SLO arg,Y  |$1B| 3 | 7
// (Indirect,X)|SLO (arg,X)|$03| 2 | 8
// (Indirect),Y|SLO (arg),Y|$13| 2 | 8
void C6502::ASO ( void )
{
   unsigned short addr;
   unsigned short val;

   addr = MAKEADDR ( amode, data );
   val = MEM ( addr );

   val <<= 1;
   MEM ( addr, (unsigned char)val );
   wC ( val&0x100 );
   val &= 0xFF;
   wA ( rA()|val );
   wN ( rA()&0x80 );
   wZ ( !rA() );

   // A missing memory cycle here?
   // Synchronize CPU and APU...
   ADVANCE ();

   return;
}

// AAX (SAX) [AXS]
// ~~~~~~~~~~~~~~~
// AND X register with accumulator and store result in memory.
// Status flags: N,Z
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Zero Page   |AAX arg    |$87| 2 | 3
// Zero Page,Y |AAX arg,Y  |$97| 2 | 4
// (Indirect,X)|AAX (arg,X)|$83| 2 | 6
// Absolute    |AAX arg    |$8F| 3 | 4
void C6502::AXS ( void )
{
   unsigned short addr;

   addr = MAKEADDR ( amode, data );

   MEM ( addr, rA()&rX() );

   return;
}

//  PHP                 PHP Push processor status on stack                PHP
//
//  Operation:  P toS                                     N Z C I D V
//                                                        _ _ _ _ _ _
//                                 (Ref: 8.11)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   PHP                 |    08   |    1    |    3     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::PHP ( void )
{
   sB();
   PUSH ( rF() );

   return;
}

//  BPL                     BPL Branch on result plus                     BPL
//
//  Operation:  Branch on N = 0                           N Z C I D V
//                                                        _ _ _ _ _ _
//                               (Ref: 4.1.1.2)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Relative      |   BPL Oper            |    10   |    2    |    2*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 if branch occurs to same page.
//  * Add 2 if branch occurs to different page.
void C6502::BPL ( void )
{
   UINT target;

   target = rPC()+GETSIGNED8(data,0);
   if ( !rN() )
   {
      if ( (rPC()&0xFF00) != (target&0xFF00) )
      {
         (*(data+2))++; // extra cycles stored here...
      }
      wPC ( target );
      (*(data+2))++; // extra cycles stored here...
   }

   return;
}

//  CLC                       CLC Clear carry flag                        CLC
//
//  Operation:  0 -> C                                    N Z C I D V
//                                                        _ _ 0 _ _ _
//                                (Ref: 3.0.2)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   CLC                 |    18   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::CLC ( void )
{
   cC ();

   return;
}

//  JSR          JSR Jump to new location saving return address           JSR
//
//  Operation:  PC + 2 toS, (PC + 1) -> PCL               N Z C I D V
//                          (PC + 2) -> PCH               _ _ _ _ _ _
//                                 (Ref: 8.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Absolute      |   JSR Oper            |    20   |    3    |    6     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::JSR ( void )
{
   UINT gopc = rPC()-1; // This was +2 but since PC is incremented in FETCH now it needs to be -1
   PUSH ( GETHI8(gopc) );
   PUSH ( GETLO8(gopc) );
   wPC ( MAKE16(GETUNSIGNED8(data,0),GETUNSIGNED8(data,1)) )

   // A missing memory cycle here?
   // Synchronize CPU and APU...
   ADVANCE ();

   return;
}

//  AND                  "AND" memory with accumulator                    AND
//
//  Operation:  A /\ M -> A                               N Z C I D V
//                                                        / / _ _ _ _
//                               (Ref: 2.2.3.0)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Immediate     |   AND #Oper           |    29   |    2    |    2     |
//  |  Zero Page     |   AND Oper            |    25   |    2    |    3     |
//  |  Zero Page,X   |   AND Oper,X          |    35   |    2    |    4     |
//  |  Absolute      |   AND Oper            |    2D   |    3    |    4     |
//  |  Absolute,X    |   AND Oper,X          |    3D   |    3    |    4*    |
//  |  Absolute,Y    |   AND Oper,Y          |    39   |    3    |    4*    |
//  |  (Indirect,X)  |   AND (Oper,X)        |    21   |    2    |    6     |
//  |  (Indirect,Y)  |   AND (Oper),Y        |    31   |    2    |    5*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 if page boundary is crossed.
void C6502::AND ( void )
{
   unsigned short addr;

   if ( amode == AM_IMMEDIATE )
   {
      wA ( rA()&(*data) );
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      wA ( rA()&MEM(addr) );
   }
   wN ( rA()&0x80 );
   wZ ( !rA() );

   return;
}

//  BIT             BIT Test bits in memory with accumulator              BIT
//
//  Operation:  A /\ M, M7 -> N, M6 -> V
//
//  Bit 6 and 7 are transferred to the status register.   N Z C I D V
//  If the result of A /\ M is zero then Z = 1, otherwise M7/ _ _ _ M6
//  Z = 0
//                               (Ref: 4.2.1.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Zero Page     |   BIT Oper            |    24   |    2    |    3     |
//  |  Absolute      |   BIT Oper            |    2C   |    3    |    4     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::BIT ( void )
{
   unsigned short addr;
   unsigned char  val;

   addr = MAKEADDR ( amode, data );
   val = MEM ( addr );
   wN ( val&0x80 );
   wV ( val&0x40 );
   val &= rA ();
   wZ ( !val );

   return;
}

//  ROL          ROL Rotate one bit left (memory or accumulator)          ROL
//
//               +------------------------------+
//               |         M or A               |
//               |   +-+-+-+-+-+-+-+-+    +-+   |
//  Operation:   +-< |7|6|5|4|3|2|1|0| <- |C| <-+         N Z C I D V
//                   +-+-+-+-+-+-+-+-+    +-+             / / / _ _ _
//                                 (Ref: 10.3)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Accumulator   |   ROL A               |    2A   |    1    |    2     |
//  |  Zero Page     |   ROL Oper            |    26   |    2    |    5     |
//  |  Zero Page,X   |   ROL Oper,X          |    36   |    2    |    6     |
//  |  Absolute      |   ROL Oper            |    2E   |    3    |    6     |
//  |  Absolute,X    |   ROL Oper,X          |    3E   |    3    |    7     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::ROL ( void )
{
   unsigned short addr = 0x0000;
   unsigned short val;

   if ( amode == AM_ACCUMULATOR )
   {
      val = rA ();
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      val = MEM ( addr );

      // dummy write
      MEM ( addr, val );
   }
   val <<= 1;
   val |= rC();
   wC ( val&0x100 );
   val &= 0xFF;
   wN ( val&0x80 );
   wZ ( !val );
   if ( amode == AM_ACCUMULATOR )
   {
      wA ( (unsigned char)val );
   }
   else
   {
      MEM ( addr, (unsigned char)val );
   }

   return;
}

//  PLP               PLP Pull processor status from stack                PLP
//
//  Operation:  P fromS                                   N Z C I D V
//                                                         From Stack
//                                 (Ref: 8.12)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   PLP                 |    28   |    1    |    4     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::PLP ( void )
{
   wF ( POP() );

   // A missing memory cycle here?
   // Synchronize CPU and APU...
   ADVANCE ();

   return;
}

//  BMI                    BMI Branch on result minus                     BMI
//
//  Operation:  Branch on N = 1                           N Z C I D V
//                                                        _ _ _ _ _ _
//                               (Ref: 4.1.1.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Relative      |   BMI Oper            |    30   |    2    |    2*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 if branch occurs to same page.
//  * Add 1 if branch occurs to different page.
void C6502::BMI ( void )
{
   UINT target;

   target = rPC()+GETSIGNED8(data,0);
   if ( rN() )
   {
      if ( (rPC()&0xFF00) != (target&0xFF00) )
      {
         (*(data+2))++; // extra cycles stored here...
      }
      wPC ( target );
      (*(data+2))++; // extra cycles stored here...
   }

   return;
}

//  SEC                        SEC Set carry flag                         SEC
//
//  Operation:  1 -> C                                    N Z C I D V
//                                                        _ _ 1 _ _ _
//                                (Ref: 3.0.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   SEC                 |    38   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::SEC ( void )
{
   sC ();

   return;
}

//  RTI                    RTI Return from interrupt                      RTI
//                                                        N Z C I D V
//  Operation:  P fromS PC fromS                           From Stack
//                                 (Ref: 9.6)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   RTI                 |    4D   |    1    |    6     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::RTI ( void )
{
   unsigned char pclo;
   wF ( POP() );
   pclo = POP ();
   wPC ( MAKE16(pclo,POP()) );

   // A missing memory cycle here?
   // Synchronize CPU and APU...
   ADVANCE ();

   return;
}

//  EOR            EOR "Exclusive-Or" memory with accumulator             EOR
//
//  Operation:  A EOR M -> A                              N Z C I D V
//                                                        / / _ _ _ _
//                               (Ref: 2.2.3.2)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Immediate     |   EOR #Oper           |    49   |    2    |    2     |
//  |  Zero Page     |   EOR Oper            |    45   |    2    |    3     |
//  |  Zero Page,X   |   EOR Oper,X          |    55   |    2    |    4     |
//  |  Absolute      |   EOR Oper            |    4D   |    3    |    4     |
//  |  Absolute,X    |   EOR Oper,X          |    5D   |    3    |    4*    |
//  |  Absolute,Y    |   EOR Oper,Y          |    59   |    3    |    4*    |
//  |  (Indirect,X)  |   EOR (Oper,X)        |    41   |    2    |    6     |
//  |  (Indirect),Y  |   EOR (Oper),Y        |    51   |    2    |    5*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 if page boundary is crossed.
void C6502::EOR ( void )
{
   unsigned short addr;

   if ( amode == AM_IMMEDIATE )
   {
      wA ( rA()^(*data) );
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      wA ( rA()^MEM(addr) );
   }
   wN ( rA()&0x80 );
   wZ ( !rA() );

   return;
}

// SRE (SRE) [LSE]
// ~~~~~~~~~~~~~~~
// Shift right one bit in memory, then EOR accumulator with
// memory. Status flags: N,Z,C
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Zero Page   |SRE arg    |$47| 2 | 5
// Zero Page,X |SRE arg,X  |$57| 2 | 6
// Absolute    |SRE arg    |$4F| 3 | 6
// Absolute,X  |SRE arg,X  |$5F| 3 | 7
// Absolute,Y  |SRE arg,Y  |$5B| 3 | 7
// (Indirect,X)|SRE (arg,X)|$43| 2 | 8
// (Indirect),Y|SRE (arg),Y|$53| 2 | 8
void C6502::LSE ( void )
{
   unsigned short addr;
   unsigned short val;

   addr = MAKEADDR ( amode, data );
   val = MEM ( addr );

   wC ( val&0x01 );
   val >>= 1;
   val &= 0xFF;
   MEM ( addr, (unsigned char)val );
   wA ( rA()^val );
   wN ( rA()&0x80 );
   wZ ( !rA() );

   // A missing memory cycle here?
   // Synchronize CPU and APU...
   ADVANCE ();

   return;
}

//  LSR          LSR Shift right one bit (memory or accumulator)          LSR
//
//                   +-+-+-+-+-+-+-+-+
//  Operation:  0 -> |7|6|5|4|3|2|1|0| -> C               N Z C I D V
//                   +-+-+-+-+-+-+-+-+                    0 / / _ _ _
//                                 (Ref: 10.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Accumulator   |   LSR A               |    4A   |    1    |    2     |
//  |  Zero Page     |   LSR Oper            |    46   |    2    |    5     |
//  |  Zero Page,X   |   LSR Oper,X          |    56   |    2    |    6     |
//  |  Absolute      |   LSR Oper            |    4E   |    3    |    6     |
//  |  Absolute,X    |   LSR Oper,X          |    5E   |    3    |    7     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::LSR ( void )
{
   unsigned short addr = 0x0000;
   unsigned short val;

   if ( amode == AM_ACCUMULATOR )
   {
      val = rA ();
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      val = MEM ( addr );

      // dummy write
      MEM ( addr, val );
   }
   wC ( val&0x01 );
   val >>= 1;
   val &= 0xFF;
   cN ();
   wZ ( !val );
   if ( amode == AM_ACCUMULATOR )
   {
      wA ( (unsigned char)val );
   }
   else
   {
      MEM ( addr, (unsigned char)val );
   }

   return;
}

//  PHA                   PHA Push accumulator on stack                   PHA
//
//  Operation:  A toS                                     N Z C I D V
//                                                        _ _ _ _ _ _
//                                 (Ref: 8.5)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   PHA                 |    48   |    1    |    3     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::PHA ( void )
{
   PUSH ( rA() );

   return;
}

//  JMP                     JMP Jump to new location                      JMP
//
//  Operation:  (PC + 1) -> PCL                           N Z C I D V
//              (PC + 2) -> PCH   (Ref: 4.0.2)            _ _ _ _ _ _
//                                (Ref: 9.8.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Absolute      |   JMP Oper            |    4C   |    3    |    3     |
//  |  Indirect      |   JMP (Oper)          |    6C   |    3    |    5     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::JMP ( void )
{
   UINT addr = MAKE16(GETUNSIGNED8(data,0),GETUNSIGNED8(data,1));
   if ( amode == AM_ABSOLUTE )
   {
      wPC ( addr );
   }
   else
   {
      if ( (addr&0xFF) == 0xFF )
      {
         wPC ( MAKE16(MEM(addr),MEM(addr&0xFF00)) );
      }
      else
      {
         wPC ( MAKE16(MEM(addr),MEM(addr+1)) );
      }
   }

   return;
}

//  BVC                   BVC Branch on overflow clear                    BVC
//
//  Operation:  Branch on V = 0                           N Z C I D V
//                                                        _ _ _ _ _ _
//                               (Ref: 4.1.1.8)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Relative      |   BVC Oper            |    50   |    2    |    2*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 if branch occurs to same page.
//  * Add 2 if branch occurs to different page.
void C6502::BVC ( void )
{
   UINT target;

   target = rPC()+GETSIGNED8(data,0);
   if ( !rV() )
   {
      if ( (rPC()&0xFF00) != (target&0xFF00) )
      {
         (*(data+2))++; // extra cycles stored here...
      }
      wPC ( target );
      (*(data+2))++; // extra cycles stored here...
   }

   return;
}

//  CLI                  CLI Clear interrupt disable bit                  CLI
//
//  Operation: 0 -> I                                     N Z C I D V
//                                                        _ _ _ 0 _ _
//                                (Ref: 3.2.2)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   CLI                 |    58   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::CLI ( void )
{
   cI ();

   return;
}

//  RTS                    RTS Return from subroutine                     RTS
//                                                        N Z C I D V
//  Operation:  PC fromS, PC + 1 -> PC                    _ _ _ _ _ _
//                                 (Ref: 8.2)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   RTS                 |    60   |    1    |    6     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::RTS ( void )
{
   unsigned short gopc = POP ();
   gopc |= (POP()<<8);
   gopc++;
   wPC ( gopc );

   // A missing memory cycle here?
   // Synchronize CPU and APU...
   ADVANCE ();
   ADVANCE ();

   return;
}

//  ADC               Add memory to accumulator with carry                ADC
//
//  Operation:  A + M + C -> A, C                         N Z C I D V
//                                                        / / / _ _ /
//                                (Ref: 2.2.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Immediate     |   ADC #Oper           |    69   |    2    |    2     |
//  |  Zero Page     |   ADC Oper            |    65   |    2    |    3     |
//  |  Zero Page,X   |   ADC Oper,X          |    75   |    2    |    4     |
//  |  Absolute      |   ADC Oper            |    6D   |    3    |    4     |
//  |  Absolute,X    |   ADC Oper,X          |    7D   |    3    |    4*    |
//  |  Absolute,Y    |   ADC Oper,Y          |    79   |    3    |    4*    |
//  |  (Indirect,X)  |   ADC (Oper,X)        |    61   |    2    |    6     |
//  |  (Indirect),Y  |   ADC (Oper),Y        |    71   |    2    |    5*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 if page boundary is crossed.
void C6502::ADC ( void )
{
   UINT addr;
   short result;
   unsigned char val;

   if ( amode == AM_IMMEDIATE )
   {
      val = (*data);
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      val = MEM ( addr );
   }

   result = rA () + val + rC ();

    wV ( !((rA()^val)&0x80) && ((rA()^result)&0x80) );
   wA ( (unsigned char)result );
   wN ( rA()&0x80 );
   wZ ( !rA() );
   wC ( result&0x100 );

   return;
}

//  ROR          ROR Rotate one bit right (memory or accumulator)         ROR
//
//               +------------------------------+
//               |                              |
//               |   +-+    +-+-+-+-+-+-+-+-+   |
//  Operation:   +-> |C| -> |7|6|5|4|3|2|1|0| >-+         N Z C I D V
//                   +-+    +-+-+-+-+-+-+-+-+             / / / _ _ _
//                                 (Ref: 10.4)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Accumulator   |   ROR A               |    6A   |    1    |    2     |
//  |  Zero Page     |   ROR Oper            |    66   |    2    |    5     |
//  |  Zero Page,X   |   ROR Oper,X          |    76   |    2    |    6     |
//  |  Absolute      |   ROR Oper            |    6E   |    3    |    6     |
//  |  Absolute,X    |   ROR Oper,X          |    7E   |    3    |    7     |
//  +----------------+-----------------------+---------+---------+----------+
//
//    Note: ROR instruction is available on MCS650X microprocessors after
//          June, 1976.
void C6502::ROR ( void )
{
   UINT addr = 0x0000;
   unsigned short val;

   if ( amode == AM_ACCUMULATOR )
   {
      val = rA ();
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      val = MEM ( addr );

      // dummy write
      MEM ( addr, val );
   }
   val |= ( rC()*0x100 );
   wC ( val&0x01 );
   val >>= 1;
   val &= 0xFF;
   wN ( val&0x80 );
   wZ ( !val );
   if ( amode == AM_ACCUMULATOR )
   {
      wA ( (unsigned char)val );
   }
   else
   {
      MEM ( addr, (unsigned char)val );
   }

   return;
}

// RLA (RLA) [RLA]
// ~~~~~~~~~~~~~~~
// Rotate one bit left in memory, then AND accumulator with
// memory. Status flags: N,Z,C
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Zero Page   |RLA arg    |$27| 2 | 5
// Zero Page,X |RLA arg,X  |$37| 2 | 6
// Absolute    |RLA arg    |$2F| 3 | 6
// Absolute,X  |RLA arg,X  |$3F| 3 | 7
// Absolute,Y  |RLA arg,Y  |$3B| 3 | 7
// (Indirect,X)|RLA (arg,X)|$23| 2 | 8
// (Indirect),Y|RLA (arg),Y|$33| 2 | 8
void C6502::RLA ( void )
{
   UINT addr;
   unsigned short val;

   addr = MAKEADDR ( amode, data );
   val = MEM ( addr );

   val <<= 1;
   val |= rC();
   wC ( val&0x100 );
   val &= 0xFF;
   wA ( rA()&val );
   MEM ( addr, (unsigned char)val );
   wN ( rA()&0x80 );
   wZ ( !rA() );

   // A missing memory cycle here?
   // Synchronize CPU and APU...
   ADVANCE ();

   return;
}

// RRA (RRA) [RRA]
// ~~~~~~~~~~~~~~~
// Rotate one bit right in memory, then add memory to accumulator
// (with carry).
//
// Status flags: N,V,Z,C
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Zero Page   |RRA arg    |$67| 2 | 5
// Zero Page,X |RRA arg,X  |$77| 2 | 6
// Absolute    |RRA arg    |$6F| 3 | 6
// Absolute,X  |RRA arg,X  |$7F| 3 | 7
// Absolute,Y  |RRA arg,Y  |$7B| 3 | 7
// (Indirect,X)|RRA (arg,X)|$63| 2 | 8
// (Indirect),Y|RRA (arg),Y|$73| 2 | 8
void C6502::RRA ( void )
{
   UINT addr;
   unsigned short val;
   short result;

   addr = MAKEADDR ( amode, data );
   val = MEM ( addr );

   val |= ( rC()*0x100 );
   wC ( val&0x01 );
   val >>= 1;
   val &= 0xFF;
   MEM ( addr, (unsigned char)val );

   result = rA () + val + rC ();

   wV ( !((rA()^val)&0x80) && ((rA()^result)&0x80) );
   wA ( (unsigned char)result );
   wN ( rA()&0x80 );
   wZ ( !rA() );
   wC ( result&0x100 );

   // A missing memory cycle here?
   // Synchronize CPU and APU...
   ADVANCE ();

   return;
}

//  PLA                 PLA Pull accumulator from stack                   PLA
//
//  Operation:  A fromS                                   N Z C I D V
//                                                        / / _ _ _ _
//                                 (Ref: 8.6)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   PLA                 |    68   |    1    |    4     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::PLA ( void )
{
   wA ( POP() );
   wN ( rA()&0x80 );
   wZ ( !rA() );

   // A missing memory cycle here?
   // Synchronize CPU and APU...
   ADVANCE ();

   return;
}

//  BVS                    BVS Branch on overflow set                     BVS
//
//  Operation:  Branch on V = 1                           N Z C I D V
//                                                        _ _ _ _ _ _
//                               (Ref: 4.1.1.7)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Relative      |   BVS Oper            |    70   |    2    |    2*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 if branch occurs to same page.
//  * Add 2 if branch occurs to different page.
void C6502::BVS ( void )
{
   UINT target;

   target = rPC()+GETSIGNED8(data,0);
   if ( rV() )
   {
      if ( (rPC()&0xFF00) != (target&0xFF00) )
      {
         (*(data+2))++; // extra cycles stored here...
      }
      wPC ( target );
      (*(data+2))++; // extra cycles stored here...
   }

   return;
}

//  SEI                 SEI Set interrupt disable status                  SED
//                                                        N Z C I D V
//  Operation:  1 -> I                                    _ _ _ 1 _ _
//                                (Ref: 3.2.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   SEI                 |    78   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::SEI ( void )
{
   sI ();

   return;
}

//  STA                  STA Store accumulator in memory                  STA
//
//  Operation:  A -> M                                    N Z C I D V
//                                                        _ _ _ _ _ _
//                                (Ref: 2.1.2)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Zero Page     |   STA Oper            |    85   |    2    |    3     |
//  |  Zero Page,X   |   STA Oper,X          |    95   |    2    |    4     |
//  |  Absolute      |   STA Oper            |    8D   |    3    |    4     |
//  |  Absolute,X    |   STA Oper,X          |    9D   |    3    |    5     |
//  |  Absolute,Y    |   STA Oper, Y         |    99   |    3    |    5     |
//  |  (Indirect,X)  |   STA (Oper,X)        |    81   |    2    |    6     |
//  |  (Indirect),Y  |   STA (Oper),Y        |    91   |    2    |    6     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::STA ( void )
{
   unsigned short addr;

   addr = MAKEADDR ( amode, data );
   MEM ( addr, rA() );

   return;
}

//  STY                    STY Store index Y in memory                    STY
//
//  Operation: Y -> M                                     N Z C I D V
//                                                        _ _ _ _ _ _
//                                 (Ref: 7.3)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Zero Page     |   STY Oper            |    84   |    2    |    3     |
//  |  Zero Page,X   |   STY Oper,X          |    94   |    2    |    4     |
//  |  Absolute      |   STY Oper            |    8C   |    3    |    4     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::STY ( void )
{
   unsigned short addr;

   addr = MAKEADDR ( amode, data );
   MEM ( addr, rY() );

   return;
}

//  STX                    STX Store index X in memory                    STX
//
//  Operation: X -> M                                     N Z C I D V
//                                                        _ _ _ _ _ _
//                                 (Ref: 7.2)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Zero Page     |   STX Oper            |    86   |    2    |    3     |
//  |  Zero Page,Y   |   STX Oper,Y          |    96   |    2    |    4     |
//  |  Absolute      |   STX Oper            |    8E   |    3    |    4     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::STX ( void )
{
   unsigned short addr;

   addr = MAKEADDR ( amode, data );
   MEM ( addr, rX() );

   return;
}

//  DEY                   DEY Decrement index Y by one                    DEY
//
//  Operation:  Y - 1 -> Y                                N Z C I D V
//                                                        / / _ _ _ _
//                                 (Ref: 7.7)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   DEY                 |    88   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::DEY ( void )
{
   wY ( rY()-1 );
   wN ( rY()&0x80 );
   wZ ( !rY() );

   return;
}

//  TXA                TXA Transfer index X to accumulator                TXA
//                                                        N Z C I D V
//  Operation:  X -> A                                    / / _ _ _ _
//                                 (Ref: 7.12)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   TXA                 |    8A   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::TXA ( void )
{
   wA ( rX() );
   wN ( rA()&0x80 );
   wZ ( !rA() );

   return;
}

//  BCC                     BCC Branch on Carry Clear                     BCC
//                                                        N Z C I D V
//  Operation:  Branch on C = 0                           _ _ _ _ _ _
//                               (Ref: 4.1.1.3)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Relative      |   BCC Oper            |    90   |    2    |    2*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 if branch occurs to same page.
//  * Add 2 if branch occurs to different page.
void C6502::BCC ( void )
{
   UINT target;

   target = rPC()+GETSIGNED8(data,0);
   if ( !rC() )
   {
      if ( (rPC()&0xFF00) != (target&0xFF00) )
      {
         (*(data+2))++; // extra cycles stored here...
      }
      wPC ( target );
      (*(data+2))++; // extra cycles stored here...
   }

   return;
}

//  TYA                TYA Transfer index Y to accumulator                TYA
//
//  Operation:  Y -> A                                    N Z C I D V
//                                                        / / _ _ _ _
//                                 (Ref: 7.14)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   TYA                 |    98   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::TYA ( void )
{
   wA ( rY() );
   wN ( rA()&0x80 );
   wZ ( !rA() );

   return;
}

//  TXS              TXS Transfer index X to stack pointer                TXS
//                                                        N Z C I D V
//  Operation:  X -> S                                    _ _ _ _ _ _
//                                 (Ref: 8.8)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   TXS                 |    9A   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::TXS ( void )
{
   wSP ( rX() );

   return;
}

//  LDY                   LDY Load index Y with memory                    LDY
//                                                        N Z C I D V
//  Operation:  M -> Y                                    / / _ _ _ _
//                                 (Ref: 7.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Immediate     |   LDY #Oper           |    A0   |    2    |    2     |
//  |  Zero Page     |   LDY Oper            |    A4   |    2    |    3     |
//  |  Zero Page,X   |   LDY Oper,X          |    B4   |    2    |    4     |
//  |  Absolute      |   LDY Oper            |    AC   |    3    |    4     |
//  |  Absolute,X    |   LDY Oper,X          |    BC   |    3    |    4*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 when page boundary is crossed.
void C6502::LDY ( void )
{
   UINT addr;

   if ( amode == AM_IMMEDIATE )
   {
      wY ( (*data) );
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      wY ( MEM(addr) );
   }
   wN ( rY()&0x80 );
   wZ ( !rY() );

   return;
}

// LAX (LAX) [LAX]
// ~~~~~~~~~~~~~~~
// Load accumulator and X register with memory.
// Status flags: N,Z
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Zero Page   |LAX arg    |$A7| 2 | 3
// Zero Page,Y |LAX arg,Y  |$B7| 2 | 4
// Absolute    |LAX arg    |$AF| 3 | 4
// Absolute,Y  |LAX arg,Y  |$BF| 3 | 4 *
// (Indirect,X)|LAX (arg,X)|$A3| 2 | 6
// (Indirect),Y|LAX (arg),Y|$B3| 2 | 5 *
//  * Add 1 if page boundary is crossed.
void C6502::LAX ( void )
{
   unsigned char val;

   UINT addr;
   if ( amode == AM_IMMEDIATE )
   {
      wA ( (*data) );
      wX ( (*data) );
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      val = MEM ( addr ); // Single memory access cycle...
      wA ( val );
      wX ( val );
   }
   wN ( rA()&0x80 );
   wZ ( !rA() );

   return;
}

//  LDA                  LDA Load accumulator with memory                 LDA
//
//  Operation:  M -> A                                    N Z C I D V
//                                                        / / _ _ _ _
//                                (Ref: 2.1.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Immediate     |   LDA #Oper           |    A9   |    2    |    2     |
//  |  Zero Page     |   LDA Oper            |    A5   |    2    |    3     |
//  |  Zero Page,X   |   LDA Oper,X          |    B5   |    2    |    4     |
//  |  Absolute      |   LDA Oper            |    AD   |    3    |    4     |
//  |  Absolute,X    |   LDA Oper,X          |    BD   |    3    |    4*    |
//  |  Absolute,Y    |   LDA Oper,Y          |    B9   |    3    |    4*    |
//  |  (Indirect,X)  |   LDA (Oper,X)        |    A1   |    2    |    6     |
//  |  (Indirect),Y  |   LDA (Oper),Y        |    B1   |    2    |    5*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 if page boundary is crossed.
void C6502::LDA ( void )
{
   UINT addr;

   if ( amode == AM_IMMEDIATE )
   {
      wA ( (*data) );
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      wA ( MEM(addr) );
   }
   wN ( rA()&0x80 );
   wZ ( !rA() );

   return;
}

//  LDX                   LDX Load index X with memory                    LDX
//
//  Operation:  M -> X                                    N Z C I D V
//                                                        / / _ _ _ _
//                                 (Ref: 7.0)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Immediate     |   LDX #Oper           |    A2   |    2    |    2     |
//  |  Zero Page     |   LDX Oper            |    A6   |    2    |    3     |
//  |  Zero Page,Y   |   LDX Oper,Y          |    B6   |    2    |    4     |
//  |  Absolute      |   LDX Oper            |    AE   |    3    |    4     |
//  |  Absolute,Y    |   LDX Oper,Y          |    BE   |    3    |    4*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 when page boundary is crossed.
void C6502::LDX ( void )
{
   UINT addr;

   if ( amode == AM_IMMEDIATE )
   {
      wX ( (*data) );
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      wX ( MEM(addr) );
   }
   wN ( rX()&0x80 );
   wZ ( !rX() );

   return;
}

//  TAY                TAY Transfer accumulator to index Y                TAY
//
//  Operation:  A -> Y                                    N Z C I D V
//                                                        / / _ _ _ _
//                                 (Ref: 7.13)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   TAY                 |    A8   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::TAY ( void )
{
   wY ( rA() );
   wN ( rY()&0x80 );
   wZ ( !rY() );

   return;
}

//  TAX                TAX Transfer accumulator to index X                TAX
//
//  Operation:  A -> X                                    N Z C I D V
//                                                        / / _ _ _ _
//                                 (Ref: 7.11)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   TAX                 |    AA   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::TAX ( void )
{
   wX ( rA() );
   wN ( rX()&0x80 );
   wZ ( !rX() );

   return;
}

//  BCS                      BCS Branch on carry set                      BCS
//
//  Operation:  Branch on C = 1                           N Z C I D V
//                                                        _ _ _ _ _ _
//                               (Ref: 4.1.1.4)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Relative      |   BCS Oper            |    B0   |    2    |    2*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 if branch occurs to same  page.
//  * Add 2 if branch occurs to next  page.
void C6502::BCS ( void )
{
   UINT target;

   target = rPC()+GETSIGNED8(data,0);
   if ( rC() )
   {
      if ( (rPC()&0xFF00) != (target&0xFF00) )
      {
         (*(data+2))++; // extra cycles stored here...
      }
      wPC ( target );
      (*(data+2))++; // extra cycles stored here...
   }

   return;
}

//  CLV                      CLV Clear overflow flag                      CLV
//
//  Operation: 0 -> V                                     N Z C I D V
//                                                        _ _ _ _ _ 0
//                                (Ref: 3.6.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   CLV                 |    B8   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::CLV ( void )
{
   cV ();

   return;
}

//  TSX              TSX Transfer stack pointer to index X                TSX
//
//  Operation:  S -> X                                    N Z C I D V
//                                                        / / _ _ _ _
//                                 (Ref: 8.9)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   TSX                 |    BA   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::TSX ( void )
{
   wX ( rSP() );
   wN ( rX()&0x80 );
   wZ ( !rX() );

   return;
}

//  CPY                  CPY Compare memory and index Y                   CPY
//                                                        N Z C I D V
//  Operation:  Y - M                                     / / / _ _ _
//                                 (Ref: 7.9)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Immediate     |   CPY *Oper           |    C0   |    2    |    2     |
//  |  Zero Page     |   CPY Oper            |    C4   |    2    |    3     |
//  |  Absolute      |   CPY Oper            |    CC   |    3    |    4     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::CPY ( void )
{
   unsigned short addr;
   unsigned char  val;

   if ( amode == AM_IMMEDIATE )
   {
      val = (*data)&0xFF;
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      val = MEM ( addr );
   }
   wC ( rY()>=val );
   val = rY() - val;
   val &= 0xFF;
   wN ( val&0x80 );
   wZ ( !val );

   return;
}

//  CMP                CMP Compare memory and accumulator                 CMP
//
//  Operation:  A - M                                     N Z C I D V
//                                                        / / / _ _ _
//                                (Ref: 4.2.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Immediate     |   CMP #Oper           |    C9   |    2    |    2     |
//  |  Zero Page     |   CMP Oper            |    C5   |    2    |    3     |
//  |  Zero Page,X   |   CMP Oper,X          |    D5   |    2    |    4     |
//  |  Absolute      |   CMP Oper            |    CD   |    3    |    4     |
//  |  Absolute,X    |   CMP Oper,X          |    DD   |    3    |    4*    |
//  |  Absolute,Y    |   CMP Oper,Y          |    D9   |    3    |    4*    |
//  |  (Indirect,X)  |   CMP (Oper,X)        |    C1   |    2    |    6     |
//  |  (Indirect),Y  |   CMP (Oper),Y        |    D1   |    2    |    5*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 if page boundary is crossed.
void C6502::CMP ( void )
{
   UINT addr;
   unsigned char val;

   if ( amode == AM_IMMEDIATE )
   {
      val = (*data)&0xFF;
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      val = MEM ( addr );
   }
   wC ( rA()>=val );
   val = rA() - val;
   val &= 0xFF;
   wN ( val&0x80 );
   wZ ( !val );

   return;
}

// DCP (DCP) [DCM]
// ~~~~~~~~~~~~~~~
// Subtract 1 from memory (without borrow).
// Status flags: C
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Zero Page   |DCP arg    |$C7| 2 | 5
// Zero Page,X |DCP arg,X  |$D7| 2 | 6
// Absolute    |DCP arg    |$CF| 3 | 6
// Absolute,X  |DCP arg,X  |$DF| 3 | 7
// Absolute,Y  |DCP arg,Y  |$DB| 3 | 7
// (Indirect,X)|DCP (arg,X)|$C3| 2 | 8
// (Indirect),Y|DCP (arg),Y|$D3| 2 | 8
void C6502::DCM ( void )
{
   unsigned short addr;
   unsigned char  val;

   addr = MAKEADDR ( amode, data );
   val = MEM ( addr );
   val -= 1;
   MEM ( addr, val );
   wC ( rA()>=val );
   val = rA() - val;
   val &= 0xFF;
   wN ( val&0x80 );
   wZ ( !val );

   // A missing memory cycle here?
   // Synchronize CPU and APU...
   ADVANCE ();

   return;
}

//  DEC                   DEC Decrement memory by one                     DEC
//
//  Operation:  M - 1 -> M                                N Z C I D V
//                                                        / / _ _ _ _
//                                 (Ref: 10.7)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Zero Page     |   DEC Oper            |    C6   |    2    |    5     |
//  |  Zero Page,X   |   DEC Oper,X          |    D6   |    2    |    6     |
//  |  Absolute      |   DEC Oper            |    CE   |    3    |    6     |
//  |  Absolute,X    |   DEC Oper,X          |    DE   |    3    |    7     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::DEC ( void )
{
   unsigned short addr;
   unsigned char  val;

   addr = MAKEADDR ( amode, data );
   val = MEM ( addr );

   // dummy write
   MEM ( addr, val );

   val -= 1;
   MEM ( addr, val );
   wN ( val&0x80 );
   wZ ( !val );

   return;
}

//  INY                    INY Increment Index Y by one                   INY
//
//  Operation:  Y + 1 -> Y                                N Z C I D V
//                                                        / / _ _ _ _
//                                 (Ref: 7.5)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   INY                 |    C8   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::INY ( void )
{
   wY ( rY()+1 );
   wN ( rY()&0x80 );
   wZ ( !rY() );

   return;
}

//  DEX                   DEX Decrement index X by one                    DEX
//
//  Operation:  X - 1 -> X                                N Z C I D V
//                                                        / / _ _ _ _
//                                 (Ref: 7.6)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   DEX                 |    CA   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::DEX ( void )
{
   wX ( rX()-1 );
   wN ( rX()&0x80 );
   wZ ( !rX() );

   return;
}

//  BNE                   BNE Branch on result not zero                   BNE
//
//  Operation:  Branch on Z = 0                           N Z C I D V
//                                                        _ _ _ _ _ _
//                               (Ref: 4.1.1.6)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Relative      |   BNE Oper            |    D0   |    2    |    2*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 if branch occurs to same page.
//  * Add 2 if branch occurs to different page.
void C6502::BNE ( void )
{
   UINT target;

   target = rPC()+GETSIGNED8(data,0);
   if ( !rZ() )
   {
      if ( (rPC()&0xFF00) != (target&0xFF00) )
      {
         (*(data+2))++; // extra cycles stored here...
      }
      wPC ( target );
      (*(data+2))++; // extra cycles stored here...
   }

   return;
}

//  CLD                      CLD Clear decimal mode                       CLD
//
//  Operation:  0 -> D                                    N A C I D V
//                                                        _ _ _ _ 0 _
//                                (Ref: 3.3.2)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   CLD                 |    D8   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::CLD ( void )
{
   cD ();

   return;
}

//  CPX                  CPX Compare Memory and Index X                   CPX
//                                                        N Z C I D V
//  Operation:  X - M                                     / / / _ _ _
//                                 (Ref: 7.8)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Immediate     |   CPX *Oper           |    E0   |    2    |    2     |
//  |  Zero Page     |   CPX Oper            |    E4   |    2    |    3     |
//  |  Absolute      |   CPX Oper            |    EC   |    3    |    4     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::CPX ( void )
{
   unsigned short addr;
   unsigned char  val;

   if ( amode == AM_IMMEDIATE )
   {
      val = (*data)&0xFF;
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      val = MEM ( addr );
   }
   wC ( rX()>=val );
   val = rX() - val;
   val &= 0xFF;
   wN ( val&0x80 );
   wZ ( !val );

   return;
}

//  SBC          SBC Subtract memory from accumulator with borrow         SBC
//                      -
//  Operation:  A - M - (1-C) -> A                        N Z C I D V
//         -                                              / / / _ _ /
//    Note:C = Borrow             (Ref: 2.2.2)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Immediate     |   SBC #Oper           |    E9   |    2    |    2     |
//  |  Zero Page     |   SBC Oper            |    E5   |    2    |    3     |
//  |  Zero Page,X   |   SBC Oper,X          |    F5   |    2    |    4     |
//  |  Absolute      |   SBC Oper            |    ED   |    3    |    4     |
//  |  Absolute,X    |   SBC Oper,X          |    FD   |    3    |    4*    |
//  |  Absolute,Y    |   SBC Oper,Y          |    F9   |    3    |    4*    |
//  |  (Indirect,X)  |   SBC (Oper,X)        |    E1   |    2    |    6     |
//  |  (Indirect),Y  |   SBC (Oper),Y        |    F1   |    2    |    5*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 when page boundary is crossed.
void C6502::SBC ( void )
{
   UINT addr;
   short result;
   unsigned char val;

   if ( amode == AM_IMMEDIATE )
   {
      val = (*data);
   }
   else
   {
      addr = MAKEADDR ( amode, data );
      val = MEM ( addr );
   }

   result = (rA() - val - (1-rC()));

    wV ( ((rA()^val)&0x80) && ((rA()^result)&0x80) );
   wA ( (unsigned char)result );
   wN ( rA()&0x80 );
   wZ ( !rA() );
   wC ( !(result&0x100) );

   return;
}

//  INC                    INC Increment memory by one                    INC
//                                                        N Z C I D V
//  Operation:  M + 1 -> M                                / / _ _ _ _
//                                 (Ref: 10.6)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Zero Page     |   INC Oper            |    E6   |    2    |    5     |
//  |  Zero Page,X   |   INC Oper,X          |    F6   |    2    |    6     |
//  |  Absolute      |   INC Oper            |    EE   |    3    |    6     |
//  |  Absolute,X    |   INC Oper,X          |    FE   |    3    |    7     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::INC ( void )
{
   unsigned short addr;
   unsigned char  val;

   addr = MAKEADDR ( amode, data );
   val = MEM ( addr );

   // dummy write
   MEM ( addr, val );

   val++;
   MEM ( addr, val );
   wN ( val&0x80 );
   wZ ( !val );

   return;
}

// ISC (ISB) [INS]
// ~~~~~~~~~~~~~~~
// Increase memory by one, then subtract memory from accumulator
// (with borrow). Status flags: N,V,Z,C
//
// Addressing  |Mnemonics  |Opc|Sz | n
// ------------|-----------|---|---|---
// Zero Page   |ISC arg    |$E7| 2 | 5
// Zero Page,X |ISC arg,X  |$F7| 2 | 6
// Absolute    |ISC arg    |$EF| 3 | 6
// Absolute,X  |ISC arg,X  |$FF| 3 | 7
// Absolute,Y  |ISC arg,Y  |$FB| 3 | 7
// (Indirect,X)|ISC (arg,X)|$E3| 2 | 8
// (Indirect),Y|ISC (arg),Y|$F3| 2 | 8
void C6502::INS ( void )
{
   unsigned short addr;
   unsigned char  val;
   short result;

   addr = MAKEADDR ( amode, data );
   val = MEM ( addr );
   val++;
   MEM ( addr, val );

   result = (rA() - val - (1-rC()));

   wV ( ((rA()^val)&0x80) && ((rA()^result)&0x80) );
   wA ( (unsigned char)result );
   wN ( rA()&0x80 );
   wZ ( !rA() );
   wC ( !(result&0x100) );

   // A missing memory cycle here?
   // Synchronize CPU and APU...
   ADVANCE ();

   return;
}

//  INX                    INX Increment Index X by one                   INX
//                                                        N Z C I D V
//  Operation:  X + 1 -> X                                / / _ _ _ _
//                                 (Ref: 7.4)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   INX                 |    E8   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::INX ( void )
{
   wX ( rX()+1 );
   wN ( rX()&0x80 );
   wZ ( !rX() );

   return;
}

//  NOP                         NOP No operation                          NOP
//                                                        N Z C I D V
//  Operation:  No Operation (2 cycles)                   _ _ _ _ _ _
//
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   NOP                 |    EA   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::NOP ( void )
{
   return;
}

void C6502::DOP ( void )
{
   unsigned short addr;

   addr = MAKEADDR ( amode, data );

   if ( amode != AM_IMMEDIATE )
   {
      // A missing memory cycle here?
      // Synchronize CPU and APU...
      ADVANCE ();
   }

   return;
}

void C6502::TOP ( void )
{
   unsigned short addr;

   addr = MAKEADDR ( amode, data );

   // A missing memory cycle here?
   // Synchronize CPU and APU...
   ADVANCE ();

   return;
}

//  BEQ                    BEQ Branch on result zero                      BEQ
//                                                        N Z C I D V
//  Operation:  Branch on Z = 1                           _ _ _ _ _ _
//                               (Ref: 4.1.1.5)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Relative      |   BEQ Oper            |    F0   |    2    |    2*    |
//  +----------------+-----------------------+---------+---------+----------+
//  * Add 1 if branch occurs to same  page.
//  * Add 2 if branch occurs to next  page.
void C6502::BEQ ( void )
{
   UINT target;

   target = rPC()+GETSIGNED8(data,0);
   if ( rZ() )
   {
      if ( (rPC()&0xFF00) != (target&0xFF00) )
      {
         (*(data+2))++; // extra cycles stored here...
      }
      wPC ( target );
      (*(data+2))++; // extra cycles stored here...
   }

   return;
}

//  SED                       SED Set decimal mode                        SED
//                                                        N Z C I D V
//  Operation:  1 -> D                                    _ _ _ _ 1 _
//                                (Ref: 3.3.1)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   SED                 |    F8   |    1    |    2     |
//  +----------------+-----------------------+---------+---------+----------+
void C6502::SED ( void )
{
   sD ();

   return;
}

//  BRK                          BRK Force Break                          BRK
//
//  Operation:  Forced Interrupt PC + 2 toS P toS         N Z C I D V
//                                                        _ _ _ 1 _ _
//                                 (Ref: 9.11)
//  +----------------+-----------------------+---------+---------+----------+
//  | Addressing Mode| Assembly Language Form| OP CODE |No. Bytes|No. Cycles|
//  +----------------+-----------------------+---------+---------+----------+
//  |  Implied       |   BRK                 |    00   |    1    |    7     |
//  +----------------+-----------------------+---------+---------+----------+
//  1. A BRK command cannot be masked by setting I.
void C6502::BRK ( void )
{
   PUSH ( GETHI8(rPC()+1) );
   PUSH ( GETLO8((rPC()+1)) );
   sB ();
   PUSH ( rF() );
   wPC ( MAKE16(MEM(VECTOR_IRQ),MEM(VECTOR_IRQ+1)) );
   sI ();

   return;
}

void C6502::ASSERTIRQ ( char source )
{
   m_irqAsserted = true;
   CNES::TRACER()->AddIRQ ( source );
}

void C6502::RELEASEIRQ ( char source )
{
   m_irqAsserted = false;
}

void C6502::IRQ ()
{
   if ( (!m_killed) && (!rI()) )
   {
      PUSH ( GETHI8(rPC()) );
      PUSH ( GETLO8(rPC()) );
      cB ();
      PUSH ( rF() );
      wPC ( MAKE16(MEM(VECTOR_IRQ),MEM(VECTOR_IRQ+1)) );
      sI ();

      // Synchronize CPU and APU...
      for ( int i = 0; i < IRQ_CYCLES; i++ )
      {
         // Synchronize CPU and APU...
         ADVANCE ();
      }

      // Check for IRQ breakpoint...
      CNES::CHECKBREAKPOINT(eBreakInCPU,eBreakOnCPUEvent,0,CPU_EVENT_IRQ);
   }
}

void C6502::ASSERTNMI ()
{
   m_nmiAsserted = true;
   CNES::TRACER()->AddNMI ( eSource_PPU );
}

void C6502::NMI ()
{
   if ( !m_killed )
   {
      CNES::TRACER()->AddNMI ( eSource_PPU );

      PUSH ( GETHI8(rPC()) );
      PUSH ( GETLO8(rPC()) );
      cB ();
      PUSH ( rF() );
      wPC ( MAKE16(MEM(VECTOR_NMI),MEM(VECTOR_NMI+1)) );

      // Synchronize CPU and APU...
      for ( int i = 0; i < NMI_CYCLES; i++ )
      {
         // Synchronize CPU and APU...
         ADVANCE ();
      }

      // Check for NMI breakpoint...
      CNES::CHECKBREAKPOINT(eBreakInCPU,eBreakOnCPUEvent,0,CPU_EVENT_NMI);
   }
}

void C6502::RESET ( void )
{
   m_killed = false;

   CAPU::RESET ();

   CNES::TRACER()->AddRESET ();

   m_cycles = 0;
   m_curCycles = 0;
   m_phase = 0;

   m_sync = false;
   m_write = false;

   // Clear the disassembly sample...
   pDisassemblySample = NULL;

   m_irqAsserted = false;
   m_irqPending = false;
   m_nmiAsserted = false;
   m_nmiPending = false;

   wEA ( 0xFFFFFFFF );

   m_pcGoto = 0xFFFFFFFF;

   wF ( 0 );
   sI ();
   sB ();
   wPC ( MAKE16(MEM(VECTOR_RESET),MEM(VECTOR_RESET+1)) );

   wA ( 0 );
   wX ( 0 );
   wY ( 0 );
   wSP ( 0xFD );

   // Clear memory...
   MEMCLR ();

   // Check for RESET breakpoint...
   CNES::CHECKBREAKPOINT(eBreakInCPU,eBreakOnCPUEvent,0,CPU_EVENT_RESET);
}

unsigned char C6502::LOAD ( UINT addr, char* pTarget )
{
   unsigned char data = 0xff;

   if ( addr >= 0x8000 )
   {
      (*pTarget) = eTarget_ROM;
      data = CROM::PRGROM ( addr );
   }
   else if ( addr >= 0x6000 )
   {
      (*pTarget) = eTarget_SRAM;
      data = CROM::SRAM ( addr );
   }
   else if ( addr >= 0x5C00 )
   {
      (*pTarget) = eTarget_EXRAM;
      data = mapperfunc [ CROM::MAPPER() ].lowread ( addr );
   }
   else if ( addr >= 0x5000 )
   {
      (*pTarget) = eTarget_Mapper;
      data = mapperfunc [ CROM::MAPPER() ].lowread ( addr );
   }
   else if ( addr >= 0x4000 )
   {
      (*pTarget) = eTarget_APURegister;
      if ( addr == 0x4014 || addr == 0x4016 || addr == 0x4017 )
      {
         (*pTarget) = eTarget_IORegister;
      }
      data = CIO::IO ( addr );
   }
   else if ( addr >= 0x2000 )
   {
      (*pTarget) = eTarget_PPURegister;
      data = CPPU::PPU ( addr );
   }
   else
   {
      (*pTarget) = eTarget_RAM;
      addr &= 0x7FF; // RAM mirrored...
      data = m_6502memory[addr];
   }

   return data;
}

void C6502::STORE ( UINT addr, unsigned char data, char* pTarget )
{
   if ( addr < 0x2000 )
   {
      (*pTarget) = eTarget_RAM;
      addr &= 0x7FF; // RAM mirrored...
      m_6502memory[addr] = data&0xFF;
   }
   else if ( addr < 0x4000 )
   {
      (*pTarget) = eTarget_PPURegister;
      CPPU::PPU ( addr, data );
   }
   else if ( addr < 0x5000 )
   {
      (*pTarget) = eTarget_APURegister;
      if ( addr == 0x4014 || addr == 0x4016 )
      {
         (*pTarget) = eTarget_IORegister;
      }
      CIO::IO ( addr, data );
   }
   else if ( addr < 0x5C00 )
   {
      (*pTarget) = eTarget_Mapper;
      mapperfunc [ CROM::MAPPER() ].lowwrite ( addr, data );
   }
   else if ( addr < 0x6000 )
   {
      (*pTarget) = eTarget_EXRAM;
      mapperfunc [ CROM::MAPPER() ].lowwrite ( addr, data );
   }
   else if ( addr < 0x8000 )
   {
      (*pTarget) = eTarget_SRAM;
      CROM::SRAM ( addr, data );
   }
   else
   {
      (*pTarget) = eTarget_Mapper;
      mapperfunc [ CROM::MAPPER() ].highwrite ( addr, data );
   }
}

unsigned char C6502::FETCH ( UINT addr )
{
   char target;
   unsigned char data;

   // Synchronize CPU and APU...
   ADVANCE ();

   data = LOAD ( addr, &target );

   // Add Tracer sample...
   CNES::TRACER()->AddSample ( m_cycles, eTracer_InstructionFetch, eSource_CPU, target, addr, data );

   // Save the pointer to where to put the disassembly of
   // the current opcode now.  This might be the last fetch
   // for an instruction and the disassembly should be placed there.
   pDisassemblySample = CNES::TRACER()->GetLastCPUSample ();

   // Check for breakpoint...
   if ( m_sync )
   {
      CNES::CHECKBREAKPOINT ( eBreakInCPU, eBreakOnCPUExecution, data );
   }

   // If ROM is being accessed, log code/data logger...
   if ( target == eTarget_ROM )
   {
      // Log to Code/Data Logger...
      CCodeDataLogger* pLogger = CROM::LOGGER ( addr );
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_InstructionFetch, eSource_CPU );

      // Update Markers...
      m_marker.UpdateMarkers ( CROM::ABSADDR(addr), CPPU::_FRAME(), CPPU::_CYCLES() );

      // ... and update opcode masking for disassembler...
      CROM::PRGROMOPCODEMASK ( addr, (unsigned char)m_sync );
   }
   else if ( target == eTarget_SRAM )
   {
      // Log to Code/Data Logger...
      CCodeDataLogger* pLogger = CROM::SRAMLOGGER ( addr );
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_InstructionFetch, eSource_CPU );

      // Update opcode masking for disassembler...
      CROM::SRAMOPCODEMASK ( addr, (unsigned char)m_sync );
   }
   else if ( target == eTarget_EXRAM )
   {
      // Log to Code/Data Logger...
      CCodeDataLogger* pLogger = CROM::EXRAMLOGGER ();
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_InstructionFetch, eSource_CPU );

      // Update opcode masking for disassembler...
      CROM::EXRAMOPCODEMASK ( addr, (unsigned char)m_sync );
   }
   else if ( target == eTarget_RAM )
   {
      m_logger.LogAccess ( m_cycles, addr, data, eLogger_InstructionFetch, eSource_CPU );

      // ... and update opcode masking for disassembler...
      OPCODEMASK ( addr, (unsigned char)m_sync );
   }

   return data;
}

unsigned char C6502::EXTRAFETCH ( UINT addr )
{
   char target;
   unsigned char data;

   // Synchronize CPU and APU...
   ADVANCE ();

   data = LOAD ( addr, &target );

   // Add Tracer sample...
   CNES::TRACER()->AddSample ( m_cycles, eTracer_InstructionFetch, eSource_CPU, target, addr, data );

   // If ROM is being accessed, log code/data logger...
   if ( target == eTarget_ROM )
   {
      CCodeDataLogger* pLogger = CROM::LOGGER ( addr );
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_InstructionFetch, eSource_CPU );
   }
   else if ( target == eTarget_SRAM )
   {
      // Log to Code/Data Logger...
      CCodeDataLogger* pLogger = CROM::SRAMLOGGER ( addr );
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_InstructionFetch, eSource_CPU );
   }
   else if ( target == eTarget_EXRAM )
   {
      // Log to Code/Data Logger...
      CCodeDataLogger* pLogger = CROM::EXRAMLOGGER ();
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_InstructionFetch, eSource_CPU );
   }
   else if ( target == eTarget_RAM )
   {
      m_logger.LogAccess ( m_cycles, addr, data, eLogger_InstructionFetch, eSource_CPU );
   }

   return data;
}

unsigned char C6502::DMA ( UINT addr )
{
   char target;
   unsigned char data;

   // Synchronize CPU and APU...
   ADVANCE ();

   data = LOAD ( addr, &target );

   // Add Tracer sample...
   CNES::TRACER()->AddSample ( m_cycles, eTracer_DMA, eSource_CPU, target, addr, data );

   // If ROM or RAM is being accessed, log code/data logger...
   if ( target == eTarget_ROM )
   {
      CCodeDataLogger* pLogger = CROM::LOGGER ( addr );
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_DMA, eSource_APU );
   }
   else if ( target == eTarget_RAM )
   {
      m_logger.LogAccess ( m_cycles, addr, data, eLogger_DMA, eSource_APU );
   }

   return data;
}

void C6502::DMA ( UINT addr, unsigned char data )
{
   TracerInfo* pSample;
   char target;

   if ( addr > 0xFFFF )
   {
      return;
   }

   // Synchronize CPU and APU...
   ADVANCE ();

   // Store unknown target because otherwise the trace will be out of order...
   CNES::TRACER()->AddSample ( m_cycles, eTracer_DMA, eSource_CPU, target, addr, data );

   STORE ( addr, data, &target );

   // If ROM or RAM is being accessed, log code/data logger...
   if ( target == eTarget_ROM )
   {
      CCodeDataLogger* pLogger = CROM::LOGGER ( addr );
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_DMA, eSource_PPU );
   }
   else if ( target == eTarget_RAM )
   {
      m_logger.LogAccess ( m_cycles, addr, data, eLogger_DMA, eSource_PPU );
   }

   // store real target...
   if ( pSample )
   {
      pSample->target = target;
   }
}

unsigned char C6502::MEM ( UINT addr )
{
   char target;
   unsigned char data;

   // Not writing...
   m_write = false;

   // Synchronize CPU and APU...
   ADVANCE ();

   data = LOAD ( addr, &target );

   // Add Tracer sample...
   CNES::TRACER()->AddSample ( m_cycles, eTracer_DataRead, eSource_CPU, target, addr, data );

   // If ROM or RAM is being accessed, log code/data logger...
   if ( target == eTarget_ROM )
   {
      CCodeDataLogger* pLogger = CROM::LOGGER ( addr );
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_DataRead, eSource_CPU );
   }
   else if ( target == eTarget_SRAM )
   {
      // Log to Code/Data Logger...
      CCodeDataLogger* pLogger = CROM::SRAMLOGGER ( addr );
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_DataRead, eSource_CPU );
   }
   else if ( target == eTarget_EXRAM )
   {
      // Log to Code/Data Logger...
      CCodeDataLogger* pLogger = CROM::EXRAMLOGGER ();
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_DataRead, eSource_CPU );
   }
   else if ( target == eTarget_RAM )
   {
      // Log to Code/Data Logger...
      m_logger.LogAccess ( m_cycles, addr, data, eLogger_DataRead, eSource_CPU );
   }
   else
   {
      // Registers...
      // Log to Code/Data Logger...
      m_logger.LogAccess ( m_cycles, addr, data, eLogger_DataRead, eSource_CPU );
   }

   // Check for breakpoint...
   CNES::CHECKBREAKPOINT ( eBreakInCPU, eBreakOnCPUMemoryRead, data );

   return data;
}

void C6502::MEM ( UINT addr, unsigned char data )
{
   TracerInfo* pSample;
   char target;

   if ( addr > 0xFFFF )
   {
      return;
   }

   // Writing...
   m_write = true;

   // Synchronize CPU and APU...
   ADVANCE ();

   // Store unknown target because otherwise the trace will be out of order...
   pSample = CNES::TRACER()->AddSample ( m_cycles, eTracer_DataWrite, eSource_CPU, 0, addr, data );

   STORE ( addr, data, &target );

   // If ROM or RAM is being accessed, log code/data logger...
   if ( target == eTarget_ROM )
   {
      CCodeDataLogger* pLogger = CROM::LOGGER ( addr );
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_DataWrite, eSource_CPU );
   }
   else if ( target == eTarget_SRAM )
   {
      // Log to Code/Data Logger...
      CCodeDataLogger* pLogger = CROM::SRAMLOGGER ( addr );
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_DataWrite, eSource_CPU );
   }
   else if ( target == eTarget_EXRAM )
   {
      // Log to Code/Data Logger...
      CCodeDataLogger* pLogger = CROM::EXRAMLOGGER ();
      pLogger->LogAccess ( m_cycles, addr, data, eLogger_DataWrite, eSource_CPU );
   }
   else if ( target == eTarget_RAM )
   {
      m_logger.LogAccess ( m_cycles, addr, data, eLogger_DataWrite, eSource_CPU );
   }
   else
   {
      // Registers...
      // Log to Code/Data Logger...
      m_logger.LogAccess ( m_cycles, addr, data, eLogger_DataWrite, eSource_CPU );
   }

   // store real target...
   if ( pSample )
   {
      pSample->target = target;
   }

   // Check for breakpoint...
   CNES::CHECKBREAKPOINT ( eBreakInCPU, eBreakOnCPUMemoryWrite, data );
}

unsigned char C6502::_MEM ( UINT addr )
{
   char target;

   return LOAD ( addr, &target );
}

void C6502::_MEM ( UINT addr, unsigned char data )
{
   char target;

   STORE ( addr, data, &target );
}

UINT C6502::MAKEADDR ( int amode, unsigned char* data )
{
   unsigned short addr = 0x00, addrpre;

   if ( amode == AM_ZEROPAGE )
   {
      addr = (*data);
   }
   else if ( amode == AM_ZEROPAGE_INDEXED_X )
   {
      // dummy read
      MEM(*data);
      addr = ((*data)+rX())&0xFF;
   }
   else if ( amode == AM_ZEROPAGE_INDEXED_Y )
   {
      // dummy read
      MEM(*data);
      addr = ((*data)+rY())&0xFF;
   }
   else if ( amode == AM_ABSOLUTE )
   {
      addr = MAKE16((*data),(*(data+1)));
   }
   else if ( amode == AM_ABSOLUTE_INDEXED_X )
   {
      addrpre = MAKE16((*data),(*(data+1)));
      addr = addrpre+rX();
      // Check for ROL special case...
      if ( ((*opcodeData) == ROL_ABS_X) || ((addrpre>>8) != (addr>>8)) || (pOpcodeStruct->forceExtraCycle) )
      {
         // dummy read
         MEM((addrpre&0xFF00)+((addrpre+rX())&0xFF));
      }
   }
   else if ( amode == AM_ABSOLUTE_INDEXED_Y )
   {
      addrpre = MAKE16((*data),(*(data+1)));
      addr = addrpre+rY();
      if ( ((addrpre>>8) != (addr>>8)) || (pOpcodeStruct->forceExtraCycle) )
      {
         // dummy read
         MEM((addrpre&0xFF00)+((addrpre+rY())&0xFF));
      }
   }
   else if ( amode == AM_PREINDEXED_INDIRECT )
   {
      // dummy read
      MEM(*data);
      addr = MAKE16(MEM(((*data)+rX())&0xFF),MEM(((*data)+rX()+1)&0xFF));
   }
   else if ( amode == AM_POSTINDEXED_INDIRECT )
   {
      addrpre = MAKE16(MEM((*data)),MEM(((*data)+1)&0xFF));
      addr = addrpre+rY();
      if ( ((addrpre>>8) != (addr>>8)) || (pOpcodeStruct->forceExtraCycle) )
      {
         // dummy read
         MEM((addrpre&0xFF00)+((addrpre+rY())&0xFF));
      }
   }

   // Set "Effective Address" for use by Tracer, Breakpoints, etc...
   wEA ( addr );

   return addr;
}

void C6502::DISASSEMBLE ()
{
   if ( rPC() < 0x800 )
   {
      DISASSEMBLE ( m_RAMdisassembly,
                    m_6502memory,
                    MEM_2KB,
                    m_RAMopcodeMask,
                    m_RAMsloc2addr,
                    m_RAMaddr2sloc,
                    &(m_RAMsloc) );
   }
}

void C6502::DISASSEMBLE ( char** disassembly, unsigned char* binary, int binaryLength, unsigned char* opcodeMask, unsigned short* sloc2addr, unsigned short* addr2sloc, unsigned int* sourceLength )
{
   C6502_opcode* pOp;
   int opSize;
   unsigned char op;
   char* ptr;
   unsigned char mask;

   (*sourceLength) = 0;

   for ( int i = 0; i < binaryLength; )
   {
      op = *(binary+i);
      pOp = m_6502opcode+op;
      opSize = *(opcode_size+pOp->amode);

      mask = *(opcodeMask+i);

      ptr = (*disassembly);

      // save sloc
      (*sloc2addr) = i;
      sloc2addr++;
      (*addr2sloc) = (*sourceLength);
      addr2sloc++;

      // If we've discovered this address has been executed by the 6502 we'll
      // attempt to provide disassembly for it...
      if ( (mask) && ((i+opSize) < binaryLength) )
      {
         ptr += sprintf ( ptr, "%s", pOp->name );

         switch ( pOp->amode )
         {
            // Single byte operands
            case AM_IMMEDIATE:
            case AM_ZEROPAGE_INDEXED_X:
            case AM_ZEROPAGE_INDEXED_Y:
            case AM_ZEROPAGE:
            case AM_PREINDEXED_INDIRECT:
            case AM_POSTINDEXED_INDIRECT:
            case AM_RELATIVE:
               ptr += sprintf ( ptr, operandFmt[pOp->amode], binary[i+1] );
            break;

            // Two byte operands
            case AM_ABSOLUTE:
            case AM_ABSOLUTE_INDEXED_X:
            case AM_ABSOLUTE_INDEXED_Y:
            case AM_INDIRECT:
               ptr += sprintf ( ptr, operandFmt[pOp->amode], binary[i+2], binary[i+1] );
            break;
         }

         if ( opSize > 2 )
         {
            strcpy((*(disassembly+2)), (*disassembly));
            (*addr2sloc) = (*sourceLength);
            addr2sloc++;
         }
         if ( opSize > 1 )
         {
            strcpy((*(disassembly+1)), (*disassembly));
            (*addr2sloc) = (*sourceLength);
            addr2sloc++;
         }

         i += opSize;
         disassembly += opSize;
      }
      else
      {
         ptr += sprintf ( ptr, ".DB $%02X", binary[i] );

         i++;
         disassembly++;
      }

      (*sourceLength)++;
   }
}

char* C6502::Disassemble ( unsigned char* pOpcode, char* buffer )
{
   char* lbuffer = buffer;
   C6502_opcode* pOp = m_6502opcode+(*pOpcode);

   if ( pOp->documented )
   {
      buffer += sprintf ( buffer, "%s", pOp->name );
   }
   else
   {
      buffer += sprintf ( buffer, "*%s", pOp->name );
   }

   switch ( pOp->amode )
   {
      // Single byte operands
      case AM_IMMEDIATE:
      case AM_ZEROPAGE_INDEXED_X:
      case AM_ZEROPAGE_INDEXED_Y:
      case AM_ZEROPAGE:
      case AM_PREINDEXED_INDIRECT:
      case AM_POSTINDEXED_INDIRECT:
      case AM_RELATIVE:
         buffer += sprintf ( buffer, operandFmt[pOp->amode],
                             (*(pOpcode+1)) );
      break;

      // Two byte operands
      case AM_ABSOLUTE:
      case AM_ABSOLUTE_INDEXED_X:
      case AM_ABSOLUTE_INDEXED_Y:
      case AM_INDIRECT:
         buffer += sprintf ( buffer, operandFmt[pOp->amode],
                             (*(pOpcode+2)),
                             (*(pOpcode+1)) );
      break;
   }
   (*buffer) = 0;

   return lbuffer;
}
