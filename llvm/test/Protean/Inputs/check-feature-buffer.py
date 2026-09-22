# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

"""Run the feature collector with complete and deliberately truncated buffers."""

import ctypes
import mmap
import os
import struct
import subprocess
import sys


class Header(ctypes.Structure):
    _fields_ = [
        ("bitcode_size", ctypes.c_size_t),
        ("module_level_ipc", ctypes.c_bool),
        ("ir2vec_dimension", ctypes.c_size_t),
        ("feature_count", ctypes.c_size_t),
        ("data_size", ctypes.c_size_t),
        ("data_offset", ctypes.c_size_t),
    ]


HEADER_SIZE = ctypes.sizeof(Header)
EMBEDDING_SIZE = 300 * ctypes.sizeof(ctypes.c_double)


def collect(capacity):
    # On Linux shm_open("/shm_PID") opens /dev/shm/shm_PID. The subprocess's
    # parent PID is ours, so this is the object the collector will use.
    path = "/dev/shm/shm_" + str(os.getpid())
    fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_RDWR, 0o600)
    try:
        os.ftruncate(fd, HEADER_SIZE + capacity)
        with mmap.mmap(fd, HEADER_SIZE + capacity) as memory:
            memory[:HEADER_SIZE] = bytes(
                Header(0, False, 300, 0, capacity, HEADER_SIZE)
            )
            subprocess.run(
                [sys.argv[1], "-protean", "-passes=protean-collect-features",
                 "-disable-output", sys.argv[2]],
                check=True,
            )
            return Header.from_buffer_copy(memory), memory[:]
    finally:
        os.close(fd)
        os.unlink(path)


header, payload = collect(1024 * 1024)
assert header.feature_count > 0
end = HEADER_SIZE + EMBEDDING_SIZE
for _ in range(header.feature_count):
    end = payload.index(b"\0", end) + 1
    struct.unpack_from("f", payload, end)
    end += ctypes.sizeof(ctypes.c_float)
print("Complete feature set written")

exact_header, _ = collect(end - HEADER_SIZE)
assert exact_header.feature_count == header.feature_count
print("Exact-fit feature set written")

header, _ = collect(end - HEADER_SIZE - 1)
assert header.feature_count == 0
print("Truncated feature set rejected")
