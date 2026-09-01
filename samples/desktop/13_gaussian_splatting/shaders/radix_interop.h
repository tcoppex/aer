#ifndef SHADERS_RADIX_INTEROP_H_
#define SHADERS_RADIX_INTEROP_H_

// ---------------------------------------------------------------------------

#ifndef STATIC_CONST
#define STATIC_CONST static const
#endif

// ---------------------------------------------------------------------------

STATIC_CONST uint32_t kRadixKeyBits       = 64u;
STATIC_CONST uint32_t kRadixBits          = 8u;
STATIC_CONST uint32_t kRadixDigitCount    = kRadixKeyBits / kRadixBits; // 8

// (default block num threads)
STATIC_CONST uint32_t kRadixSize          = 1 << kRadixBits; // 256

STATIC_CONST uint32_t kRadixHistogramSize = kRadixSize * kRadixDigitCount;

// ---------------------------------------------------------------------------

struct RadixPushConstant {
  uint64_t numkeys_addr;

  uint64_t unsorted_keys_addr;
  uint64_t unsorted_values_addr;
  uint64_t sorted_keys_addr;
  uint64_t sorted_values_addr;

  uint64_t histogram_addr;
  uint64_t histogram_prefixes_addr;

  uint64_t descriptor_addr;

  uint32_t pad0_[1];
  uint32_t pass;
};

// ---------------------------------------------------------------------------

#endif