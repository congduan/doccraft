// Copyright 2026 The doccraft Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "crypto/rc4.h"

#include <algorithm>
#include <cstddef>

namespace doccraft::crypto {

Rc4::Rc4(std::span<const uint8_t> key) {
  for (size_t n = 0; n < 256; ++n) state_[n] = static_cast<uint8_t>(n);
  if (key.empty()) return;
  uint8_t j = 0;
  for (size_t n = 0; n < 256; ++n) {
    j = static_cast<uint8_t>(j + state_[n] + key[n % key.size()]);
    std::swap(state_[n], state_[j]);
  }
}

void Rc4::Xor(std::span<uint8_t> data) {
  for (uint8_t& byte : data) {
    i_ = static_cast<uint8_t>(i_ + 1);
    j_ = static_cast<uint8_t>(j_ + state_[i_]);
    std::swap(state_[i_], state_[j_]);
    byte ^= state_[static_cast<uint8_t>(state_[i_] + state_[j_])];
  }
}

}  // namespace doccraft::crypto
