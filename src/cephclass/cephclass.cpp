#include <cstdint>
#include <cstdio>
#include <memory>

#include <rados/objclass.h>

#include "cephcomm_generated.h"

CLS_VER(1, 0)
CLS_NAME(faasm)

cls_handle_t h_class;
cls_method_handle_t h_maybe_exec_wasm_ro;
cls_method_handle_t h_maybe_exec_wasm_rw;
cls_method_handle_t h_maybe_exec_wasm_wo;

int maybe_exec_wasm_ro(cls_method_context_t hctx,
                       ceph::buffer::list* in,
                       ceph::buffer::list* out);

int maybe_exec_wasm_rw(cls_method_context_t hctx,
                       ceph::buffer::list* in,
                       ceph::buffer::list* out);

int maybe_exec_wasm_wo(cls_method_context_t hctx,
                       ceph::buffer::list* in,
                       ceph::buffer::list* out);

CLS_INIT(faasm)
{
    CLS_LOG(0, "Loading Faasm-NDP Ceph interface class");

    cls_register("faasm", &h_class);

    // TODO: Determine what PROMOTE does
    cls_register_cxx_method(h_class,
                            "maybe_exec_wasm_ro",
                            CLS_METHOD_RD,
                            maybe_exec_wasm_ro,
                            &h_maybe_exec_wasm_ro);
    cls_register_cxx_method(h_class,
                            "maybe_exec_wasm_rw",
                            CLS_METHOD_RD | CLS_METHOD_WR,
                            maybe_exec_wasm_rw,
                            &h_maybe_exec_wasm_rw);
    cls_register_cxx_method(h_class,
                            "maybe_exec_wasm_wo",
                            CLS_METHOD_WR,
                            maybe_exec_wasm_wo,
                            &h_maybe_exec_wasm_wo);
}

int maybe_exec_wasm(cls_method_context_t hctx,
                    ceph::buffer::list* in,
                    ceph::buffer::list* out,
                    bool readAllowed,
                    bool writeAllowed)
{
    CLS_LOG(3, "maybe_exec_wasm called");

    return 0;
}

int maybe_exec_wasm_ro(cls_method_context_t hctx,
                       ceph::buffer::list* in,
                       ceph::buffer::list* out)
{
    return maybe_exec_wasm(hctx, in, out, true, false);
}

int maybe_exec_wasm_rw(cls_method_context_t hctx,
                       ceph::buffer::list* in,
                       ceph::buffer::list* out)
{
    return maybe_exec_wasm(hctx, in, out, true, true);
}

int maybe_exec_wasm_wo(cls_method_context_t hctx,
                       ceph::buffer::list* in,
                       ceph::buffer::list* out)
{
    return maybe_exec_wasm(hctx, in, out, false, true);
}
