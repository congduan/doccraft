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

#ifndef SRC_CFB_CFB_WRITER_H_
#define SRC_CFB_CFB_WRITER_H_

#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

#include "cfb/cfb_reader.h"

namespace doccraft::cfb {
// Repackage one ObjectPool child storage as an independent CFB file.
std::expected<std::vector<uint8_t>, Error> RepackageStorage(
    const CfbReader& reader, std::string_view storage_path);
}  // namespace doccraft::cfb

#endif  // SRC_CFB_CFB_WRITER_H_
