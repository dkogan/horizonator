#!/usr/bin/env python3

r'''Bakes '.landcover' tiles for horizonator's --materials real-data path

SYNOPSIS

  $ ./build-landcover-tiles.py \
        --corine CLC2018_CLC2018_V2018_20_raster100m.tif \
        45.77294 4.82993 200000

    Downloading ESA WorldCover tile N45E003 (cache miss)...
    Wrote /home/user/.horizonator/landcover/N45E004.landcover
    Wrote /home/user/.horizonator/landcover/N46E004.landcover
    ...

DESCRIPTION

This is a companion tool to horizonator, in the same spirit as
query-peaks-from-osm.py: it prepares data OFFLINE, ahead of time, that the
renderer then reads at runtime (see landcover.h). It is NOT run by
horizonator itself.

For every 1-degree tile that horizonator_dem_init() would load for the
given viewer position and radius (the same SRTM .hgt tile grid: see
dem.c), this script produces a matching 'N45E004.landcover' file: one raw
byte per DEM cell, classifying that point into one of the
horizonator_landcover_class_t codes in landcover.h (0 unknown, 1 forest, 2
grass, 3 rock, 4 snow/ice, 5 water). horizonator_landcover_sample() reads
these back with the exact same indexing dem.c uses for the .hgt files, so
the two grids line up automatically.

Two data sources are combined:

  - ESA WorldCover 10m (2021, v200): downloaded on demand, per 3x3-degree
    tile, from the public S3 bucket (no auth needed). This is the base
    classification for everything.
  - Copernicus CORINE Land Cover (--corine PATH, optional): NOT
    downloaded automatically -- fetch a raster clip from
    https://land.copernicus.eu/en/products/corine-land-cover yourself and
    pass it in. Used ONLY to identify permanent glaciers (CLC code 335):
    WorldCover alone often can't tell a glacier apart from bare rock in a
    given year's satellite pass, and CORINE has a dedicated class for it.
    Every other CORINE class is ignored -- its 100m resolution is too
    coarse to use as a general classification on this project's DEM mesh.
    Without --corine, permanent snow/ice still comes through wherever
    WorldCover's own "Snow and Ice" class says so; only the
    glacier-vs-bare-rock disambiguation is lost.

Requires GDAL's Python bindings (rasterio). On Debian/Ubuntu:
  $ sudo apt install python3-rasterio
'''

import sys
import os
import argparse
import math
import urllib.request


def parse_args():
    parser = argparse.ArgumentParser(description = __doc__,
                                     formatter_class = argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--srtm1',
                        action = 'store_true',
                        help = '''Bake tiles matching the SRTM1 (1-arcsecond,
                        3601x3601) grid instead of the default SRTM3
                        (3-arcsecond, 1201x1201). Must match the --SRTM1
                        setting horizonator itself is run with''')
    parser.add_argument('--out-dir',
                        type = str,
                        default = None,
                        help = '''Where to write the .landcover tiles.
                        Defaults to ~/.horizonator/landcover, matching
                        horizonator's own --dirlandcover default''')
    parser.add_argument('--worldcover-cache-dir',
                        type = str,
                        default = '~/.horizonator/worldcover_cache',
                        help = '''Where to cache downloaded ESA WorldCover
                        3x3-degree source tiles (several hundred MB each).
                        Re-used across runs, and across neighboring
                        --lat/--lon calls that happen to share a tile''')
    parser.add_argument('--corine',
                        type = str,
                        default = None,
                        help = '''Path to a local CORINE Land Cover raster
                        GeoTIFF clip (see DESCRIPTION above for where to get
                        one). If omitted, the glacier/bare-rock
                        disambiguation is skipped; everything else still
                        works''')
    parser.add_argument('lat',
                        type = float,
                        help = 'Viewer latitude, degrees')
    parser.add_argument('lon',
                        type = float,
                        help = 'Viewer longitude, degrees')
    parser.add_argument('radius_m',
                        type = float,
                        help = '''Radius around (lat,lon) to cover, in
                        meters. Should match (or exceed) the --zfar you
                        plan to render with''')

    return parser.parse_args()

args = parse_args()

try:
    import rasterio
    import rasterio.warp
    import rasterio.enums
    import numpy as np
except ModuleNotFoundError:
    print("This tool requires rasterio (GDAL's Python bindings).\n"
          "On Debian/Ubuntu: sudo apt install python3-rasterio",
          file = sys.stderr)
    sys.exit(1)


def expand_user(path):
    return os.path.expanduser(path)

out_dir = expand_user(args.out_dir) if args.out_dir else \
    expand_user('~/.horizonator/landcover')
worldcover_cache_dir = expand_user(args.worldcover_cache_dir)
os.makedirs(out_dir,               exist_ok = True)
os.makedirs(worldcover_cache_dir,  exist_ok = True)

CELLS_PER_DEG = 3600 if args.srtm1 else 1200
# One extra row/col: adjacent tiles overlap by one cell, exactly like the
# SRTM .hgt files this mirrors (see dem.c)
TILE_WIDTH = CELLS_PER_DEG + 1

# WorldCover class codes -> our compact horizonator_landcover_class_t codes
# (landcover.h). Anything not listed here (currently nothing: WorldCover's
# 11 classes are all mapped) falls through to LANDCOVER_UNKNOWN, which just
# means the renderer's procedural fallback kicks in for that point instead
# -- never a crash, never an invalid color
LANDCOVER_UNKNOWN, LANDCOVER_FOREST, LANDCOVER_GRASS, LANDCOVER_ROCK, \
    LANDCOVER_SNOWICE, LANDCOVER_WATER = range(6)

WORLDCOVER_TO_LANDCOVER = {
    10: LANDCOVER_FOREST,  # Tree cover
    20: LANDCOVER_GRASS,   # Shrubland
    30: LANDCOVER_GRASS,   # Grassland
    40: LANDCOVER_GRASS,   # Cropland (no dedicated class; visually closer
                           # to grass than to anything else we have)
    50: LANDCOVER_ROCK,    # Built-up (no dedicated class; buildings read
                           # closer to bare rock/gray than to green)
    60: LANDCOVER_ROCK,    # Bare / sparse vegetation
    70: LANDCOVER_SNOWICE, # Snow and ice
    80: LANDCOVER_WATER,   # Permanent water bodies
    90: LANDCOVER_GRASS,   # Herbaceous wetland
    95: LANDCOVER_FOREST,  # Mangroves (irrelevant in the Alps, but mapped
                           # for completeness)
    100: LANDCOVER_GRASS,  # Moss and lichen
}

CORINE_GLACIER_CODE = 335 # "Glaciers et neiges eternelles"

WORLDCOVER_URL_FMT = \
    'https://esa-worldcover.s3.eu-central-1.amazonaws.com/v200/2021/map/ESA_WorldCover_10m_2021_v200_{tile}_Map.tif'


def worldcover_tile_name(lat, lon):
    # WorldCover tiles are named by their SW corner, on a 3-degree grid
    lat0 = int(math.floor(lat / 3.0)) * 3
    lon0 = int(math.floor(lon / 3.0)) * 3
    ns = 'N' if lat0 >= 0 else 'S'
    we = 'E' if lon0 >= 0 else 'W'
    return f'{ns}{abs(lat0):02d}{we}{abs(lon0):03d}'


def fetch_worldcover_tile(lat, lon):
    tile = worldcover_tile_name(lat, lon)
    path = os.path.join(worldcover_cache_dir, f'ESA_WorldCover_10m_2021_v200_{tile}_Map.tif')
    if not os.path.exists(path):
        url = WORLDCOVER_URL_FMT.format(tile = tile)
        print(f"Downloading ESA WorldCover tile {tile} (cache miss)...")
        try:
            urllib.request.urlretrieve(url, path + '.tmp')
        except Exception as e:
            print(f"  Couldn't download {url}: {e}\n"
                  f"  (this just means points in this area fall back to the "
                  f"procedural classification; not fatal)",
                  file = sys.stderr)
            return None
        os.rename(path + '.tmp', path)
    return path


def output_tile_bounds(lat0_int, lon0_int):
    # Matches dem.c's tile convention exactly: a tile named e.g. N45E004
    # covers [45,46] x [4,5] degrees, TILE_WIDTH samples per side, with a
    # 1-cell overlap shared with each neighboring tile
    return (lon0_int, lat0_int, lon0_int + 1, lat0_int + 1) # (west,south,east,north)


def resample_to_tile_grid(src_path, lat0_int, lon0_int):
    '''Nearest-neighbor resample of a source raster's classification onto
    our output tile's exact grid. Returns a (TILE_WIDTH,TILE_WIDTH) uint8
    array in the same row order horizonator_landcover_sample() expects:
    row 0 = northmost, row TILE_WIDTH-1 = southmost (GDAL's normal
    north-up convention -- no manual flipping needed)'''
    west, south, east, north = output_tile_bounds(lat0_int, lon0_int)
    dst_transform = rasterio.transform.from_bounds(
        west, south, east, north, TILE_WIDTH, TILE_WIDTH)

    with rasterio.open(src_path) as src:
        dst = np.zeros((TILE_WIDTH, TILE_WIDTH), dtype = src.dtypes[0])
        rasterio.warp.reproject(
            source        = rasterio.band(src, 1),
            destination   = dst,
            src_transform = src.transform,
            src_crs       = src.crs,
            dst_transform = dst_transform,
            dst_crs       = 'EPSG:4326',
            # Categorical data: never average/interpolate class codes
            resampling    = rasterio.enums.Resampling.nearest)
    return dst


def build_tile(lat0_int, lon0_int, corine_path):
    wc_path = fetch_worldcover_tile(lat0_int + 0.5, lon0_int + 0.5)

    out = np.full((TILE_WIDTH, TILE_WIDTH), LANDCOVER_UNKNOWN, dtype = np.uint8)
    if wc_path is not None:
        wc_raw = resample_to_tile_grid(wc_path, lat0_int, lon0_int)
        for wc_code, landcover_code in WORLDCOVER_TO_LANDCOVER.items():
            out[wc_raw == wc_code] = landcover_code

    if corine_path is not None:
        corine_raw = resample_to_tile_grid(corine_path, lat0_int, lon0_int)
        out[corine_raw == CORINE_GLACIER_CODE] = LANDCOVER_SNOWICE

    return out


def tiles_covering(lat, lon, radius_m):
    '''Same spirit as horizonator_dem_init()'s bbox, but simpler: we don't
    need to be exact to the cell, just to cover every 1-degree tile that
    might be touched'''
    Rearth = 6371000.0
    dlat_deg = math.degrees(radius_m / Rearth)
    dlon_deg = dlat_deg / max(0.1, math.cos(math.radians(lat))) # guard near the poles

    lat_min = int(math.floor(lat - dlat_deg))
    lat_max = int(math.floor(lat + dlat_deg))
    lon_min = int(math.floor(lon - dlon_deg))
    lon_max = int(math.floor(lon + dlon_deg))

    for lat0 in range(lat_min, lat_max + 1):
        for lon0 in range(lon_min, lon_max + 1):
            yield (lat0, lon0)


def tile_filename(lat0_int, lon0_int):
    ns = 'N' if lat0_int >= 0 else 'S'
    we = 'E' if lon0_int >= 0 else 'W'
    return f'{ns}{abs(lat0_int):02d}{we}{abs(lon0_int):03d}.landcover'


for lat0_int, lon0_int in tiles_covering(args.lat, args.lon, args.radius_m):
    out = build_tile(lat0_int, lon0_int, args.corine)
    path = os.path.join(out_dir, tile_filename(lat0_int, lon0_int))
    with open(path, 'wb') as f:
        f.write(out.tobytes())
    print(f"Wrote {path}")
