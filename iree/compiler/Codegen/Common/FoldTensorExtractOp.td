// Copyright 2021 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef IREE_COMPILER_CODEGEN_COMMON_FOLDTENSOREXTRACTOP
#define IREE_COMPILER_CODEGEN_COMMON_FOLDTENSOREXTRACTOP

include "mlir/Dialect/MemRef/IR/MemRefOps.td"
include "mlir/Dialect/Tensor/IR/TensorOps.td"

// Canonicalize unnecessary tensor_load when the load is used just for
// an extract
def : Pat<(Tensor_ExtractOp (TensorLoadOp $value), $indices),
          (LoadOp $value, $indices)>;

#endif // IREE_COMPILER_CODEGEN_COMMON_FOLDTENSOREXTRACTOP
