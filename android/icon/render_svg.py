#!/usr/bin/env python3
"""Renders an SVG file to a square PNG or PAM with the system's librsvg and cairo.

Uses only ctypes (no Python package), for systems with the librsvg and
cairo shared libraries but without the `rsvg-convert` program. A `.pam`
output (8-bit RGB_ALPHA, straight alpha) is what `make_launcher_icon
--foreground` reads.

Usage: render_svg.py <in.svg> <out.png|out.pam> <size in pixels>
"""

import ctypes
import ctypes.util
import sys


class RsvgRectangle(ctypes.Structure):
    """The `RsvgRectangle` of librsvg: a viewport in user units."""

    _fields_ = [
        ("x", ctypes.c_double),
        ("y", ctypes.c_double),
        ("width", ctypes.c_double),
        ("height", ctypes.c_double),
    ]


def _load(name: str, fallback: str) -> ctypes.CDLL:
    """Returns the shared library `name`, found by ctypes or by its soname."""
    return ctypes.CDLL(ctypes.util.find_library(name) or fallback)


def _pam(argb: bytes, size: int, stride: int) -> bytes:
    """Returns a PAM file of cairo's `size` x `size` ARGB32 pixels (native
    little-endian words, premultiplied alpha) as straight-alpha RGBA."""
    out = bytearray(f"P7\nWIDTH {size}\nHEIGHT {size}\nDEPTH 4\nMAXVAL 255\n"
                    "TUPLTYPE RGB_ALPHA\nENDHDR\n".encode())
    for y in range(size):
        row = argb[y * stride:y * stride + size * 4]
        rgba = bytearray(size * 4)
        rgba[0::4] = row[2::4]
        rgba[1::4] = row[1::4]
        rgba[2::4] = row[0::4]
        rgba[3::4] = row[3::4]
        for i in range(0, size * 4, 4):
            a = rgba[i + 3]
            if 0 < a < 255:
                for k in range(3):
                    rgba[i + k] = min(255, (rgba[i + k] * 255 + a // 2) // a)
        out += rgba
    return bytes(out)


def render(svg_path: str, out_path: str, size: int) -> None:
    """Renders `svg_path` scaled into a `size` x `size` picture at `out_path`:
    a PAM file if its name ends with `.pam`, else a PNG.

    Raises `RuntimeError` when librsvg cannot read the SVG or cairo cannot
    write the PNG.
    """
    if sys.byteorder != "little":
        raise RuntimeError("render_svg.py reads cairo's pixels as little-endian words")
    rsvg = _load("rsvg-2", "librsvg-2.so.2")
    cairo = _load("cairo", "libcairo.so.2")
    rsvg.rsvg_handle_new_from_file.restype = ctypes.c_void_p
    rsvg.rsvg_handle_new_from_file.argtypes = [ctypes.c_char_p, ctypes.c_void_p]
    rsvg.rsvg_handle_render_document.restype = ctypes.c_int
    rsvg.rsvg_handle_render_document.argtypes = [
        ctypes.c_void_p,
        ctypes.c_void_p,
        ctypes.POINTER(RsvgRectangle),
        ctypes.c_void_p,
    ]
    cairo.cairo_image_surface_create.restype = ctypes.c_void_p
    cairo.cairo_image_surface_create.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
    cairo.cairo_create.restype = ctypes.c_void_p
    cairo.cairo_create.argtypes = [ctypes.c_void_p]
    cairo.cairo_surface_write_to_png.restype = ctypes.c_int
    cairo.cairo_surface_write_to_png.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    cairo.cairo_image_surface_get_data.restype = ctypes.c_void_p
    cairo.cairo_image_surface_get_data.argtypes = [ctypes.c_void_p]
    cairo.cairo_image_surface_get_stride.restype = ctypes.c_int
    cairo.cairo_image_surface_get_stride.argtypes = [ctypes.c_void_p]
    cairo.cairo_surface_flush.argtypes = [ctypes.c_void_p]
    cairo.cairo_destroy.argtypes = [ctypes.c_void_p]
    cairo.cairo_surface_destroy.argtypes = [ctypes.c_void_p]
    gobject = _load("gobject-2.0", "libgobject-2.0.so.0")
    gobject.g_object_unref.argtypes = [ctypes.c_void_p]

    handle = rsvg.rsvg_handle_new_from_file(svg_path.encode(), None)
    if not handle:
        raise RuntimeError(f"librsvg cannot read {svg_path}")
    cairo_format_argb32 = 0
    surface = cairo.cairo_image_surface_create(cairo_format_argb32, size, size)
    context = cairo.cairo_create(surface)
    try:
        viewport = RsvgRectangle(0, 0, size, size)
        if not rsvg.rsvg_handle_render_document(handle, context, ctypes.byref(viewport), None):
            raise RuntimeError(f"librsvg cannot render {svg_path}")
        if out_path.endswith(".pam"):
            cairo.cairo_surface_flush(surface)
            stride = cairo.cairo_image_surface_get_stride(surface)
            data = ctypes.string_at(cairo.cairo_image_surface_get_data(surface), stride * size)
            with open(out_path, "wb") as f:
                f.write(_pam(data, size, stride))
        elif cairo.cairo_surface_write_to_png(surface, out_path.encode()) != 0:
            raise RuntimeError(f"cairo cannot write {out_path}")
    finally:
        cairo.cairo_destroy(context)
        cairo.cairo_surface_destroy(surface)
        gobject.g_object_unref(handle)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(f"usage: {sys.argv[0]} <in.svg> <out.png|out.pam> <size>")
    render(sys.argv[1], sys.argv[2], int(sys.argv[3]))
