"""Builds the one severed tentacle half, res/Meshes/TentacleCombat/SeveredTentacle.nif, from scratch, and
src/MeshData.h (the numbers the DLL needs).

The half is the outer part of the vanilla tentacle beyond the start of Bone06: 5 segments of 32 units. It is
generated as a fine, smooth tube that copies the vanilla mesh's thickness along the tentacle, its texture mapping
(DarkTentacle01.dds strip: U around the tube, V along it, same texel density) and vertex color, and gets a closed cut
face whose UVs use the texture's eye (black core, olive gold ring).

Layout, like the vanilla moveable statics with jointed physics (e.g. the Whiterun shop signs):
  root BSFadeNode       first body (capsule) - the game ties the reference to the root's body
    Seg1                skin bone of segment 1 (no body: it moves with the root)
      Seg2 ... Seg5     a body each, ragdoll constraint to the segment before
    SeveredTentacle     skinned mesh, every vertex blended between the two nearest segments
Rest pose = bind pose = straight along +X, so nothing in the file needs to agree with anything else but itself.

usage: python make_severed.py   (needs numpy and nifgen from the Blender Niftools addon, NIFGEN environment variable)
"""
import json
import math
import os
import sys
import warnings
from pathlib import Path

import numpy as np

import gamefiles

NIFGEN = os.environ.get("NIFGEN", str(Path(os.environ["APPDATA"]) / "Blender Foundation/Blender/4.4/scripts/addons/io_scene_niftools/dependencies"))
sys.path.insert(0, NIFGEN)
warnings.filterwarnings("ignore")
from nifgen.formats.nif import NifFile  # noqa: E402
import nifgen.formats.nif as NF  # noqa: E402

# nifgen's writer expects constraint.entities (the blocks a constraint links)
NF.niobject_map["bhkConstraint"].entities = property(lambda self: [self.constraint_info.entity_a, self.constraint_info.entity_b])

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "res" / "Meshes" / "TentacleCombat" / "SeveredTentacle.nif"
VANILLA = r"meshes\dlc02\traps\apotentacletrap\apotentacletrap01.nif"
TEMPLATE = r"meshes\clutter\deadsoldiers\desecratedstormcloak.nif"  # capsule bodies + ragdoll constraint blocks

HAVOK = 69.99124          # game units per havok unit
CUT_BONE = 6              # the vanilla bone the half starts with
SEGMENTS = 5              # Bone06 - Bone10
SEG = 32.0                # segment length (the vanilla bone length)
LENGTH = SEGMENTS * SEG
TIP = 2.0                 # the tip's point beyond the last ring
RING_STEP = 4.0           # a ring every 4 units
SIDES = 12                # vertices around (plus a seam copy for the UVs)

# vanilla tube (from its rings): radius and V along the vanilla chain (Bone01 start = 0)
VANILLA_RADIUS = [(128.6, 8.7), (147.0, 8.3), (165.3, 7.9), (183.6, 7.4), (201.9, 6.8), (220.3, 6.3), (235.5, 5.8),
                  (250.8, 5.3), (266.0, 4.8), (278.3, 4.2), (292.6, 3.7), (306.6, 2.8), (320.1, 1.8)]
V_AT = (128.6, 0.588)     # V at that chain position ...
V_PER_UNIT = -0.002579    # ... and its change per unit along the tentacle
U_PER_UNIT = 0.002300     # U per unit around the tube
COLOR = (128, 124, 113, 255)

# the lower eye in DarkTentacle01.dds (pupil center; radii of the gold ring and just outside it)
EYE_UV = np.array([0.495, 0.679])
EYE_INNER = np.array([0.070, 0.100])
EYE_OUTER = np.array([0.100, 0.142])

CONE = 50.0               # joint limits (degrees)
PLANE = 45.0
TWIST = 10.0


def radius(s):
    """tube radius s units from the cut"""
    chain = (CUT_BONE - 1) * SEG + s
    xs = [c for c, _ in VANILLA_RADIUS]
    rs = [r for _, r in VANILLA_RADIUS]
    return float(np.interp(chain, xs, rs))


def v_coord(s):
    return V_AT[1] + V_PER_UNIT * ((CUT_BONE - 1) * SEG + s - V_AT[0])


def weights(s):
    """[(segment, weight)]: linear between the centers of the two nearest segments"""
    first, last = SEG / 2, (SEGMENTS - 1) * SEG + SEG / 2
    if s <= first:
        return [(0, 1.0)]
    if s >= last:
        return [(SEGMENTS - 1, 1.0)]
    j = int((s - first) // SEG)
    w = (s - first - j * SEG) / SEG
    return [(j, 1.0 - w), (j + 1, w)] if 0.0 < w < 1.0 else [(j + (w >= 1.0), 1.0)]


def unit(v):
    n = np.linalg.norm(v)
    return v / n if n > 1e-9 else v


# -------------------------------------------------------------------------------------------------------------- mesh

class Mesh:
    def __init__(self):
        self.p, self.n, self.t, self.b, self.uv, self.color, self.w = [], [], [], [], [], [], []
        self.tris = []

    def add(self, p, n, t, b, uv, s, color=COLOR):
        """t = the nif 'tangent' (the direction V grows), b = the nif 'bitangent' (the direction U grows)"""
        self.p.append(np.array(p, float))
        self.n.append(unit(np.array(n, float)))
        self.t.append(unit(np.array(t, float)))
        self.b.append(unit(np.array(b, float)))
        self.uv.append(np.array(uv, float))
        self.color.append(color)
        self.w.append(weights(s))
        return len(self.p) - 1

    def face(self, a, b, c, outward):
        """a triangle facing along 'outward'"""
        pa, pb, pc = self.p[a], self.p[b], self.p[c]
        if np.dot(np.cross(pb - pa, pc - pa), outward) < 0:
            b, c = c, b
        self.tris.append((a, b, c))


def build_mesh():
    m = Mesh()
    stations = list(np.arange(0.0, LENGTH + 1e-6, RING_STEP))
    rings = []
    for s in stations:
        r = radius(s)
        slope = (radius(s + 0.5) - radius(s - 0.5)) if s > 0.5 else (radius(1.0) - radius(0.0))
        ring = []
        for k in range(SIDES + 1):  # the last one is the seam copy (U = full width)
            a = 2 * math.pi * k / SIDES
            c, sn = math.cos(a), math.sin(a)
            p = (s, r * c, r * sn)
            n = (-slope, c, sn)
            along = np.array([1.0, slope * c, slope * sn])
            around = np.array([0.0, -sn, c])
            u = (k / SIDES) * 2 * math.pi * r * U_PER_UNIT
            ring.append(m.add(p, n, -along, around, (u, v_coord(s)), s))  # V shrinks toward the tip: tangent = -along
        rings.append((s, ring))
    for (s0, r0), (s1, r1) in zip(rings, rings[1:]):
        for k in range(SIDES):
            mid = (m.p[r0[k]] + m.p[r1[k + 1]]) / 2
            out = mid - np.array([mid[0], 0.0, 0.0])
            m.face(r0[k], r0[k + 1], r1[k + 1], out)
            m.face(r0[k], r1[k + 1], r1[k], out)
    # tip: a point
    s_last, last = rings[-1]
    for k in range(SIDES):
        a = 2 * math.pi * (k + 0.5) / SIDES
        u = ((k + 0.5) / SIDES) * 2 * math.pi * radius(s_last) * U_PER_UNIT
        apex = m.add((LENGTH + TIP, 0.0, 0.0), (1.0, 0.0, 0.0), (-1.0, 0.0, 0.0), (0.0, -math.sin(a), math.cos(a)),
                     (u, v_coord(LENGTH + TIP)), LENGTH)
        m.face(last[k], last[k + 1], apex, np.array([1.0, math.cos(a), math.sin(a)]))

    # cut face at s = 0, facing -X: outer ring = the tube's edge, inner ring a bit smaller and recessed, center
    inward = np.array([-1.0, 0.0, 0.0])
    r0 = radius(0.0)
    outer, inner = [], []
    for k in range(SIDES):
        a = 2 * math.pi * k / SIDES
        d = np.array([math.cos(a), math.sin(a)])
        outer.append(m.add((0.0, r0 * d[0], r0 * d[1]), inward, (0, 0, 1), (0, 1, 0), EYE_UV + d * EYE_OUTER, 0.0, (118, 112, 104, 255)))
        inner.append(m.add((-0.8, 0.72 * r0 * d[0], 0.72 * r0 * d[1]), inward, (0, 0, 1), (0, 1, 0), EYE_UV + d * EYE_INNER, 0.0))
    center = m.add((-1.6, 0.0, 0.0), inward, (0, 0, 1), (0, 1, 0), EYE_UV, 0.0)
    for k in range(SIDES):
        j = (k + 1) % SIDES
        m.face(outer[k], outer[j], inner[j], inward)
        m.face(outer[k], inner[j], inner[k], inward)
        m.face(center, inner[k], inner[j], inward)
    return m


# -------------------------------------------------------------------------------------------------------------- nif

def set_vec(v, values):
    for name, value in zip(("x", "y", "z", "w"), values):
        setattr(v, name, float(value))


def set_identity(m):
    m.m_11, m.m_21, m.m_31 = 1.0, 0.0, 0.0
    m.m_12, m.m_22, m.m_32 = 0.0, 1.0, 0.0
    m.m_13, m.m_23, m.m_33 = 0.0, 0.0, 1.0


def adopt(nif):
    """Blocks taken from the template file still point at it as their context (refs are written through it)."""
    blocks, index, types, type_index = [], {}, [], {}
    nif.blocks = blocks
    for r in nif.roots:
        nif._makeBlockList(r, index, types, type_index)
    seen = set()
    block_ids = {id(b) for b in nif.blocks}

    def visit(obj, top=False):
        if id(obj) in seen or (not top and id(obj) in block_ids):
            return
        seen.add(id(obj))
        if not hasattr(obj, "_context") or not hasattr(type(obj), "_get_filtered_attribute_list"):
            return
        obj._context = nif
        try:
            fields = list(type(obj)._get_filtered_attribute_list(obj, include_abstract=False))
        except TypeError:
            return
        for name, *_ in fields:
            value = getattr(obj, name, None)
            if value is None or isinstance(value, (int, float, str, bytes, np.ndarray)):
                continue
            if type(value).__name__ == "Array" or isinstance(value, (list, tuple)):
                if hasattr(value, "_context"):
                    value._context = nif
                for item in value:
                    if item is not None and not isinstance(item, (int, float, str, bytes, np.ndarray)):
                        visit(item)
            else:
                visit(value)

    for b in list(nif.blocks):
        visit(b, top=True)


def build_nif(mesh: Mesh):
    nif = NifFile.from_path(str(gamefiles.get(VANILLA)))
    root = nif.roots[0]
    shape = next(b for b in nif.blocks if type(b).__name__ == "BSTriShape" and b.name == "Tentacle01")
    vanilla_nodes = {b.name: b for b in nif.blocks if type(b).__name__ == "NiNode"}
    bsx = next(b for b in nif.blocks if type(b).__name__ == "BSXFlags")

    # nodes: reuse five vanilla NiNode blocks as Seg1 - Seg5 (straight chain along +X)
    segs = [vanilla_nodes[f"Bone{CUT_BONE + j:02d}"] for j in range(SEGMENTS)]
    for j, node in enumerate(segs):
        node.name = f"Seg{j + 1}"
        set_identity(node.rotation)
        set_vec(node.translation, (0.0 if j == 0 else SEG, 0.0, 0.0))
        node.scale = 1.0
        node.controller = None
        node.collision_object = None
        node.flags = 14
        node.num_extra_data_list = 0
        node.reset_field("extra_data_list")
        node.num_children = 1 if j + 1 < SEGMENTS else 0
        node.reset_field("children")
        if j + 1 < SEGMENTS:
            node.children[0] = segs[j + 1]

    root.name = OUT.name
    root.flags = 14
    set_identity(root.rotation)
    set_vec(root.translation, (0, 0, 0))
    root.num_extra_data_list = 1
    root.reset_field("extra_data_list")
    root.extra_data_list[0] = bsx
    bsx.integer_data = 202  # havok, complex, dynamic, articulated (like the vanilla physics clutter)
    root.num_children = 2
    root.reset_field("children")
    root.children[0] = segs[0]
    root.children[1] = shape

    shape.name = "SeveredTentacle"
    shape.flags = 14
    set_identity(shape.rotation)
    set_vec(shape.translation, (0, 0, 0))
    shape.scale = 1.0

    # ---- skin: bind pose = the straight rest pose
    skin = shape.skin
    skin.skeleton_root = root
    skin.num_bones = SEGMENTS
    skin.reset_field("bones")
    for j in range(SEGMENTS):
        skin.bones[j] = segs[j]
    data = skin.data
    set_identity(data.skin_transform.rotation)
    set_vec(data.skin_transform.translation, (0, 0, 0))
    data.skin_transform.scale = 1.0
    data.num_bones = SEGMENTS
    data.reset_field("bone_list")
    P = np.array(mesh.p)
    for j in range(SEGMENTS):
        bd = data.bone_list[j]
        set_identity(bd.skin_transform.rotation)
        set_vec(bd.skin_transform.translation, (-j * SEG, 0.0, 0.0))
        bd.skin_transform.scale = 1.0
        mine = [(i, w) for i, ws in enumerate(mesh.w) for b, w in ws if b == j]
        local = np.array([P[i] - np.array([j * SEG, 0, 0]) for i, _ in mine])
        c = local.mean(axis=0)
        set_vec(bd.bounding_sphere.center, c)
        bd.bounding_sphere.radius = float(np.max(np.linalg.norm(local - c, axis=1)))
        bd.num_vertices = len(mine)
        bd.reset_field("vertex_weights")
        for k, (i, w) in enumerate(mine):
            bd.vertex_weights[k].index = i
            bd.vertex_weights[k].weight = float(w)

    def packed(ws):
        bones = [b for b, _ in ws] + [0] * (4 - len(ws))
        values = [w for _, w in ws] + [0.0] * (4 - len(ws))
        return tuple(values), tuple(bones)

    part_block = skin.skin_partition
    count = len(mesh.p)
    part_block.data_size = part_block.vertex_size * count
    part_block.reset_field("vertex_data")
    for i in range(count):
        out = part_block.vertex_data[i]
        set_vec(out.vertex, mesh.p[i])
        set_vec(out.normal, mesh.n[i])
        set_vec(out.tangent, mesh.t[i])
        out.bitangent_x, out.bitangent_y, out.bitangent_z = (float(v) for v in mesh.b[i])
        out.uv.u, out.uv.v = float(mesh.uv[i][0]), float(mesh.uv[i][1])
        out.vertex_colors.r, out.vertex_colors.g, out.vertex_colors.b, out.vertex_colors.a = mesh.color[i]
        values, bones = packed(mesh.w[i])
        out.bone_weights[:] = values
        out.bone_indices[:] = bones
    part = part_block.partitions[0]
    part.num_vertices = count
    part.num_triangles = len(mesh.tris)
    part.num_bones = SEGMENTS
    part.reset_field("bones")
    part.bones[:] = list(range(SEGMENTS))
    part.reset_field("vertex_map")
    part.vertex_map[:] = list(range(count))
    part.reset_field("vertex_weights")
    part.reset_field("bone_indices")
    for i in range(count):
        values, bones = packed(mesh.w[i])
        part.vertex_weights[i] = values
        part.bone_indices[i] = bones
    for field in ("triangles", "triangles_copy"):
        if hasattr(part, field):
            part.reset_field(field)
            for i, tri in enumerate(mesh.tris):
                t = getattr(part, field)[i]
                t.v_1, t.v_2, t.v_3 = (int(x) for x in tri)
    c = P.mean(axis=0)
    set_vec(shape.bounding_sphere.center, c)
    shape.bounding_sphere.radius = float(np.max(np.linalg.norm(P - c, axis=1)))

    # ---- physics: capsule body per segment (the first one on the root), ragdoll constraint to the one before
    bodies = []
    for j in range(SEGMENTS):
        template = NifFile.from_path(str(gamefiles.get(TEMPLATE))).blocks
        capsule = next(b for b in template if type(b).__name__ == "bhkCapsuleShape")
        body = next(b for b in template if type(b).__name__ == "bhkRigidBody" and b.num_constraints == 1
                    and type(b.constraints[0]).__name__ == "bhkRagdollConstraint")
        collision = next(b for b in template if type(b).__name__ == "bhkCollisionObject" and b.body is body)
        constraint = body.constraints[0]

        s_mid = j * SEG + SEG / 2
        r = max(radius(s_mid) * 0.85, 2.0)
        x0 = 0.0
        x1 = SEG if j + 1 < SEGMENTS else SEG + TIP
        p0, p1 = x0 + r, x1 - r
        if p1 <= p0:
            p0 = p1 = (x0 + x1) / 2
        capsule.radius = capsule.radius_1 = capsule.radius_2 = r / HAVOK
        set_vec(capsule.first_point, (p1 / HAVOK, 0, 0))
        set_vec(capsule.second_point, (p0 / HAVOK, 0, 0))

        rh, length = r / HAVOK, (x1 - x0) / HAVOK
        mass = float(min(max(3.0, 600.0 * math.pi * rh * rh * length), 10.0))
        body.shape = capsule
        flags = 0x80 | j  # linked group + depth: jointed neighbors don't collide (as on the vanilla physics corpses)
        for f in (body.havok_filter, body.rigid_body_info.havok_filter):
            f.flags = type(f.flags).from_value(flags) if hasattr(type(f.flags), "from_value") else flags
        info = body.rigid_body_info
        set_vec(info.translation, (j * SEG / HAVOK, 0, 0, 0))
        info.rotation.x, info.rotation.y, info.rotation.z, info.rotation.w = 0.0, 0.0, 0.0, 1.0
        set_vec(info.linear_velocity, (0, 0, 0, 0))
        set_vec(info.angular_velocity, (0, 0, 0, 0))
        it = info.inertia_tensor
        it.m_11, it.m_22, it.m_33 = mass * rh * rh / 2, mass * (3 * rh * rh + length * length) / 12, mass * (3 * rh * rh + length * length) / 12
        set_vec(info.center, ((p0 + p1) / 2 / HAVOK, 0, 0, 0))
        info.mass = mass
        info.linear_damping = 0.1
        info.angular_damping = 0.4
        info.friction = 0.8
        info.restitution = 0.1
        info.penetration_depth = float(min(rh, 0.05))
        info.motion_system = type(info.motion_system).MO_SYS_SPHERE_INERTIA if j == 0 else type(info.motion_system).MO_SYS_SPHERE_STABILIZED
        target = root if j == 0 else segs[j]
        collision.target = target
        target.collision_object = collision
        bodies.append((body, constraint))

    for j, (body, constraint) in enumerate(bodies):
        if j == 0:
            body.num_constraints = 0
            body.reset_field("constraints")
            continue
        ci = constraint.constraint_info
        ci.entity_a = body
        ci.entity_b = bodies[j - 1][0]
        c = constraint.constraint
        for name, value in (("twist_a", (1, 0, 0)), ("plane_a", (0, 1, 0)), ("motor_a", (0, 0, 1)), ("pivot_a", (0, 0, 0)),
                            ("twist_b", (1, 0, 0)), ("plane_b", (0, 1, 0)), ("motor_b", (0, 0, 1)),
                            ("pivot_b", (SEG / HAVOK, 0, 0))):
            set_vec(getattr(c, name), (*value, 0))
        c.cone_max_angle = math.radians(CONE)
        c.plane_min_angle = -math.radians(PLANE)
        c.plane_max_angle = math.radians(PLANE)
        c.twist_min_angle = -math.radians(TWIST)
        c.twist_max_angle = math.radians(TWIST)
        c.max_friction = 0.6
        body.num_constraints = 1
        body.reset_field("constraints")
        body.constraints[0] = constraint

    adopt(nif)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "wb") as stream:
        nif.write(stream)
    return P.min(axis=0), P.max(axis=0)


def write_header(face):
    radii = []
    for b in range(1, 11):
        chain = (b - 1) * SEG + SEG / 2
        radii.append(float(np.interp(chain, [c for c, _ in VANILLA_RADIUS], [r for _, r in VANILLA_RADIUS])))
    text = f"""#pragma once

// Generated by tools/make_severed.py - do not edit.

namespace MeshData
{{
	// the tentacle is always cut at the start of this bone; the half (SeveredTentacle.nif) is the rest of it
	inline constexpr int kCutBone = {CUT_BONE};

	// the stump's last ring (on Bone{CUT_BONE - 1:02d}, bone space): Bone{CUT_BONE:02d} shrinks into it = a flat cut face
	inline constexpr float kFace[3] = {{ {face[0]:.3f}f, {face[1]:.3f}f, {face[2]:.3f}f }};

	// tube radius on Bone01 ... Bone10 (unscaled)
	inline constexpr float kRadius[10] = {{ {", ".join(f"{r:.2f}f" for r in radii)} }};

	// the half in TentacleCombat.esp
	inline constexpr std::uint32_t kSeveredForm = 0x800;
}}
"""
    (ROOT / "src" / "MeshData.h").write_text(text, encoding="utf-8")


def main():
    mesh = build_mesh()
    lo, hi = build_nif(mesh)
    print(f"wrote {OUT}: {len(mesh.p)} vertices, {len(mesh.tris)} triangles, bounds {np.round(lo, 1)} - {np.round(hi, 1)}")
    face = (19.002, -0.872, 0.106)  # vanilla Bone05's last ring (x 19.0): the stump face for a cut at Bone06
    write_header(face)
    bounds = [[int(math.floor(v)) for v in lo], [int(math.ceil(v)) for v in hi]]
    (ROOT / "tools" / "bounds.json").write_text(json.dumps({"SeveredTentacle.nif": bounds}, indent=1))
    print("wrote src/MeshData.h and tools/bounds.json")


if __name__ == "__main__":
    main()
