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


generator, converter, source = sys.argv[1:]
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    document = root / "bad-note.doc"
    output = root / "bad-note.docx"
    generated = subprocess.run([generator, source, str(document)], capture_output=True)
    assert generated.returncode == 0, generated.stderr
    result = subprocess.run(
        [converter, "convert", str(document), "-o", str(output)],
        capture_output=True,
    )
    assert result.returncode == 0, (result.returncode, result.stderr)
    assert result.stderr == (
        b"warning: skipped footnote story PLC: invalid note reference PLC\n"
    ), result.stderr
    with zipfile.ZipFile(output) as archive:
        names = archive.namelist()
        assert "word/footnotes.xml" not in names
        assert "word/endnotes.xml" in names
        assert b"Main paragraph" in archive.read("word/document.xml")
