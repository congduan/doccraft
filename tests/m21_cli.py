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


def main():
    generator, converter, mode, source = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="doccraft-m21-cli-") as directory:
        root = pathlib.Path(directory)
        document = root / "input.doc"
        output = root / "output.docx"
        made = subprocess.run(
            [generator, mode, source, str(document)], capture_output=True
        )
        assert made.returncode == 0, (made.returncode, made.stderr)
        result = subprocess.run(
            [converter, "convert", str(document), "-o", str(output)],
            capture_output=True, text=True,
        )
        assert result.returncode == 0, (result.returncode, result.stderr)
        with zipfile.ZipFile(output) as archive:
            xml = archive.read("word/document.xml").decode("utf-8")
        if mode == "short-piece":
            assert "non-complex piece coverage short" in result.stderr, result.stderr
            assert "Hello doccraft. Plain single paragraph." in xml, xml
        else:
            warning = "has no ObjectPool payload; placeholder emitted"
            assert result.stderr.count(warning) == 3, result.stderr
            assert xml.count("[Embedded object]") == 3, xml
            assert "<o:OLEObject" not in xml, xml
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
