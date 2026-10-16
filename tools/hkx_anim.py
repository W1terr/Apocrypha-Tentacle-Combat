"""Reads the first frame of a Skyrim SE (hk_2010, 64 bit) hkaSplineCompressedAnimation: the local rotation of every
bone at t = 0 (for a clamped B-spline that's the first control point, so no curve evaluation is needed).
usage: hkx_anim.py <animation.hkx> <skeleton.hkx> [bone substrings...]"""
import math
import struct
import sys

from skeleton import read_skeleton


def sections(data):
    out = {}
    for i in range(struct.unpack_from("<i", data, 0x14)[0]):
        base = 0x40 + i * 0x30
        out[data[base:base + 20].split(b"\0")[0].decode()] = struct.unpack_from("<7i", data, base + 0x14)
    return out


def objects(data):
    secs = sections(data)
    classes = secs["__classnames__"][0]
    d, local_fix, global_fix, virtual_fix, exports, _, _ = secs["__data__"]
    local = {}
    p = d + local_fix
    while p + 8 <= d + global_fix:
        src, dst = struct.unpack_from("<2i", data, p)
        p += 8
        if src != -1:
            local[src] = dst
    objs = {}
    p = d + virtual_fix
    while p + 12 <= d + exports:
        obj, _, name = struct.unpack_from("<3i", data, p)
        p += 12
        if obj != -1:
            objs[obj] = data[classes + name:].split(b"\0")[0].decode()
    return d, local, objs


def array(data, d, local, field):
    """(data offset, count) of an hkArray member"""
    return local.get(field), struct.unpack_from("<i", data, d + field + 8)[0]


def align(pos, n):
    return (pos + n - 1) // n * n


ROT_SIZE = {0: 4, 1: 5, 2: 6, 3: 3, 4: 2, 5: 16}
ROT_ALIGN = {0: 4, 1: 1, 2: 2, 3: 1, 4: 2, 5: 4}


def quat40(b):
    c = int.from_bytes(b, "little")
    mask = (1 << 12) - 1
    frac = 0.000345436
    vals = [((c >> (12 * i)) & mask) - 2047 for i in range(3)]
    vals = [v * frac for v in vals]
    shift = (c >> 36) & 3
    invert = (c >> 38) & 1
    w = math.sqrt(max(0.0, 1.0 - sum(v * v for v in vals)))
    if invert:
        w = -w
    q = vals[:]
    q.insert(shift, w)
    return q  # x, y, z, w


def quat48(b):
    x, y, z = struct.unpack("<3H", b)
    mask = 0x7FFF
    frac = 0.000043161
    shift = ((y >> 14) & 2) | ((x >> 15) & 1)
    invert = (z >> 15) & 1
    vals = [((v & mask) - (mask >> 1)) * frac for v in (x, y, z)]
    w = math.sqrt(max(0.0, 1.0 - sum(v * v for v in vals)))
    if invert:
        w = -w
    q = vals[:]
    q.insert(shift, w)
    return q


def read_quat(buf, pos, qtype):
    size = ROT_SIZE[qtype]
    raw = buf[pos:pos + size]
    if qtype == 1:
        return quat40(raw)
    if qtype == 2:
        return quat48(raw)
    if qtype == 5:
        return list(struct.unpack("<4f", raw))
    raise ValueError(f"rotation quantization {qtype} not supported")


def skip_vector(buf, pos, types, qtype):
    """reads a position / scale track; returns (new pos, first value or None)"""
    spline = types & 0xF0
    static = types & 0x0F
    value = [0.0, 0.0, 0.0]
    if spline:
        n, degree = struct.unpack_from("<HB", buf, pos)
        pos += 3 + n + degree + 2
        pos = align(pos, 4)
        ranges = []
        for i in range(3):
            if types & (0x10 << i):
                lo, hi = struct.unpack_from("<2f", buf, pos)
                pos += 8
                ranges.append((i, lo, hi))
            elif types & (0x01 << i):
                value[i] = struct.unpack_from("<f", buf, pos)[0]
                pos += 4
        size = 1 if qtype == 0 else 2
        if size == 2:
            pos = align(pos, 2)
        for k in range(n + 1):
            for i, lo, hi in ranges:
                q = buf[pos] if size == 1 else struct.unpack_from("<H", buf, pos)[0]
                if k == 0:
                    value[i] = lo + (hi - lo) * q / (255.0 if size == 1 else 65535.0)
                pos += size
        pos = align(pos, 4)
    elif static:
        for i in range(3):
            if types & (0x01 << i):
                value[i] = struct.unpack_from("<f", buf, pos)[0]
                pos += 4
    return pos, value


def first_frame(path):
    data = open(path, "rb").read()
    d, local, objs = objects(data)
    anim = next(o for o, c in objs.items() if c == "hkaSplineCompressedAnimation")
    binding = next((o for o, c in objs.items() if c == "hkaAnimationBinding"), None)
    tracks = struct.unpack_from("<i", data, d + anim + 0x18)[0]
    float_tracks = struct.unpack_from("<i", data, d + anim + 0x1C)[0]
    block_off, _ = array(data, d, local, anim + 0x58)
    transform_off, transform_count = array(data, d, local, anim + 0x78)
    mask_size = struct.unpack_from("<i", data, d + anim + 0x44)[0]
    buf_at, buf_len = array(data, d, local, anim + 0x98)
    buf = data[d + buf_at:d + buf_at + buf_len]
    block0 = struct.unpack_from("<I", data, d + block_off)[0]
    # per block offset of the track data; Skyrim's files leave it empty: the data follows the masks
    tdata = struct.unpack_from("<I", data, d + transform_off)[0] if transform_count else align(mask_size, 4)
    masks = [buf[block0 + 4 * i:block0 + 4 * i + 4] for i in range(tracks)]
    pos = block0 + tdata
    rotations = []
    for quant, ptypes, rtypes, stypes in masks:
        pos, _ = skip_vector(buf, pos, ptypes, quant & 3)
        pos = align(pos, 4)
        rq = (quant >> 2) & 0xF
        q = [0.0, 0.0, 0.0, 1.0]
        if rtypes & 0xF0:
            n, degree = struct.unpack_from("<HB", buf, pos)
            pos += 3 + n + degree + 2
            pos = align(pos, ROT_ALIGN[rq])
            q = read_quat(buf, pos, rq)
            pos += ROT_SIZE[rq] * (n + 1)
        elif rtypes & 0x0F:
            pos = align(pos, ROT_ALIGN[rq])
            q = read_quat(buf, pos, rq)
            pos += ROT_SIZE[rq]
        pos = align(pos, 4)
        pos, _ = skip_vector(buf, pos, stypes, (quant >> 6) & 3)
        pos = align(pos, 4)
        rotations.append(q)
    mapping = list(range(tracks))
    if binding is not None:
        at, count = array(data, d, local, binding + 0x20)
        if at is not None and count == tracks:
            mapping = list(struct.unpack_from(f"<{count}h", data, d + at))
    return {mapping[t]: rotations[t] for t in range(tracks)}, float_tracks


if __name__ == "__main__":
    rots, _ = first_frame(sys.argv[1])
    names, parents, pose = read_skeleton(sys.argv[2])
    for bone, q in sorted(rots.items()):
        name = names[bone] if bone < len(names) else f"#{bone}"
        if len(sys.argv) < 4 or any(s in name for s in sys.argv[3:]):
            norm = math.sqrt(sum(v * v for v in q))
            print(f"{bone:3} {name:30} q=({q[0]: .4f},{q[1]: .4f},{q[2]: .4f},{q[3]: .4f}) |q|={norm:.3f}")
