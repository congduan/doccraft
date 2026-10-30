# Copyright 2026 The doccraft Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
import pathlib
import subprocess
import sys
import tempfile


binary = sys.argv[1]
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    unreadable = root / "unreadable.doc"
    unreadable.write_bytes(b"test")
    unreadable.chmod(0)
    for path, message in (
        (root / "missing.doc", "input path does not exist"),
        # A missing parent maps to ENOTDIR/ERROR_PATH_NOT_FOUND, which must
        # read as "does not exist" rather than "stat failed".
        (root / "missing-dir" / "missing.doc", "input path does not exist"),
        (root, "input path is not a regular file"),
        (unreadable, "input file is not readable"),
    ):
        for command in ("convert", "inspect"):
            args = [binary, command, str(path)]
            if command == "convert":
                args += ["-o", str(root / "out.docx")]
            result = subprocess.run(args, capture_output=True, timeout=10)
            expected = f"{command}: {message}\n".encode()
            assert result.returncode == 1, (args, result.returncode, result.stderr)
            assert result.stderr == expected, (args, result.stderr, expected)
