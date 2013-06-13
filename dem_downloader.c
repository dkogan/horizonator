#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdbool.h>
#include <string.h>

#include <curl/curl.h>
#include <curl/easy.h>
#include <zip.h>

// tries to read a DEM at the path. Returns true/false based on whether it looks
// like a good DEM
static int works( const char* path )
{
  // I should be able to stat() the DEM
  struct stat buf;
  if( stat( path, &buf ) )
    return 0;

  // The DEM should be the right size
  if( buf.st_size != 1201*1201*2 )
    return 0;

  // The DEM should be a "regular" file
  if( !S_ISREG(buf.st_mode) )
    return 0;

  // The DEM should be readable
  int fd = open( path, O_RDONLY );
  close(fd);
  if( fd <= 0 )
    return 0;

  // All good
  return 1;
}

static const char* downloadZip( const char* url )
{
  const char* filename_zip_template = "/tmp/horizonator_DEM.zip.XXXXXX";
  static char filename_zip[1024];
  strcpy( filename_zip, filename_zip_template );

  bool  final_result = false;
  int   fd_zip       = -1;
  FILE* FILE_zip     = NULL;
  CURL* curl         = NULL;

  fd_zip = mkstemp( filename_zip );
  if( fd_zip < 0 )
  {
    fprintf(stderr, "DEM downloader couldn't mkstemp...\n" );
    goto getDEM_done;
  }

  FILE_zip = fdopen( fd_zip, "w" );
  if( FILE_zip == NULL )
  {
    fprintf(stderr, "DEM downloader couldn't fdopen...\n" );
    goto getDEM_done;
  }

  curl_global_init(CURL_GLOBAL_ALL);

  curl = curl_easy_init();
  if( curl == NULL )
  {
    fprintf(stderr, "DEM downloader couldn't init libcurl...\n" );
    goto getDEM_done;
  }

  curl_easy_setopt(curl, CURLOPT_URL,         url );
  curl_easy_setopt(curl, CURLOPT_FILE,        FILE_zip );
  curl_easy_setopt(curl, CURLOPT_USERAGENT,   "horizonator");
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
  CURLcode result = curl_easy_perform(curl);

  if( result != CURLE_OK )
  {
    fprintf(stderr, "DEM downloader couldn't download DEM from URL '%s'. Might try other URLs\n", url );
    goto getDEM_done;
  }

  final_result = true;

 getDEM_done:
  if( FILE_zip )   fclose(FILE_zip);
  if( fd_zip > 0 ) close(fd_zip);
  if( curl )       curl_easy_cleanup(curl);

  if( final_result )
    return filename_zip;
  return NULL;
}

static bool unzip( const char* zipfilename, const char* wantedfilename )
{
  struct zip*      zip          = NULL;
  struct zip_file* zip_file     = NULL;
  int              fd_out       = -1;
  char*            buf          = NULL;
  bool             final_result = false;

  zip = zip_open( zipfilename, 0, NULL );
  if( zip == NULL )
  {
    fprintf(stderr, "unzip couldn't open zip file '%s'\n", zipfilename);
    goto unzip_done;
  }

  struct zip_stat sb;
  if( zip_stat_index( zip, 0, 0, &sb ) )
  {
    fprintf(stderr, "unzip couldn't read the zip file '%s'\n", zipfilename);
    goto unzip_done;
  }

  if( !(sb.valid & ZIP_STAT_NAME) || !(sb.valid & ZIP_STAT_SIZE) )
  {
    fprintf(stderr, "unzip couldn't read the zip file '%s'\n", zipfilename);
    goto unzip_done;
  }

  if( strcmp( wantedfilename, sb.name ) != 0 )
  {
    fprintf(stderr, "I wanted to get DEM '%s', but the zip file '%s' had DEM '%s'\n",
            wantedfilename, zipfilename, sb.name);
    goto unzip_done;
  }

  zip_file = zip_fopen_index( zip, 0, 0 );
  if( zip_file == NULL )
  {
    fprintf(stderr, "unzip couldn't read the zip file '%s'\n", zipfilename);
    goto unzip_done;
  }

  char* namedup       = strdup( sb.name );
  char path_out[1024];

  snprintf(path_out, sizeof(path_out), "%s/.horizonator", getenv("HOME") );
  mkdir( path_out, 0777 );
  snprintf(path_out, sizeof(path_out), "%s/.horizonator/DEMs_SRTM3", getenv("HOME") );
  mkdir( path_out, 0777 );

  snprintf(path_out, sizeof(path_out), "%s/.horizonator/DEMs_SRTM3/%s", getenv("HOME"), basename(namedup));
  free(namedup);

  fd_out = open( path_out, O_CREAT | O_WRONLY, 0777  );
  if( fd_out <= 0 )
  {
    fprintf(stderr, "unzip couldn't open output DEM '%s'\n", path_out );
    goto unzip_done;
  }

  buf = malloc(sb.size);
  if( buf == NULL )
  {
    fprintf(stderr, "unzip couldn't allocate memory to read from zip file '%s'. Wanted %ld bytes\n", zipfilename, sb.size);
    goto unzip_done;
  }

  if( (zip_uint64_t)sb.size != (zip_uint64_t)zip_fread( zip_file, buf, sb.size ) )
  {
    fprintf(stderr, "unzip couldn't read from zip file '%s'\n", zipfilename);
    goto unzip_done;
  }

  if( (zip_uint64_t)sb.size != (zip_uint64_t)write(fd_out, buf, sb.size) )
  {
    fprintf(stderr, "unzip couldn't write DEM output to '%s'\n", path_out);
    goto unzip_done;
  }

  final_result = true;

 unzip_done:
  if( buf )   free(buf);
  if( fd_out > 0 ) close(fd_out);
  if( zip_file ) zip_fclose( zip_file );
  if( zip ) zip_close( zip );

  return final_result;
}

static bool downloadDEM( const char* url, const char* wantedfilename )
{
  const char* filenameZip = downloadZip( url );
  if( filenameZip == NULL )
    return false;

  bool result = unzip( filenameZip, wantedfilename );
  unlink( filenameZip );
  return result;
}

const char* getDEM_filename( int demfileN, int demfileE )
{
  static char path[1024];
  char filename[1024];

  if     ( demfileN >= 0 && demfileE >= 0 )
    snprintf(filename, sizeof(filename), "N%.2dE%.3d.hgt", demfileN, demfileE);
  else if( demfileN >= 0 && demfileE <  0 )
    snprintf(filename, sizeof(filename), "N%.2dW%.3d.hgt", demfileN, -demfileE);
  else if( demfileN  < 0 && demfileE >= 0 )
    snprintf(filename, sizeof(filename), "S%.2dE%.3d.hgt", -demfileN, demfileE);
  else
    snprintf(filename, sizeof(filename), "S%.2dW%.3d.hgt", -demfileN, -demfileE);


  snprintf(path, sizeof(path), "%s", filename);
  if( works( path ) )
    return path;

  snprintf(path, sizeof(path), "DEMs_SRTM3/%s", filename);
  if( works( path ) )
    return path;

  snprintf(path, sizeof(path), "%s/.horizonator/DEMs_SRTM3/%s", getenv("HOME"), filename);
  if( works( path ) )
    return path;

  // no DEM was available. I'm going to try to download it.
  char* regions[] = {"Africa",
                     "Australia",
                     "Eurasia",
                     "Islands",
                     "North_America",
                     "South_America"};

  for( unsigned int i=0; i<sizeof(regions)/sizeof(regions[0]); i++ )
  {
    char url[1024];

    snprintf(url, sizeof(url),
             "http://dds.cr.usgs.gov/srtm/version2_1/SRTM3/%s/%s.zip",
             regions[i], filename);

    if( downloadDEM( url, filename ) )
      return path;
  }

  // couldn't download. Give up.
  return NULL;
}
