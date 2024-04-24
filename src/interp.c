#include <hlmodule.h>
#include <ffi.h>
#include <math.h>

// TODO: use hl_same_type

struct interp_ctx {
    hl_alloc alloc;
    hl_module *m;
    int **fregs_offsets;
    int *fargs_sizes;
};

#define hl_interp_error(msg) hl_fatal(msg);

static interp_ctx *global_ctx = NULL;

static inline void hl_copy_type_data(void *restrict dst, const void *restrict src, hl_type *type) {
    switch (type->kind) {
        case HUI8:  memcpy(dst, src, sizeof(hl_ui8)); break;
        case HUI16: memcpy(dst, src, sizeof(hl_ui16)); break;
        case HI32:  memcpy(dst, src, sizeof(hl_i32)); break;
        case HI64:  memcpy(dst, src, sizeof(hl_i64)); break;
        case HF32:  memcpy(dst, src, sizeof(hl_f32)); break;
        case HF64:  memcpy(dst, src, sizeof(hl_f64)); break;
        case HBOOL: memcpy(dst, src, sizeof(hl_bool)); break;
        case HBYTES:
        case HDYN:
        case HFUN:
        case HOBJ:
        case HARRAY:
        case HTYPE:
        case HREF:
        case HVIRTUAL:
        case HDYNOBJ:
        case HABSTRACT:
        case HENUM:
        case HNULL:
        case HMETHOD:
        case HSTRUCT: memcpy(dst, src, sizeof(void*)); break;
        case HVOID: break;
        case HPACKED:
        default: HL_UNREACHABLE; break;
    }
}

static inline hl_usize hl_minusize(hl_usize a, hl_usize b) {
    return a < b ? a : b;
}

static inline void hl_copy_type_data_until(void *restrict dst, const void *restrict src, hl_type *type, hl_usize max_n) {
    switch (type->kind) {
        case HUI8:  memcpy(dst, src, hl_minusize(sizeof(hl_ui8), max_n)); break;
        case HUI16: memcpy(dst, src, hl_minusize(sizeof(hl_ui16), max_n)); break;
        case HI32:  memcpy(dst, src, hl_minusize(sizeof(hl_i32), max_n)); break;
        case HI64:  memcpy(dst, src, hl_minusize(sizeof(hl_i64), max_n)); break;
        case HF32:  memcpy(dst, src, hl_minusize(sizeof(hl_f32), max_n)); break;
        case HF64:  memcpy(dst, src, hl_minusize(sizeof(hl_f64), max_n)); break;
        case HBOOL: memcpy(dst, src, hl_minusize(sizeof(hl_bool), max_n)); break;
        case HBYTES:
        case HDYN:
        case HFUN:
        case HOBJ:
        case HARRAY:
        case HTYPE:
        case HREF:
        case HVIRTUAL:
        case HDYNOBJ:
        case HABSTRACT:
        case HENUM:
        case HNULL:
        case HMETHOD:
        case HSTRUCT: memcpy(dst, src, hl_minusize(sizeof(void*), max_n)); break;
        case HVOID: break;
        case HPACKED:
        default: HL_UNREACHABLE; break;
    }
}

static inline hl_usize hl_ntype_size(hl_type *type) {
    switch (type->kind) {
        case HUI8: return sizeof(hl_ui8);
	    case HUI16: return sizeof(hl_ui16);
	    case HI32: return sizeof(hl_i32);
        case HI64: return sizeof(hl_i64);
        case HF32: return sizeof(hl_f32);
        case HF64: return sizeof(hl_f64);
        case HBOOL: return sizeof(hl_bool);
        case HBYTES:
        case HDYN:
        case HFUN:
        case HOBJ:
        case HARRAY:
        case HTYPE:
        case HREF:
        case HVIRTUAL:
        case HDYNOBJ:
        case HABSTRACT:
        case HENUM:
        case HNULL:
        case HMETHOD:
        case HSTRUCT: return sizeof(void*);
        case HVOID:
        case HPACKED: return 0;
        default: HL_UNREACHABLE; return 0;
    }
}

static inline hl_bool hl_type_can_be_null(hl_type *type) {
    switch (type->kind) {
        case HUI8:
        case HUI16:
        case HI32:
        case HI64:
        case HF32:
        case HF64:
        case HBOOL: return false;
        default: return true;
    }
}

static inline hl_bool hl_type_is_int(hl_type *type) {
    switch (type->kind) {
        case HUI8:
        case HUI16:
        case HI32:
        case HI64: return true;
        default: return false;
    }
}

static inline hl_bool hl_type_is_float(hl_type *type) {
    switch (type->kind) {
        case HF32:
        case HF64: return true;
        default: return false;
    }
}

static inline hl_bool hl_type_is_number(hl_type *type) {
    return hl_type_is_int(type) || hl_type_is_float(type);
}

void *hl_interp_run(interp_ctx *ctx, hl_function *f, vbyte* reg_data, vdynamic *ret);
void hl_interp_raw_function_call(interp_ctx *ctx, int findex, hl_usize nargs, vbyte **args, hl_type **args_types, vdynamic *ret);
void hl_interp_native_ffi_call(interp_ctx *ctx, void (*fptr)(), hl_usize nargs, vbyte **args, hl_type **args_types, vdynamic *ret);
void hl_interp_raw_bytecode_function_call(interp_ctx *ctx, hl_function *callee, hl_usize nargs, vbyte **args, hl_type **args_types, vdynamic *ret);

static inline ffi_type *hl_type_to_ffi(hl_type *type) {
    switch (type->kind) {
        case HUI8: return &ffi_type_uint8;
	    case HUI16: return &ffi_type_uint16;
	    case HI32: return &ffi_type_sint32;
        case HI64: return &ffi_type_sint64;
        case HF32: return &ffi_type_float;
        case HF64: return &ffi_type_double;
        case HBOOL: return &ffi_type_uint8;
        case HBYTES:
        case HDYN:
        case HFUN:
        case HOBJ:
        case HARRAY:
        case HTYPE:
        case HREF:
        case HVIRTUAL:
        case HDYNOBJ:
        case HABSTRACT:
        case HENUM:
        case HNULL:
        case HMETHOD:
        case HSTRUCT: return &ffi_type_pointer;
        case HVOID: return &ffi_type_void;
        case HPACKED:
        default: HL_UNREACHABLE; return &ffi_type_void;
    }
}

void hl_interp_raw_function_call(interp_ctx *ctx, int findex, hl_usize nargs, vbyte **args, hl_type **args_types, vdynamic *ret) {
    hl_module *m = ctx->m;
    hl_code *code = m->code;
    int real_findex = m->functions_indexes[findex];
    hl_bool is_native = real_findex >= code->nfunctions;

    if(!is_native) {
        hl_function *hl_callee = &code->functions[real_findex];
        return hl_interp_raw_bytecode_function_call(ctx, hl_callee, nargs, args, args_types, ret);
    }

    void(*fptr)() = m->functions_ptrs[findex];
    return hl_interp_native_ffi_call(ctx, fptr, nargs, args, args_types, ret);
}

void hl_interp_native_ffi_call(interp_ctx *ctx, void (*fptr)(), hl_usize nargs, vbyte **args, hl_type **args_types, vdynamic *ret) {
    hl_module *m = ctx->m;
    hl_code *code = m->code;
    hl_type *ret_type = ret->t;

    ffi_cif cif;
    ffi_type **ffi_args_type = (ffi_type**)malloc(nargs * sizeof(ffi_type*)); // FIXME: mem pool?

    for (int i = 0; i < nargs; ++i) {
        ffi_args_type[i] = hl_type_to_ffi(args_types[i]);
    }

    if(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, hl_type_to_ffi(ret_type), ffi_args_type) != FFI_OK) {
        HL_UNREACHABLE;
    }

    ffi_call(&cif, fptr, &ret->v, (void**)args);
    free(ffi_args_type);
}

void hl_interp_raw_bytecode_function_call(interp_ctx *ctx, hl_function *callee, hl_usize nargs, vbyte **args, hl_type **args_types, vdynamic *ret) {
    hl_module* m = ctx->m;
    int real_findex = m->functions_indexes[callee->findex];
    hl_type_fun *callee_type = callee->type->fun;
    hl_type *callee_return_type = callee_type->ret;
    hl_type *ret_type = ret->t;

    if(ret_type->kind != callee_return_type->kind) {
        HL_UNREACHABLE;
    }

    int *function_regs_offsets = ctx->fregs_offsets[real_findex];
    int total_regs_size = function_regs_offsets[callee->nregs];
    vbyte *regs_data = total_regs_size ? (vbyte*)malloc(total_regs_size*sizeof(vbyte)) : NULL;

    if(nargs) {
        if(!args || !args_types) {
            HL_UNREACHABLE;
        }

        int i;
        for (i = 0; i < nargs; ++i) {
            vbyte* arg_data = args[i];
            hl_type* arg_type = args_types[i];

            int reg_offset = function_regs_offsets[i];

            hl_copy_type_data(&regs_data[reg_offset], arg_data, arg_type);
        }

        // TODO: Write 0xCC in debug
        int total_offset = function_regs_offsets[i];
        memset(&regs_data[total_offset], 0x00, total_regs_size-total_offset);
    }

    hl_interp_run(ctx, callee, regs_data, ret);

    if(regs_data)
        free(regs_data);
}

#include <signal.h>
void *hl_interp_run(interp_ctx *ctx, hl_function *f, vbyte* reg_data, vdynamic *ret) {
    hl_type *ret_type = ret->t;

    hl_alloc *alloc = &ctx->alloc;
    hl_module *m = ctx->m;
    hl_code *code = m->code;
    int real_findex = m->functions_indexes[f->findex];

    int* const_integer_pool = code->ints;
    double *const_floating_pool = code->floats;
    vbyte *const_bytes_pool = (vbyte*)code->bytes;
    int *const_bytes_offsets = code->bytes_pos;
    char **const_string_pool = code->strings;

    int *function_regs_offsets = ctx->fregs_offsets[real_findex];
    int total_regs_size = function_regs_offsets[f->nregs];
    hl_type **fregs = f->regs;

    for (int i = 0; i < f->nregs; ++i) {
        if(hl_type_can_be_null(fregs[i]))
            hl_add_root(&reg_data[function_regs_offsets[i]]);
    }

    hl_usize bytecode_size = f->nops;
    hl_opcode *bytecode = f->ops;

    hl_usize current_op = 0;
    while (current_op < bytecode_size) {
        hl_opcode *opcode = &bytecode[current_op];

        switch (opcode->op) {
            case OMov: {
                int dst_reg_id = opcode->p1;
                int src_reg_id = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(dst_reg_type->kind != src_reg_type->kind) {
                    HL_UNREACHABLE;
                }

                hl_copy_type_data(dst_reg_data, src_reg_data, dst_reg_type);
                break;
            }
            case OInt: {
                int dst_reg_id = opcode->p1;
                int src_int_index = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                switch (dst_reg_type->kind) {
                    case HUI8: {
                        hl_ui8 src = const_integer_pool[src_int_index];
                        memcpy(dst_reg_data, &src, sizeof(hl_ui8));
                        break;
                    }
                    case HUI16: {
                        hl_ui16 src = const_integer_pool[src_int_index];
                        memcpy(dst_reg_data, &src, sizeof(hl_ui16));
                        break;
                    }
                    case HI32: {
                        hl_i32 src = const_integer_pool[src_int_index];
                        memcpy(dst_reg_data, &src, sizeof(hl_i32));
                        break;
                    }
                    case HI64: {
                        hl_i64 src = const_integer_pool[src_int_index];
                        memcpy(dst_reg_data, &src, sizeof(hl_i64));
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case OFloat: {
                int dst_reg_id = opcode->p1;
                int src_float_index = opcode->p2;
                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                switch (dst_reg_type->kind) {
                    case HF32: {
                        hl_f32 src = const_floating_pool[src_float_index];
                        memcpy(dst_reg_data, &src, sizeof(hl_f32));
                        break;
                    }
                    case HF64: {
                        hl_f64 src = const_floating_pool[src_float_index];
                        memcpy(dst_reg_data, &src, sizeof(hl_f64));
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case OBool: {
                int dst_reg_id = opcode->p1;
                hl_bool bool_value = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                
                if(dst_reg_type->kind != HBOOL) {
                    HL_UNREACHABLE;
                }

                memcpy(dst_reg_data, &bool_value, sizeof(hl_bool));
                break;
            }
            case OBytes: {
                int dst_reg_id = opcode->p1;
                int src_bytes_index = opcode->p2;
                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                
                if(dst_reg_type->kind != HBYTES) {
                    HL_UNREACHABLE;
                }

                int src_bytes_offset = const_bytes_offsets[src_bytes_index];
                vbyte *src_bytes = &const_bytes_pool[src_bytes_offset];
                memcpy(dst_reg_data, &src_bytes, sizeof(vbyte*));
                break;
            }
            case OString: {
                int dst_reg_id = opcode->p1;
                int src_string_index = opcode->p2;
                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                
                if(dst_reg_type->kind != HBYTES) {
                    HL_UNREACHABLE;
                }

                const uchar *src_ustring = hl_get_ustring(code, src_string_index);
                memcpy(dst_reg_data, &src_ustring, sizeof(char*));
                break;
            }
            case ONull: {
                int dst_reg_id = opcode->p1;
                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                if(!hl_type_can_be_null(dst_reg_type)) {
                    HL_UNREACHABLE;
                }

                memset(dst_reg_data, 0x00, sizeof(void*));
                break;
            }
            case OAdd:
            case OSub:
            case OMul:
            case OSDiv: // Maintain JIT behaviour, a / b when b == 0 -> 0
            case OUDiv:
            case OSMod:
            case OUMod: {
                int dst_reg_id = opcode->p1;
                int a_reg_id = opcode->p2;
                int b_reg_id = opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                hl_type *a_reg_type = fregs[a_reg_id];
                vbyte *a_reg_data = &reg_data[function_regs_offsets[a_reg_id]];
                hl_type *b_reg_type = fregs[b_reg_id];
                vbyte *b_reg_data = &reg_data[function_regs_offsets[b_reg_id]];

                if(hl_type_is_number(dst_reg_type) && (dst_reg_type->kind != a_reg_type->kind || a_reg_type->kind != b_reg_type->kind)) {
                    HL_UNREACHABLE; // ?? is it true
                }

                switch (dst_reg_type->kind) {
                    case HUI8:
                    case HUI16: // We can't do this if they were signed. For these we don't need sign extension.
                    case HI32: {
                        hl_i32 a = 0; hl_copy_type_data(&a, a_reg_data, a_reg_type);
                        hl_i32 b = 0; hl_copy_type_data(&b, b_reg_data, b_reg_type);

                        hl_i32 result;
                        switch (opcode->op) {
                            case OAdd: result = a + b; break;
                            case OSub: result = a - b; break;
                            case OMul: result = a * b; break;
                            case OSDiv: result = b ? a / b : 0; break;
                            case OUDiv: result = b ? (((hl_u32)a) / ((hl_u32)b)) : 0; break;
                            case OSMod: result = b ? a % b : 0; break;
                            case OUMod: result = b ? (((hl_u32)a) % ((hl_u32)b)) : 0; break;
                            default: HL_UNREACHABLE; break;
                        }
                        hl_copy_type_data(dst_reg_data, &result, dst_reg_type);
                        break;
                    }
                    case HI64: {
                        hl_i64 a; memcpy(&a, a_reg_data, sizeof(hl_i64));
                        hl_i64 b; memcpy(&b, b_reg_data, sizeof(hl_i64));

                        hl_i64 result;
                        switch (opcode->op) {
                            case OAdd: result = a + b; break;
                            case OSub: result = a - b; break;
                            case OMul: result = a * b; break;
                            case OSDiv: result = b ? a / b : 0; break;
                            case OUDiv: result = b ? (((hl_u64)a) / ((hl_u64)b)) : 0; break;
                            case OSMod: result = b ? a % b : 0; break;
                            case OUMod: result = b ? (((hl_u64)a) % ((hl_u64)b)) : 0; break;
                            default: HL_UNREACHABLE; break;
                        }
                        memcpy(dst_reg_data, &result, sizeof(hl_i64));
                        break;
                    }
                    case HF32: {
                        hl_f32 a; memcpy(&a, a_reg_data, sizeof(hl_f32));
                        hl_f32 b; memcpy(&b, b_reg_data, sizeof(hl_f32));

                        hl_f32 result;
                        switch (opcode->op) {
                            case OAdd: result = a + b; break;
                            case OSub: result = a - b; break;
                            case OMul: result = a * b; break;
                            case OSDiv: result = b ? a / b : 0; break;
                            case OSMod: result = b ? fmod(a, b) : 0; break;
                            default: HL_UNREACHABLE; break;
                        }
                        memcpy(dst_reg_data, &result, sizeof(hl_f32));
                        break;
                    }
                    case HF64: {
                        hl_f64 a; memcpy(&a, a_reg_data, sizeof(hl_f64));
                        hl_f64 b; memcpy(&b, b_reg_data, sizeof(hl_f64));

                        hl_f64 result;
                        switch (opcode->op) {
                            case OAdd: result = a + b; break;
                            case OSub: result = a - b; break;
                            case OMul: result = a * b; break;
                            case OSDiv: result = b ? a / b : 0; break;
                            case OSMod: result = b ? fmod(a, b) : 0; break;
                            default: HL_UNREACHABLE; break;
                        }
                        memcpy(dst_reg_data, &result, sizeof(hl_f64));
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case OShl:
            case OSShr:
            case OUShr:
            case OAnd:
            case OOr:
            case OXor: {
                int dst_reg_id = opcode->p1;
                int a_reg_id = opcode->p2;
                int b_reg_id = opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                hl_type *a_reg_type = fregs[a_reg_id];
                vbyte *a_reg_data = &reg_data[function_regs_offsets[a_reg_id]];
                hl_type *b_reg_type = fregs[b_reg_id];
                vbyte *b_reg_data = &reg_data[function_regs_offsets[b_reg_id]];

                if(!hl_type_is_int(dst_reg_type) || dst_reg_type->kind != a_reg_type->kind || a_reg_type->kind != b_reg_type->kind) {
                    HL_UNREACHABLE;
                }

                switch (dst_reg_type->kind) {
                    case HUI8:
                    case HUI16:
                    case HI32: {
                        hl_i32 a = 0; hl_copy_type_data(&a, a_reg_data, a_reg_type);
                        hl_i32 b = 0; hl_copy_type_data(&b, b_reg_data, b_reg_type);

                        hl_i32 result;
                        switch (opcode->op) {
                            case OShl: result = a << b; break;
                            case OSShr: result = a >> b; break;
                            case OUShr: result = (hl_i32)(((hl_u32)a) >> ((hl_u32)b)); break;
                            case OAnd: result = a & b; break;
                            case OOr: result = a | b; break;
                            case OXor: result = a ^ b; break;
                            default: HL_UNREACHABLE; break;
                        }
                        hl_copy_type_data(dst_reg_data, &result, dst_reg_type);
                        break;
                    }
                    case HI64: {
                        hl_i64 a; memcpy(&a, a_reg_data, sizeof(hl_i64));
                        hl_i64 b; memcpy(&b, b_reg_data, sizeof(hl_i64));

                        hl_i64 result;
                        switch (opcode->op) {
                            case OShl: result = a << b; break;
                            case OSShr: result = a >> b; break;
                            case OUShr: result = (hl_i64)(((hl_u64)a) >> ((hl_u64)b)); break;
                            case OAnd: result = a & b; break;
                            case OOr: result = a | b; break;
                            case OXor: result = a ^ b; break;
                            default: HL_UNREACHABLE; break;
                        }
                        memcpy(dst_reg_data, &a, sizeof(hl_i64));
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case ONeg: {
                int dst_reg_id = opcode->p1;
                int a_reg_id = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                hl_type *a_reg_type = fregs[a_reg_id];
                vbyte *a_reg_data = &reg_data[function_regs_offsets[a_reg_id]];

                if(!hl_type_is_number(dst_reg_type) || dst_reg_type->kind != a_reg_type->kind ) {
                    HL_UNREACHABLE;
                }

                switch (dst_reg_type->kind) {
                    case HUI8:
                    case HUI16:
                    case HI32: {
                        hl_i32 a = 0; hl_copy_type_data(&a, a_reg_data, a_reg_type);
                        a = -a;
                        hl_copy_type_data(dst_reg_data, &a, dst_reg_type);
                        break;
                    }
                    case HI64: {
                        hl_i64 a; memcpy(&a, a_reg_data, sizeof(hl_i64));
                        a = -a;
                        memcpy(dst_reg_data, &a, sizeof(hl_i64));
                        break;
                    }
                    case HF32: {
                        hl_f32 a; memcpy(&a, a_reg_data, sizeof(hl_f32));
                        a = -a;
                        memcpy(dst_reg_data, &a, sizeof(hl_f32));
                        break;
                    }
                    case HF64: {
                        hl_f64 a; memcpy(&a, a_reg_data, sizeof(hl_f64));
                        a = -a;
                        memcpy(dst_reg_data, &a, sizeof(hl_f64));
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case ONot: {
                int dst_reg_id = opcode->p1;
                int a_reg_id = opcode->p2;
                
                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                hl_type *a_reg_type = fregs[a_reg_id];
                vbyte *a_reg_data = &reg_data[function_regs_offsets[a_reg_id]];

                if(dst_reg_type->kind != HBOOL || dst_reg_type->kind != a_reg_type->kind) {
                    HL_UNREACHABLE;
                }

                hl_bool a; memcpy(&a, a_reg_data, sizeof(hl_bool));
                a = !a;
                hl_copy_type_data(dst_reg_data, &a, dst_reg_type);
                break;
            }
            case OIncr:
            case ODecr: {
                int dst_reg_id = opcode->p1;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                if(!hl_type_is_int(dst_reg_type)) {
                    HL_UNREACHABLE;
                }

                switch (dst_reg_type->kind) {
                    case HUI8:
                    case HUI16:
                    case HI32: {
                        hl_i32 dst = 0; hl_copy_type_data(&dst, dst_reg_data, dst_reg_type);
                        switch (opcode->op) {
                            case OIncr: ++dst; break;
                            case ODecr: --dst; break;
                            default: HL_UNREACHABLE; break;
                        }
                        hl_copy_type_data(dst_reg_data, &dst, dst_reg_type);
                        break;
                    }
                    case HI64: {
                        hl_i64 dst; memcpy(&dst, dst_reg_data, sizeof(hl_i64));
                        switch (opcode->op) {
                            case OIncr: ++dst; break;
                            case ODecr: --dst; break;
                            default: HL_UNREACHABLE; break;
                        }
                        memcpy(dst_reg_data, &dst, sizeof(hl_i64));
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case OCall0:
            case OCall1:
            case OCall2:
            case OCall3:
            case OCall4:
            case OCallN: {
                int dst_reg_id = opcode->p1;
                int findex = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                vdynamic callee_return = { .t = dst_reg_type };
                switch (opcode->op) {
                    case OCall0: {
                        hl_interp_raw_function_call(ctx, findex, 0, NULL, NULL, &callee_return);
                        break;
                    }
                    case OCall1: {
                        hl_type *args_types[1] = { fregs[opcode->p3] };
                        vbyte *args_data[1] = { &reg_data[function_regs_offsets[opcode->p3]] };

                        hl_interp_raw_function_call(ctx, findex, 1, args_data, args_types, &callee_return);
                        break;
                    }
                    case OCall2: {
                        hl_type *args_types[2] = { fregs[opcode->p3], fregs[(hl_usize)opcode->extra] };
                        vbyte *args_data[2] = { &reg_data[function_regs_offsets[opcode->p3]], &reg_data[function_regs_offsets[(hl_usize)opcode->extra]] };

                        hl_interp_raw_function_call(ctx, findex, 2, args_data, args_types, &callee_return);
                        break;
                    }
                    case OCall3: {
                        hl_type *args_types[3] = { fregs[opcode->p3], fregs[opcode->extra[0]], fregs[opcode->extra[1]] };
                        vbyte *args_data[3] = { 
                            &reg_data[function_regs_offsets[opcode->p3]], 
                            &reg_data[function_regs_offsets[opcode->extra[0]]], 
                            &reg_data[function_regs_offsets[opcode->extra[1]]] 
                        };

                        hl_interp_raw_function_call(ctx, findex, 3, args_data, args_types, &callee_return);
                        break;
                    }
                    case OCall4: {
                        hl_type *args_types[4] = { fregs[opcode->p3], fregs[opcode->extra[0]], fregs[opcode->extra[1]], fregs[opcode->extra[2]] };
                        vbyte *args_data[4] = { 
                            &reg_data[function_regs_offsets[opcode->p3]], 
                            &reg_data[function_regs_offsets[opcode->extra[0]]], 
                            &reg_data[function_regs_offsets[opcode->extra[1]]],
                            &reg_data[function_regs_offsets[opcode->extra[2]]]
                        };

                        hl_interp_raw_function_call(ctx, findex, 4, args_data, args_types, &callee_return);
                        break;
                    }
                    case OCallN: {
                        int args = opcode->p3;
                        hl_type **args_types = (hl_type**)malloc(args*sizeof(hl_type*)); // FIXME: Memory pool or smth?
                        vbyte **args_data = (vbyte**)malloc(args*sizeof(vbyte*));
                        
                        for (hl_usize i = 0; i < args; ++i) {
                            int reg_idx = opcode->extra[i];
                            args_types[i] = fregs[reg_idx];
                            args_data[i] = &reg_data[function_regs_offsets[reg_idx]];
                        }

                        hl_interp_raw_function_call(ctx, findex, args, args_data, args_types, &callee_return);
                        free(args_types);
                        free(args_data);
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                hl_copy_type_data(dst_reg_data, (vbyte*)&callee_return.v, dst_reg_type);
                break;
            }
            case OCallMethod:
            case OCallThis: {
                hl_bool is_reg0 = opcode->op == OCallThis;
                int dst_reg_id = opcode->p1;
                int src_reg_id = is_reg0 ? 0 : opcode->p2;
                int src_field_index = is_reg0 ? opcode->p2 : opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                __builtin_trap(); // TODO!
                break;
            }
            case OCallClosure: {
                int dst_reg_id = opcode->p1;
                int func_reg_id = opcode->p2;
                int args = opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                hl_type *func_reg_type = fregs[func_reg_id];
                vbyte *func_reg_data = &reg_data[function_regs_offsets[func_reg_id]];

                switch (func_reg_type->kind) {
                    case HDYN: {
                        __builtin_trap();
                    }
                    case HFUN: {
                        vclosure *closure;
                        memcpy(&closure, func_reg_data, sizeof(void*));
                        hl_function *closure_fun = (hl_function*)closure->fun; // TODO: This is not always an hl_function! It could be a native call
                        int closure_findex = closure_fun->findex;

                        hl_usize total_args = args + (closure->hasValue ? 1 : 0);
                        hl_usize current_arg = 0;
                        hl_type **args_types = (hl_type**)malloc(args*sizeof(hl_type*));
                        vbyte **args_data = (vbyte**)malloc(args*sizeof(vbyte*));
                        
                        if(closure->hasValue) {
                            args_types[current_arg] = &hlt_dyn;
                            args_data[current_arg] = (vbyte*)closure->value;
                            ++current_arg;
                        }

                        for (; current_arg < args; ++current_arg) {
                            int reg_idx = opcode->extra[current_arg];
                            args_types[current_arg] = fregs[reg_idx];
                            args_data[current_arg] = &reg_data[function_regs_offsets[reg_idx]];
                        }

                        vdynamic callee_return = { .t = dst_reg_type };
                        hl_interp_raw_function_call(ctx, closure_findex, total_args, args_data, args_types, &callee_return);
                        hl_copy_type_data(dst_reg_data, (vbyte*)&callee_return.v, dst_reg_type);

                        free(args_types);
                        free(args_data);
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case OInstanceClosure: {
                int dst_reg_id = opcode->p1;
                int function_index = opcode->p2;
                int obj_reg_id = opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                hl_type *obj_reg_type = fregs[obj_reg_id];
                vbyte *obj_reg_data = &reg_data[function_regs_offsets[obj_reg_id]];

                int real_function_index = m->functions_indexes[function_index];

                if(dst_reg_type->kind != HFUN) {
                    HL_UNREACHABLE;
                }

                // TODO: Not always an hl function, may be a native
                hl_function *function = &code->functions[real_function_index];
                hl_type *function_type = function->type;

                vobj* obj; memcpy(&obj, obj_reg_data, sizeof(void*));
                vclosure *closure = hl_alloc_closure_ptr(function_type, function, obj);
                memcpy(dst_reg_data, &closure, sizeof(void*));
                break;
            }
            case OGetGlobal: {
                int dst_reg_id = opcode->p1;
                int global_index = opcode->p2;
                hl_type *global_type = code->globals[global_index];
                
                int global_data_index = m->globals_indexes[global_index];
                vbyte *global_data = &m->globals_data[global_data_index];

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                if(dst_reg_type->kind != global_type->kind) {
                    HL_UNREACHABLE;
                }

                hl_copy_type_data(dst_reg_data, global_data, dst_reg_type);
                break;
            }
            case OSetGlobal: {
                int global_index = opcode->p1;
                int src_reg_id = opcode->p2;

                hl_type *global_type = code->globals[global_index];
                
                int global_data_index = m->globals_indexes[global_index];
                vbyte *global_data = &m->globals_data[global_data_index];

                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(global_type->kind != src_reg_type->kind) {
                    HL_UNREACHABLE;
                }

                hl_copy_type_data(global_data, src_reg_data, src_reg_type);
                break;
            }
            case OField:
            case OGetThis: {
                hl_bool is_reg0 = opcode->op == OGetThis;

                int dst_reg_id = opcode->p1;
                int src_reg_id = is_reg0 ? 0 : opcode->p2;
                int src_field_index = is_reg0 ? opcode->p2 : opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                switch (src_reg_type->kind) {
                    case HSTRUCT:
                    case HOBJ: {
                        hl_runtime_obj *rt = hl_get_obj_rt(src_reg_type);
                        hl_obj_field *src_field = hl_obj_field_fetch(src_reg_type, src_field_index);
                        hl_type *src_field_type = src_field->t;

                        if(dst_reg_type->kind == HSTRUCT) {
                            if(src_field_type->kind == HPACKED) {
                                __builtin_trap(); // TODO
                            }
                        }

                        if(dst_reg_type->kind != src_field_type->kind) {
                            HL_UNREACHABLE;
                        }

                        int field_offset = rt->fields_indexes[src_field_index];

                        vbyte *raw_obj;
                        memcpy(&raw_obj, src_reg_data, sizeof(void*));
                        hl_copy_type_data(dst_reg_data, &raw_obj[field_offset], dst_reg_type);
                        break;
                    }
                    case HVIRTUAL: {
                        vvirtual *v;
                        memcpy(&v, src_reg_data, sizeof(void*));
                        hl_obj_field *obj_field = &src_reg_type->virt->fields[src_field_index];
                        void** field = hl_vfields(v)[src_field_index];
                        
                        if(obj_field->t->kind != dst_reg_type->kind) {
                            HL_UNREACHABLE;
                        }

                        if(field) {
                            hl_copy_type_data(dst_reg_data, field, dst_reg_type);
                            break;
                        }

                        int hashed = obj_field->hashed_name;
                        vdynamic *vdyn = (vdynamic*)v;

                        switch (dst_reg_type->kind) {
                            case HBOOL:
                            case HUI8:
                            case HUI16:
                            case HI32: {
                                hl_i32 v = hl_dyn_geti(vdyn, hashed, dst_reg_type);
                                hl_copy_type_data(dst_reg_data, &v, src_reg_type);
                                break;
                            }
                            case HI64: {
                                hl_i64 v = hl_dyn_geti64(vdyn, hashed);
                                hl_copy_type_data(dst_reg_data, &v, src_reg_type);
                                break;
                            }
                            case HF32: {
                                hl_f32 v = hl_dyn_getf(vdyn, hashed);
                                hl_copy_type_data(dst_reg_data, &v, src_reg_type);
                                break;
                            }
                            case HF64: {
                                hl_f64 v = hl_dyn_getd(vdyn, hashed);
                                hl_copy_type_data(dst_reg_data, &v, src_reg_type);
                                break;
                            }
                            case HBYTES:
                            case HDYN:
                            case HFUN:
                            case HOBJ:
                            case HARRAY:
                            case HTYPE:
                            case HREF:
                            case HVIRTUAL:
                            case HDYNOBJ:
                            case HABSTRACT:
                            case HENUM:
                            case HNULL:
                            case HMETHOD:
                            case HSTRUCT: {
                                void *v = hl_dyn_getp(vdyn, hashed, dst_reg_type);
                                hl_copy_type_data(dst_reg_data, &v, src_reg_type);
                                break;
                            }
                            default: {
                                HL_UNREACHABLE;
                                break;
                            }
                        }
                        break;
                    }
                    case HDYN: {
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case OSetField:
            case OSetThis: {
                hl_bool is_reg0 = opcode->op == OSetThis;

                int dst_reg_id = is_reg0 ? 0 : opcode->p1;
                int dst_field_index = is_reg0 ? opcode->p1 : opcode->p2;
                int src_reg_id = is_reg0 ? opcode->p2 : opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                switch (dst_reg_type->kind) {
                    case HSTRUCT:
                    case HOBJ: {
                        hl_runtime_obj *rt = hl_get_obj_rt(dst_reg_type);
                        hl_obj_field *dst_field = hl_obj_field_fetch(dst_reg_type, dst_field_index);
                        hl_type *dst_field_type = dst_field->t;

                        if(src_reg_type->kind != dst_field_type->kind) {
                            HL_UNREACHABLE;
                        }

                        int field_offset = rt->fields_indexes[dst_field_index];
                        
                        vbyte *raw_obj;
                        memcpy(&raw_obj, dst_reg_data, sizeof(void*));
                        hl_copy_type_data(&raw_obj[field_offset], src_reg_data, src_reg_type);
                        break;
                    }
                    case HVIRTUAL: {
                        vvirtual *v; memcpy(&v, dst_reg_data, sizeof(void*));
                        hl_obj_field *obj_field = &src_reg_type->virt->fields[dst_field_index];
                        void** field = hl_vfields(v)[dst_field_index];
                        
                        if(obj_field->t->kind != src_reg_type->kind) {
                            HL_UNREACHABLE;
                        }

                        if(field) {
                            hl_copy_type_data(field, src_reg_data, src_reg_type);
                            break;
                        }

                        int hashed = obj_field->hashed_name;
                        vdynamic *vdyn = (vdynamic*)v;

                        switch (dst_reg_type->kind) {
                            case HBOOL:
                            case HUI8:
                            case HUI16:
                            case HI32: {
                                hl_i32 v = 0; hl_copy_type_data(&v, src_reg_data, src_reg_type);
                                hl_dyn_seti(vdyn, hashed, src_reg_type, v);
                                break;
                            }
                            case HI64: {
                                hl_i64 v; memcpy(&v, src_reg_data, sizeof(hl_i64));
                                hl_dyn_seti64(vdyn, hashed, v);
                                break;
                            }
                            case HF32: {
                                hl_f32 v; memcpy(&v, src_reg_data, sizeof(hl_f32));
                                hl_dyn_setf(vdyn, hashed, v);
                                break;
                            }
                            case HF64: {
                                hl_f64 v; memcpy(&v, src_reg_data, sizeof(hl_f64));
                                hl_dyn_setd(vdyn, hashed, v);
                                break;
                            }
                            case HBYTES:
                            case HDYN:
                            case HFUN:
                            case HOBJ:
                            case HARRAY:
                            case HTYPE:
                            case HREF:
                            case HVIRTUAL:
                            case HDYNOBJ:
                            case HABSTRACT:
                            case HENUM:
                            case HNULL:
                            case HMETHOD:
                            case HSTRUCT: {
                                void *v; memcpy(&v, src_reg_data, sizeof(void*));
                                hl_dyn_setp(vdyn, hashed, src_reg_type, v);
                                break;
                            }
                            default: {
                                HL_UNREACHABLE;
                                break;
                            }
                        }
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case ODynSet: {
                int dst_reg_id = opcode->p1;
                int field_name_string_index = opcode->p2;
                int src_reg_id = opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(dst_reg_type->kind != HDYNOBJ) {
                    HL_UNREACHABLE;
                }
                
                vdynamic *dyn; memcpy(&dyn, dst_reg_data, sizeof(void*));

                const uchar *field_name = hl_get_ustring(code, field_name_string_index);
                int field_hash = hl_hash_gen(field_name, true);

                switch (src_reg_type->kind) {
                    case HBOOL:
                    case HUI8:
                    case HUI16:
                    case HI32: {
                        hl_i32 v = 0; hl_copy_type_data(&v, src_reg_data, src_reg_type);
                        hl_dyn_seti(dyn, field_hash, src_reg_type, v);
                        break;
                    }
                    case HI64: {
                        hl_i64 v; memcpy(&v, src_reg_data, sizeof(hl_i64));
                        hl_dyn_seti64(dyn, field_hash, v);
                        break;
                    }
                    case HF32: {
                        hl_f32 v; memcpy(&v, src_reg_data, sizeof(hl_f32));
                        hl_dyn_setf(dyn, field_hash, v);
                        break;
                    }
                    case HF64: {
                        hl_f64 v; memcpy(&v, src_reg_data, sizeof(hl_f64));
                        hl_dyn_setd(dyn, field_hash, v);
                        break;
                    }
                    case HBYTES:
                    case HDYN:
                    case HFUN:
                    case HOBJ:
                    case HARRAY:
                    case HTYPE:
                    case HREF:
                    case HVIRTUAL:
                    case HDYNOBJ:
                    case HABSTRACT:
                    case HENUM:
                    case HNULL:
                    case HMETHOD:
                    case HSTRUCT: {
                        void *v; memcpy(&v, src_reg_data, sizeof(void*));
                        hl_dyn_setp(dyn, field_hash, src_reg_type, v);
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case OJFalse:
            case OJTrue: {
                hl_bool is_negated = opcode->op == OJFalse;
                int src_reg_id = opcode->p1;
                int offset = opcode->p2;

                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(src_reg_type->kind != HBOOL) {
                    HL_UNREACHABLE;
                }

                hl_bool raw_data; memcpy(&raw_data, src_reg_data, sizeof(hl_bool));
                
                if((is_negated && !raw_data) || (!is_negated && raw_data)) {
                    current_op += offset;
                }
                break;
            }
            case OJNotNull:
            case OJNull: {
                hl_bool is_negated = opcode->op == OJNull;
                int src_reg_id = opcode->p1;
                int offset = opcode->p2;

                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(!hl_type_can_be_null(src_reg_type)) {
                    HL_UNREACHABLE;
                }

                vbyte *raw_data; memcpy(&raw_data, src_reg_data, sizeof(vbyte*));
                
                if((is_negated && !raw_data) || (!is_negated && raw_data)) {
                    current_op += offset;
                }
                break;
            }
            case OJSLt:
            case OJSGte:
            case OJSLte:
            case OJSGt:
            case OJULt:
            case OJUGte: {
                int a_reg_id = opcode->p1;
                int b_reg_id = opcode->p2;
                int offset = opcode->p3;

                hl_type *a_reg_type = fregs[a_reg_id];
                vbyte *a_reg_data = &reg_data[function_regs_offsets[a_reg_id]];
                hl_type *b_reg_type = fregs[b_reg_id];
                vbyte *b_reg_data = &reg_data[function_regs_offsets[b_reg_id]];

                if(!hl_type_is_number(a_reg_type) || a_reg_type->kind != b_reg_type->kind) {
                    HL_UNREACHABLE;
                }

                hl_bool result;
                switch (a_reg_type->kind) {
                    case HUI8:
                    case HUI16:
                    case HI32: {
                        hl_i32 a = 0; hl_copy_type_data(&a, a_reg_data, a_reg_type);
                        hl_i32 b = 0; hl_copy_type_data(&b, b_reg_data, b_reg_type);

                        switch (opcode->op) {
                            case OJSLt: result = a < b; break;
                            case OJSGte: result = a >= b; break;
                            case OJSLte: result = a <= b; break;
                            case OJSGt: result = a > b; break;
                            case OJULt: result = ((hl_u32)a) < ((hl_u32)b); break;
                            case OJUGte: result = ((hl_u32)a) >= ((hl_u32)b); break;
                            default: HL_UNREACHABLE; break;
                        }
                        break;
                    }
                    case HI64: {
                        hl_i64 a; memcpy(&a, a_reg_data, sizeof(hl_i64));
                        hl_i64 b; memcpy(&b, b_reg_data, sizeof(hl_i64));

                        switch (opcode->op) {
                            case OJSLt: result = a < b; break;
                            case OJSGte: result = a >= b; break;
                            case OJSLte: result = a <= b; break;
                            case OJSGt: result = a > b; break;
                            case OJULt: result = ((hl_u64)a) < ((hl_u64)b); break;
                            case OJUGte: result = ((hl_u64)a) >= ((hl_u64)b); break;
                            default: HL_UNREACHABLE; break;
                        }
                        break;
                    }
                    case HF32: {
                        hl_f32 a; memcpy(&a, a_reg_data, sizeof(hl_f32));
                        hl_f32 b; memcpy(&b, b_reg_data, sizeof(hl_f32));

                        switch (opcode->op) {
                            case OJSLt: result = a < b; break;
                            case OJSGte: result = a >= b; break;
                            case OJSLte: result = a <= b; break;
                            case OJSGt: result = a > b; break;
                            default: HL_UNREACHABLE; break;
                        }
                        break;
                    }
                    case HF64: {
                        hl_f64 a; memcpy(&a, a_reg_data, sizeof(hl_f64));
                        hl_f64 b; memcpy(&b, b_reg_data, sizeof(hl_f64));

                        switch (opcode->op) {
                            case OJSLt: result = a < b; break;
                            case OJSGte: result = a >= b; break;
                            case OJSLte: result = a <= b; break;
                            case OJSGt: result = a > b; break;
                            default: HL_UNREACHABLE; break;
                        }
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }

                if(result) {
                    current_op += offset;
                }
                break;
            }
            case OJNotLt:
            case OJNotGte: {
                __builtin_trap(); // What are these for, signed, unsigned, what??
            }
            case OJNotEq:
            case OJEq: {
                hl_bool is_negated = opcode->op == OJNotEq;

                int a_reg_id = opcode->p1;
                int b_reg_id = opcode->p2;
                int offset = opcode->p3;

                hl_type *a_reg_type = fregs[a_reg_id];
                vbyte *a_reg_data = &reg_data[function_regs_offsets[a_reg_id]];
                hl_type *b_reg_type = fregs[b_reg_id];
                vbyte *b_reg_data = &reg_data[function_regs_offsets[b_reg_id]];

                hl_bool result;
                if(a_reg_type->kind == HDYN || b_reg_type->kind == HDYN || a_reg_type->kind == HFUN || b_reg_type->kind == HFUN) {
                    __builtin_trap(); // TODO DYN COMPARE
                } else {
                    if(a_reg_type->kind != b_reg_type->kind) {
                        HL_UNREACHABLE;
                    }

                    switch (a_reg_type->kind) {
                        case HBOOL:
                        case HUI8:
                        case HUI16:
                        case HI32: {
                            hl_i32 a = 0; hl_copy_type_data(&a, a_reg_data, a_reg_type);
                            hl_i32 b = 0; hl_copy_type_data(&b, b_reg_data, b_reg_type);
                            result = a == b;
                            break;
                            break;
                        }
                        case HI64: {
                            hl_i64 a; memcpy(&a, a_reg_data, sizeof(hl_i64));
                            hl_i64 b; memcpy(&b, b_reg_data, sizeof(hl_i64));
                            result = a == b;
                            break;
                        }
                        case HF32: {
                            hl_f32 a; memcpy(&a, a_reg_data, sizeof(hl_f32));
                            hl_f32 b; memcpy(&b, b_reg_data, sizeof(hl_f32));
                            result = a == b;
                            break;
                        }
                        case HF64: {
                            hl_f64 a; memcpy(&a, a_reg_data, sizeof(hl_f64));
                            hl_f64 b; memcpy(&b, b_reg_data, sizeof(hl_f64));
                            result = a == b;
                            break;
                        }
                        case HBYTES:
                        case HDYN:
                        case HFUN:
                        case HOBJ:
                        case HARRAY:
                        case HTYPE:
                        case HREF:
                        case HVIRTUAL:
                        case HDYNOBJ:
                        case HABSTRACT:
                        case HENUM:
                        case HNULL:
                        case HMETHOD:
                        case HSTRUCT: {
                            __builtin_trap(); // TODO
                            break;
                        }
                        default: {
                            HL_UNREACHABLE;
                            break;
                        }
                    }
                }

                if((is_negated && !result) || (!is_negated && result)) {
                    current_op += offset;
                }
                break;
            }
            case OJAlways: {
                int offset = opcode->p1;
                current_op += offset;
                break;
            }
            case OToDyn: {
                int dst_reg_id = opcode->p1;
                int src_reg_id = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if (dst_reg_type->kind != HDYN && dst_reg_type->kind != HNULL) {
                    HL_UNREACHABLE;
                }

                vdynamic *dyn;
                switch (src_reg_type->kind) {
                    case HBOOL: {
                        hl_bool bool_value; memcpy(&bool_value, src_reg_data, sizeof(hl_bool));
                        dyn = hl_alloc_dynbool(bool_value);
                        break;
                    }
                    case HUI8:
                    case HUI16:
                    case HI32:
                    case HI64:
                    case HF32:
                    case HF64:
                    case HBYTES:
                    case HDYN:
                    case HFUN:
                    case HOBJ:
                    case HARRAY:
                    case HTYPE:
                    case HREF:
                    case HVIRTUAL:
                    case HDYNOBJ:
                    case HABSTRACT:
                    case HENUM:
                    case HNULL:
                    case HMETHOD:
                    case HSTRUCT: {
                        if(hl_type_can_be_null(src_reg_type)) {
                            void *ptr; memcpy(&ptr, src_reg_data, sizeof(void*));

                            if(!ptr) {
                                dyn = NULL;
                                break;
                            }
                        }

                        dyn = hl_alloc_dynamic(src_reg_type);
                        hl_copy_type_data(&dyn->v, src_reg_data, src_reg_type);
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }

                memcpy(dst_reg_data, &dyn, sizeof(void*));
                break;
            }
            case OToSFloat:
            case OToUFloat: {
                hl_bool unsig = opcode->op == OToUFloat;
                
                int dst_reg_id = opcode->p1;
                int src_reg_id = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(!hl_type_is_float(dst_reg_type) || !hl_type_is_number(src_reg_type)) {
                    HL_UNREACHABLE;
                }

                if(dst_reg_type->kind == src_reg_type->kind) {
                    hl_copy_type_data(dst_reg_data, src_reg_data, dst_reg_type);
                    break;
                }
            
                switch (dst_reg_type->kind) {
                    case HF32: {
                        hl_f32 result;

                        switch (src_reg_type->kind) {
                            case HUI8:
                            case HUI16:
                            case HI32: {
                                hl_i32 src = 0; hl_copy_type_data(&src, src_reg_data, src_reg_type);
                                result = unsig ? (hl_f32)src : ((hl_f32)(hl_u32)src);
                                break;
                            }
                            case HI64: {
                                hl_i64 src; memcpy(&src, src_reg_data, sizeof(hl_i64));
                                result = unsig ? (hl_f32)src : ((hl_f32)(hl_u64)src);
                                break;
                            }
                            case HF64: {
                                if(unsig) {
                                    HL_UNREACHABLE;
                                }

                                hl_f64 src; memcpy(&src, src_reg_data, sizeof(hl_f64));
                                result = (hl_f32)src;
                                break;
                            }
                            default: {
                                HL_UNREACHABLE;
                                break;
                            }
                        }

                        memcpy(dst_reg_data, &result, sizeof(hl_f32));
                        break;
                    }
                    case HF64: {
                        hl_f64 result;

                        switch (src_reg_type->kind) {
                            case HUI8:
                            case HUI16:
                            case HI32: {
                                hl_i32 src = 0; hl_copy_type_data(&src, src_reg_data, src_reg_type);
                                result = unsig ? (hl_f64)src : ((hl_f64)(hl_u32)src);
                                break;
                            }
                            case HI64: {
                                hl_i64 src; memcpy(&src, src_reg_data, sizeof(hl_i64));
                                result = unsig ? (hl_f64)src : ((hl_f64)(hl_u64)src);
                                break;
                            }
                            case HF32: {
                                if(unsig) {
                                    HL_UNREACHABLE;
                                }

                                hl_f32 src; memcpy(&src, src_reg_data, sizeof(hl_f32));
                                result = (hl_f64)src;
                                break;
                            }
                            default: {
                                HL_UNREACHABLE;
                                break;
                            }
                        }

                        memcpy(dst_reg_data, &result, sizeof(hl_f64));
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case OSafeCast: {
                int dst_reg_id = opcode->p1;
                int src_reg_id = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                switch (dst_reg_type->kind) {
                    case HBOOL:
                    case HUI8:
                    case HUI16:
                    case HI32: {
                        hl_i32 cast = hl_dyn_casti(src_reg_data, src_reg_type, dst_reg_type);
                        hl_copy_type_data(dst_reg_data, &cast, dst_reg_type);
                        break;
                    }
                    case HI64: {
                        hl_i64 cast = hl_dyn_casti64(src_reg_data, src_reg_type);
                        memcpy(dst_reg_data, &cast, sizeof(hl_i64));
                        break;
                    }
                    case HF32: {
                        hl_f32 cast = hl_dyn_castf(src_reg_data, src_reg_type);
                        memcpy(dst_reg_data, &cast, sizeof(hl_f32));
                        break;
                    }
                    case HF64: {
                        hl_f64 cast = hl_dyn_castd(src_reg_data, src_reg_type);
                        memcpy(dst_reg_data, &cast, sizeof(hl_f64));
                        break;
                    }
                    case HBYTES:
                    case HDYN:
                    case HFUN:
                    case HOBJ:
                    case HARRAY:
                    case HTYPE:
                    case HREF:
                    case HVIRTUAL:
                    case HDYNOBJ:
                    case HABSTRACT:
                    case HENUM:
                    case HNULL:
                    case HMETHOD:
                    case HSTRUCT: {
                        vdynamic *cast = hl_dyn_castp(src_reg_data, src_reg_type, dst_reg_type);
                        memcpy(dst_reg_data, &cast, sizeof(void*));
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case OToVirtual: {
                int dst_reg_id = opcode->p1;
                int src_reg_id = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];
                
                if(!hl_type_can_be_null(src_reg_type) || !hl_type_can_be_null(dst_reg_type)) {
                    HL_UNREACHABLE;
                }

                if(src_reg_type->kind == HOBJ) {
                    hl_get_obj_rt(src_reg_type); // ensure it is initialized
                }

                vdynamic *v; memcpy(&v, src_reg_data, sizeof(void*));

                vvirtual *virtual = hl_to_virtual(dst_reg_type, v);
                memcpy(dst_reg_data, &virtual, sizeof(void*));
                break;
            }
            case OLabel: {
                // NOP
                break;
            }
            case ORet: {
                int src_reg_id = opcode->p1;

                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(ret_type->kind != src_reg_type->kind) {
                    HL_UNREACHABLE;
                }

                hl_copy_type_data(&ret->v, src_reg_data, src_reg_type);
                goto function_interp_end;
            }
            case OSwitch: {
                int src_reg_id = opcode->p1;
                hl_usize offsets_length = opcode->p2;
                int end = opcode->p3;
                int *offsets = opcode->extra;

                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(!hl_type_is_int(src_reg_type)) {
                    HL_UNREACHABLE;
                }

                hl_usize index; hl_copy_type_data_until(&index, src_reg_data, src_reg_type, sizeof(hl_usize));

                if(index < offsets_length) {
                    current_op += offsets[index];
                    break;
                }

                break;
            }
            case ONullCheck: {
                int check_reg_id = opcode->p1;

                hl_type *check_reg_type = fregs[check_reg_id];
                vbyte *check_reg_data = &reg_data[function_regs_offsets[check_reg_id]];

                if(!hl_type_can_be_null(check_reg_type)) {
                    HL_UNREACHABLE;
                }

                void *data; memcpy(&data, check_reg_data, sizeof(void*));

                if(!data) {
                    hl_interp_error("NULL CHECK FAILED!"); // Throw instead of aborting
                }

                break;
            }
            case OGetArray: {
                int dst_reg_id = opcode->p1;
                int src_reg_id = opcode->p2;
                int src_index_reg_id = opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                hl_type *src_index_reg_type = fregs[src_index_reg_id];
                vbyte *src_index_reg_data = &reg_data[function_regs_offsets[src_index_reg_id]];

                if(!hl_type_is_int(src_index_reg_type)) {
                    HL_UNREACHABLE;
                }

                hl_usize index = 0; hl_copy_type_data_until(&index, src_index_reg_data, src_index_reg_type, sizeof(hl_usize));

                switch (src_reg_type->kind) {
                    case HABSTRACT: {
                        __builtin_trap(); // TODO
                        break;
                    }
                    case HARRAY: {
                        varray *array; memcpy(&array, src_reg_data, sizeof(void*));

                        hl_type *array_type = array->at;

                        if(array_type->kind != dst_reg_type->kind) {
                            HL_UNREACHABLE;
                        }

                        vbyte *array_data = &((vbyte*)array)[sizeof(varray)];
                        hl_copy_type_data(dst_reg_data, &array_data[index*hl_ntype_size(array_type)], array_type);
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case OSetI8:
            case OSetI16:
            case OSetMem: {
                int dst_reg_id = opcode->p1;
                int dst_index_reg_id = opcode->p2;
                int src_reg_id = opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                hl_type *dst_offset_reg_type = fregs[dst_index_reg_id];
                vbyte *dst_offset_reg_data = &reg_data[function_regs_offsets[dst_index_reg_id]];

                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(dst_reg_type->kind != HBYTES || !hl_type_is_int(dst_offset_reg_type) || !hl_type_is_number(src_reg_type) || src_reg_type->kind == HI64) {
                    HL_UNREACHABLE;
                }

                vbyte* bytes; memcpy(&bytes, dst_reg_data, sizeof(void*));
                hl_usize offset = 0; hl_copy_type_data_until(&offset, dst_offset_reg_data, dst_offset_reg_type, sizeof(hl_usize));

                switch (opcode->op) {
                    case OSetI8:
                    case OSetI16: {
                        if(!hl_type_is_int(src_reg_type)) {
                            HL_UNREACHABLE;
                        }

                        hl_i32 v = 0; hl_copy_type_data(&v, src_reg_data, src_reg_type); 
                        
                        if(opcode->op == OSetI8) {
                            memcpy(&bytes[offset], &v, sizeof(hl_ui8));
                            break;
                        }

                        memcpy(&bytes[offset], &v, sizeof(hl_ui16));
                        break;
                    }
                    case OSetMem: {
                        if(!hl_type_is_number(src_reg_type)) {
                            HL_UNREACHABLE;
                        }

                        hl_copy_type_data(&bytes[offset], src_reg_data, src_reg_type);
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case OSetArray: {
                int dst_reg_id = opcode->p1;
                int dst_index_reg_id = opcode->p2;
                int src_reg_id = opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                hl_type *dst_index_reg_type = fregs[dst_index_reg_id];
                vbyte *dst_index_reg_data = &reg_data[function_regs_offsets[dst_index_reg_id]];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(!hl_type_is_int(dst_index_reg_type)) {
                    HL_UNREACHABLE;
                }

                hl_usize index = 0; hl_copy_type_data_until(&index, dst_index_reg_data, dst_index_reg_type, sizeof(hl_usize));

                switch (dst_reg_type->kind) {
                    case HABSTRACT: {
                        __builtin_trap(); // TODO
                        break;
                    }
                    case HARRAY: {
                        varray *array; memcpy(&array, dst_reg_data, sizeof(void*));
                        hl_type *array_type = array->at;

                        if(array_type->kind != src_reg_type->kind) {
                            HL_UNREACHABLE;
                        }

                        vbyte *array_data = &((vbyte*)array)[sizeof(varray)];
                        hl_copy_type_data(&array_data[index*hl_ntype_size(array_type)], src_reg_data, array_type);
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }
                break;
            }
            case ONew: {
                int dst_reg_id = opcode->p1;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                void *obj;
                switch (dst_reg_type->kind) {
                    case HOBJ:
                    case HSTRUCT: obj = hl_alloc_obj(dst_reg_type); break;
                    case HDYNOBJ: obj = hl_alloc_dynobj(); break;
                    case HVIRTUAL: obj = hl_alloc_virtual(dst_reg_type); break;
                    default: HL_UNREACHABLE; break;
                }

                memcpy(dst_reg_data, &obj, sizeof(void*));
                break;
            }
            case OArraySize: {
                int dst_reg = opcode->p1;
                int array_reg = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg]];
                hl_type *array_reg_type = fregs[array_reg];
                vbyte *array_reg_data = &reg_data[function_regs_offsets[array_reg]];

                if(array_reg_type->kind != HARRAY || !hl_type_is_int(dst_reg_type)) {
                    HL_UNREACHABLE;
                }

                varray *array; memcpy(&array, array_reg_data, sizeof(void*));
                hl_usize array_size = array->size;
                hl_copy_type_data(dst_reg_data, &array_size, dst_reg_type);
                break;
            }
            case OType: {
                int dst_reg = opcode->p1;
                int type_index = opcode->p2;
                hl_type *dst_reg_type = fregs[dst_reg];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg]];

                hl_type *code_type = &code->types[type_index];

                if(dst_reg_type->kind != HTYPE) {
                    HL_UNREACHABLE;
                }

                memcpy(dst_reg_data, &code_type, sizeof(hl_type*));
                break;
            }
            case ORef: { // TODO: Check if ref is compatible with type
                int dst_reg_id = opcode->p1;
                int src_reg_id = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(dst_reg_type->kind != HREF) {
                    HL_UNREACHABLE;
                }

                memcpy(dst_reg_data, &src_reg_data, sizeof(hl_ref));
                break;
            }
            case OUnref: {
                int dst_reg_id = opcode->p1;
                int src_reg_id = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(src_reg_type->kind != HREF) {
                    HL_UNREACHABLE;
                }

                hl_ref ref; memcpy(&ref, src_reg_data, sizeof(hl_ref));
                hl_copy_type_data(dst_reg_data, ref, dst_reg_type);
                break;
            }
            case OEnumAlloc: {
                int dst_reg_id = opcode->p1;
                int constructor_index = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                if(dst_reg_type->kind != HENUM) {
                    HL_UNREACHABLE;
                }

                venum *en = hl_alloc_enum(dst_reg_type, constructor_index);
                memcpy(dst_reg_data, &en, sizeof(void*));
                break;
            }
            case OEnumIndex: {
                int dst_reg_id = opcode->p1;
                int src_reg_id = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(src_reg_type->kind != HENUM || !hl_type_is_int(dst_reg_type) || dst_reg_type->kind == HI64) {
                    HL_UNREACHABLE;
                }

                venum *enm; memcpy(&enm, src_reg_data, sizeof(void*));
                hl_i32 index = enm->index;
                hl_copy_type_data(dst_reg_data, &index, dst_reg_type);
                break;
            }
            case OEnumField: {
                int dst_reg_id = opcode->p1;
                int src_reg_id = opcode->p2;
                int construct_id = opcode->p3;
                int field_id = (int)(hl_usize)opcode->extra;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(src_reg_type->kind != HENUM) {
                    HL_UNREACHABLE;
                }

                hl_type_enum *src_enum_type = src_reg_type->tenum;
                hl_enum_construct *src_enum_construct = &src_enum_type->constructs[construct_id];
                hl_type *field_type = src_enum_construct->params[field_id];
                hl_usize field_offset = src_enum_construct->offsets[field_id];

                if(field_type->kind != dst_reg_type->kind) {
                    HL_UNREACHABLE;
                }

                vbyte *enum_data; memcpy(&enum_data, src_reg_data, sizeof(void*));
                hl_copy_type_data(dst_reg_data, &enum_data[field_offset], dst_reg_type);
                break;
            }
            case OSetEnumField: {
                int dst_reg_id = opcode->p1;
                int field_id = opcode->p2;
                int src_reg_id = opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(dst_reg_type->kind != HENUM) {
                    HL_UNREACHABLE;
                }

                hl_type_enum *dst_enum_type = dst_reg_type->tenum;
                hl_enum_construct *dst_enum_construct = &dst_enum_type->constructs[0];
                hl_type *field_type = dst_enum_construct->params[field_id];
                hl_usize field_offset = dst_enum_construct->offsets[field_id];

                if(field_type->kind != src_reg_type->kind) {
                    HL_UNREACHABLE;
                }
                
                vbyte *enum_data; memcpy(&enum_data, dst_reg_data, sizeof(void*)); 
                hl_copy_type_data(&enum_data[field_offset], src_reg_data, src_reg_type);
                break;
            }
            default: {
                hl_interp_error(hl_op_name(opcode->op));
                HL_UNREACHABLE;
                break;
            }
        } // switch
        
        ++current_op;
    } // while
    
    HL_UNREACHABLE;
    function_interp_end:
    for (int i = 0; i < f->nregs; ++i) {
        if(hl_is_ptr(fregs[i]))
            hl_remove_root(&reg_data[function_regs_offsets[i]]);
    }
    return NULL;
}

void *hl_interp_callback_c2hl(void *fun, hl_type *t, void **args, vdynamic *out) {
    interp_ctx *ctx = global_ctx;
    hl_function* f = (hl_function*)fun;
    hl_type_fun *fun_type = t->fun;
    int findex = f->findex;
    
    hl_usize nargs = fun_type->nargs;
    hl_type **args_types = fun_type->args;

    out->t = fun_type->ret; // out type is not initialized
    hl_interp_raw_function_call(ctx, findex, nargs, (vbyte**)args, args_types, out); // TODO: args!
    return out;
}

void hl_interp_init(interp_ctx *ctx, hl_module *m) {
    ctx->m = m;
    m->interp.ctx = global_ctx = ctx;
    hl_alloc *alloc = &ctx->alloc;
    hl_code *module_code = m->code;
    int total_functions = module_code->nfunctions + module_code->nnatives;
    int **function_regs_offsets = (int**)hl_malloc(alloc, sizeof(int*) * total_functions);

    for (int i = 0; i < module_code->nfunctions; ++i) {
        hl_function *f = &module_code->functions[i];
        hl_type_fun *f_type = f->type->fun;

        int offsets_size = f->nregs + 1;
        int *offsets = (int*)hl_malloc(alloc, offsets_size * sizeof(int));

        int currentOffset = 0;
        for (int j = 0; j < f->nregs; ++j) {
            offsets[j] = currentOffset;

            hl_type *reg_type = f->regs[j];
            currentOffset += hl_pad_size(currentOffset, reg_type);
            currentOffset += hl_ntype_size(reg_type);
        }
        offsets[f->nregs] = currentOffset;

        function_regs_offsets[i] = offsets;
    }

    ctx->fregs_offsets = function_regs_offsets;
    hl_setup_callbacks(hl_interp_callback_c2hl, NULL);
}

interp_ctx *hl_interp_alloc() {
    interp_ctx *ctx = (interp_ctx*)malloc(sizeof(interp_ctx));
    memset(ctx, 0, sizeof(interp_ctx));
    hl_alloc_init(&ctx->alloc);
    return ctx;
}

void hl_interp_free(interp_ctx *ctx, hl_bool can_reset) {
    hl_free(&ctx->alloc);
    ctx->fregs_offsets = NULL;
    ctx->fargs_sizes = NULL;

    if(!can_reset)
        free(ctx);
}