#include "frida-gumjs.h"

#include "debug.h"
#include "cmplog.h"

#include "ctx.h"
#include "frida_cmplog.h"
#include "instrument.h"
#include "util.h"

#if defined(__x86_64__)

typedef struct {
  x86_op_type type;
  uint8_t     size;

  union {
    x86_op_mem mem;
    x86_reg    reg;
    int64_t    imm;
  };

} cmplog_ctx_t;

typedef struct {
  cmplog_ctx_t operand1;
  cmplog_ctx_t operand2;

} cmplog_pair_ctx_t;

static gboolean cmplog_read_mem(GumCpuContext *ctx, uint8_t size,
                                x86_op_mem *mem, gsize *val) {
  gsize base = 0;
  gsize index = 0;
  gsize address;

  if (mem->base != X86_REG_INVALID) base = ctx_read_reg(ctx, mem->base);

  if (mem->index != X86_REG_INVALID) index = ctx_read_reg(ctx, mem->index);

  address = base + (index * mem->scale) + mem->disp;

  if (!cmplog_is_readable(address, size)) { return FALSE; }

  switch (size) {
    case 1:
      *val = *((guint8 *)GSIZE_TO_POINTER(address));
      return TRUE;
    case 2:
      *val = *((guint16 *)GSIZE_TO_POINTER(address));
      return TRUE;
    case 4:
      *val = *((guint32 *)GSIZE_TO_POINTER(address));
      return TRUE;
    case 8:
      *val = *((guint64 *)GSIZE_TO_POINTER(address));
      return TRUE;
    default:
      FFATAL("Invalid operand size: %d\n", size);
  }

  return FALSE;
}

static gboolean cmplog_get_operand_value(GumCpuContext *context,
                                         cmplog_ctx_t *ctx, gsize *val) {
  switch (ctx->type) {
    case X86_OP_REG:
      *val = ctx_read_reg(context, ctx->reg);
      return TRUE;
    case X86_OP_IMM:
      *val = ctx->imm;
      return TRUE;
    case X86_OP_MEM:
      return cmplog_read_mem(context, ctx->size, &ctx->mem, val);
    default:
      FFATAL("Invalid operand type: %d\n", ctx->type);
  }

  return FALSE;
}

static void cmplog_call_callout(GumCpuContext *context, gpointer user_data) {
  UNUSED_PARAMETER(user_data);

  gsize address = ctx_read_reg(context, X86_REG_RIP);
  gsize rdi = ctx_read_reg(context, X86_REG_RDI);
  gsize rsi = ctx_read_reg(context, X86_REG_RSI);

  if (((G_MAXULONG - rdi) < 31) || ((G_MAXULONG - rsi) < 31)) return;

  if (!cmplog_is_readable(rdi, 31) || !cmplog_is_readable(rsi, 31)) return;

  void *ptr1 = GSIZE_TO_POINTER(rdi);
  void *ptr2 = GSIZE_TO_POINTER(rsi);

  guint64 k = instrument_get_offset_hash(GUM_ADDRESS(address));

  if (__afl_cmp_map->headers[k].type != CMP_TYPE_RTN) {
    __afl_cmp_map->headers[k].type = CMP_TYPE_RTN;
    __afl_cmp_map->headers[k].hits = 0;
  }

  u32 hits = 0;

  if (__afl_cmp_map->headers[k].hits == 0) {
    __afl_cmp_map->headers[k].shape = 30;

  } else {
    hits = __afl_cmp_map->headers[k].hits;
  }

  __afl_cmp_map->headers[k].hits = hits + 1;

  hits &= CMP_MAP_RTN_H - 1;
  ((struct cmpfn_operands *)__afl_cmp_map->log[k])[hits].v0_len = 31;
  ((struct cmpfn_operands *)__afl_cmp_map->log[k])[hits].v1_len = 31;
  gum_memcpy(((struct cmpfn_operands *)__afl_cmp_map->log[k])[hits].v0, ptr1,
             31);
  gum_memcpy(((struct cmpfn_operands *)__afl_cmp_map->log[k])[hits].v1, ptr2,
             31);
}

static void cmplog_instrument_put_operand(cmplog_ctx_t *ctx,
                                          cs_x86_op    *operand) {
  ctx->type = operand->type;
  ctx->size = operand->size;
  switch (operand->type) {
    case X86_OP_REG:
      gum_memcpy(&ctx->reg, &operand->reg, sizeof(x86_reg));
      break;
    case X86_OP_IMM:
      gum_memcpy(&ctx->imm, &operand->imm, sizeof(int64_t));
      break;
    case X86_OP_MEM:
      gum_memcpy(&ctx->mem, &operand->mem, sizeof(x86_op_mem));
      break;
    default:
      FFATAL("Invalid operand type: %d\n", operand->type);
  }
}

static void cmplog_instrument_call(const cs_insn      *instr,
                                   GumStalkerIterator *iterator) {
  cs_x86     x86 = instr->detail->x86;
  cs_x86_op *operand;

  if (instr->id != X86_INS_CALL) return;

  if (x86.op_count != 1) return;

  operand = &x86.operands[0];

  if (operand->type == X86_OP_INVALID) return;
  if (operand->type == X86_OP_MEM && operand->mem.segment != X86_REG_INVALID)
    return;

  gum_stalker_iterator_put_callout(iterator, cmplog_call_callout, NULL, NULL);
}

static void cmplog_handle_cmp_sub(GumCpuContext *context, gsize operand1,
                                  gsize operand2, uint8_t size) {
  gsize address = ctx_read_reg(context, X86_REG_RIP);

  register uintptr_t k = instrument_get_offset_hash(GUM_ADDRESS(address));

  if (__afl_cmp_map->headers[k].type != CMP_TYPE_INS)
    __afl_cmp_map->headers[k].hits = 0;

  u32 hits = 0;

  if (__afl_cmp_map->headers[k].hits == 0) {
    __afl_cmp_map->headers[k].type = CMP_TYPE_INS;
    __afl_cmp_map->headers[k].shape = (size - 1);

  } else {
    hits = __afl_cmp_map->headers[k].hits;
  }

  __afl_cmp_map->headers[k].hits = hits + 1;
  hits &= CMP_MAP_H - 1;
  __afl_cmp_map->log[k][hits].v0 = operand1;
  __afl_cmp_map->log[k][hits].v1 = operand2;
}

static void cmplog_cmp_sub_callout(GumCpuContext *context, gpointer user_data) {
  cmplog_pair_ctx_t *ctx = (cmplog_pair_ctx_t *)user_data;
  gsize              operand1;
  gsize              operand2;

  if (!cmplog_get_operand_value(context, &ctx->operand1, &operand1)) { return; }
  if (!cmplog_get_operand_value(context, &ctx->operand2, &operand2)) { return; }

  cmplog_handle_cmp_sub(context, operand1, operand2, ctx->operand1.size);
}

static void cmplog_instrument_cmp_sub_put_callout(GumStalkerIterator *iterator,
                                                  cs_x86_op          *operand1,
                                                  cs_x86_op *operand2) {
  cmplog_pair_ctx_t *ctx = g_malloc(sizeof(cmplog_pair_ctx_t));
  if (ctx == NULL) return;

  cmplog_instrument_put_operand(&ctx->operand1, operand1);
  cmplog_instrument_put_operand(&ctx->operand2, operand2);

  gum_stalker_iterator_put_callout(iterator, cmplog_cmp_sub_callout, ctx,
                                   g_free);
}

static void cmplog_instrument_cmp_sub(const cs_insn      *instr,
                                      GumStalkerIterator *iterator) {
  cs_x86     x86 = instr->detail->x86;
  cs_x86_op *operand1;
  cs_x86_op *operand2;

  switch (instr->id) {
    case X86_INS_CMP:
    case X86_INS_SUB:
    case X86_INS_SCASB:
    case X86_INS_SCASD:
    case X86_INS_SCASQ:
    case X86_INS_SCASW:
    case X86_INS_CMPSB:
    case X86_INS_CMPSD:
    case X86_INS_CMPSQ:
    case X86_INS_CMPSS:
    case X86_INS_CMPSW:
      break;
    default:
      return;
  }

  if (x86.op_count != 2) return;

  operand1 = &x86.operands[0];
  operand2 = &x86.operands[1];

  if (operand1->type == X86_OP_INVALID) return;
  if (operand2->type == X86_OP_INVALID) return;

  /* Both operands are the same size */
  if (operand1->size == 1) { return; }

  cmplog_instrument_cmp_sub_put_callout(iterator, operand1, operand2);
}

void cmplog_instrument(const cs_insn *instr, GumStalkerIterator *iterator) {
  if (__afl_cmp_map == NULL) return;

  cmplog_instrument_call(instr, iterator);
  cmplog_instrument_cmp_sub(instr, iterator);
}

#endif
