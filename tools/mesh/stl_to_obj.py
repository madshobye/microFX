#!/usr/bin/env python3
"""Convert a binary STL to a welded, smooth-normal OBJ."""

import argparse
import math
import struct
from pathlib import Path


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1]*b[2] - a[2]*b[1],
            a[2]*b[0] - a[0]*b[2],
            a[0]*b[1] - a[1]*b[0])


def read_binary_stl(path):
    data = Path(path).read_bytes()
    if len(data) < 84:
        raise ValueError("STL is too short")
    triangle_count = struct.unpack_from("<I", data, 80)[0]
    expected_size = 84 + triangle_count*50
    if len(data) != expected_size:
        raise ValueError("only binary STL input is supported")

    triangles = []
    offset = 84
    for _ in range(triangle_count):
        values = struct.unpack_from("<12fH", data, offset)
        triangles.append(tuple(tuple(values[index:index + 3])
                               for index in (3, 6, 9)))
        offset += 50
    return triangles


def z_up_to_y_up(vertex):
    # A proper rotation (determinant +1), so face winding remains unchanged.
    return (vertex[0], vertex[2], -vertex[1])


def convert(triangles):
    vertices = []
    indices = {}
    faces = []
    for triangle in triangles:
        face = []
        for source_vertex in triangle:
            vertex = z_up_to_y_up(source_vertex)
            if vertex not in indices:
                indices[vertex] = len(vertices)
                vertices.append(vertex)
            face.append(indices[vertex])
        if len(set(face)) == 3:
            faces.append(tuple(face))

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
    return vertices, normals, faces


def write_obj(path, vertices, normals, faces, attribution):
    group = Path(path).stem.replace("-", "_")
    lines = ["# Model converted from Shapr3D STL",
             f"# Original model copyright: {attribution}",
             "# Z-up STL rotated to Y-up OBJ; geometry otherwise unchanged",
             f"g {group}"]
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
    parser.add_argument("--attribution", default="Mads Hobye")
    args = parser.parse_args()
    triangles = read_binary_stl(args.source)
    vertices, normals, faces = convert(triangles)
    write_obj(args.output, vertices, normals, faces, args.attribution)
    print(f"triangles={len(triangles)} vertices={len(vertices)} faces={len(faces)}")


if __name__ == "__main__":
    main()
