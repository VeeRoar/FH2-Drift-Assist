"""Read local PC FH6 character/steering data. Translation, quaternion XYZW, scale.

Uses the Forza Mod Tool ACL decoder, matching the CLIPD field indices in
ClipdEndpointCodec.cs. The BSI root and debugger string maps are parsed first
so the chosen animation and all of its track names are checked by identifier.
"""
import ctypes as c
import math
import struct


BSI = bytes.fromhex("cc14a4b1")
CLIPS = 0xB581FE38EDC12453
SKELETONS = 0xF2FDC126D251B7E2
ACL = 0xCBE977A85A54EB5E
STR = 0x704BE0D8FAAFFC58
U32 = 0x4D2BDF193E8526D1
U64 = 0x0582B341E27698CE
def u32(b, p=0):
    return struct.unpack_from("<I", b, p)[0]


def bag(b, at=0):
    size, kind = struct.unpack_from("<IQ", b, at)
    start = at + 12
    if start + size > len(b):
        raise ValueError("BSI bag extends past its container")
    return kind, b[start:start+size], start+size


def fields(block):
    data = []
    at = 0
    while at < len(block):
        size = u32(block, at)
        start = at+4
        if start+size > len(block):
            raise ValueError("BSI field extends past the property bag")
        data.append(block[start:start+size])
        at = start+size
    return data


def root_fields(data):
    assert data[:4] == BSI
    kind, block, end = bag(data, 9 + u32(data, 5))
    assert data[end:end+4] == BSI and not any(data[end+4:])
    return kind, fields(block)


def pairs(block):
    count, at = u32(block), 4
    for _ in range(count):
        key_kind, key, at = bag(block, at)
        value_kind, value, at = bag(block, at)
        assert len(key) in (4,8)
        yield int.from_bytes(key, "little"), key_kind, value_kind, value
    assert at == len(block)


def names(z, path):
    _, tables = root_fields(z.read(path+".debugstringtable"))
    result = {}
    for table in tables:
        for key, key_kind, value_kind, name in pairs(table):
            assert key_kind in (U32,U64) and value_kind == STR
            result[key] = name.decode("utf8")
    return result


def records(z, path, expected):
    kind, tables = root_fields(z.read(path))
    assert kind == expected and len(tables)==2
    debug = names(z,path)
    return [(debug[key], raw) for key, _, _, raw in pairs(tables[1])], debug


def vector(block, fmt):
    count, stride = u32(block), struct.calcsize("<"+fmt)
    assert len(block) == 4+count*stride
    return list(struct.iter_unpack("<"+fmt,block[4:]))


def acl_decode(chunk, decoder):
    field = fields(chunk)
    compressed = field[0][4:]
    assert u32(field[0])==len(compressed) and len(compressed)==u32(compressed)
    assert u32(compressed,8)==0xAC11AC11
    hashes = [h for (h,) in vector(field[6], "Q")]
    samples, duration = u32(field[9]), struct.unpack("<f",field[10])[0]
    if len(hashes)>2048 or samples>4096 or duration<0:
        raise ValueError("Unexpected animation dimensions")
    data = c.create_string_buffer(len(compressed)+16)
    addr = (c.addressof(data)+15)&~15
    c.memmove(addr,compressed,len(compressed))
    func = decoder.acl_decompress_pose_samples
    func.argtypes = [c.c_void_p,c.c_int,c.POINTER(c.c_uint32),c.c_int,c.c_int,c.c_float,
                     c.POINTER(c.c_float),c.c_int]
    tracks = len(hashes)
    raw = (c.c_float*(tracks*samples*10))()
    sig = (c.c_uint32*tracks)(*[i&0xffffffff for i in hashes])
    n=func(addr,len(compressed),sig,tracks,samples,duration,raw,len(raw))
    if n!=len(raw) or not all(math.isfinite(f) for f in raw):
        raise ValueError(f"ACL decoder failed: {n}")
    transforms = {}
    for bone,h in enumerate(hashes):
        frames = []
        for j in range(samples):
            off=(j*tracks+bone)*10
            item=list(raw[off:off+10])
            if abs(sum(v*v for v in item[3:7])-1)>1e-3:
                raise ValueError("Invalid rotation")
            frames.append([round(v,7) for v in item])
        transforms[h]=frames
    return samples,duration,transforms


def read_clip(z,path,clip,decoder):
    entries, debug=records(z,path,CLIPS)
    data=dict(entries)[clip]
    tag=struct.pack("<Q",ACL)
    off=data.find(tag)
    assert off>=4
    _, chunk, _=bag(data,off-4)
    count,duration,tracks=acl_decode(chunk,decoder)
    namespaced={debug[h]:v for h,v in tracks.items()}
    assert len(namespaced)==len(tracks)
    return {"path":path,"clip":clip,"samples":count,"duration":duration,
            "frames_by_bone":namespaced}


def bind_skeleton(z, profile, tracks):
    entries, debug = records(z,"skeleton.skeld",SKELETONS)
    data = fields(dict(entries)[profile])
    labels = [debug[key] for key, in vector(data[0],"Q")]
    assert set(labels) == set(tracks)
    parents = [i for i, in vector(data[1],"h")]
    bind = vector(data[2],"8f")
    assert len(parents)==len(labels)==len(bind)
    bones = {}
    for i,name in enumerate(labels):
        assert -1<=parents[i]<i
        x,y,zv,unit,qx,qy,qz,qw=bind[i]
        assert abs(unit-1)<1e-5 and abs(qx*qx+qy*qy+qz*qz+qw*qw-1)<1e-4
        bones[name]={"parent":labels[parents[i]] if parents[i]>=0 else None,
                     "local":[round(v,7) for v in (x,y,zv,qx,qy,qz,qw,1,1,1)]}
    return {"path":"skeleton.skeld", "profile":profile, "bones":bones}


def wheel_phase(wheel):
    frames=wheel["frames_by_bone"]["boneSteeringWheelSpindle"]
    angles=[]
    for frame in frames:
        assert max(abs(frame[3]),abs(frame[4]))<1e-3
        raw=math.degrees(2*math.atan2(frame[5],frame[6]))
        angles.append(raw if not angles else angles[-1]+((raw-angles[-1]+180)%360-180))
    center=len(angles)//2
    degrees=[round(angle-angles[center],5) for angle in angles]
    assert min(degrees[:-1][i]-degrees[1:][i] for i in range(len(degrees)-1))>=-0.01
    return {"spindle_local_axis":"Z","center_frame":center,"degrees_by_frame":degrees,
            "centered_degrees_positive":"counterclockwise viewed along the wheel from the driver"}

