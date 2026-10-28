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


generator, converter, source, mode = sys.argv[1:]
assert mode in ("textbox", "textbox-story")
with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    document = root / "bad-textbox.doc"
    output = root / "bad-textbox.docx"
    generated = subprocess.run(
        [generator, source, str(document), mode], capture_output=True
    )
    assert generated.returncode == 0, generated.stderr
    result = subprocess.run(
        [converter, "convert", str(document), "-o", str(output)],
        capture_output=True,
    )
    assert result.returncode == 0, (result.returncode, result.stderr)
    warning = (
        b"warning: skipped textbox story PLC at FTXBXS 1\n"
        if mode == "textbox-story"
        else b"warning: skipped textbox story PLC: invalid textbox PLC size\n"
    )
    assert result.stderr == warning, result.stderr
    with zipfile.ZipFile(output) as archive:
        xml = archive.read("word/document.xml")
        assert b"<w:body>" in xml
        assert b"Before  After textbox." in xml
        assert b"Box sample line" not in xml
