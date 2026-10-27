#!/usr/bin/env python3
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

import pathlib
import subprocess
import sys
import tempfile
import zipfile


binary, plain, *fixtures = sys.argv[1:]
with tempfile.TemporaryDirectory() as directory:
    plain_out = pathlib.Path(directory) / "plain.docx"
    subprocess.run([binary, "convert", plain, "-o", str(plain_out)], check=True)
    for encrypted in fixtures:
        encrypted_out = pathlib.Path(directory) / "encrypted.docx"
        subprocess.run(
            [binary, "convert", encrypted, "--password", "doccraft-m6", "-o",
             str(encrypted_out)],
            check=True,
        )
        with zipfile.ZipFile(plain_out) as a, zipfile.ZipFile(encrypted_out) as b:
            assert a.read("word/document.xml") == b.read("word/document.xml")
        wrong = subprocess.run(
            [binary, "convert", encrypted, "--password", "wrong", "-o",
             str(encrypted_out)],
            capture_output=True,
            text=True,
            check=False,
        )
        assert wrong.returncode != 0
        assert "incorrect document password" in wrong.stderr
        missing = subprocess.run(
            [binary, "convert", encrypted, "-o", str(encrypted_out)],
            capture_output=True,
            text=True,
            check=False,
        )
        assert missing.returncode != 0
        assert "requires --password" in missing.stderr
        expected_type = "legacy RC4/MD5" if "legacy" in encrypted else "RC4 CryptoAPI"
        assert expected_type in missing.stderr
