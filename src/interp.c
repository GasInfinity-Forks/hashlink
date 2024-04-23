#include <hlmodule.h>
#include <ffi.h>

struct interp_ctx {
    hl_alloc alloc;
    hl_module *m;
    int **fregs_offsets;
    int *fargs_sizes;
};

#define hl_interp_error(msg) hl_fatal(msg);

static interp_ctx *global_ctx = NULL;

static inline void hl_copy_type_data(void *restrict dst, void *restrict src, hl_type *type) {
    switch (type->kind) {
        case HUI8:  memcpy(dst, src, sizeof(hl_ui8));
            break;
        case HUI16: memcpy(dst, src, sizeof(hl_ui16));
            break;
        case HI32:  memcpy(dst, src, sizeof(hl_i32));
            break;
        case HI64:  memcpy(dst, src, sizeof(hl_i64));
            break;
        case HF32:  memcpy(dst, src, sizeof(hl_f32));
            break;
        case HF64:  memcpy(dst, src, sizeof(hl_f64));
            break;
        case HBOOL: memcpy(dst, src, sizeof(hl_bool));
            break;
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
            memcpy(dst, src, sizeof(void*));
            break;
        }
        case HVOID: break;
        case HPACKED:
        default: {
            HL_UNREACHABLE;
        }
    }
}

static inline size_t hl_ntype_size(hl_type *type) {
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
        default: {
            HL_UNREACHABLE;
            return 0;
        }
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
void hl_interp_raw_function_call(interp_ctx *ctx, int findex, size_t nargs, vbyte **args, hl_type **args_types, vdynamic *ret);
void hl_interp_native_ffi_call(interp_ctx *ctx, void (*fptr)(), size_t nargs, vbyte **args, hl_type **args_types, vdynamic *ret);
void hl_interp_raw_bytecode_function_call(interp_ctx *ctx, hl_function *callee, size_t nargs, vbyte **args, hl_type **args_types, vdynamic *ret);

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
        default: {
            HL_UNREACHABLE;
            return 0;
        }
    }
}

void hl_interp_raw_function_call(interp_ctx *ctx, int findex, size_t nargs, vbyte **args, hl_type **args_types, vdynamic *ret) {
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

void hl_interp_native_ffi_call(interp_ctx *ctx, void (*fptr)(), size_t nargs, vbyte **args, hl_type **args_types, vdynamic *ret) {
    hl_module *m = ctx->m;
    hl_code *code = m->code;
    hl_type *ret_type = ret->t;

    ffi_cif cif;
    ffi_type **ffi_args_type = (ffi_type**)malloc(nargs * sizeof(ffi_type*)); // FIXME: mem pool?

    for (size_t i = 0; i < nargs; ++i) {
        ffi_args_type[i] = hl_type_to_ffi(args_types[i]);
    }

    if(ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, hl_type_to_ffi(ret_type), ffi_args_type) != FFI_OK) {
        HL_UNREACHABLE;
    }

    ffi_call(&cif, fptr, &ret->v, (void**)args);
    free(ffi_args_type);
}

void hl_interp_raw_bytecode_function_call(interp_ctx *ctx, hl_function *callee, size_t nargs, vbyte **args, hl_type **args_types, vdynamic *ret) {
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

        size_t regs_arg_offset = 0;
        for (size_t i = 0; i < nargs; ++i) {
            vbyte* arg_data = args[i];
            hl_type* arg_type = args_types[i];

            hl_copy_type_data(&regs_data[regs_arg_offset], arg_data, arg_type);
            regs_arg_offset += hl_ntype_size(arg_type);
        }

        // TODO: Write 0xCC in debug
        memset(&regs_data[regs_arg_offset], 0x00, total_regs_size-regs_arg_offset);
    }

    hl_interp_run(ctx, callee, regs_data, ret);

    if(regs_data)
        free(regs_data);
}

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

    size_t bytecode_size = f->nops;
    hl_opcode *bytecode = f->ops;

    size_t current_op = 0;
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

                memset(dst_reg_data, bool_value, sizeof(hl_bool));
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
            case OAdd: {
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
                    case HUI16:
                    case HI32:
                    case HI64: {
                        hl_intmax a = 0;
                        hl_intmax b = 0;
                        hl_copy_type_data(&a, a_reg_data, a_reg_type);
                        hl_copy_type_data(&b, b_reg_data, b_reg_type);

                        hl_intmax result = a + b;
                        hl_copy_type_data(dst_reg_data, &result, dst_reg_type);
                        break;
                    }
                    case HF32:
                    case HF64: {
                        __builtin_trap(); // TODO!
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }

                break;
            }
            case OMul: {
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
                    case HUI16:
                    case HI32:
                    case HI64: {
                        hl_intmax a = 0;
                        hl_intmax b = 0;
                        hl_copy_type_data(&a, a_reg_data, a_reg_type);
                        hl_copy_type_data(&b, b_reg_data, b_reg_type);

                        hl_intmax result = a * b;
                        hl_copy_type_data(dst_reg_data, &result, dst_reg_type);
                        break;
                    }
                    case HF32:
                    case HF64: {
                        __builtin_trap(); // TODO!
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }

                break;
            }
            case OShl: {
                int dst_reg_id = opcode->p1;
                int a_reg_id = opcode->p2;
                int b_reg_id = opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                hl_type *a_reg_type = fregs[a_reg_id];
                vbyte *a_reg_data = &reg_data[function_regs_offsets[a_reg_id]];

                hl_type *b_reg_type = fregs[b_reg_id];
                vbyte *b_reg_data = &reg_data[function_regs_offsets[b_reg_id]];

                if(hl_type_is_int(dst_reg_type) && (dst_reg_type->kind != a_reg_type->kind || a_reg_type->kind != b_reg_type->kind)) {
                    HL_UNREACHABLE;
                }

                switch (dst_reg_type->kind) {
                    case HUI8:
                    case HUI16:
                    case HI32:
                    case HI64: {
                        hl_intmax a = 0;
                        hl_intmax b = 0;
                        hl_copy_type_data(&a, a_reg_data, a_reg_type);
                        hl_copy_type_data(&b, b_reg_data, b_reg_type);

                        hl_intmax result = a << b;
                        hl_copy_type_data(dst_reg_data, &result, dst_reg_type);
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }

                break;
            }
            case OSShr: {
                int dst_reg_id = opcode->p1;
                int a_reg_id = opcode->p2;
                int b_reg_id = opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                hl_type *a_reg_type = fregs[a_reg_id];
                vbyte *a_reg_data = &reg_data[function_regs_offsets[a_reg_id]];

                hl_type *b_reg_type = fregs[b_reg_id];
                vbyte *b_reg_data = &reg_data[function_regs_offsets[b_reg_id]];

                if(hl_type_is_int(dst_reg_type) && (dst_reg_type->kind != a_reg_type->kind || a_reg_type->kind != b_reg_type->kind)) {
                    HL_UNREACHABLE;
                }

                switch (dst_reg_type->kind) {
                    case HUI8:
                    case HUI16:
                    case HI32:
                    case HI64: {
                        hl_intmax a = 0;
                        hl_intmax b = 0;
                        hl_copy_type_data(&a, a_reg_data, a_reg_type);
                        hl_copy_type_data(&b, b_reg_data, b_reg_type);

                        hl_intmax result = a >> b;
                        hl_copy_type_data(dst_reg_data, &result, dst_reg_type);
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }

                break;
            }
            case OIncr: {
                int dst_reg_id = opcode->p1;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                if(!hl_type_is_int(dst_reg_type)) {
                    HL_UNREACHABLE;
                }

                hl_intmax dst = 0;
                hl_copy_type_data(&dst, dst_reg_data, dst_reg_type);
                ++dst;
                hl_copy_type_data(dst_reg_data, &dst, dst_reg_type);
                break;
            }
            case OCall0: {
                int dst_reg_id = opcode->p1;
                int findex = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                vdynamic callee_return = { .t = dst_reg_type };
                hl_interp_raw_function_call(ctx, findex, 0, NULL, NULL, &callee_return);
                hl_copy_type_data(dst_reg_data, (vbyte*)&callee_return.v, dst_reg_type);
                break;
            }
            case OCall1: {
                int dst_reg_id = opcode->p1;
                int findex = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                hl_type *args_types[1] = { fregs[opcode->p3] };
                vbyte *args_data[1] = { &reg_data[function_regs_offsets[opcode->p3]] };

                vdynamic callee_return = { .t = dst_reg_type };
                hl_interp_raw_function_call(ctx, findex, 1, args_data, args_types, &callee_return);
                hl_copy_type_data(dst_reg_data, (vbyte*)&callee_return.v, dst_reg_type);
                break;
            }
            case OCall2: {
                int dst_reg_id = opcode->p1;
                int findex = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                hl_type *args_types[2] = { fregs[opcode->p3], fregs[(size_t)opcode->extra] };
                vbyte *args_data[2] = { &reg_data[function_regs_offsets[opcode->p3]], &reg_data[function_regs_offsets[(size_t)opcode->extra]] };

                vdynamic callee_return = { .t = dst_reg_type };
                hl_interp_raw_function_call(ctx, findex, 2, args_data, args_types, &callee_return);
                hl_copy_type_data(dst_reg_data, (vbyte*)&callee_return.v, dst_reg_type);
                break;
            }
            case OCall3: {
                int dst_reg_id = opcode->p1;
                int findex = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                hl_type *args_types[3] = { fregs[opcode->p3], fregs[opcode->extra[0]], fregs[opcode->extra[1]] };
                vbyte *args_data[3] = { 
                    &reg_data[function_regs_offsets[opcode->p3]], 
                    &reg_data[function_regs_offsets[opcode->extra[0]]], 
                    &reg_data[function_regs_offsets[opcode->extra[1]]] 
                };

                vdynamic callee_return = { .t = dst_reg_type };
                hl_interp_raw_function_call(ctx, findex, 3, args_data, args_types, &callee_return);
                hl_copy_type_data(dst_reg_data, (vbyte*)&callee_return.v, dst_reg_type);
                break;
            }
            case OCallN: {
                int dst_reg_id = opcode->p1;
                int findex = opcode->p2;
                int args = opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                hl_type **args_types = (hl_type**)malloc(args*sizeof(hl_type*));
                vbyte **args_data = (vbyte**)malloc(args*sizeof(vbyte*));
                
                for (size_t i = 0; i < args; ++i) {
                    int reg_idx = opcode->extra[i];
                    args_types[i] = fregs[reg_idx];
                    args_data[i] = &reg_data[function_regs_offsets[reg_idx]];
                }

                vdynamic callee_return = { .t = dst_reg_type };
                hl_interp_raw_function_call(ctx, findex, args, args_data, args_types, &callee_return);
                hl_copy_type_data(dst_reg_data, (vbyte*)&callee_return.v, dst_reg_type);

                free(args_types);
                free(args_data);
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
                        vvirtual *v;
                        memcpy(&v, dst_reg_data, sizeof(void*));
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
                                hl_i32 v = 0;
                                hl_copy_type_data(&v, src_reg_data, src_reg_type);
                                hl_dyn_seti(vdyn, hashed, src_reg_type, v);
                                break;
                            }
                            case HI64: {
                                hl_i64 v;
                                memcpy(&v, src_reg_data, sizeof(hl_i64));
                                hl_dyn_seti64(vdyn, hashed, v);
                                break;
                            }
                            case HF32: {
                                hl_f32 v;
                                memcpy(&v, src_reg_data, sizeof(hl_f32));
                                hl_dyn_setf(vdyn, hashed, v);
                                break;
                            }
                            case HF64: {
                                hl_f64 v;
                                memcpy(&v, src_reg_data, sizeof(hl_f64));
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
                                void *v;
                                memcpy(&v, src_reg_data, sizeof(void*));
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
                
                vdynamic *dyn;
                memcpy(&dyn, dst_reg_data, sizeof(void*));

                const uchar *field_name = hl_get_ustring(code, field_name_string_index);
                int field_hash = hl_hash_gen(field_name, true);

                switch (src_reg_type->kind) {
                    case HBOOL:
                    case HUI8:
                    case HUI16:
                    case HI32: {
                        hl_i32 v = 0;
                        hl_copy_type_data(&v, src_reg_data, src_reg_type);
                        hl_dyn_seti(dyn, field_hash, src_reg_type, v);
                        break;
                    }
                    case HI64: {
                        hl_i64 v;
                        memcpy(&v, src_reg_data, sizeof(hl_i64));
                        hl_dyn_seti64(dyn, field_hash, v);
                        break;
                    }
                    case HF32: {
                        hl_f32 v;
                        memcpy(&v, src_reg_data, sizeof(hl_f32));
                        hl_dyn_setf(dyn, field_hash, v);
                        break;
                    }
                    case HF64: {
                        hl_f64 v;
                        memcpy(&v, src_reg_data, sizeof(hl_f64));
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
                        void *v;
                        memcpy(&v, src_reg_data, sizeof(void*));
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

                hl_bool raw_data;
                memcpy(&raw_data, src_reg_data, sizeof(hl_bool));
                
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

                vbyte *raw_data;
                memcpy(&raw_data, src_reg_data, sizeof(vbyte*));
                
                if((is_negated && !raw_data) || (!is_negated && raw_data)) {
                    current_op += offset;
                }
                break;
            }
            case OJSGte: {
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

                hl_bool is_int = hl_type_is_int(a_reg_type);

                hl_bool result;
                if(is_int && is_int) {
                    hl_intmax a = 0;
                    hl_intmax b = 0;

                    hl_copy_type_data(&a, a_reg_data, a_reg_type);
                    hl_copy_type_data(&b, b_reg_data, b_reg_type);

                    result = a >= b;
                } else {
                    __builtin_trap(); // TODO
                }

                if(result) {
                    current_op += offset;
                }
                break;
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
                        case HI32:
                        case HI64: {
                            hl_intmax a = 0;
                            hl_intmax b = 0;
                            hl_copy_type_data(&a, a_reg_data, a_reg_type);
                            hl_copy_type_data(&b, b_reg_data, b_reg_type);
                            result = a == b;
                            break;
                        }
                        case HF32:
                        case HF64: {
                            __builtin_trap(); // TODO
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
                        hl_bool bool_value;
                        memcpy(&bool_value, src_reg_data, sizeof(hl_bool));
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
                            void *ptr;
                            memcpy(&ptr, src_reg_data, sizeof(void*));

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

                vdynamic *v;
                memcpy(&v, src_reg_data, sizeof(void*));

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
            case ONullCheck: {
                int check_reg_id = opcode->p1;

                hl_type *check_reg_type = fregs[check_reg_id];
                vbyte *check_reg_data = &reg_data[function_regs_offsets[check_reg_id]];

                if(!hl_type_can_be_null(check_reg_type)) {
                    HL_UNREACHABLE;
                }

                void *data;
                memcpy(&data, check_reg_data, sizeof(void*));

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

                size_t index = 0;
                hl_copy_type_data(&index, src_index_reg_data, src_index_reg_type);

                switch (src_reg_type->kind) {
                    case HABSTRACT: {
                        __builtin_trap(); // TODO
                        break;
                    }
                    case HARRAY: {
                        varray *array;
                        memcpy(&array, src_reg_data, sizeof(void*));

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
            case OSetI16: {
                int dst_reg_id = opcode->p1;
                int dst_index_reg_id = opcode->p2;
                int src_reg_id = opcode->p3;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];

                hl_type *dst_index_reg_type = fregs[dst_index_reg_id];
                vbyte *dst_index_reg_data = &reg_data[function_regs_offsets[dst_index_reg_id]];

                hl_type *src_reg_type = fregs[src_reg_id];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(dst_reg_type->kind != HBYTES || !hl_type_is_int(dst_index_reg_type) || (!hl_type_is_int(src_reg_type) && src_reg_type->kind != HUI8)) {
                    HL_UNREACHABLE;
                }

                vbyte* bytes;
                memcpy(&bytes, dst_reg_data, sizeof(void*));

                size_t offset = 0;
                hl_copy_type_data(&offset, dst_index_reg_data, dst_index_reg_type);

                memcpy(&bytes[offset], src_reg_data, sizeof(hl_ui16));
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

                size_t index = 0;
                hl_copy_type_data(&index, dst_index_reg_data, dst_index_reg_type);

                switch (dst_reg_type->kind) {
                    case HABSTRACT: {
                        __builtin_trap(); // TODO
                        break;
                    }
                    case HARRAY: {
                        varray *array;
                        memcpy(&array, dst_reg_data, sizeof(void*));

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
                    case HSTRUCT: {
                        obj = hl_alloc_obj(dst_reg_type);
                        break;
                    }
                    case HDYNOBJ: {
                        obj = hl_alloc_dynobj();
                        break;
                    }
                    case HVIRTUAL: {
                        obj = hl_alloc_virtual(dst_reg_type);
                        break;
                    }
                    default: {
                        HL_UNREACHABLE;
                        break;
                    }
                }

                memcpy(dst_reg_data, &obj, sizeof(void*));
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

                        size_t total_args = args + (closure->hasValue ? 1 : 0);
                        size_t current_arg = 0;
                        hl_type **args_types = (hl_type**)malloc(args*sizeof(hl_type*));
                        vbyte **args_data = (vbyte**)malloc(args*sizeof(vbyte*));
                        
                        if(closure->hasValue) {
                            args_types[current_arg] = &hlt_dyn;
                            args_data[current_arg] = (vbyte*)&closure->value;
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

                varray *array;
                memcpy(&array, array_reg_data, sizeof(void*));

                size_t array_size = array->size;
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
            case ORef: {
                int dst_reg_id = opcode->p1;
                int src_reg_id = opcode->p2;

                hl_type *dst_reg_type = fregs[dst_reg_id];
                hl_type *src_reg_type = fregs[src_reg_id];

                vbyte *dst_reg_data = &reg_data[function_regs_offsets[dst_reg_id]];
                vbyte *src_reg_data = &reg_data[function_regs_offsets[src_reg_id]];

                if(dst_reg_type->kind != HREF) {
                    HL_UNREACHABLE;
                }

                memcpy(dst_reg_data, &src_reg_data, sizeof(void*));
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
    
    out->t = fun_type->ret; // out type is not initialized
    hl_interp_raw_function_call(ctx, findex, 0, NULL, NULL, out); // TODO: args!
    return out;
}

void hl_interp_init(interp_ctx *ctx, hl_module *m) {
    ctx->m = m;
    m->interp.ctx = global_ctx = ctx;
    hl_alloc *alloc = &ctx->alloc;
    hl_code *module_code = m->code;
    int total_functions = module_code->nfunctions + module_code->nnatives;
    int **function_regs_offsets = (int**)hl_malloc(alloc, sizeof(int*) * total_functions);
    int *function_args_sizes = (int*)hl_malloc(alloc, sizeof(int*) * total_functions);

    for (size_t i = 0; i < module_code->nfunctions; ++i) {
        hl_function *f = &module_code->functions[i];
        hl_type_fun *f_type = f->type->fun;

        int offsets_size = f->nregs + 1;
        int *offsets = (int*)hl_malloc(alloc, offsets_size * sizeof(int));

        int currentOffset = 0;
        for (int j = 0; j < f->nregs; ++j) {
            offsets[j] = currentOffset;
            currentOffset += hl_ntype_size(f->regs[j]);
        }
        offsets[f->nregs] = currentOffset;

        int argsSize = 0;
        for (int j = 0; j < f_type->nargs; ++j) {
            argsSize += hl_ntype_size(f->regs[j]);
        }

        function_regs_offsets[i] = offsets;
        function_args_sizes[i] = argsSize;
    }

    ctx->fregs_offsets = function_regs_offsets;
    ctx->fargs_sizes = function_args_sizes;
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