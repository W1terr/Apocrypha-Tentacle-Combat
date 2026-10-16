"""Samples a Skyrim SE (hk_2010, 64 bit) hkaSplineCompressedAnimation at any time: local transforms of every bone
(B-spline evaluation of the compressed tracks, like Havok's decompressor), and model space positions through the
skeleton's hierarchy.
usage: hkx_sample.py <animation.hkx> <skeleton.hkx> <step seconds> [bone substrings...]
       prints the model space position of the matching bones over the animation"""
import math
import struct
import sys

import numpy as np

from hkx_anim import ROT_ALIGN, ROT_SIZE, align, array, objects, read_quat
from skeleton import read_skeleton


def find_span(degree, value, count, knots):
    """knot span of a B-spline with count control points"""
    if value >= knots[count]:
        return count - 1
    low, high = degree, count
    mid = (low + high) // 2
    while value < knots[mid] or value >= knots[mid + 1]:
        if value < knots[mid]:
            high = mid
        else:
            low = mid
        mid = (low + high) // 2
    return mid


def spline_point(span, degree, frame, knots, points):
    """point of the B-spline at frame: control points span-degree..span weighted by the basis (The NURBS Book A2.2)"""
    n = [1.0] + [0.0] * degree
    left = [0.0] * (degree + 1)
    right = [0.0] * (degree + 1)
    for j in range(1, degree + 1):
        left[j] = frame - knots[span + 1 - j]
        right[j] = knots[span + j] - frame
        saved = 0.0
        for r in range(j):
            denominator = right[r + 1] + left[j - r]
            temp = n[r] / denominator if denominator != 0.0 else 0.0
            n[r] = saved + right[r + 1] * temp
            saved = left[j - r] * temp
        n[j] = saved
    result = np.zeros(len(points[0]))
    for k in range(degree + 1):
        result += np.array(points[span - degree + k]) * n[k]
    return result


def read_vector(buf, pos, types, qtype, frame, default):
    """a position / scale track at the block's local frame; returns (new pos, value)"""
    value = list(default)
    if types & 0xF0:
        n, degree = struct.unpack_from("<HB", buf, pos)
        pos += 3
        knots = list(buf[pos:pos + n + degree + 2])
        pos += n + degree + 2
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
        points = []
        for _ in range(n + 1):
            p = []
            for _, lo, hi in ranges:
                q = buf[pos] if size == 1 else struct.unpack_from("<H", buf, pos)[0]
                p.append(lo + (hi - lo) * q / (255.0 if size == 1 else 65535.0))
                pos += size
            points.append(p)
        pos = align(pos, 4)
        if ranges:
            v = spline_point(find_span(degree, frame, n + 1, knots), degree, frame, knots, points)
            for k, (i, _, _) in enumerate(ranges):
                value[i] = v[k]
    elif types & 0x0F:
        for i in range(3):
            if types & (0x01 << i):
                value[i] = struct.unpack_from("<f", buf, pos)[0]
                pos += 4
    return pos, value


class Animation:
    def __init__(self, path):
        data = open(path, "rb").read()
        d, local, objs = objects(data)
        anim = next(o for o, c in objs.items() if c == "hkaSplineCompressedAnimation")
        binding = next((o for o, c in objs.items() if c == "hkaAnimationBinding"), None)
        at = d + anim
        self.duration = struct.unpack_from("<f", data, at + 0x14)[0]
        self.tracks = struct.unpack_from("<i", data, at + 0x18)[0]
        self.frames, self.blocks, self.frames_per_block, self.mask_size = struct.unpack_from("<4i", data, at + 0x38)
        self.block_duration, _, self.frame_duration = struct.unpack_from("<3f", data, at + 0x48)
        block_off, count = array(data, d, local, anim + 0x58)
        self.block_offsets = list(struct.unpack_from(f"<{count}I", data, d + block_off))
        buf_at, buf_len = array(data, d, local, anim + 0x98)
        self.buf = data[d + buf_at:d + buf_at + buf_len]
        self.mapping = list(range(self.tracks))
        if binding is not None:
            at2, count2 = array(data, d, local, binding + 0x20)
            if at2 is not None and count2 == self.tracks:
                self.mapping = list(struct.unpack_from(f"<{count2}h", data, d + at2))

    def sample(self, time):
        """{bone: (translation, quaternion xyzw, scale)} at a time in seconds"""
        time = min(max(time, 0.0), self.duration)
        block = min(int(time / self.block_duration), self.blocks - 1)
        frame = (time - block * self.block_duration) / self.frame_duration
        buf = self.buf
        start = self.block_offsets[block]
        masks = [buf[start + 4 * i:start + 4 * i + 4] for i in range(self.tracks)]
        pos = start + align(self.mask_size, 4)
        result = {}
        for track, (quant, ptypes, rtypes, stypes) in enumerate(masks):
            pos, translation = read_vector(buf, pos, ptypes, quant & 3, frame, (0.0, 0.0, 0.0))
            pos = align(pos, 4)
            rq = (quant >> 2) & 0xF
            q = np.array([0.0, 0.0, 0.0, 1.0])
            if rtypes & 0xF0:
                n, degree = struct.unpack_from("<HB", buf, pos)
                pos += 3
                knots = list(buf[pos:pos + n + degree + 2])
                pos += n + degree + 2
                pos = align(pos, ROT_ALIGN[rq])
                points = []
                for _ in range(n + 1):
                    points.append(read_quat(buf, pos, rq))
                    pos += ROT_SIZE[rq]
                # neighbouring control points on the same side of the sphere, then the weighted sum, normalized
                for k in range(1, len(points)):
                    if np.dot(points[k], points[k - 1]) < 0.0:
                        points[k] = [-v for v in points[k]]
                q = spline_point(find_span(degree, frame, n + 1, knots), degree, frame, knots, points)
                q /= np.linalg.norm(q)
            elif rtypes & 0x0F:
                pos = align(pos, ROT_ALIGN[rq])
                q = np.array(read_quat(buf, pos, rq))
                pos += ROT_SIZE[rq]
            pos = align(pos, 4)
            pos, scale = read_vector(buf, pos, stypes, (quant >> 6) & 3, frame, (1.0, 1.0, 1.0))
            pos = align(pos, 4)
            result[self.mapping[track]] = (np.array(translation), q, scale[0])
        return result


def quat_matrix(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def model_space(names, parents, pose, local):
    """bone -> (position, rotation matrix) in model space; bones without a track keep the reference pose"""
    world = {}
    for i in range(len(names)):
        t, q, _ = local.get(i, pose[i])
        r = quat_matrix(q)
        p = parents[i]
        if p < 0:
            world[i] = (np.array(t), r)
        else:
            pt, pr = world[p]
            world[i] = (pt + pr @ np.array(t), pr @ r)
    return world


if __name__ == "__main__":
    anim = Animation(sys.argv[1])
    names, parents, pose = read_skeleton(sys.argv[2])
    step = float(sys.argv[3])
    wanted = [i for i, n in enumerate(names) if any(s in n for s in sys.argv[4:])]
    print(f"duration {anim.duration:.3f} s, {anim.frames} frames, {anim.blocks} blocks, {anim.tracks} tracks")
    steps = int(math.floor(anim.duration / step)) + 1
    for k in range(steps):
        t = k * step
        world = model_space(names, parents, pose, anim.sample(t))
        cells = [f"{names[i][4:]}=({world[i][0][0]:6.1f},{world[i][0][1]:6.1f},{world[i][0][2]:6.1f})" for i in wanted]
        print(f"{t:5.2f}  " + "  ".join(cells))
