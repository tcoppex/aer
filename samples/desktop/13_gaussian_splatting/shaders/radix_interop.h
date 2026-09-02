#ifndef SHADERS_RADIX_INTEROP_H_
#define SHADERS_RADIX_INTEROP_H_

// ---------------------------------------------------------------------------
//
// Reference:
//   " Onesweep: A Faster Least Significant Digit Radix Sort for GPUs "
//      from Andy Adinets & Duane Merrill
//
// ---------------------------------------------------------------------------

static const uint32_t kRadixKeyBits       = 64u;
static const uint32_t kRadixBits          = 8u;

static const uint32_t kRadixNumPasses     = kRadixKeyBits / kRadixBits; // 8

// (default block num threads)
static const uint32_t kRadixSize          = 1 << kRadixBits; // 256

// ---------------------------------------------------------------------------

static const uint32_t kRadixHistogramSize = kRadixSize * kRadixNumPasses;

static const uint32_t kItemsPerThreads    = 15;
static const uint32_t kTileSize           = kRadixSize * kItemsPerThreads;

// ---------------------------------------------------------------------------

struct RadixPushConstant {
  uint32_t pass;
  uint32_t pad0_[3];

  uint64_t numkeys_addr;

  uint64_t unsorted_keys_addr;
  uint64_t unsorted_values_addr;
  uint64_t sorted_keys_addr;
  uint64_t sorted_values_addr;

  uint64_t histogram_addr;
  uint64_t descriptor_addr;
  uint64_t counter_addr;
};

// ---------------------------------------------------------------------------

#endif