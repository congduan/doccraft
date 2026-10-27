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

#ifndef TESTS_FUZZ_UTIL_H_
#define TESTS_FUZZ_UTIL_H_

#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace doccraft::test {

// Outcome of one library-level conversion attempt (the pipeline behind
// `doccraft convert`, minus the CLI). `stage` names the failing layer:
// "cfb", "streams", "fib", "encrypted", "decrypt", "parse", "ole", "writer".
struct ConvertOutcome {
  bool ok = false;
  std::string stage;
  std::string error;
};

// Runs CfbReader::Open -> stream reads -> FIB/RC4 -> ParseFormatted -> OLE
// repackage -> docx::Write on a whole-file byte image. With an empty password
// an encrypted document is rejected the way `convert` without --password is.
ConvertOutcome ConvertBytes(std::span<const uint8_t> file,
                            std::string_view password = {});

// Applies 1..64 deterministic mutations (flip / xor / replace) to random
// positions inside bytes[begin, end). The rng is caller-owned so rounds are
// reproducible from a fixed std::mt19937 seed.
void MutateRegion(std::mt19937& rng, std::vector<uint8_t>& bytes, size_t begin,
                  size_t end);

// Reads a file from disk in binary mode; an unreadable path yields {}.
std::vector<uint8_t> ReadSeed(const std::string& path);

}  // namespace doccraft::test

#endif  // TESTS_FUZZ_UTIL_H_
