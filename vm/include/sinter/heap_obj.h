#ifndef SINTER_HEAP_OBJ_H
#define SINTER_HEAP_OBJ_H

#include "opcode.h"
#include "heap.h"

typedef struct siheap_env {
  siheap_header_t header;
  struct siheap_env *parent;
  uint16_t entry_count;
  sinanbox_t entry[];
} siheap_env_t;

#define SIENV_SIZE(entry_count) (sizeof(siheap_env_t) + (entry_count)*sizeof(sinanbox_t))

/**
 * Create a new environment heap object.
 *
 * This increments the reference count on the parent environment, if any.
 */
static inline siheap_env_t *sienv_new(
  siheap_env_t *parent,
  const uint16_t entry_count) {
  siheap_env_t *env = (siheap_env_t *) siheap_malloc(SIENV_SIZE(entry_count), sitype_env);
  env->parent = parent;
  env->entry_count = entry_count;
  for (size_t i = 0; i < entry_count; ++i) {
    env->entry[i] = NANBOX_OFEMPTY();
  }
  if (parent) {
    siheap_ref(parent);
  }

  return env;
}

static inline void sienv_destroy(siheap_env_t *const env) {
  for (size_t i = 0; i < env->entry_count; ++i) {
    siheap_derefbox(env->entry[i]);
  }
  if (env->parent) {
    siheap_deref(env->parent);
  }
}

/**
 * Get a value from the environment.
 *
 * Note: the caller is responsible for incrementing the reference count, if needed.
 */
static inline sinanbox_t sienv_get(
  siheap_env_t *const env,
  const uint16_t index) {

#ifndef SINTER_SEATBELTS_OFF
  if (index >= env->entry_count) {
    sifault(sinter_fault_invalid_load);
    return NANBOX_OFEMPTY();
  }
#endif

  return env->entry[index];
}

/**
 * Put a value into the environment.
 *
 * If a heap pointer is replaced, the heap object's reference count is decremented.
 *
 * Note: the caller "passes" its reference to the environment. That is, the caller should increment the reference
 * count of the heap object (if the value is a pointer) if it is going to continue holding on to the value.
 */
static inline void sienv_put(
  siheap_env_t *const env,
  const uint16_t index,
  const sinanbox_t val) {

#ifndef SINTER_SEATBELTS_OFF
  if (index >= env->entry_count) {
    sifault(sinter_fault_invalid_load);
    return;
  }
#endif

  siheap_derefbox(env->entry[index]);
  env->entry[index] = val;
}

static inline siheap_env_t *sienv_getparent(
  siheap_env_t *env,
  unsigned int index) {
  while (env && index--) {
    env = env->parent;
  }
  return env;
}

typedef struct {
  siheap_header_t header;
  const svm_function_t *code;
  siheap_env_t *env;
} siheap_function_t;

static inline siheap_function_t *sifunction_new(const svm_function_t *code, siheap_env_t *parent_env) {
  siheap_function_t *fn = (siheap_function_t *) siheap_malloc(sizeof(siheap_function_t), sinter_type_function);
  fn->code = code;
  fn->env = parent_env;
  siheap_ref(parent_env);

  return fn;
}

static inline void sifunction_destroy(siheap_function_t *fn) {
  if (fn->env) {
    siheap_deref(fn->env);
  }
}

typedef struct {
  siheap_header_t header;
  const opcode_t *return_address;
  sinanbox_t *saved_stack_bottom;
  sinanbox_t *saved_stack_limit;
  sinanbox_t *saved_stack_top;
  siheap_env_t *saved_env;
} siheap_frame_t;

static inline siheap_frame_t *siframe_new(void) {
  return (siheap_frame_t *) siheap_malloc(
    sizeof(siheap_frame_t), sitype_frame);
}

#endif