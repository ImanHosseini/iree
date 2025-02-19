# Copyright 2020 The IREE Authors
#
# Licensed under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Doesn't use bazel_to_cmake because of custom configuration vars

# Driver registration has implicit thread dependency from task executor.

# TODO(#4298): Separate HAL driver registration from threading.
if(NOT ${IREE_ENABLE_THREADING})
  return()
endif()

set(IREE_HAL_DRIVER_MODULES)
if(${IREE_HAL_DRIVER_CUDA})
  list(APPEND IREE_HAL_DRIVER_MODULES iree::hal::cuda::registration)
endif()
if(${IREE_HAL_DRIVER_DYLIB})
  list(APPEND IREE_HAL_DRIVER_MODULES iree::hal::dylib::registration)
  # TODO(benvanik): add a IREE_HAL_DRIVER_DYLIB_SYNC or global flag.
  list(APPEND IREE_HAL_DRIVER_MODULES iree::hal::dylib::registration::sync)
endif()
if(${IREE_HAL_DRIVER_VMVX})
  list(APPEND IREE_HAL_DRIVER_MODULES iree::hal::vmvx::registration)
endif()
if(${IREE_HAL_DRIVER_VULKAN})
  list(APPEND IREE_HAL_DRIVER_MODULES iree::hal::vulkan::registration)
endif()
if(${IREE_BUILD_EXPERIMENTAL_ROCM})
  list(APPEND IREE_HAL_DRIVER_MODULES experimental::rocm::registration)
endif()

iree_cc_library(
  NAME
    drivers
  HDRS
    "init.h"
  SRCS
    "init.c"
  DEPS
    iree::base
    iree::base::tracing
    ${IREE_HAL_DRIVER_MODULES}
  PUBLIC
)
