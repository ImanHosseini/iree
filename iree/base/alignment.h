// Copyright 2020 The IREE Authors
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Implementation of the primitives from stdalign.h used for cross-target
// value alignment specification and queries.

#ifndef IREE_BASE_ALIGNMENT_H_
#define IREE_BASE_ALIGNMENT_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "iree/base/config.h"
#include "iree/base/target_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

//===----------------------------------------------------------------------===//
// Alignment utilities
//===----------------------------------------------------------------------===//

// https://en.cppreference.com/w/c/types/max_align_t
#if defined(IREE_PLATFORM_WINDOWS)
// NOTE: 16 is a specified Microsoft API requirement for some functions.
#define iree_max_align_t 16
#else
#define iree_max_align_t sizeof(long double)
#endif  // IREE_PLATFORM_*

// https://en.cppreference.com/w/c/language/_Alignas
// https://en.cppreference.com/w/c/language/_Alignof
#if defined(IREE_COMPILER_MSVC)
#define iree_alignas(x) __declspec(align(x))
#define iree_alignof(x) __alignof(x)
#else
#define iree_alignas(x) __attribute__((__aligned__(x)))
#define iree_alignof(x) __alignof__(x)
#endif  // IREE_COMPILER_*

// Aligns |value| up to the given power-of-two |alignment| if required.
// https://en.wikipedia.org/wiki/Data_structure_alignment#Computing_padding
static inline iree_host_size_t iree_host_align(iree_host_size_t value,
                                               iree_host_size_t alignment) {
  return (value + (alignment - 1)) & ~(alignment - 1);
}

// Aligns |value| up to the given power-of-two |alignment| if required.
// https://en.wikipedia.org/wiki/Data_structure_alignment#Computing_padding
static inline iree_device_size_t iree_device_align(
    iree_device_size_t value, iree_device_size_t alignment) {
  return (value + (alignment - 1)) & ~(alignment - 1);
}

// Returns the size of a struct padded out to iree_max_align_t.
// This must be used when performing manual trailing allocation packing to
// ensure the alignment requirements of the trailing data are satisified.
//
// NOTE: do not use this if using VLAs (`struct { int trailing[]; }`) - those
// must precisely follow the normal sizeof(t) as the compiler does the padding
// for you.
//
// Example:
//  some_buffer_ptr_t* p = NULL;
//  iree_host_size_t total_size = iree_sizeof_struct(*buffer) + extra_data_size;
//  IREE_CHECK_OK(iree_allocator_malloc(allocator, total_size, (void**)&p));
#define iree_sizeof_struct(t) iree_host_align(sizeof(t), iree_max_align_t)

//===----------------------------------------------------------------------===//
// Alignment-safe memory accesses
//===----------------------------------------------------------------------===//

// Map little-endian byte indices in memory to the host memory order indices.
#if defined(IREE_ENDIANNESS_LITTLE)
#define IREE_LE_IDX_1(i) (i)
#define IREE_LE_IDX_2(i) (i)
#define IREE_LE_IDX_4(i) (i)
#define IREE_LE_IDX_8(i) (i)
#else
#define IREE_LE_IDX_1(i) (i)
#define IREE_LE_IDX_2(i) (1 - (i))
#define IREE_LE_IDX_4(i) (3 - (i))
#define IREE_LE_IDX_8(i) (7 - (i))
#endif  // IREE_ENDIANNESS_*

#if IREE_MEMORY_ACCESS_ALIGNMENT_REQUIRED

static inline uint8_t iree_unaligned_load_le_u8(const uint8_t* ptr) {
  return *ptr;
}
static inline uint16_t iree_unaligned_load_le_u16(const uint16_t* ptr) {
  const uint8_t* p = (const uint8_t*)ptr;
  return ((uint16_t)p[IREE_LE_IDX_2(0)]) | ((uint16_t)p[IREE_LE_IDX_2(1)] << 8);
}
static inline uint32_t iree_unaligned_load_le_u32(const uint32_t* ptr) {
  const uint8_t* p = (const uint8_t*)ptr;
  return ((uint32_t)p[IREE_LE_IDX_4(0)]) |
         ((uint32_t)p[IREE_LE_IDX_4(1)] << 8) |
         ((uint32_t)p[IREE_LE_IDX_4(2)] << 16) |
         ((uint32_t)p[IREE_LE_IDX_4(3)] << 24);
}
static inline uint64_t iree_unaligned_load_le_u64(const uint64_t* ptr) {
  const uint8_t* p = (const uint8_t*)ptr;
  return ((uint64_t)p[IREE_LE_IDX_8(0)]) |
         ((uint64_t)p[IREE_LE_IDX_8(1)] << 8) |
         ((uint64_t)p[IREE_LE_IDX_8(2)] << 16) |
         ((uint64_t)p[IREE_LE_IDX_8(3)] << 24) |
         ((uint64_t)p[IREE_LE_IDX_8(4)] << 32) |
         ((uint64_t)p[IREE_LE_IDX_8(5)] << 40) |
         ((uint64_t)p[IREE_LE_IDX_8(6)] << 48) |
         ((uint64_t)p[IREE_LE_IDX_8(7)] << 56);
}
static inline float iree_unaligned_load_le_f32(const float* ptr) {
  uint32_t uint_value = iree_unaligned_load_le_u32((const uint32_t*)ptr);
  float value;
  memcpy(&value, &uint_value, sizeof(value));
  return value;
}
static inline double iree_unaligned_load_le_f64(const double* ptr) {
  uint64_t uint_value = iree_unaligned_load_le_u64((const uint64_t*)ptr);
  double value;
  memcpy(&value, &uint_value, sizeof(value));
  return value;
}

static inline void iree_unaligned_store_le_u8(uint8_t* ptr, uint8_t value) {
  *ptr = value;
}
static inline void iree_unaligned_store_le_u16(uint16_t* ptr, uint16_t value) {
  uint8_t* p = (uint8_t*)ptr;
  p[IREE_LE_IDX_2(0)] = value;
  p[IREE_LE_IDX_2(1)] = value >> 8;
}
static inline void iree_unaligned_store_le_u32(uint32_t* ptr, uint32_t value) {
  uint8_t* p = (uint8_t*)ptr;
  p[IREE_LE_IDX_4(0)] = value;
  p[IREE_LE_IDX_4(1)] = value >> 8;
  p[IREE_LE_IDX_4(2)] = value >> 16;
  p[IREE_LE_IDX_4(3)] = value >> 24;
}
static inline void iree_unaligned_store_le_u64(uint64_t* ptr, uint64_t value) {
  uint8_t* p = (uint8_t*)ptr;
  p[IREE_LE_IDX_8(0)] = value;
  p[IREE_LE_IDX_8(1)] = value >> 8;
  p[IREE_LE_IDX_8(2)] = value >> 16;
  p[IREE_LE_IDX_8(3)] = value >> 24;
  p[IREE_LE_IDX_8(4)] = value >> 32;
  p[IREE_LE_IDX_8(5)] = value >> 40;
  p[IREE_LE_IDX_8(6)] = value >> 48;
  p[IREE_LE_IDX_8(7)] = value >> 56;
}
static inline void iree_unaligned_store_le_f32(float* ptr, float value) {
  uint32_t uint_value;
  memcpy(&uint_value, &value, sizeof(value));
  iree_unaligned_store_le_u32((uint32_t*)ptr, uint_value);
}
static inline void iree_unaligned_store_le_f64(double* ptr, double value) {
  uint64_t uint_value;
  memcpy(&uint_value, &value, sizeof(value));
  iree_unaligned_store_le_u64((uint64_t*)ptr, uint_value);
}

#else

#if defined(IREE_ENDIANNESS_LITTLE)

#define iree_unaligned_load_le_u8(ptr) *(ptr)
#define iree_unaligned_load_le_u16(ptr) *(ptr)
#define iree_unaligned_load_le_u32(ptr) *(ptr)
#define iree_unaligned_load_le_u64(ptr) *(ptr)
#define iree_unaligned_load_le_f32(ptr) *(ptr)
#define iree_unaligned_load_le_f64(ptr) *(ptr)

#define iree_unaligned_store_le_u8(ptr, value) *(ptr) = (value)
#define iree_unaligned_store_le_u16(ptr, value) *(ptr) = (value)
#define iree_unaligned_store_le_u32(ptr, value) *(ptr) = (value)
#define iree_unaligned_store_le_u64(ptr, value) *(ptr) = (value)
#define iree_unaligned_store_le_f32(ptr, value) *(ptr) = (value)
#define iree_unaligned_store_le_f64(ptr, value) *(ptr) = (value)

#else

#error "TODO(benvanik): little-endian load/store for big-endian archs"

#endif  // IREE_ENDIANNESS_*

#endif  // IREE_MEMORY_ACCESS_ALIGNMENT_REQUIRED

// clang-format off


// clang-format on

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // IREE_BASE_ALIGNMENT_H_
