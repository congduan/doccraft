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

"""The embedding API must produce the same package as the CLI.

tests/api_cli_fixture converts through doccraft_convert_buffer(); the CLI
converts the same file through its own orchestration. Both end in the same
writer, so every part must be byte-identical. Zip entry timestamps are ignored
by comparing content only.

usage: api_cli_parity.py <api_fixture> <doccraft> <sample.doc>...
"""
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


def parts(path):
    with zipfile.ZipFile(path) as archive:
        return {name: archive.read(name) for name in archive.namelist()}


def main():
    fixture, binary, *samples = sys.argv[1:]
    assert samples, "no samples given"
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        for sample in samples:
            sample = Path(sample)
            api_out = root / "api.docx"
            cli_out = root / "cli.docx"
            subprocess.run([fixture, str(sample), str(api_out)], check=True)
            subprocess.run(
                [binary, "convert", str(sample), "-o", str(cli_out)],
                check=True,
                capture_output=True,
            )
            api_parts = parts(api_out)
            cli_parts = parts(cli_out)
            assert api_parts.keys() == cli_parts.keys(), (
                sample.name,
                sorted(api_parts),
                sorted(cli_parts),
            )
            for name in api_parts:
                assert api_parts[name] == cli_parts[name], (sample.name, name)
    print(f"API/CLI parity PASS over {len(samples)} samples")


if __name__ == "__main__":
    main()
