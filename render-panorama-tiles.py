#!/usr/bin/env python3

r'''Renders a wide panorama as a sequence of narrow, memory-bounded tiles

SYNOPSIS

  $ ./render-panorama-tiles.py \
        --width-per-degree 400 --height 2000 \
        --znear 30000 --zfar 190000 --curvature --materials --shading \
        --SRTM1 \
        --tile-deg 10 \
        --out panorama.png \
        45.77294 4.82993 65 175

    Tile 0: az [65.0, 75.0]...
    Tile 1: az [75.0, 85.0]...
    ...
    Wrote panorama.png (4400x2000)

DESCRIPTION

This is a companion tool to horizonator, in the same spirit as
query-peaks-from-osm.py and build-landcover-tiles.py: it drives the
horizonator Python bindings (horizonator-pywrap.c) directly, rather than
running ./standalone once per tile.

Why tiles at all: at high resolution (in particular --SRTM1, 9x the
vertex/triangle count of the default 3" SRTM data) a single render
covering a wide panorama can use more memory than this kind of machine
comfortably has. Splitting the azimuth range into narrow wedges and
rendering them one at a time keeps each tile's mesh memory bounded (see
horizonator_rebuild_mesh() in horizonator.h) -- the same "render a strip
at a time" approach udeuschle.de's panoramas appear to use.

Why ONE process (not one ./standalone invocation per tile, in separate
processes): that would also work, and trivially avoids any memory concern
(the OS reclaims everything when each process exits), but reloads the
DEM/land-cover data and recompiles the GL shaders from scratch for every
single tile. This script instead constructs ONE horizonator object (one
DEM/land-cover load, one shader compile) and calls rebuild_mesh() between
tiles to change the meshed wedge, on the SAME GL context throughout --
measured to keep memory flat across many tiles, unlike looping
construct+destroy (see the horizonator_rebuild_mesh() docstring, and the
commit that introduced it, for why: repeated context creation leaks
memory inside the Mesa/llvmpipe software-rendering driver itself, outside
this project's reach to fix).

Tiles are rendered edge-to-edge with no overlap and no gap: horizonator's
azimuth-to-pixel mapping is exact at the pixel edges (see standalone.c's
--help), so consecutive tiles stitch losslessly.
'''

import sys
import argparse
import numpy as np

sys.path.insert(0, '.')
import horizonator


def parse_args():
    parser = argparse.ArgumentParser(description = __doc__,
                                     formatter_class = argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--width-per-degree',
                        type = float,
                        required = True,
                        help = '''Output pixels per degree of azimuth. The
                        total image width is
                        (az_deg1-az_deg0)*width_per_degree''')
    parser.add_argument('--height',
                        type = int,
                        required = True,
                        help = 'Output image height, in pixels')
    parser.add_argument('--tile-deg',
                        type = float,
                        default = 10.0,
                        help = '''Azimuth width of each tile, in degrees
                        (default 10). Smaller tiles use less memory each,
                        at the cost of more, slower tiles''')
    parser.add_argument('--out',
                        type = str,
                        required = True,
                        help = 'Output PNG path for the assembled panorama')

    parser.add_argument('--SRTM1',       action = 'store_true')
    parser.add_argument('--curvature',   action = 'store_true')
    parser.add_argument('--refraction-k', type = float, default = 0.13)
    parser.add_argument('--shading',     action = 'store_true')
    parser.add_argument('--sun-azimuth', type = float, default = 135.0)
    parser.add_argument('--sun-elevation', type = float, default = 45.0)
    parser.add_argument('--materials',   action = 'store_true')
    parser.add_argument('--znear',       type = float, default = 100.0)
    parser.add_argument('--zfar',        type = float, default = 40000.0)
    parser.add_argument('--znear-color', type = float, default = None)
    parser.add_argument('--zfar-color',  type = float, default = None)

    parser.add_argument('lat',       type = float)
    parser.add_argument('lon',       type = float)
    parser.add_argument('az_deg0',   type = float)
    parser.add_argument('az_deg1',   type = float)

    return parser.parse_args()

args = parse_args()

znear_color = args.znear_color if args.znear_color is not None else args.znear
zfar_color  = args.zfar_color  if args.zfar_color  is not None else args.zfar

Ntiles = max(1, round((args.az_deg1 - args.az_deg0) / args.tile_deg))
tile_edges = np.linspace(args.az_deg0, args.az_deg1, Ntiles+1)

width_total = round((args.az_deg1 - args.az_deg0) * args.width_per_degree)

# One object, one DEM/land-cover load, one shader compile, for the whole
# job: restrict_mesh_azimuth bounds each tile's mesh memory;
# rebuild_mesh() (below) changes the wedge between tiles without ever
# recreating the GL context
h = horizonator.horizonator(args.lat, args.lon,
                            width_total, args.height,
                            SRTM1 = args.SRTM1,
                            render_radius_m = args.zfar,
                            restrict_mesh_azimuth = True,
                            mesh_az_deg0 = tile_edges[0],
                            mesh_az_deg1 = tile_edges[1])

h.set_curvature(args.curvature, args.refraction_k)
h.set_sun(args.shading, args.sun_azimuth, args.sun_elevation)
h.set_materials(args.materials)

tile_images = []
for i in range(Ntiles):
    az0 = tile_edges[i]
    az1 = tile_edges[i+1]
    print(f"Tile {i}: az [{az0:.1f}, {az1:.1f}]...")

    h.rebuild_mesh(az0, az1)
    # return_range=False: render() then returns just the image, not a
    # (image,ranges) tuple
    image = h.render(az0, az1,
                     znear = args.znear, zfar = args.zfar,
                     znear_color = znear_color, zfar_color = zfar_color,
                     return_range = False)
    tile_images.append(image)

panorama_bgr = np.concatenate(tile_images, axis = 1)
# horizonator returns BGR (see horizonator_render_offscreen() in
# horizonator.h); flip to RGB for a standard PNG
panorama_rgb = panorama_bgr[:, :, ::-1]

import rasterio
h_px, w_px, _ = panorama_rgb.shape
with rasterio.open(args.out, 'w', driver = 'PNG',
                   height = h_px, width = w_px, count = 3, dtype = 'uint8') as dst:
    dst.write(np.transpose(panorama_rgb, (2, 0, 1)))
print(f"Wrote {args.out} ({w_px}x{h_px})")
