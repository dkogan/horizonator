#!/usr/bin/env python3

r'''Bakes '.landcover' tiles for horizonator's --materials real-data path

SYNOPSIS

  $ ./build-landcover-tiles.py \
        --ocsge OCCUPATION_SOL.gpkg \
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
horizonator_landcover_class_t codes in landcover.h. horizonator_landcover_
sample() reads these back with the exact same indexing dem.c uses for the
.hgt files, so the two grids line up automatically.

Three data sources are combined, in priority order (later overrides
earlier, wherever it has data):

  1. ESA WorldCover 10m (2021, v200): downloaded on demand, per
     3x3-degree tile, from the public S3 bucket (no auth needed). This is
     the base classification for everything, worldwide.
  2. IGN OCS GE (--ocsge PATH, optional, France only): NOT downloaded
     automatically -- IGN doesn't (as of this writing) expose a simple
     bulk-downloadable national WFS/API for it, only a per-department
     GeoPackage/shapefile via https://cartes.gouv.fr (search "OCS GE") or
     the Geoplateforme WFS for departments that do have one. Fetch
     whatever local extract covers your area and pass its path here.
     Wherever it has data, OCS GE overrides WorldCover: it's a much finer
     official French classification (CNIG nomenclature, ~1:5000 scale)
     with distinctions WorldCover can't make (e.g. deciduous vs. conifer
     forest -- see OCSGE_CODE_CS_TO_LANDCOVER below). The CODE_CS values
     mapped here are from the CNIG/IGN nomenclature v1.1 (Dec 2014, rev.
     Juin 2016); if your file uses a different OCS GE version, check its
     CODE_CS values match before relying on this mapping.
  3. Copernicus CORINE Land Cover (--corine PATH, optional): also not
     downloaded automatically -- fetch a raster clip from
     https://land.copernicus.eu/en/products/corine-land-cover yourself
     and pass it in. Used ONLY to identify permanent glaciers (CLC code
     335), overriding BOTH sources above: neither WorldCover (whose
     classification can vary by satellite pass/season) nor OCS GE
     (produced region-by-region, glaciers aren't consistently flagged
     everywhere yet) reliably separates glacier ice from bare rock.
     Every other CORINE class is ignored -- its 100m resolution is too
     coarse to use as a general classification on this project's DEM
     mesh. Without --corine, permanent snow/ice still comes through
     wherever WorldCover's or OCS GE's own snow/ice class says so; only
     this last disambiguation pass is skipped.

Requires GDAL's Python bindings (rasterio) for WorldCover/CORINE, and
additionally fiona for --ocsge (vector data). On Debian/Ubuntu:
  $ sudo apt install python3-rasterio python3-fiona
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
    parser.add_argument('--ocsge',
                        type = str,
                        action = 'append',
                        default = [],
                        help = '''Path to a local IGN OCS GE vector extract
                        (GeoPackage or shapefile, CODE_CS attribute; see
                        DESCRIPTION above for where to get one). Overrides
                        WorldCover wherever it has data (France only). If
                        omitted, WorldCover alone classifies France too.
                        Repeat the option to layer several extracts (one per
                        department): a tile can straddle department borders''')
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

# Our compact horizonator_landcover_class_t codes (landcover.h) -- must
# match that enum exactly, including which numbers are WorldCover-only
# (0-5, backwards compatible with tiles baked before OCS GE support
# existed) vs. only ever produced from a finer source like OCS GE (6-8)
(LANDCOVER_UNKNOWN, LANDCOVER_FOREST, LANDCOVER_GRASS, LANDCOVER_ROCK,
 LANDCOVER_SNOWICE, LANDCOVER_WATER,
 LANDCOVER_FOREST_DECIDUOUS, LANDCOVER_FOREST_CONIFER, LANDCOVER_SHRUB) = range(9)

# WorldCover class codes -> our compact codes. Anything not listed here
# (currently nothing: WorldCover's 11 classes are all mapped) falls
# through to LANDCOVER_UNKNOWN, which just means the renderer's
# procedural fallback kicks in for that point instead -- never a crash,
# never an invalid color
WORLDCOVER_TO_LANDCOVER = {
    10: LANDCOVER_FOREST,  # Tree cover (WorldCover can't tell deciduous
                           # from conifer; see LANDCOVER_FOREST_* for the
                           # OCS GE-only distinction)
    20: LANDCOVER_SHRUB,   # Shrubland
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

# IGN OCS GE CODE_CS (couverture du sol) values -> our compact codes, per
# the CNIG/IGN nomenclature v1.1 (Dec 2014, rev. Juin 2016). See
# OCS_GE_Descriptif_de_contenu, section 4.1.2, for the authoritative table
# this was transcribed from -- re-check against it if a newer OCS GE
# version changes these codes
OCSGE_CODE_CS_TO_LANDCOVER = {
    'CS1.1.1.1': LANDCOVER_ROCK,              # Zones baties
    'CS1.1.1.2': LANDCOVER_ROCK,              # Zones non baties (routes, parkings...)
    'CS1.1.2.1': LANDCOVER_ROCK,              # Zones a materiaux mineraux
    'CS1.1.2.2': LANDCOVER_ROCK,              # Zones a autres materiaux composites
    'CS1.2.1':   LANDCOVER_ROCK,              # Sols nus
    'CS1.2.2':   LANDCOVER_WATER,             # Surfaces d'eau
    'CS1.2.3':   LANDCOVER_SNOWICE,           # Neves et glaciers
    'CS2.1.1.1': LANDCOVER_FOREST_DECIDUOUS,  # Peuplements de feuillus
    'CS2.1.1.2': LANDCOVER_FOREST_CONIFER,    # Peuplements de coniferes
    'CS2.1.1.3': LANDCOVER_FOREST,            # Peuplements mixtes
    'CS2.1.2':   LANDCOVER_SHRUB,             # Formations arbustives et sous-arbrisseaux
    'CS2.1.3':   LANDCOVER_GRASS,             # Autres formations ligneuses (vignes...)
    'CS2.2.1':   LANDCOVER_GRASS,             # Formations herbacees
    'CS2.2.2':   LANDCOVER_GRASS,             # Autres formations non ligneuses (lichen, mousse...)
}

# Grid step for the intermediate raster in rasterize_ocsge_to_tile_grid():
# scaled to stay well under our output cell size, so nearest-neighbor
# sampling of it is effectively exact. 25m was tuned for SRTM3's ~65-90m
# cells; left fixed, it's comparable to (not "well under") an SRTM1 cell
# (~22-31m), which aliased OCS GE polygon edges once meshed at that finer
# resolution instead of looking any crisper
OCSGE_INTERMEDIATE_RES_M = 25 * 1200 / CELLS_PER_DEG

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


def rasterize_ocsge_to_tile_grid(ocsge_path, lat0_int, lon0_int):
    '''Rasterizes IGN OCS GE (vector polygons, CODE_CS attribute, usually
    Lambert-93) onto our output tile's exact grid, the same
    (TILE_WIDTH,TILE_WIDTH) uint8 layout resample_to_tile_grid() produces
    for the raster sources. Returns None where the file has nothing
    intersecting this tile (outside France, or just not covered yet)'''
    import fiona
    import rasterio.features

    west, south, east, north = output_tile_bounds(lat0_int, lon0_int)
    dst_transform = rasterio.transform.from_bounds(
        west, south, east, north, TILE_WIDTH, TILE_WIDTH)

    with fiona.open(ocsge_path) as src:
        # Window the read to just this tile (reprojected into the source
        # file's own CRS first): avoids ever loading a whole department's
        # worth of polygons just to bake one 1-degree tile
        src_bbox = rasterio.warp.transform_bounds(
            'EPSG:4326', src.crs, west, south, east, north)

        shapes = []
        for feature in src.filter(bbox = src_bbox):
            # The attribute is 'CODE_CS' in the older shapefile deliveries
            # but 'code_cs' in the OCS GE 2.0 GeoPackages: match either
            props = {k.upper(): v for k, v in feature['properties'].items()}
            landcover_code = OCSGE_CODE_CS_TO_LANDCOVER.get(props.get('CODE_CS'))
            if landcover_code is None:
                # Not in our table (e.g. an unmapped/unexpected CODE_CS
                # value): leave this polygon's area as LANDCOVER_UNKNOWN
                # there, same as if OCS GE simply had no data for it
                continue
            # Geometries stay in the source CRS: reprojecting them one by
            # one costs ~8ms each, and a single 1-degree tile has ~300k
            shapes.append((feature['geometry'], landcover_code))
        src_crs = src.crs

    if not shapes:
        return None

    # Rasterize in the source (projected, meters) CRS on a fine intermediate
    # grid...
    xmin, ymin, xmax, ymax = src_bbox
    ncols = int(math.ceil((xmax - xmin) / OCSGE_INTERMEDIATE_RES_M))
    nrows = int(math.ceil((ymax - ymin) / OCSGE_INTERMEDIATE_RES_M))
    intermediate = rasterio.features.rasterize(
        shapes, out_shape = (nrows, ncols),
        transform = rasterio.transform.from_origin(
            xmin, ymax, OCSGE_INTERMEDIATE_RES_M, OCSGE_INTERMEDIATE_RES_M),
        fill = LANDCOVER_UNKNOWN, dtype = 'uint8')

    # ...then nearest-neighbor sample it at the centers of our output cells,
    # reprojected in one batch
    cols, rows = np.meshgrid(np.arange(TILE_WIDTH), np.arange(TILE_WIDTH))
    lons, lats = rasterio.transform.xy(dst_transform, rows.ravel(), cols.ravel(),
                                       offset = 'center')
    xs, ys = rasterio.warp.transform('EPSG:4326', src_crs, lons, lats)
    ix = np.floor((np.asarray(xs) - xmin) / OCSGE_INTERMEDIATE_RES_M).astype(int)
    iy = np.floor((ymax - np.asarray(ys)) / OCSGE_INTERMEDIATE_RES_M).astype(int)
    ok = (ix >= 0) & (ix < ncols) & (iy >= 0) & (iy < nrows)
    out = np.full(TILE_WIDTH * TILE_WIDTH, LANDCOVER_UNKNOWN, dtype = np.uint8)
    out[ok] = intermediate[iy[ok], ix[ok]]
    return out.reshape(TILE_WIDTH, TILE_WIDTH)


def build_tile(lat0_int, lon0_int, ocsge_paths, corine_path):
    wc_path = fetch_worldcover_tile(lat0_int + 0.5, lon0_int + 0.5)

    # 1. ESA WorldCover: the base layer, worldwide
    out = np.full((TILE_WIDTH, TILE_WIDTH), LANDCOVER_UNKNOWN, dtype = np.uint8)
    if wc_path is not None:
        wc_raw = resample_to_tile_grid(wc_path, lat0_int, lon0_int)
        for wc_code, landcover_code in WORLDCOVER_TO_LANDCOVER.items():
            out[wc_raw == wc_code] = landcover_code

    # 2. IGN OCS GE: overrides WorldCover wherever it has data (France only)
    for ocsge_path in ocsge_paths:
        ocsge_raw = rasterize_ocsge_to_tile_grid(ocsge_path, lat0_int, lon0_int)
        if ocsge_raw is not None:
            covered = ocsge_raw != LANDCOVER_UNKNOWN
            out[covered] = ocsge_raw[covered]

    # 3. CORINE glacier mask: overrides both of the above, since neither
    # reliably separates glacier ice from bare rock (see module docstring)
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
    out = build_tile(lat0_int, lon0_int, args.ocsge, args.corine)
    path = os.path.join(out_dir, tile_filename(lat0_int, lon0_int))
    with open(path, 'wb') as f:
        f.write(out.tobytes())
    print(f"Wrote {path}")
