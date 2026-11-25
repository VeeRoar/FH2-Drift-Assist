"""Sample and rebuild individual Q/P curves in local FH2/FM6 64-bit Granny clips.

All operations take and return in-memory assets. Caller selects the destination.
Track rotations are (x, y, z, w). Positions are in the target rig's local units.
"""
from contextlib import AbstractContextManager
import ctypes as C
import math
from pathlib import Path
import struct
import zlib

from gr2_sections import Gr2, bind_decompressor

_lib = None

class _Pack(C.Structure):
    _pack_ = 4


class _List(_Pack):
    _fields_ = [("count", C.c_int32), ("items", C.c_void_p)]


class _FileInfo(_Pack):
    _fields_ = [(n, C.c_void_p) for n in ("tool", "exporter", "filename")] + [
        (n, _List) for n in ("textures", "materials", "skeletons", "vertex_data",
                             "topologies", "meshes", "models", "groups", "animations")]


class _Anim(_Pack):
    _fields_ = [("name", C.c_char_p), ("duration", C.c_float), ("step", C.c_float),
                ("oversampling", C.c_int32), ("groups", _List)]


class _Group(_Pack):
    _fields_ = [("name", C.c_char_p), ("vectors", _List), ("transforms", _List)]


class _Curve(_Pack):
    _fields_ = [("type", C.c_void_p), ("data", C.c_void_p)]


class _Track(_Pack):
    _fields_ = [("name", C.c_char_p), ("flags", C.c_int32), ("rotation", _Curve),
                ("position", _Curve), ("scale", _Curve)]


_ptrf = C.POINTER(C.c_float)
def use_granny(path):
    """Bind the user's Win64 Granny runtime once before reading target game assets."""
    global _lib
    path = Path(path).resolve(strict=True)
    if _lib is not None and Path(_lib._name) == path:
        return
    library = C.CDLL(str(path))
    bind_decompressor(library)
    library.GrannyReadEntireFileFromMemory.argtypes = [C.c_int32, C.c_void_p]
    library.GrannyReadEntireFileFromMemory.restype = C.c_void_p
    library.GrannyGetFileInfo.argtypes = [C.c_void_p]
    library.GrannyGetFileInfo.restype = C.POINTER(_FileInfo)
    library.GrannyFreeFile.argtypes = [C.c_void_p]
    library.GrannyFreeFile.restype = None
    library.GrannyEvaluateCurveAtT.argtypes = [C.c_int32, C.c_bool, C.c_bool, C.POINTER(_Curve),
                                            C.c_bool, C.c_float, C.c_float, _ptrf, _ptrf]
    library.GrannyEvaluateCurveAtT.restype = None
    _lib = library


_unit = (C.c_float * 4)(0, 0, 0, 1)
_zero = (C.c_float * 3)(0, 0, 0)

# Name and selector pairs read from the supported FH2 and FM6 steering clips.
_FORMATS = {"DaK32fC32f": 1, "DaIdentity": 2, "DaConstant32f": 3,
            "D3Constant32f": 4, "D4Constant32f": 5, "D4nK16uC15u": 8,
            "D4nK8uC7u": 9, "D3K16uC16u": 10, "D3K8uC8u": 11,
            "D3I1K16uC16u": 17, "D3I1K8uC8u": 18}
_FORMAT_PREFIX = "CurveDataHeader_"


def _members(g, descriptor):
    section, start = descriptor
    fields = []
    for i in range(32):
        pos = start + i * 44
        kind = g.i(section, pos)
        if not kind:
            return fields
        fields.append((kind, g.name(section, pos+4), g.ptr(section, pos+12), g.i(section, pos+20)))
    raise ValueError("Unsupported Granny curve descriptor length")


def _quat16_shape(g, descriptor):
    """Identify the known alternate UInt16 element spelling only on an identical layout."""
    fields = _members(g, descriptor)
    if [(k, n, w) for k, n, _, w in fields] != [
        (1, "CurveDataHeader_D4nK16uC15u", 0), (16, "ScaleOffsetTableEntries", 0),
        (10, "OneOverKnotScale", 0), (3, "KnotsControls", 0)
    ] or fields[1][2] is not None or fields[2][2] is not None:
        return None
    if not fields[0][2] or not fields[3][2]:
        return None
    if _members(g, fields[0][2]) != [(12, "Format", None, 0), (12, "Degree", None, 0)]:
        return None
    members = _members(g, fields[3][2])
    if len(members) == 1 and members[0][0] == 16 and members[0][2:] == (None, 0):
        return members[0][1]
    return None


def prepare_steering_clip(raw):
    """Validate format selectors and normalize compatible aliases in a private file copy."""
    g = Gr2(raw)
    slots, _ = _slots(g)
    types, curves = {}, []
    for name, (section, start) in slots.items():
        for offset in (12, 28, 44):
            slot = start + offset
            descriptor = g.ptr(section, slot)
            data = g.ptr(section, slot+8)
            if not descriptor or not data:
                raise ValueError(f"Missing curve descriptor or payload on {name}")
            if descriptor not in types:
                label = g.name(descriptor[0], descriptor[1]+4) or ""
                kind = label.removeprefix(_FORMAT_PREFIX)
                if not label.startswith(_FORMAT_PREFIX) or kind not in _FORMATS:
                    raise ValueError(f"Unsupported {name} curve type: {label}")
                element = _quat16_shape(g, descriptor) if kind == "D4nK16uC15u" else None
                if kind == "D4nK16uC15u" and element not in ("Value", "UInt16"):
                    raise ValueError(f"Unrecognized quaternion curve layout on {name}")
                types[descriptor] = _FORMATS[kind], element
            expected, element = types[descriptor]
            if g.data[data[0]][data[1]] != expected:
                raise ValueError(f"File curve selector disagrees with its descriptor on {name}")
            if element == "Value":
                curves.append((section, slot))
    if not curves:
        return raw
    canonical = next((position for position, (_, item) in types.items() if item == "UInt16"), None)
    if canonical is None:
        raise ValueError("No standard UInt16 quaternion curve for validating the custom alias")
    pending = set(curves)
    patched = bytearray(raw)
    for section, header in enumerate(g.sec):
        for i in range(header[8]):
            pos = header[7] + i*12
            slot, = struct.unpack_from("<I", raw, pos)
            if (section, slot) in pending:
                struct.pack_into("<II", patched, pos+4, *canonical)
                pending.remove((section, slot))
    if pending:
        raise ValueError("Missing curve relocation for the custom alias")
    struct.pack_into("<I", patched, 40, zlib.crc32(patched[104:]))
    return bytes(patched)


def read_skeleton(raw):
    """Return the parent-ordered target bind pose, bone names and inverse-world matrices.

    'parent' is a name or None. Q/P are local xyzw/xyz. scale_shear (3x3)
    and inverse_world (4x4) are in Granny's original row-vector ordering.
    """
    g = Gr2(raw)
    if g.p != 8:
        raise ValueError("Expected a 64-bit skeleton")
    s, o = g.h[7:9]
    if g.i(s, o+48) != 1:
        raise ValueError("Expected one skeleton")
    ss, so = g.ptr(*g.ptr(s, o+52))
    bs, bo = g.ptr(ss, so+12)
    rows = {}
    names = []
    for k in range(g.i(ss, so+8)):
        pos = bo+164*k
        name = g.name(bs, pos)
        parent = g.i(bs, pos+8)
        if parent < -1 or parent >= k or not name or name in rows:
            raise ValueError(f"Invalid skeleton hierarchy at {name}")
        names.append(name)
        rows[name] = {
            "parent": names[parent] if parent >= 0 else None,
            "translation": struct.unpack_from("<3f", g.data[bs], pos+16),
            "rotation": struct.unpack_from("<4f", g.data[bs], pos+28),
            "scale_shear": struct.unpack_from("<9f", g.data[bs], pos+44),
            "inverse_world": struct.unpack_from("<16f", g.data[bs], pos+80),
        }
    return rows


class ClipSampler(AbstractContextManager):
    def __init__(self, raw):
        self._handle = None
        if raw[:4] != bytes.fromhex("e59b495e"):
            raise ValueError("Expected a little-endian 64-bit Granny file")
        self._handle = _lib.GrannyReadEntireFileFromMemory(len(raw), raw)
        if not self._handle:
            raise ValueError("Granny rejected the clip")
        try:
            animations = _lib.GrannyGetFileInfo(self._handle).contents.animations
            if animations.count != 1:
                raise ValueError("One animation expected")
            anim = C.cast(animations.items, C.POINTER(C.POINTER(_Anim)))[0].contents
            self.duration = float(anim.duration)
            self.tracks = {}
            groups = C.cast(anim.groups.items, C.POINTER(C.POINTER(_Group)))
            for i in range(anim.groups.count):
                group = groups[i].contents
                tracks = C.cast(group.transforms.items, C.POINTER(_Track))
                for j in range(group.transforms.count):
                    track = tracks[j]
                    key = track.name.decode()
                    if key in self.tracks:
                        raise ValueError(f"Ambiguous track name {key}")
                    # Granny may substitute its unsupported 0xff selector even when the
                    # on-disk selector was valid. Never evaluate a rejected native curve.
                    for curve in (track.rotation, track.position, track.scale):
                        if not curve.type or not curve.data:
                            raise ValueError(f"Missing native curve on {key}")
                        ptr = C.c_void_p.from_address(curve.type + 4).value
                        label = C.string_at(ptr).decode("ascii") if ptr else ""
                        fmt = _FORMATS.get(label.removeprefix(_FORMAT_PREFIX))
                        if fmt is None or not label.startswith(_FORMAT_PREFIX) or C.c_ubyte.from_address(curve.data).value != fmt:
                            raise ValueError(f"Granny rejected the {key} curve descriptor {label}")
                    self.tracks[key] = track
        except BaseException:
            self.close()
            raise

    def close(self):
        if self._handle:
            self.tracks = {}
            _lib.GrannyFreeFile(self._handle)
            self._handle = None

    def __exit__(self, *args):
        self.close()

    def _sample(self, name, phase, is_rotation):
        if not 0 <= phase <= 1:
            raise ValueError("Phase outside [0, 1]")
        track = self.tracks[name]
        dim, curve, base = (4, track.rotation, _unit) if is_rotation else (3, track.position, _zero)
        out = (C.c_float * dim)()
        _lib.GrannyEvaluateCurveAtT(dim, is_rotation, False, C.byref(curve), False,
                                    self.duration, self.duration * phase, out, base)
        return tuple(out)

    def pose(self, name, phase):
        return self._sample(name, phase, True), self._sample(name, phase, False)


def _slots(g):
    s, o = g.h[7:9]
    if g.p != 8 or g.i(s, o+120) != 1:
        raise ValueError("Expected a 64-bit file with one animation")
    a = g.ptr(s, o+124)
    s, o = g.ptr(*a)
    duration = struct.unpack_from("<f", g.data[s], o+8)[0]
    group_array = g.ptr(s, o+24)
    slots = {}
    for j in range(g.i(s, o+20)):
        gs, go = g.ptr(group_array[0], group_array[1]+j*8)
        ts, to = g.ptr(gs, go+24)
        for k in range(g.i(gs, go+20)):
            pos = to+k*60
            key = g.name(ts, pos)
            if key in slots:
                raise ValueError(f"Ambiguous track name {key}")
            slots[key] = ts, pos
    return slots, duration


class _Editor:
    def __init__(self, raw):
        self.g = Gr2(raw)
        g = self.g
        if g.p != 8 or g.h[0] != 7 or g.h[5] >= len(g.sec):
            raise ValueError("Only the validated FH2/FM6 64-bit schema is supported")
        self.data = [bytearray(d) for d in g.data]
        self.fix = [dict(d) for d in g.fix]
        self.changed = set()
        self.slots, self.duration = _slots(g)
        self.types = g.h[5]
        self.float_curve = self._curve_type()

    def append(self, sec, b, align=4):
        buf = self.data[sec]
        buf.extend(bytes((-len(buf)) % align))
        offset = len(buf)
        buf.extend(b)
        self.changed.add(sec)
        return sec, offset

    def _typedef(self, fields):
        # Each 64-bit Granny member record is 44 bytes, packed on 4-byte boundaries.
        sec, off = self.append(self.types, bytes(44*(len(fields)+1)))
        for i, (kind, name, ref) in enumerate(fields):
            at = off+44*i
            struct.pack_into("<I", self.data[sec], at, kind)
            self.fix[sec][at+4] = self.append(0, name.encode()+b"\0", 1)
            if ref is not None:
                self.fix[sec][at+12] = ref
        return sec, off

    def _curve_type(self):
        # Same descriptor as the original Firebird 'shiftWheelUp.gr2'.
        header = self._typedef([(12, "Format", None), (12, "Degree", None)])
        real = self._typedef([(10, "Real32", None)])
        return self._typedef([(1, "CurveDataHeader_DaK32fC32f", header), (15, "Padding", None),
                              (3, "Knots", real), (3, "Controls", real)])

    def put(self, name, is_rotation, samples, times):
        ts, pos = self.slots[name]
        dim = 4 if is_rotation else 3
        values = []
        previous = None
        for sample in samples:
            v = tuple(float(x) for x in sample)
            if len(v) != dim or not all(map(math.isfinite, v)):
                raise ValueError(f"Invalid {name} sample")
            if is_rotation:
                magnitude = math.sqrt(sum(x*x for x in v))
                if magnitude < 1e-6:
                    raise ValueError(f"Zero quaternion on {name}")
                v = tuple(x/magnitude for x in v)
                if previous and sum(x*y for x, y in zip(v, previous)) < 0:
                    v = tuple(-x for x in v)
                previous = v
            values.extend(v)
        n = len(times)
        if len(values) != n*dim:
            raise ValueError(f"Phase and pose counts differ on {name}")
        sec, offset = self.append(ts, struct.pack("<BBHIQIQ", 1, 1, 0, n, 0, len(values), 0))
        self.fix[sec][offset+8] = self.append(sec, struct.pack(f"<{n}f", *times))
        self.fix[sec][offset+20] = self.append(sec, struct.pack(f"<{len(values)}f", *values))
        at = pos+(12 if is_rotation else 28)
        self.fix[ts][at] = self.float_curve
        self.fix[ts][at+8] = sec, offset
        self.changed.add(ts)

    def finish(self):
        g = self.g
        raw = bytearray(g.raw)
        for s in sorted(self.changed):
            dest = self.data[s]
            raw.extend(bytes((-len(raw)) % 16))
            data_at = len(raw)
            raw.extend(dest)
            raw.extend(bytes((-len(raw)) % 4))
            fixup_at = len(raw)
            for off, (sec, pos) in sorted(self.fix[s].items()):
                raw.extend(struct.pack("<3I", off, sec, pos))
            header = 32+g.h[3]+44*s
            # Preserve the original mixed-marshalling records and the two
            # endian partition boundaries; both source and target are x64 LE.
            struct.pack_into("<4I", raw, header, 0, data_at, len(dest), len(dest))
            struct.pack_into("<2I", raw, header+28, fixup_at, len(self.fix[s]))
        struct.pack_into("<I", raw, 36, len(raw))
        struct.pack_into("<I", raw, 40, zlib.crc32(raw[104:]))
        return bytes(raw)


def rewrite_clip(template, rotations=None, translations=None, phases=None, *, verify=True):
    """Replace only named Q/P channels with degree-one float curves on shared phases."""
    rotations = rotations or {}
    translations = translations or {}
    if not rotations and not translations:
        return template
    n = len(next(iter((rotations or translations).values())))
    if n < 2:
        raise ValueError("Need at least two distinct samples")
    phases = tuple(float(p) for p in phases) if phases is not None else tuple(i/(n-1) for i in range(n))
    if len(phases) != n or phases[0] != 0 or phases[-1] != 1 or not all(
            a < b for a, b in zip(phases, phases[1:])):
        raise ValueError("Phases must strictly increase from zero to one")
    edit = _Editor(template)
    times = [p*edit.duration for p in phases]
    for curves, rotation in ((rotations, True), (translations, False)):
        for name, samples in curves.items():
            edit.put(name, rotation, samples, times)
    raw = edit.finish()
    if verify:
        with ClipSampler(raw) as clip:
            if set(clip.tracks) != set(edit.slots):
                raise ValueError("Track names changed during export")
            for curves, rotation in ((rotations, True), (translations, False)):
                for name, samples in curves.items():
                    for phase, expected in zip(phases, samples):
                        actual = clip._sample(name, phase, rotation)
                        if rotation:
                            norm = math.sqrt(sum(v*v for v in expected))
                            good = abs(sum(x*y/norm for x,y in zip(actual, expected))) > 1-2e-6
                        else:
                            good = max(abs(x-y) for x,y in zip(actual, expected)) < 1e-4
                        if not good:
                            raise ValueError(f"Written curve failed native validation at {name}, phase {phase}")
    return raw
