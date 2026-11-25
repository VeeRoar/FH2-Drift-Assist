"""Read the section table and relocation records of the target games' GR2 files."""
import ctypes
import struct


_decompress = None


def bind_decompressor(library):
    global _decompress
    func = library.GrannyDecompressData
    func.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_void_p,
                     ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_void_p]
    func.restype = ctypes.c_int
    _decompress = func


class Gr2:
    def __init__(self, raw):
        if raw[:4] != bytes.fromhex("e59b495e") or len(raw) < 104:
            raise ValueError("Expected an x64 little-endian Granny file")
        self.raw = raw
        self.p = 8
        self.h = struct.unpack_from("<10I", raw, 32)
        start = 32 + self.h[3]
        count = self.h[4]
        if self.h[0] != 7 or not 1 <= count <= 128 or start + count * 44 > len(raw):
            raise ValueError("Unsupported Granny section table")
        self.sec = [struct.unpack_from("<11I", raw, start+i*44) for i in range(count)]
        self.data = []
        self.fix = []
        for section in self.sec:
            codec, offset, size, unpacked = section[:4]
            fix_at, fix_count = section[7:9]
            if offset+size > len(raw) or unpacked > 64*1024*1024 or fix_at+12*fix_count > len(raw):
                raise ValueError("Granny section extends past its container")
            data = raw[offset:offset+size]
            if codec & 3 and size:
                dest = ctypes.create_string_buffer(unpacked)
                if not _decompress(codec & 3, 0, size, data, section[5], section[6], unpacked, dest):
                    raise ValueError("Granny section decompression failed")
                data = dest.raw
            self.data.append(data)
            self.fix.append({a: (b, c) for a, b, c in
                             struct.iter_unpack("<3I", raw[fix_at:fix_at+12*fix_count])})

    def ptr(self, section, offset):
        return self.fix[section].get(offset)

    def name(self, section, offset):
        reference = self.ptr(section, offset)
        if reference is None:
            return None
        target, start = reference
        data = self.data[target]
        end = data.find(b"\0", start)
        if start < 0 or end < start:
            raise ValueError("Granny name extends past its section")
        return data[start:end].decode("utf-8")

    def i(self, section, offset):
        return struct.unpack_from("<i", self.data[section], offset)[0]
