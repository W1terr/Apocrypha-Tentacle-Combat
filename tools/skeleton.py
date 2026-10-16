"""Reads an hkaSkeleton from a Skyrim SE (hk_2010, 64 bit) packfile: bone names, parents, reference pose.
usage: skeleton.py <skeleton.hkx> [bone substrings...]  -> model space positions of matching bones"""
import struct
import sys

import numpy as np


def read_skeleton(path):
    data = open(path, "rb").read()
    sections = {}
    for i in range(struct.unpack_from("<i", data, 0x14)[0]):
        base = 0x40 + i * 0x30
        sections[data[base:base + 20].split(b"\0")[0].decode()] = struct.unpack_from("<7i", data, base + 0x14)
    classes = sections["__classnames__"][0]
    d, local_fix, global_fix, virtual_fix, exports, _, _ = sections["__data__"]
    local = {}
    p = d + local_fix
    while p + 8 <= d + global_fix:
        src, dst = struct.unpack_from("<2i", data, p)
        p += 8
        if src != -1:
            local[src] = dst
    p = d + virtual_fix
    while p + 12 <= d + exports:
        obj, _, name = struct.unpack_from("<3i", data, p)
        p += 12
        if obj != -1 and data[classes + name:].split(b"\0")[0] == b"hkaSkeleton":
            break
    else:
        raise ValueError("no hkaSkeleton")

    def array(field):
        return local.get(obj + field), struct.unpack_from("<i", data, d + obj + field + 8)[0]

    parents_at, count = array(0x18)
    parents = list(struct.unpack_from(f"<{count}h", data, d + parents_at))
    bones_at, _ = array(0x28)
    names = [data[d + local[bones_at + 16 * i]:].split(b"\0")[0].decode() for i in range(count)]
    pose_at, _ = array(0x38)
    pose = []
    for i in range(count):
        v = struct.unpack_from("<12f", data, d + pose_at + 48 * i)
        pose.append((np.array(v[0:3]), np.array(v[4:8]), v[8]))  # translation, quaternion xyzw, scale
    return names, parents, pose


def quat_matrix(q):
    x, y, z, w = q
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def model_space(parents, pose):
    out = []
    for i, (t, q, s) in enumerate(pose):
        r = quat_matrix(q)
        if parents[i] < 0:
            out.append((t.copy(), r))
        else:
            pt, pr = out[parents[i]]
            out.append((pt + pr @ t, pr @ r))
    return out


if __name__ == "__main__":
    names, parents, pose = read_skeleton(sys.argv[1])
    world = model_space(parents, pose)
    for i, n in enumerate(names):
        if len(sys.argv) < 3 or any(s in n for s in sys.argv[2:]):
            t, r = world[i]
            parent = names[parents[i]] if parents[i] >= 0 else "-"
            print(f"{i:3} {n:32} parent={parent:28} pos=({t[0]:7.2f},{t[1]:7.2f},{t[2]:7.2f})")
