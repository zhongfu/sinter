#include <math.h>
#include <inttypes.h>

#include <sinter.h>

#include "opcode.h"
#include "fault.h"
#include "nanbox.h"
#include "heap.h"
#include "heap_obj.h"
#include "vm.h"
#include "stack.h"
#include "debug.h"
#include "debug_heap.h"
#include "program.h"

struct sistate sistate;

static inline void unimpl_instr() {
  SIBUGV("Unimplemented instruction %02x at address 0x%tx\n", *sistate.pc, SISTATE_CURADDR);
  sifault(sinter_fault_invalid_program);
}

static inline void validate_header(const struct svm_header *const header) {
  if (header->magic != SVM_MAGIC) {
    SIDEBUG("Invalid magic: %x\n", header->magic);
    sifault(sinter_fault_invalid_program);
    return;
  }
}

#define WRAP_INTEGER(v) (((v) >= NANBOX_INTMIN && (v) <= NANBOX_INTMAX) ? \
  NANBOX_OFINT(v) : NANBOX_OFFLOAT(v))
#define DECLOPSTRUCT(type) const struct type *instr = (const struct type *) sistate.pc
#define ADVANCE_PCONE() sistate.pc += sizeof(opcode_t); continue
#define ADVANCE_PCI() sistate.pc += sizeof(*instr); continue

static void main_loop(void) {
#ifdef SINTER_DEBUG
  const opcode_t *previous_pc = NULL;
#endif
  while (1) {
#ifdef SINTER_DEBUG
    if (sistate.pc >= sistate.program_end) {
      SIBUGV("Jumped out of bounds to 0x%tx after instruction at address 0x%tx\n", SISTATE_CURADDR, previous_pc - sistate.program);
      sifault(sinter_fault_internal_error);
      return;
    }
    previous_pc = sistate.pc;

    SITRACE("PC: 0x%tx; opcode: %02x (%s)\n", SISTATE_CURADDR, *sistate.pc, get_opcode_name(*sistate.pc));
#endif
    const opcode_t this_opcode = *sistate.pc;
    switch (this_opcode) {
    case op_nop:
      ADVANCE_PCONE();
    case op_ldc_i:
    case op_lgc_i: {
      DECLOPSTRUCT(op_i32);
      sistack_push(WRAP_INTEGER(instr->operand));
      ADVANCE_PCI();
    }
    case op_ldc_f32:
    case op_lgc_f32: {
      DECLOPSTRUCT(op_f32);
      sistack_push(NANBOX_OFFLOAT(instr->operand));
      ADVANCE_PCI();
    }
    case op_ldc_f64:
    case op_lgc_f64: {
      DECLOPSTRUCT(op_f64);
      sistack_push(NANBOX_OFFLOAT((float) instr->operand));
      ADVANCE_PCI();
    }
    case op_ldc_b_0:
    case op_lgc_b_0:
      sistack_push(NANBOX_OFBOOL(false));
      ADVANCE_PCONE();
    case op_ldc_b_1:
    case op_lgc_b_1:
      sistack_push(NANBOX_OFBOOL(true));
      ADVANCE_PCONE();
    case op_lgc_u:
      sistack_push(NANBOX_OFUNDEF());
      ADVANCE_PCONE();
    case op_lgc_n:
      sistack_push(NANBOX_OFNULL());
      ADVANCE_PCONE();
    case op_lgc_s:
      // TODO
      unimpl_instr();
      DECLOPSTRUCT(op_address);
      ADVANCE_PCI();
    case op_pop_g:
    case op_pop_b:
    case op_pop_f:
      siheap_derefbox(sistack_pop());
      ADVANCE_PCONE();

#define ARITHMETIC_TYPECHECK() do { if (!NANBOX_ISNUMERIC(v0) || !NANBOX_ISNUMERIC(v1)) {\
  sifault(sinter_fault_type); \
  return; \
} } while (0)

    // TODO: optimised _f variants
    case op_add_g:
    case op_add_f: {
      sinanbox_t v1 = sistack_pop();
      sinanbox_t v0 = sistack_pop();
      sinanbox_t r;

      if (NANBOX_ISNUMERIC(v0) && NANBOX_ISNUMERIC(v1)) {
        switch (NANBOX_ISFLOAT(v1) << 1 | NANBOX_ISFLOAT(v0)) {
        case 0: /* neither are floats */
          /* addition/subtraction of 2 21-bit integers won't overflow a 32-bit integer; no worries here */
          r = WRAP_INTEGER(NANBOX_INT(v0) + NANBOX_INT(v1));
          break;
        case 1: /* v0 is float */
          r = NANBOX_OFFLOAT(NANBOX_FLOAT(v0) + NANBOX_INT(v1));
          break;
        case 2: /* v1 is float */
          r = NANBOX_OFFLOAT(NANBOX_INT(v0) + NANBOX_FLOAT(v1));
          break;
        case 3: /* both are float */
          r = NANBOX_OFFLOAT(NANBOX_FLOAT(v0) + NANBOX_FLOAT(v1));
          break;
        default:
          SIBUG();
          break;
        }
      } else if (NANBOX_ISPTR(v0) & NANBOX_ISPTR(v1)) {
        struct siheap_header *hv0 = SIHEAP_NANBOXTOPTR(v0);
        struct siheap_header *hv1 = SIHEAP_NANBOXTOPTR(v1);
        if (hv0->type == sinter_type_string && hv1->type == sinter_type_string) {
          // TODO string concat
          unimpl_instr();
          break;
        } else {
          SIDEBUG("Invalid operands to add.\n");
          sifault(sinter_fault_type);
          return;
        }
      } else {
        SIDEBUG("Invalid operands to add.\n");
        sifault(sinter_fault_type);
        return;
      }

      sistack_push(r);
      siheap_derefbox(v0);
      siheap_derefbox(v1);
      ADVANCE_PCONE();
    }
    break;
    case op_sub_g:
    case op_sub_f: {
      sinanbox_t v1 = sistack_pop();
      sinanbox_t v0 = sistack_pop();
      ARITHMETIC_TYPECHECK();
      sinanbox_t r;
      switch (NANBOX_ISFLOAT(v1) << 1 | NANBOX_ISFLOAT(v0)) {
      case 0: /* neither are floats */
        /* addition/subtraction of 2 21-bit integers won't overflow a 32-bit integer; no worries here */
        r = WRAP_INTEGER(NANBOX_INT(v0) - NANBOX_INT(v1));
        break;
      case 1: /* v0 is float */
        r = NANBOX_OFFLOAT(NANBOX_FLOAT(v0) - NANBOX_INT(v1));
        break;
      case 2: /* v1 is float */
        r = NANBOX_OFFLOAT(NANBOX_INT(v0) - NANBOX_FLOAT(v1));
        break;
      case 3: /* both are float */
        r = NANBOX_OFFLOAT(NANBOX_FLOAT(v0) - NANBOX_FLOAT(v1));
        break;
      default:
        SIBUG();
        sifault(sinter_fault_internal_error);
        return;
      }
      sistack_push(r);
      siheap_derefbox(v0);
      siheap_derefbox(v1);
      ADVANCE_PCONE();
    }

    case op_mul_g:
    case op_mul_f: {
      sinanbox_t v1 = sistack_pop();
      sinanbox_t v0 = sistack_pop();
      ARITHMETIC_TYPECHECK();
      sinanbox_t r;
      switch (NANBOX_ISFLOAT(v1) << 1 | NANBOX_ISFLOAT(v0)) {
      case 0: /* neither are floats */
        /* this can overflow, use int64 instead */
        r = WRAP_INTEGER(((int64_t) NANBOX_INT(v0)) * ((int64_t) NANBOX_INT(v1)));
        break;
      case 1: /* v0 is float */
        r = NANBOX_OFFLOAT(NANBOX_FLOAT(v0) *  NANBOX_INT(v1));
        break;
      case 2: /* v1 is float */
        r = NANBOX_OFFLOAT(NANBOX_INT(v0) * NANBOX_FLOAT(v1));
        break;
      case 3: /* both are float */
        r = NANBOX_OFFLOAT(NANBOX_FLOAT(v0) * NANBOX_FLOAT(v1));
        break;
      default:
        SIBUG();
        sifault(sinter_fault_internal_error);
        return;
      }
      sistack_push(r);
      siheap_derefbox(v0);
      siheap_derefbox(v1);
      ADVANCE_PCONE();
    }

    case op_div_g:
    case op_div_f: {
      sinanbox_t v1 = sistack_pop();
      sinanbox_t v0 = sistack_pop();
      ARITHMETIC_TYPECHECK();
      sinanbox_t r;
      switch (NANBOX_ISFLOAT(v1) << 1 | NANBOX_ISFLOAT(v0)) {
      case 0: /* neither are floats */
        r = NANBOX_OFFLOAT(((float) NANBOX_INT(v0)) / NANBOX_INT(v1));
        break;
      case 1: /* v0 is float */
        r = NANBOX_OFFLOAT(NANBOX_FLOAT(v0) / NANBOX_INT(v1));
        break;
      case 2: /* v1 is float */
        r = NANBOX_OFFLOAT(NANBOX_INT(v0) / NANBOX_FLOAT(v1));
        break;
      case 3: /* both are float */
        r = NANBOX_OFFLOAT(NANBOX_FLOAT(v0) / NANBOX_FLOAT(v1));
        break;
      default:
        SIBUG();
        sifault(sinter_fault_internal_error);
        return;
      }
      sistack_push(r);
      siheap_derefbox(v0);
      siheap_derefbox(v1);
      ADVANCE_PCONE();
    }

    case op_mod_g:
    case op_mod_f: {
      sinanbox_t v1 = sistack_pop();
      sinanbox_t v0 = sistack_pop();
      ARITHMETIC_TYPECHECK();
      sinanbox_t r;
      switch (NANBOX_ISFLOAT(v1) << 1 | NANBOX_ISFLOAT(v0)) {
      case 0: /* neither are floats */
        r = NANBOX_OFFLOAT(fmodf(NANBOX_INT(v0),  NANBOX_INT(v1)));
        break;
      case 1: /* v0 is float */
        r = NANBOX_OFFLOAT(fmodf(NANBOX_FLOAT(v0), NANBOX_INT(v1)));
        break;
      case 2: /* v1 is float */
        r = NANBOX_OFFLOAT(fmodf(NANBOX_INT(v0), NANBOX_FLOAT(v1)));
        break;
      case 3: /* both are float */
        r = NANBOX_OFFLOAT(fmodf(NANBOX_FLOAT(v0), NANBOX_FLOAT(v1)));
        break;
      default:
        SIBUG();
        break;
      }
      sistack_push(r);
      siheap_derefbox(v0);
      siheap_derefbox(v1);
      ADVANCE_PCONE();
    }

    case op_not_g:
    case op_not_b: {
      sinanbox_t v = sistack_pop();
      if (!NANBOX_ISBOOL(v)) {
        sifault(sinter_fault_type);
        return;
      }
      sistack_push(NANBOX_OFBOOL(!NANBOX_BOOL(v)));
      ADVANCE_PCONE();
    }

#define COMPARISON_OP(op) { \
      sinanbox_t v1 = sistack_pop(); \
      sinanbox_t v0 = sistack_pop(); \
      sinanbox_t r; \
 \
      if (NANBOX_ISNUMERIC(v0) && NANBOX_ISNUMERIC(v1)) { \
        switch (NANBOX_ISFLOAT(v1) << 1 | NANBOX_ISFLOAT(v0)) { \
        case 0: /* neither are floats */ \
          r = NANBOX_OFBOOL(NANBOX_INT(v0) op NANBOX_INT(v1)); \
          break; \
        case 1: /* v0 is float */ \
          r = NANBOX_OFBOOL(NANBOX_FLOAT(v0) op NANBOX_INT(v1)); \
          break; \
        case 2: /* v1 is float */ \
          r = NANBOX_OFBOOL(NANBOX_INT(v0) op NANBOX_FLOAT(v1)); \
          break; \
        case 3: /* both are float */ \
          r = NANBOX_OFBOOL(NANBOX_FLOAT(v0) op NANBOX_FLOAT(v1)); \
          break; \
        default: \
          SIBUG(); \
          break; \
        } \
      } else if (NANBOX_ISPTR(v0) & NANBOX_ISPTR(v1)) { \
        struct siheap_header *hv0 = SIHEAP_NANBOXTOPTR(v0); \
        struct siheap_header *hv1 = SIHEAP_NANBOXTOPTR(v1); \
        if (hv0->type == sinter_type_string && hv1->type == sinter_type_string) { \
          /* TODO string comparison */ \
          /* strcmp(hv0->str, hv1->str) op 0 */ \
          unimpl_instr(); \
          break; \
        } else { \
          SIDEBUG("Invalid operands to comparison.\n"); \
          sifault(sinter_fault_type); \
          return; \
        } \
      } else { \
        SIDEBUG("Invalid operands to comparison.\n"); \
        sifault(sinter_fault_type); \
        return; \
      } \
 \
      sistack_push(r); \
      siheap_derefbox(v0); \
      siheap_derefbox(v1); \
      ADVANCE_PCONE(); \
    }

    case op_lt_g:
    case op_lt_f:
      COMPARISON_OP(<)
    case op_gt_g:
    case op_gt_f:
      COMPARISON_OP(>)
    case op_le_g:
    case op_le_f:
      COMPARISON_OP(<=)
    case op_ge_g:
    case op_ge_f:
      COMPARISON_OP(>=)
    case op_eq_g:
    case op_eq_f:
    case op_eq_b: {
      sinanbox_t v0 = sistack_pop();
      sinanbox_t v1 = sistack_pop();
      sinanbox_t r;

      if (NANBOX_GETTYPE(v0) == NANBOX_GETTYPE(v1)) {
        // if they are *identical* then they are equal provided they are not NaN
        r = NANBOX_OFBOOL(NANBOX_IDENTICAL(v0, v1) && !NANBOX_IDENTICAL(v0, NANBOX_CANONICAL_NAN));
      } else if (NANBOX_ISNUMERIC(v0) && NANBOX_ISNUMERIC(v1)) {
        switch (NANBOX_ISFLOAT(v1) << 1 | NANBOX_ISFLOAT(v0)) {
        case 0: /* neither are floats */
          r = NANBOX_OFBOOL(NANBOX_INT(v0) == NANBOX_INT(v1));
          break;
        case 1: /* v0 is float */
          r = NANBOX_OFBOOL(NANBOX_FLOAT(v0) == NANBOX_INT(v1));
          break;
        case 2: /* v1 is float */
          r = NANBOX_OFBOOL(NANBOX_INT(v0) == NANBOX_FLOAT(v1));
          break;
        case 3: /* both are float */
          r = NANBOX_OFBOOL(NANBOX_FLOAT(v0) == NANBOX_FLOAT(v1));
          break;
        default:
          SIBUG();
          sifault(sinter_fault_internal_error);
          return;
        }
      } else if (NANBOX_ISPTR(v0) & NANBOX_ISPTR(v1)) {
        struct siheap_header *hv0 = SIHEAP_NANBOXTOPTR(v0);
        struct siheap_header *hv1 = SIHEAP_NANBOXTOPTR(v1);
        if (hv0->type == sinter_type_string && hv1->type == sinter_type_string) {
          /* TODO string comparison */
          /* strcmp(hv0->str, hv1->str) op 0 */
          unimpl_instr();
        } else {
          // for arrays and functions, identical only if they are the SAME object
          r = NANBOX_OFBOOL(hv0 == hv1);
        }
      } else {
        SIBUG();
        sifault(sinter_fault_internal_error);
        return;
      }

      sistack_push(r);
      siheap_derefbox(v0);
      siheap_derefbox(v1);
      ADVANCE_PCONE();
    }

    case op_new_c: {
      DECLOPSTRUCT(op_address);
      const struct svm_function *fn_code = (const struct svm_function *) SISTATE_ADDRTOPC(instr->address);
      struct siheap_function *fn_obj = sifunction_new(fn_code, sistate.env);
      siheap_ref(fn_obj);
      sistack_push(SIHEAP_PTRTONANBOX(fn_obj));
      ADVANCE_PCI();
    }
    break;
    case op_new_a:
      unimpl_instr();
      ADVANCE_PCONE();

    case op_ldl_g:
    case op_ldl_f:
    case op_ldl_b: {
      DECLOPSTRUCT(op_oneindex);
      sinanbox_t v = sienv_get(sistate.env, instr->index);
      siheap_refbox(v);
      sistack_push(v);
      ADVANCE_PCI();
    }

    case op_stl_g:
    case op_stl_b:
    case op_stl_f: {
      DECLOPSTRUCT(op_oneindex);
      sinanbox_t v = sistack_pop();
      sienv_put(sistate.env, instr->index, v);
      ADVANCE_PCI();
    }

    case op_ldp_g:
    case op_ldp_f:
    case op_ldp_b: {
      DECLOPSTRUCT(op_twoindex);
      struct siheap_env *env = sienv_getparent(sistate.env, instr->envindex);
      if (!env) {
        sifault(sinter_fault_invalid_load);
        return;
      }
      sinanbox_t v = sienv_get(env, instr->index);
      siheap_refbox(v);
      sistack_push(v);
      ADVANCE_PCI();
    }

    case op_stp_g:
    case op_stp_b:
    case op_stp_f: {
      DECLOPSTRUCT(op_twoindex);
      struct siheap_env *env = sienv_getparent(sistate.env, instr->envindex);
      if (!env) {
        sifault(sinter_fault_invalid_load);
        return;
      }
      sinanbox_t v = sistack_pop();
      sienv_put(env, instr->index, v);
      ADVANCE_PCI();
    }

    case op_lda_g:
    case op_lda_b:
    case op_lda_f:
    case op_sta_g:
    case op_sta_b:
    case op_sta_f:
      unimpl_instr();
      break;

    case op_br_t:
    case op_br_f: {
      DECLOPSTRUCT(op_offset);
      sinanbox_t v = sistack_pop();
      if (!NANBOX_ISBOOL(v)) {
        sifault(sinter_fault_type);
        return;
      }
      if (NANBOX_BOOL(v) == (this_opcode == op_br_t)) {
        sistate.pc += instr->offset + sizeof(*instr);
        break;
      } else {
        ADVANCE_PCI();
      }
    }

    case op_br: {
      DECLOPSTRUCT(op_offset);
      sistate.pc += instr->offset + sizeof(*instr);
      break;
    }

    case op_jmp: {
      DECLOPSTRUCT(op_address);
      sistate.pc = SISTATE_ADDRTOPC(instr->address);
      break;
    }

    case op_call:
    case op_call_t: {
      DECLOPSTRUCT(op_call);

      // get the function object
      sinanbox_t fn_ptr = sistack_peek(instr->num_args);
      struct siheap_function *fn_obj = SIHEAP_NANBOXTOPTR(fn_ptr);

      // check the type before we dereference further
      if (!NANBOX_ISPTR(fn_ptr) || fn_obj->header.type != sinter_type_function) {
        sifault(sinter_fault_type);
        return;
      }

      // get the code
      const struct svm_function *fn_code = fn_obj->code;

      // create the new environment
      struct siheap_env *new_env = sienv_new(fn_obj->env, fn_code->env_size);
      siheap_ref(new_env);

      // pop the arguments off the caller's stack
      // insert them into the callee's environment
      const unsigned int num_args = instr->num_args > fn_code->num_args
        ? fn_code->num_args : instr->num_args;
      for (unsigned int i = 0; i < num_args; ++i) {
        sinanbox_t v = sistack_pop();
        sienv_put(new_env, num_args - 1 - i, v);
      }

      // pop the function off the caller's stack
      sistack_pop();

      // if tail call, we destroy the caller's stack now, and "return" to the caller's caller
      if (this_opcode == op_call_t) {
        sistack_destroy(&sistate.pc, &sistate.env);
      } else {
        // otherwise we advance to the return address
        sistate.pc += sizeof(*instr);
      }

      // create the stack frame for the callee, which stores the return address and environment
      sistack_new(fn_code->stack_size, sistate.pc, sistate.env);

      // set the environment
      sistate.env = new_env;

      // enter the function
      sistate.pc = &fn_code->code;

      break;
    }

    case op_call_p:
    case op_call_t_p:
    case op_call_v:
    case op_call_t_v:
      unimpl_instr();
      break;

    case op_ret_g:
    case op_ret_f:
    case op_ret_b: {
      // pop the return value
      sinanbox_t v = sistack_pop();

      // destroy this stack frame, and return to the caller
      siheap_deref(sistate.env);
      sistack_destroy(&sistate.pc, &sistate.env);

      // push the return value onto the caller's stack
      sistack_push(v);

      // return from top-level (main); exit loop
      if (!sistate.pc) {
        return;
      }

      break;
    }

    case op_ret_u:
    case op_ret_n:
      // destroy this stack frame, and return to the caller
      siheap_deref(sistate.env);
      sistack_destroy(&sistate.pc, &sistate.env);

      // push the return value onto the caller's stack
      if (this_opcode == op_ret_u) {
        sistack_push(NANBOX_OFUNDEF());
      } else {
        sistack_push(NANBOX_OFNULL());
      }

      // return from top-level (main); exit loop
      if (!sistate.pc) {
        return;
      }

      break;

    case op_newenv: {
      DECLOPSTRUCT(op_oneindex);
      struct siheap_env *new_env = sienv_new(sistate.env, instr->index);
      sistate.env = new_env;
      ADVANCE_PCI();
    }

    case op_popenv: {
      struct siheap_env *old_env = sistate.env;
      sistate.env = old_env->parent;
      siheap_deref(old_env);
      ADVANCE_PCONE();
    }

    default:
      SIBUGV("Invalid instruction %02x at address 0x%tx\n", this_opcode, SISTATE_CURADDR);
      sifault(sinter_fault_invalid_program);
      break;
    }

  }
}

static void set_result(struct sinter_value *result) {
  if (sistack_top == sistack_bottom) {
    SIDEBUG("Program did not return value from toplevel\n");
    return;
  }

  sinanbox_t v = *sistack_bottom;
  SIDEBUG("Return value: ");
  SIDEBUG_NANBOX(v);
  SIDEBUG("\n");
  switch (NANBOX_GETTYPE(v)) {
  NANBOX_CASES_TINT
    result->type = sinter_type_integer;
    result->integer_value = NANBOX_INT(v);
    break;
  case NANBOX_TBOOL:
    result->type = sinter_type_boolean;
    result->boolean_value = NANBOX_BOOL(v);
    break;
  case NANBOX_TUNDEF:
    result->type = sinter_type_undefined;
    break;
  case NANBOX_TNULL:
    result->type = sinter_type_null;
    break;
  default:
    if (NANBOX_ISFLOAT(v)) {
      result->type = sinter_type_float;
      result->float_value = NANBOX_FLOAT(v);
    } else {
      SIBUGM("Unexpected return type\n");
    }
    break;
  }
}

enum sinter_fault sinter_run(const unsigned char *const code, const size_t code_size, struct sinter_value *result) {
  sistate.fault_reason = sinter_fault_none;
  sistate.program = code;
  sistate.program_end = code + code_size;
  sistate.running = true;
  sistate.pc = NULL;
  sistate.env = NULL;

  if (SINTER_FAULTED()) {
    *result = (struct sinter_value) { 0 };
    return sistate.fault_reason;
  }

  // Reset the heap and stack
  siheap_init();
  sistack_init();

  // Create one entry for the return value of the entrypoint
  sistack_limit++;

  const struct svm_header *header = (const struct svm_header *) code;
  validate_header(header);

  const struct svm_function *entry_fn = (const struct svm_function *) SISTATE_ADDRTOPC(header->entry);
  sistate.env = sienv_new(NULL, entry_fn->env_size);
  sistack_new(entry_fn->stack_size, NULL, NULL);
  sistate.pc = &entry_fn->code;

  main_loop();

  set_result(result);

  return sinter_fault_none;
}