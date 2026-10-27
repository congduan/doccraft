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

#ifndef SRC_CRYPTO_MD5_H_
#define SRC_CRYPTO_MD5_H_

#include <array>
#include <cstdint>
#include <span>

namespace doccraft::crypto {
std::array<uint8_t, 16> Md5(std::span<const uint8_t> input);
}  // namespace doccraft::crypto
#endif  // SRC_CRYPTO_MD5_H_
