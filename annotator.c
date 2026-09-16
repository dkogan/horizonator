#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <float.h>
#include <cairo-pdf.h>
#include <cairo-svg.h>
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>

#include "util.h"
#include "annotator.h"
#include "horizonator.h"


#define MIN_MARKER_DIST 500.0

#define FUZZ_RANGE   500.

// The vertical pixel search radius used to find a POI's true rendered
// position isn't a fixed pixel count: the real positional uncertainty
// (OSM coordinate precision, DEM sampling) is an ANGULAR one, a small
// fraction of a degree. At low resolution a handful of pixels happens to
// cover that; at high resolution the same few pixels cover only a tiny
// sliver of a degree, and otherwise-valid matches silently fail. So a
// fixed angular tolerance is converted to a pixel count from the actual
// image resolution (see fuzz_pixel_y() below) instead
#define ANGULAR_FUZZ_DEG 0.05


#define LABEL_CROSSHAIR_R 3
#define TEXT_MARGIN       2

// Label text in black, leader line in a soft "almond green" -- readable on
// the white background, and the line doesn't compete visually with the text
#define LABEL_TEXT_R 0.0
#define LABEL_TEXT_G 0.0
#define LABEL_TEXT_B 0.0
#define LABEL_LINE_R 0.576
#define LABEL_LINE_G 0.773
#define LABEL_LINE_B 0.447

// Label text is drawn rotated by this angle: first letter at the bottom,
// last letter at the top (a vertical column of text, reading bottom-up,
// as if tilting your head to the left). cairo_rotate() rotates towards
// the (downward-pointing) +y axis for positive angles, so a negative
// angle here goes up, as wanted
#define LABEL_ROTATION_RAD (-M_PI/2.0)

// Every kept label's last (topmost) character sits this many pixels below
// the top edge of the canvas (not the rendered/mesh area -- the canvas)
#define LABEL_TOP_MARGIN_PX 30.0

// Two labels whose horizontal (screen-x) positions are closer than this
// are considered to conflict, and only the higher-elevation one of the
// group is kept. Since the label text is vertical, its own on-screen
// footprint is about one font-height wide; this multiplies that by a
// safety margin
#define LABEL_CONFLICT_GAP_FACTOR 1.5

static const double POINTS_PER_INCH = 72.;
static const double PIXELS_PER_INCH = 300.;
static const double CAIRO_SCALE     = POINTS_PER_INCH / PIXELS_PER_INCH;

static
double string_width(cairo_t *cr,
                    const char* s)
{
  // WARNING: users of tihs are assuming that x_bearing == 0 (i.e. that
  // there's no margin on the left of the text). But there could be
  cairo_text_extents_t extents;
  cairo_text_extents(cr, s,
                     &extents);
  return extents.x_bearing+extents.width;
}




// compares two visible_poi_t by their screen-x position
static int compar_visible_poi_x( const void* _a, const void* _b )
{
  const visible_poi_t* a = (const visible_poi_t*)_a;
  const visible_poi_t* b = (const visible_poi_t*)_b;

  if( a->x < b->x )
    return -1;
  else
    return 1;
}

static
void draw_label( cairo_t* cr,
                 // the peak's own screen position
                 double x, double y,
                 double lat, double lon,
                 const char* name )
{
  // The text is vertical (LABEL_ROTATION_RAD), growing from its first
  // (bottom) character up to its last (top) character. We want the LAST
  // character LABEL_TOP_MARGIN_PX below the top of the canvas, so the
  // anchor (first character, where the leader line ends) is that margin
  // plus the full string length below the canvas top
  const double string_w    = string_width(cr, name);
  const double text_start_y = LABEL_TOP_MARGIN_PX + string_w;

  cairo_set_source_rgb(cr, LABEL_LINE_R, LABEL_LINE_G, LABEL_LINE_B);

  cairo_move_to(cr, x-LABEL_CROSSHAIR_R, y);
  cairo_rel_line_to(cr, 2*LABEL_CROSSHAIR_R, 0);

  cairo_move_to(cr, x, y+LABEL_CROSSHAIR_R);
  cairo_line_to(cr, x, text_start_y);

  cairo_stroke(cr);

  // The label text, rotated LABEL_ROTATION_RAD: anchored exactly where the
  // leader line ends, and drawn along the rotated axis from there, so the
  // anchor point doesn't move as the rotation changes. cairo's "current
  // point" is a user-space coordinate, so to rotate around a specific
  // device-space point we translate there FIRST, rotate, then draw at the
  // new (local) origin
  cairo_set_source_rgb(cr, LABEL_TEXT_R, LABEL_TEXT_G, LABEL_TEXT_B);

  cairo_save(cr);
  cairo_translate(cr, x, text_start_y);
  cairo_rotate(cr, LABEL_ROTATION_RAD);
  cairo_move_to(cr, 0, 0);

  char url[256];
  bool url_valid =
    (snprintf(url, sizeof(url),
              "uri='https://caltopo.com/map.html#ll=%f,%f&z=15&b=mbt'",
              lat, lon)
     < (int)sizeof(url));
  if(url_valid) cairo_tag_begin (cr, CAIRO_TAG_LINK, url);
  cairo_show_text(cr, name);
  if(url_valid) cairo_tag_end (cr, CAIRO_TAG_LINK);

  cairo_restore(cr);
}


#define TRY(x) do {                             \
    if(!(x))                                    \
    {                                           \
      MSG( "ERROR: " #x " failed");             \
      goto done;                                \
    }                                           \
  } while(0)



// My image stores a pixel in 24 bits, while cairo expects 32 bits (despite the
// name of the format being CAIRO_FORMAT_RGB24). I convert with this function
//
// Dense storage assumed in both input and output.
static
bool RGB32_from_BGR24(// output
                      uint8_t* image_rgb32,
                      // input
                      const uint8_t* image_bgr24,
                      const int width,
                      const int height)
{
  bool result = false;
  int stride_rgb32 = width*4;
  int stride_bgr24 = width*3;

  struct SwsContext* sws_ctx = NULL;
  TRY(NULL != (sws_ctx =
               sws_getContext(width, height, AV_PIX_FMT_BGR24,
                              width, height, AV_PIX_FMT_RGB32,
                              SWS_POINT, NULL, NULL, NULL)));

  sws_scale(sws_ctx,
            &image_bgr24, &stride_bgr24, 0, height,
            &image_rgb32, &stride_rgb32);
  result = true;

done:
  if(sws_ctx != NULL)
    sws_freeContext(sws_ctx);
  return result;
}

int find_visible_pois(// output
                      visible_poi_t* visible,

                      // input
                      const float* range_image,
                      const int width,
                      const int height,
                      const int cut_off_bottom_px,

                      const poi_t* pois,
                      const int Npois,
                      const double lat,
                      const double lon,
                      const double az_deg0,
                      const double az_deg1,
                      const double ele_m,

                      const bool   curvature_enabled,
                      const double refraction_k,

                      const double max_marker_dist_m)
{
  const int height_out = height - cut_off_bottom_px;
  const double cos_lat = cos(lat * M_PI/180.);

  // Same angular resolution horizontally and vertically (see README), so
  // this is the degrees/pixel in both directions
  const double deg_per_pixel = (az_deg1-az_deg0) / (double)width;
  const int fuzz_pixel_y = (int)ceil(ANGULAR_FUZZ_DEG / deg_per_pixel);

  int Nvisible = 0;

  for(int i=0; i<Npois; i++)
  {
      double crosshair_x, crosshair_y;
      double range_have;
      if(!horizonator_project(&crosshair_x, &crosshair_y, &range_have,
                              lat, cos_lat,
                              lon,
                              ele_m,
                              pois[i].lat,
                              pois[i].lon,
                              pois[i].ele_m,
                              az_deg0 * M_PI/180.,
                              az_deg1 * M_PI/180.,
                              width,
                              height,
                              curvature_enabled,
                              refraction_k))
          continue;

      if(range_have < MIN_MARKER_DIST ||
         range_have > max_marker_dist_m )
          // too close or too far to label
          continue;

      // I'm finished with the projection. I now unproject to look for
      // occlusions

      // The rendered peaks usually don't end up exactly where the POI list
      // says they should be. I scan the range map vertically to find the true
      // peak (or to decide that it's occluded)
      int   fuzz_nearest = 0; // initializing to pacify compiler
      double err_nearest = DBL_MAX;

      for( int fuzz = -fuzz_pixel_y; fuzz < fuzz_pixel_y; fuzz++ )
      {
        if(crosshair_y + (double)fuzz < 0)
          continue;
        if( crosshair_y + (double)fuzz >= height_out )
          break;

        // As I move down the image the range will get closer and closer. I
        // pick the highest value that's closest
        const float range =
          range_image[width*( (int)round(crosshair_y) + fuzz) +
                      (int)round(crosshair_x)];

        if(range <= 0.0f)
          // no render data here
          continue;

        double err = fabs(range_have - range);
        if( err < err_nearest )
        {
          err_nearest  = err;
          fuzz_nearest = fuzz;
        }
        else
          // it can only get worse from here, so give up
          break;
      }

      if( err_nearest < FUZZ_RANGE )
      {
          visible[Nvisible].poi_index = i;
          visible[Nvisible].x         = crosshair_x;
          visible[Nvisible].y         = crosshair_y + (double)fuzz_nearest;
          visible[Nvisible].range     = range_have;
          Nvisible++;
      }
  }

  return Nvisible;
}

bool annotate(// input
              const char* out_filename,
              // assumed to be stored densely.
              const uint8_t* image_bgr,
              const float*   range_image,
              const int width,
              const int height,
              const int cut_off_bottom_px,

              const poi_t* pois,
              const int Npois,
              const double lat,
              const double lon,
              const double az_deg0,
              const double az_deg1,
              const double ele_m,

              const bool   curvature_enabled,
              const double refraction_k,

              // POIs farther than this are never labelled. Pass the same
              // zfar used for the render: there's no point labelling
              // something farther than what was actually rendered
              const double max_marker_dist_m,

              // Label text height, in real typographic points (1/72in),
              // as it'll appear in the output PDF/SVG
              const double label_font_size_pt)
{
  bool result = false;

  // label_font_size_pt is in real typographic points, i.e. as it appears
  // in the final PDF/SVG page (which is CAIRO_SCALE units per image
  // pixel). font_height is the corresponding size in the image's own
  // pixel-like coordinate system, which is what cairo_set_font_size()
  // wants here, since we're drawing under a cairo_scale(CAIRO_SCALE)
  const double font_height = label_font_size_pt / CAIRO_SCALE;

  const int height_out = height - cut_off_bottom_px;

  visible_poi_t visible[Npois];
  int Nvisible = 0;

  uint8_t*         image_rgb32 = NULL;
  cairo_surface_t* surface     = NULL;
  cairo_t*         cr          = NULL;
  cairo_surface_t* frame       = NULL;

  ////// Paint the render into the surface
  TRY(NULL != (image_rgb32 = malloc(width*height*4)));

  TRY(RGB32_from_BGR24(// output
                       image_rgb32,
                       // input
                       image_bgr,
                       width,
                       height));

  const int strlen_out_filename = strlen(out_filename);
  if(0 == strcasecmp(".pdf", &out_filename[strlen_out_filename-4]))
      TRY(NULL !=
          (surface = cairo_pdf_surface_create(out_filename,
                                              width      * CAIRO_SCALE,
                                              height_out * CAIRO_SCALE)));
  else if(0 == strcasecmp(".svg", &out_filename[strlen_out_filename-4]))
  {
      MSG("WARNING: writing out an .svg file; the links don't work with those yet; fix it, or use .pdf");
      TRY(NULL !=
          (surface = cairo_svg_surface_create(out_filename,
                                              width      * CAIRO_SCALE,
                                              height_out * CAIRO_SCALE)));
  }
  else
  {
      MSG("ERROR: output filename must be either xxx.pdf or xxx.svg; got '%s'", out_filename);
      goto done;
  }

  TRY(NULL != (cr = cairo_create(surface)));
  cairo_scale(cr, CAIRO_SCALE, CAIRO_SCALE);

  const double cos_lat = cos(lat * M_PI/180.);

  ////// Make links to the map

  ////// I do this FIRST because I cannot figure out how to tell cairo to make
  ////// transparent link boxes. I have to draw SOMETHING to get links. So I
  ////// draw rectangles below the panorama. These will end up invisible
  ////// (occluded by the panorama), and I still get my links
  cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); // Doesn't matter; anything will do

  // 10x10 doesn't work with big images:
  //   $ pdfimages -list /tmp/tst.pdf
  //   Syntax Error: Couldn't find trailer dictionary
  //   Syntax Error: Catalog object is wrong type (null)
  //   Syntax Error: Couldn't find trailer dictionary
  //   Internal Error: xref num -1 not found but needed, try to reconstruct<0a>
  //   Syntax Error: Couldn't find trailer dictionary
  //   Syntax Error: Couldn't find trailer dictionary
  //   Syntax Error: Catalog object is wrong type (null)
  //   Syntax Error: Couldn't read page catalog
  // mupdf still works, but evince does not
  const int cell_width  = 14;
  const int cell_height = 14;
  for(int y=0; y<height_out-cell_height; y += cell_height)
  {
    for(int x=0; x<width-cell_width; x += cell_width)
    {
      const float range = range_image[width*y + x];

      if(range <= 0.0f)
        // no render data here
        continue;

      float lat_cell,lon_cell;
      if(!horizonator_unproject(&lat_cell, &lon_cell,
                                x+cell_width/2, y+cell_height/2,
                                // have range_enh, not range_en
                                range, -1.,
                                lat, cos_lat,
                                lon,
                                az_deg0, az_deg1,
                                width, height))
        continue;

      char url[256];
      bool url_valid =
        (snprintf(url, sizeof(url),
                  "uri='https://caltopo.com/map.html#ll=%f,%f&z=15&b=mbt'",
                  lat_cell, lon_cell)
         < (int)sizeof(url));
      if(!url_valid) break;

      cairo_tag_begin (cr, CAIRO_TAG_LINK, url);
      cairo_rectangle(cr, x,y,cell_width,cell_height);
      cairo_fill(cr);
      cairo_tag_end (cr, CAIRO_TAG_LINK);
    }
  }

  /////// Draw the pano
  TRY(NULL != (frame =
               cairo_image_surface_create_for_data(image_rgb32,
                                                   CAIRO_FORMAT_RGB24,
                                                   width, height_out,
                                                   width*4) ));
  cairo_set_source_surface(cr, frame, 0,0);
  cairo_paint(cr);

  cairo_set_font_size(cr, font_height - TEXT_MARGIN);

  ////// Pick and render the annotations
  Nvisible = find_visible_pois(visible,
                               range_image, width, height, cut_off_bottom_px,
                               pois, Npois,
                               lat, lon, az_deg0, az_deg1, ele_m,
                               curvature_enabled, refraction_k,
                               max_marker_dist_m);

  // Now that I have all the crosshair positions: detect horizontal
  // conflicts, and within each conflicting group, keep only the
  // highest-elevation POI -- discarding the leader line and label
  // entirely for the others in that group.
  //
  // In its own block: the VLA below has a size that depends on
  // Nvisible, and the TRY() macro used earlier in this function goto's
  // past this point to the shared `done` label -- a goto is not allowed
  // to jump into the scope of a variably-sized array, so that scope must
  // end (with this block) before `done`
  {
  qsort( visible, Nvisible, sizeof(visible[0]), &compar_visible_poi_x );

  const double conflict_gap = font_height * LABEL_CONFLICT_GAP_FACTOR;

  bool keep[Nvisible ? Nvisible : 1];

  int i = 0;
  while( i < Nvisible )
  {
    // Chain together consecutive (in x order) POIs whose gap to their
    // neighbor is under the threshold: [i,j] is one conflicting group
    int j = i;
    while( j+1 < Nvisible &&
           visible[j+1].x - visible[j].x < conflict_gap )
      j++;

    int ibest = i;
    for( int k=i+1; k<=j; k++ )
      if( pois[ visible[k].poi_index ].ele_m > pois[ visible[ibest].poi_index ].ele_m )
        ibest = k;

    for( int k=i; k<=j; k++ )
      keep[k] = (k == ibest);

    i = j+1;
  }

  for( int i=0; i<Nvisible; i++ )
  {
    if( !keep[i] )
      continue;

    const poi_t* poi = &pois[ visible[i].poi_index ];

    draw_label(cr,
               visible[i].x, visible[i].y,
               poi->lat, poi->lon,
               poi->name);
  }
  }

  cairo_set_source_rgb(cr, LABEL_TEXT_R, LABEL_TEXT_G, LABEL_TEXT_B);

  const int bearing_annotation_spacing = 15;
  for(int az=180; az>-180; az -= bearing_annotation_spacing)
  {
      double x;
      if(!horizonator_x_from_az(// output
                                &x, NULL,
                                // input
                                (double)az * M_PI/180.,
                                az_deg0 * M_PI/180.,
                                az_deg1 * M_PI/180.,
                                width))
          continue;

      char text[16];
      sprintf(text, "%ddeg", az);

      double w = string_width(cr, text);
      // cairo wants the bottom of the label
      cairo_move_to(cr, x-w/2., height_out - font_height);
      cairo_show_text(cr, text);
  }

  cairo_surface_show_page(surface);

  result = true;

 done:

  if(frame   != NULL) cairo_surface_destroy(frame);
  if(cr      != NULL) cairo_destroy(cr);
  if(surface != NULL) cairo_surface_destroy(surface);

  free(image_rgb32);

  return result;
}
