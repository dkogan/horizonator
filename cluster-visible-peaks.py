#!/usr/bin/env python3
"""Group the peaks visible in a render into "massifs" by geographic proximity

Reads the tab-separated output of

    ./standalone --list-visible-peaks OUT.txt ...

(one line per visible peak: name, lat, lon, ele_m, range_m), drops peaks
with no letters in their name (OSM had no real name for these; the query
script falls back to printing their altitude instead, e.g. "1583.0"), and
groups the rest into "massifs" via simple distance-based clustering: any
two peaks closer than --cluster-km are considered part of the same group
(transitively, so a chain of nearby peaks can span a wider area than
--cluster-km end to end).

There's no reliable "massif" tag in OpenStreetMap for this area, so these
groups have no real name -- they're just numbered, ordered by mean
distance from the viewer (nearest massif first). Rename/relabel them by
hand afterwards if wanted.

Usage:
    ./cluster-visible-peaks.py visible-peaks.txt [--cluster-km 8]
"""

import argparse
import math
import sys


def haversine_km(lat1, lon1, lat2, lon2):
    R = 6371.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dphi    = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    a = math.sin(dphi/2)**2 + math.cos(p1)*math.cos(p2)*math.sin(dlambda/2)**2
    return 2*R*math.asin(math.sqrt(a))


def has_letter(s):
    return any(c.isalpha() for c in s)


def read_peaks(filename):
    peaks = []
    with open(filename, encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line:
                continue
            name, lat, lon, ele, rng = line.split('\t')
            peaks.append({'name':  name,
                          'lat':   float(lat),
                          'lon':   float(lon),
                          'ele':   float(ele),
                          'range': float(rng)})
    return peaks


# Union-find: transitively chains together any peaks closer than
# cluster_km, same idea as the horizontal-conflict grouping in annotator.c
def cluster(peaks, cluster_km):
    n = len(peaks)
    parent = list(range(n))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[ra] = rb

    for i in range(n):
        for j in range(i+1, n):
            if haversine_km(peaks[i]['lat'], peaks[i]['lon'],
                            peaks[j]['lat'], peaks[j]['lon']) < cluster_km:
                union(i, j)

    groups = {}
    for i in range(n):
        groups.setdefault(find(i), []).append(i)
    return groups


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('infile',
                        help="Output of standalone --list-visible-peaks")
    parser.add_argument('--cluster-km', type=float, default=8.0,
                        help="Peaks closer than this (km) are grouped into the same massif (default 8)")
    args = parser.parse_args()

    peaks = read_peaks(args.infile)
    n_before = len(peaks)
    peaks = [p for p in peaks if has_letter(p['name'])]

    print(f"# {n_before} sommets visibles, {len(peaks)} avec un vrai nom "
          f"(altitude-only exclus)", file=sys.stderr)

    groups = cluster(peaks, args.cluster_km)

    def massif_mean_range(idxs):
        return sum(peaks[i]['range'] for i in idxs) / len(idxs)

    ordered = sorted(groups.values(), key=massif_mean_range)

    for mi, idxs in enumerate(ordered):
        idxs = sorted(idxs, key=lambda i: peaks[i]['range'])
        mean_km = massif_mean_range(idxs) / 1000.
        print(f"\n=== Massif {mi+1} ({len(idxs)} sommets, ~{mean_km:.0f} km) ===")
        for i in idxs:
            p = peaks[i]
            print(f"  {p['name']:<45s} ele={p['ele']:6.0f}m  "
                  f"dist={p['range']/1000:5.1f}km  "
                  f"({p['lat']:.5f}, {p['lon']:.5f})")


if __name__ == '__main__':
    main()
