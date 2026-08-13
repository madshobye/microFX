#!/usr/bin/env python3
"""Create a reproducible low-poly OBJ using topology-safe vertex clustering."""

import argparse
import math
from pathlib import Path


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1]*b[2] - a[2]*b[1],
            a[2]*b[0] - a[0]*b[2],
            a[0]*b[1] - a[1]*b[0])


def read_obj(path):
    vertices, faces = [], []
    for line in Path(path).read_text().splitlines():
        if line.startswith("v "):
            vertices.append(tuple(map(float, line.split()[1:4])))
        elif line.startswith("f "):
            indices = [int(token.split("/")[0]) - 1 for token in line.split()[1:]]
            for i in range(1, len(indices) - 1):
                faces.append((indices[0], indices[i], indices[i + 1]))
    return vertices, faces


def cluster(vertices, faces, divisions):
    low = [min(v[axis] for v in vertices) for axis in range(3)]
    high = [max(v[axis] for v in vertices) for axis in range(3)]
    extent = [max(high[axis] - low[axis], 1e-9) for axis in range(3)]
    buckets = {}
    for index, vertex in enumerate(vertices):
        key = tuple(min(divisions - 1, int((vertex[axis] - low[axis]) /
                                          extent[axis]*divisions)) for axis in range(3))
        buckets.setdefault(key, []).append(index)

    compact_vertices, remap = [], [0]*len(vertices)
    for members in buckets.values():
        new_index = len(compact_vertices)
        compact_vertices.append(tuple(sum(vertices[i][axis] for i in members)/len(members)
                                      for axis in range(3)))
        for old_index in members:
            remap[old_index] = new_index

    compact_faces, seen = [], set()
    for face in faces:
        mapped = tuple(remap[index] for index in face)
        if len(set(mapped)) != 3:
            continue
        canonical = tuple(sorted(mapped))
        if canonical in seen:
            continue
        seen.add(canonical)
        compact_faces.append(mapped)
    return compact_vertices, compact_faces


def write_obj(path, vertices, faces, source, divisions):
    normals = [[0.0, 0.0, 0.0] for _ in vertices]
    for a, b, c in faces:
        normal = cross(sub(vertices[b], vertices[a]), sub(vertices[c], vertices[a]))
        for index in (a, b, c):
            normals[index][0] += normal[0]
            normals[index][1] += normal[1]
            normals[index][2] += normal[2]
    for index, normal in enumerate(normals):
        length = math.sqrt(sum(value*value for value in normal)) or 1.0
        normals[index] = tuple(value/length for value in normal)

    lines = [f"# Low-poly model from {source.name}",
             f"# vertex clustering divisions={divisions}", "g head_low"]
    lines += ["v %.7f %.7f %.7f" % vertex for vertex in vertices]
    lines += ["vn %.7f %.7f %.7f" % normal for normal in normals]
    lines += ["f %d//%d %d//%d %d//%d" %
              (a + 1, a + 1, b + 1, b + 1, c + 1, c + 1)
              for a, b, c in faces]
    Path(path).write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--target-faces", type=int, default=650)
    args = parser.parse_args()
    vertices, faces = read_obj(args.source)

    candidates = []
    for divisions in range(4, 65):
        reduced_vertices, reduced_faces = cluster(vertices, faces, divisions)
        candidates.append((abs(len(reduced_faces) - args.target_faces), divisions,
                           reduced_vertices, reduced_faces))
    _, divisions, reduced_vertices, reduced_faces = min(candidates, key=lambda item: item[0])
    write_obj(args.output, reduced_vertices, reduced_faces, args.source, divisions)
    print(f"source_vertices={len(vertices)} source_faces={len(faces)} "
          f"vertices={len(reduced_vertices)} faces={len(reduced_faces)} "
          f"divisions={divisions}")


if __name__ == "__main__":
    main()
