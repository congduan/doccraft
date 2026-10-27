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

// libFuzzer entry point (built only with DOCCRAFT_ENABLE_LIBFUZZER=ON and
// clang -fsanitize=fuzzer). Every input runs through the same library
// pipeline as `doccraft convert`; crashes are the only failures.
//
//   cmake -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ \
//         -DDOCCRAFT_ENABLE_LIBFUZZER=ON -DCMAKE_BUILD_TYPE=Release
//   cmake --build build-fuzz -j
//   build-fuzz/tests/doccraft_fuzz tests/corpus/corpus/ -max_total_time=300

#include <cstddef>
#include <cstdint>

#include "tests/fuzz_util.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Fixed corpus password: the encrypted fixtures use "doccraft-m6".
  (void)doccraft::test::ConvertBytes({data, size}, "doccraft-m6");
  return 0;
}
