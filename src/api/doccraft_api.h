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

#ifndef SRC_API_DOCCRAFT_API_H_
#define SRC_API_DOCCRAFT_API_H_

// Embedding API: one buffer in, one buffer out.
//
// This is the entry point for targets that have no command line and no file
// system - WebAssembly (the module cannot call open/read without a virtual
// file system), Android through the NDK, and iOS. It only depends on the four
// core layers, so the same binary layout is produced by every build.
//
// The header is C99 and self-contained: no C++ types, no third-party headers.
//
// Ownership: doccraft_convert_buffer() allocates the result with the library's
// allocator; release it with doccraft_free(). Mixing allocators across an FFI
// boundary (allocating here, freeing with the caller's free()) corrupts the
// heap on Windows and on every target that uses a different CRT.
//
// Thread safety: no global mutable state. Concurrent calls on distinct buffers
// are safe; the only shared state is a thread-local iconv descriptor.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Incremented on any incompatible change to the functions below.
#define DOCCRAFT_API_VERSION 1

// Result codes returned by doccraft_convert_buffer().
typedef enum doccraft_status {
  DOCCRAFT_OK = 0,
  // A required pointer was NULL, input_len was 0, or *output_len was NULL.
  DOCCRAFT_ERROR_INVALID_ARGUMENT = 1,
  // The input is not a .doc this build can read, or the conversion failed.
  // err_buf carries the diagnostic.
  DOCCRAFT_ERROR_CONVERSION_FAILED = 2,
  // Allocating the output buffer failed.
  DOCCRAFT_ERROR_OUT_OF_MEMORY = 3,
} doccraft_status;

// Converts a Word 97-2003 .doc image to a .docx archive.
//
// input/input_len   the whole .doc file in memory; it is not modified.
// password          the document password, or NULL for none. An empty string
//                   is treated as no password.
// output            receives a malloc-compatible pointer owned by the caller;
//                   set to NULL on failure. Must not be NULL.
// output_len        receives the byte length of *output; set to 0 on failure.
//                   Must not be NULL.
// err_buf/err_buf_len
//                   optional. On failure the message is copied here and
//                   NUL-terminated; it is truncated when the buffer is too
//                   small. Pass NULL/0 to discard diagnostics.
//
// Returns DOCCRAFT_OK on success, otherwise one of doccraft_status.
int doccraft_convert_buffer(const uint8_t* input, size_t input_len,
                            const char* password, uint8_t** output,
                            size_t* output_len, char* err_buf,
                            size_t err_buf_len);

// Releases a buffer returned by doccraft_convert_buffer(). NULL is ignored.
void doccraft_free(void* memory);

// True when this build can decode `codepage` (a Windows ANSI code page number
// as stored in FibBase.lid or SummaryInformation). 1252 is always true; other
// pages depend on the backend: iconv on POSIX, MultiByteToWideChar on Windows,
// and no decoder at all on WebAssembly, which therefore reports false for
// everything but 1252.
int doccraft_supports_codepage(uint32_t codepage);

// "major.minor.patch" of the library, static storage.
const char* doccraft_version(void);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // SRC_API_DOCCRAFT_API_H_
