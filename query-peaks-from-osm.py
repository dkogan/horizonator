#!/usr/bin/python3

r'''Query OSM data for peak annotation

SYNOPSIS

  $ ./query-peaks-from-osm.py 34. -118 100000 > socal-peaks.h
  # generates peak data from lat,lon 34,-118 in a radius of 100000m
'''

import sys
import argparse
import re
import os

def parse_args():

    parser = \
        argparse.ArgumentParser(description = __doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)

    parser.add_argument('lat',
                        type=float,
                        help='''Latitude''')
    parser.add_argument('lon',
                        type=float,
                        help='''Longitude''')
    parser.add_argument('radius',
                        type=float,
                        help='''Radius, in m''')

    args = parser.parse_args()

    return args

args = parse_args()





import requests
import json

api = 'http://overpass-api.de/api/interpreter'

query = f'''
[out:json];

node
 ["natural" = "peak" ]
 (around: {args.radius},{args.lat},{args.lon});

(._;>;);
out;
'''

r = requests.post(api,
                  data = query)
if r.status_code != 200:
    print(f"Overpass api error: {r.status_code} {r.reason=}",
          file = sys.stderr)
    sys.exit(1)

l = json.loads(r.text)


# with open("peaks.json") as f:
#     l = json.load(f)



def name_from_element(t,
                      *,
                      ele):
    keys = ('name:en', 'name', 'name:th')
    for k in keys:
        if k in t:
            return t[k]

    # No name available. Use the elevation
    return f"{ele}m"


try:
    E = l['elements']
except:
    print("Expected 'elements' in the json output", sys.stderr)
    sys.exit(1);

for e in E:
    try:
        t = e['tags']
    except:
        print("Expected 'tags' for each element", sys.stderr)
        sys.exit(1);

    try:             ele = float(t['ele'])
    except:          continue

    name = name_from_element(t, ele = ele)
    if name is None: continue

    print(f'{{ "{name}", {e["lat"]}, {e["lon"]}, {ele} }},')

