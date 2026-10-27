# Copyright 2026 The doccraft Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0

"""End-to-end XOR obfuscation regression."""

import pathlib
import subprocess
import sys
import tempfile
import zipfile


binary, plain, encrypted = sys.argv[1:]
with tempfile.TemporaryDirectory() as temp:
    plain_out = pathlib.Path(temp) / "plain.docx"
    xor_out = pathlib.Path(temp) / "xor.docx"
    subprocess.run([binary, "convert", plain, "-o", str(plain_out)], check=True)
    good = subprocess.run(
        [binary, "convert", encrypted, "--password", "doccraft-m6",
         "-o", str(xor_out)], capture_output=True, text=True)
    assert good.returncode == 0, good.stderr
    with zipfile.ZipFile(plain_out) as a, zipfile.ZipFile(xor_out) as b:
        assert set(a.namelist()) == set(b.namelist())
        for part in a.namelist():
            assert a.read(part) == b.read(part), part
    wrong = subprocess.run(
        [binary, "convert", encrypted, "--password", "wrong", "-o",
         str(xor_out)], capture_output=True, text=True)
    assert wrong.returncode != 0
    assert "incorrect document password" in wrong.stderr
    missing = subprocess.run(
        [binary, "convert", encrypted, "-o", str(xor_out)],
        capture_output=True, text=True)
    assert missing.returncode != 0
    assert "XOR obfuscation document requires --password" in missing.stderr
