// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_FLATBUFFERS_SAFE_FLATBUFFERS_H_
#define THIRD_PARTY_FLATBUFFERS_SAFE_FLATBUFFERS_H_

#include <optional>

#include "base/containers/span.h"
#include "flatbuffers/flatbuffers.h"

// VerifiedFlatBuffer<T> is a type-safe wrapper that enforces FlatBuffer
// verification before allowing access to the root object. This prevents
// using unverified FlatBuffer data, which could otherwise lead to
// out-of-bounds memory access or other memory-safety issues.
//
// The only way to construct a VerifiedFlatBuffer<T> is via the Create()
// factory method, which runs the provided verification function and returns
// std::nullopt if verification fails.
//
// Usage example:
//
//   // Each generated FlatBuffer header provides a Verify*Buffer() function:
//   //   bool VerifyMyTableBuffer(flatbuffers::Verifier&);
//   //   const MyNS::MyTable* GetMyTable(const void*);
//
//   auto verified = VerifiedFlatBuffer<MyNS::MyTable>::Create(
//       raw_bytes, MyNS::VerifyMyTableBuffer);
//   if (!verified) {
//     // Buffer is invalid; do not access data.
//     return false;
//   }
//   const MyNS::MyTable* root = verified->root();  // Always safe to use.
//
// Note: The VerifiedFlatBuffer holds a non-owning view (base::span) of the
// raw bytes. The caller must ensure that the underlying buffer outlives the
// VerifiedFlatBuffer instance.
template <typename T>
class VerifiedFlatBuffer {
 public:
  // Function pointer type matching the flatc-generated Verify*Buffer()
  // functions.
  using VerifyFn = bool (*)(::flatbuffers::Verifier&);

  // Factory method. Runs `verify_fn` against `data`. Returns a
  // VerifiedFlatBuffer on success, or std::nullopt if the buffer is invalid.
  static std::optional<VerifiedFlatBuffer<T>> Create(
      base::span<const uint8_t> data,
      VerifyFn verify_fn) {
    ::flatbuffers::Verifier verifier(data.data(), data.size());
    if (!verify_fn(verifier)) {
      return std::nullopt;
    }
    return VerifiedFlatBuffer<T>(data);
  }

  // Returns the root table pointer for the verified buffer. Safe to call
  // because Create() guarantees the buffer passed verification.
  const T* root() const {
    return ::flatbuffers::GetRoot<T>(data_.data());
  }

 private:
  explicit VerifiedFlatBuffer(base::span<const uint8_t> data) : data_(data) {}

  base::span<const uint8_t> data_;
};

#endif  // THIRD_PARTY_FLATBUFFERS_SAFE_FLATBUFFERS_H_
