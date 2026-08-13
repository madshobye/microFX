#!/usr/bin/env python3
"""Weld the mirrored halves of the Rhino head OBJ into one smooth mesh."""

import math
import sys
from pathlib import Path


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1]*b[2] - a[2]*b[1],
            a[2]*b[0] - a[0]*b[2],
            a[0]*b[1] - a[1]*b[0])


def main(source_name, output_name):
    source = Path(source_name)
    vertices = []
    faces = []
    for line in source.read_text().splitlines():
        if line.startswith("v "):
            vertices.append(tuple(map(float, line.split()[1:4])))
        elif line.startswith("f "):
            faces.append([int(token.split("/")[0]) - 1 for token in line.split()[1:4]])

    half = len(vertices)//2
    remap = list(range(len(vertices)))
    welded = 0
    for left_index, left in enumerate(vertices[:half]):
        if abs(left[0]) > 0.20:
            continue
        best = None
        for right_index, right in enumerate(vertices[half:], half):
            if abs(right[0]) > 0.20:
                continue
            distance = math.dist(left, right)
            if distance <= 0.20 and (best is None or distance < best[0]):
                best = (distance, right_index)
        if best is not None:
            right_index = best[1]
            right = vertices[right_index]
            vertices[left_index] = tuple((a + b)*0.5 for a, b in zip(left, right))
            remap[right_index] = left_index
            welded += 1

    kept = [i for i in range(len(vertices)) if remap[i] == i]
    compact = {old: new for new, old in enumerate(kept)}
    for i in range(len(remap)):
        remap[i] = compact[remap[i]]
    vertices = [vertices[i] for i in kept]

    clean_faces = []
    for face in faces:
        mapped = [remap[i] for i in face]
        if len(set(mapped)) == 3:
            clean_faces.append(mapped)

    normals = [[0.0, 0.0, 0.0] for _ in vertices]
    for a, b, c in clean_faces:
        normal = cross(sub(vertices[b], vertices[a]), sub(vertices[c], vertices[a]))
        for index in (a, b, c):
            normals[index][0] += normal[0]
            normals[index][1] += normal[1]
            normals[index][2] += normal[2]
    for i, normal in enumerate(normals):
        length = math.sqrt(sum(value*value for value in normal)) or 1.0
        normals[i] = tuple(value/length for value in normal)

    lines = ["# Welded from mirrored Rhino halves", "g head"]
    lines += ["v %.7f %.7f %.7f" % vertex for vertex in vertices]
    lines += ["vn %.7f %.7f %.7f" % normal for normal in normals]
    lines += ["f %d//%d %d//%d %d//%d" %
              (a + 1, a + 1, b + 1, b + 1, c + 1, c + 1)
              for a, b, c in clean_faces]
    Path(output_name).write_text("\n".join(lines) + "\n")
    print(f"welded={welded} vertices={len(vertices)} faces={len(clean_faces)}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: weld_obj.py INPUT.obj OUTPUT.obj")
    main(sys.argv[1], sys.argv[2])
