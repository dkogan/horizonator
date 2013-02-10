/* -*- c -*- */
#version 110

uniform float view_z;
uniform int   demfileN, demfileW;
uniform int   WDEM;
uniform float sin_view_lon;
uniform float cos_view_lon;
uniform float sin_view_lat;
uniform float cos_view_lat;

uniform float aspect;
varying float red;

const float Rearth = 6371000.0;
const float pi     = 3.14159265358979;
const float znear  = 100.0, zfar = 200000.0;