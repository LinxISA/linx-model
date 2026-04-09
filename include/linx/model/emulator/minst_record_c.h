#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  LINX_MINST_TEXT_CAPACITY = 32,
};

typedef enum LinxMinstOperandKindC {
  LINX_MINST_OPERAND_INVALID = 0,
  LINX_MINST_OPERAND_REGISTER = 1,
  LINX_MINST_OPERAND_IMMEDIATE = 2,
  LINX_MINST_OPERAND_PREDICATE = 3,
  LINX_MINST_OPERAND_MEMORY = 4,
  LINX_MINST_OPERAND_CONTROL = 5,
  LINX_MINST_OPERAND_METADATA = 6,
} LinxMinstOperandKindC;

typedef struct LinxMinstOperandRecordC {
  uint8_t valid;
  uint8_t kind;
  uint8_t is_dst;
  uint8_t ready;
  uint16_t encoded_bits;
  uint64_t value;
  uint64_t data;
  char field_name[LINX_MINST_TEXT_CAPACITY];
} LinxMinstOperandRecordC;

typedef struct LinxMinstMemoryRecordC {
  uint8_t valid;
  uint8_t is_load;
  uint8_t is_store;
  uint8_t reserved0;
  uint32_t size;
  uint64_t addr;
  uint64_t wdata;
  uint64_t rdata;
} LinxMinstMemoryRecordC;

typedef struct LinxMinstTrapRecordC {
  uint8_t valid;
  uint8_t reserved0;
  uint16_t cause;
  uint64_t traparg0;
} LinxMinstTrapRecordC;

typedef struct LinxMinstTraceRecordC {
  uint64_t cycle;
  uint64_t pc;
  uint64_t next_pc;
  uint64_t insn;
  uint32_t len;
  int32_t lane_id;
  char mnemonic[LINX_MINST_TEXT_CAPACITY];
  char form_id[LINX_MINST_TEXT_CAPACITY];
  char opcode_class[LINX_MINST_TEXT_CAPACITY];
  char lifecycle[LINX_MINST_TEXT_CAPACITY];
  char block_kind[LINX_MINST_TEXT_CAPACITY];
  LinxMinstOperandRecordC src0;
  LinxMinstOperandRecordC src1;
  LinxMinstOperandRecordC dst0;
  LinxMinstMemoryRecordC memory;
  LinxMinstTrapRecordC trap;
} LinxMinstTraceRecordC;

#ifdef __cplusplus
}
#endif
