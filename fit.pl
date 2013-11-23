#!/usr/bin/perl
use strict;
use warnings;
use feature qw(say);

set_autopthread_targ(2);
set_autopthread_size(0);

use PDL;
use PDL::IO::GD;
use PDL::Graphics::Gnuplot;
use PDL::NiceSlice;
use PDL::OpenCV qw(Smooth Sobel Remap Rodrigues2 %cvdef);
use PDL::LinearAlgebra;
use PDL::Complex;
use PDL::FFTW3;
use PDL::Image2D;
use PDL::IO::Storable;
use PDL::Constants qw(PI);
use Storable qw(store retrieve);
use Getopt::Euclid;
use Algorithm::LBFGS;


my $cache;
my %image;
my @cache_stage2;
if( defined $ARGV{'--cache'} )
{
  say STDERR "Reading cache";

  $cache = retrieve 'cache';

  %image        = %{ $cache->{image} };
  @cache_stage2 = @{ $cache->{stage2} } if defined $cache->{stage2} && !$ARGV{'--only1'};
}



if( !%image )
{
  my ($img_remapped, $pano) = readImages();

  for my $name_img ( ['img',  $img_remapped],
                     ['pano', $pano ] )
  {
    my ($name, $img) = @$name_img;

    my $gradx = $img->copy;
    my $grady = $img->copy;

    my $img_smoothed = $img->zeros;

    if( $name eq 'pano' )
    {
      # don't smooth the panorama image, since it's perfect
      $img_smoothed = $img->copy;
    }
    else
    {
      Smooth( $img, $img_smoothed, $cvdef{CV_GAUSSIAN},
              $ARGV{'--smoothradius'},
              $ARGV{'--smoothradius'}, 0, 0 );
      $img_smoothed /= 3*3;
    }

    $image{$name}{orig}     = $img;
    $image{$name}{smoothed} = $img_smoothed;
    $image{$name}{size}     = [$img->dims];
    pop $image{$name}{size};

    Sobel( $img_smoothed, $gradx, 1, 0, $cvdef{CV_SCHARR} );
    Sobel( $img_smoothed, $grady, 0, 1, $cvdef{CV_SCHARR} );

    # the gradients have dimensions (x,y,rgb)
    my $V = cat($gradx, $grady)->mv(3,0); # dims (grad, x,y,rgb )

    # photo: join the channels intelligently; pano: just take the red
    if( $name eq 'pano' )
    {
      $image{$name}{edges} = $V(:,:,:,(0));
    }
    else
    {
      # I now join the channel-independent edges into single-channel edge vectors. I
      # do this by computing the most-aligned direction, weighted by the magnitude
      # of each vector. The magnitude of the result is simply the mean magnitude of
      # the sources

      my $M = outer( $V, $V );  # dims (M0, M1, x, y, rgb)
      $M = $M->mv(-1,0)->sumover; # dims (M0, M1, x, y)
      my ($l, $v) = msymeigen( $M, 0, 1 );
      $v = $v((1),:,:,:);       # select the larger eigenvalue
      my $m = sqrt(inner( $V, $V) )->mv(-1,0)->sumover; # sum of the lengths of the gradient vectors

      # Done. I have the normalized directions ($v) and the magnitudes ($m). I now
      # construct the output array of vectors
      $image{$name}{edges} = $v * $m->dummy(0);
    }

    # I want to align the vectors mod pi. I treat these as complex numbers.Given two
    # complex numbers,
    #
    # Re(a*conj(b)) = |a||b| cos( th_a - th_b ). This shows angle differences mod
    # 2pi. To show differences mod pi, I simply double the angles by squaring the
    # numbers: Re( a^2 * conj(b^2) ) = |a|^2 |b|^ cos( 2* (th_a - th_b) )

    # I square each of the vectors
    $image{$name}{edges} = cplx $image{$name}{edges};
    $image{$name}{edges} = $image{$name}{edges} * $image{$name}{edges};
  }
}


my ($dx,$dy, @mounted_size);
if( !@cache_stage2 )
{
  # and correlate
  ($dx,$dy, @mounted_size) = correlate_conj( $image{pano}{edges},
                                             $image{img} {edges} );
}
else
{
  ($dx,$dy, @mounted_size) = @cache_stage2;
}

store { image  => \%image,
        stage2 => [$dx,$dy, @mounted_size] }, "cache";


if ( $ARGV{'--forcerightanswer'} )
{
  # works with this command:
  #  ./fit.pl --pano pano.png --smoothradius 7 --photo ironcut.png
  ($dx,$dy)=(642,86);
}

fullOptimization( $image{img}, $image{pano}, $dx, $dy );




if($ARGV{'--plot'} =~ /alignpair/ )
{
  # plot the aligned images
  my $which = $ARGV{'--plot'} =~ /smoothed/ ? 'smoothed' : 'orig';
  my $img_orig  = $image{img} { $which };
  my $pano_orig = $image{pano}{ $which };

  my $img0_gray = real $img_orig ->mv(-1,0)->average;
  my $img1_gray = real $pano_orig->(:,:,(0));
  my @mounted = map { $_->range( [0,0], \@mounted_size, 'e') } ( $img0_gray, $img1_gray );

  debugPlot( {clut => 'gray',
              xrange => [0,$img_orig->dim(0)-1],
              yrange => [0,$img_orig->dim(1)-1] },
             $mounted[0], $mounted[1]->range( [$dx,$dy], [$mounted[1]->dims], 'p' ) );
}
if($ARGV{'--plot'} eq 'regions')
{
  my @mounted = mount_images( $image{img}{edges}, $image{pano}{edges} );
  $mounted[0] = $mounted[0]->range( [0,-$dx,-$dy], [$mounted[1]->dims], 'p' );

  $mounted[0] = cplx $mounted[0];
  $mounted[1] = cplx $mounted[1];

  my $p = real( $mounted[0] * Cconj $mounted[1] );
  say "This should match the reported corr: value: " . sum( $p((0),:,:) );

  debugPlot( $p((0),:,:));
}






sub readImages
{
  my $pi         = 3.14159265359;
  my @sensorsize = ( 7.31, 5.49 );
  my $focal      = 5.1;


  my $img  = PDL::IO::GD->new( $ARGV{'--photo'} )->to_pdl->double / 255.0;
  my $pano = PDL::IO::GD->new( $ARGV{'--pano'}  )->to_pdl->double / 255.0;


  if( !$ARGV{'--doremap'} )
  {
    return ($img, $pano);
  }


  # remap!
  my $px_per_rad = $pano->dim(0) / (2.0 * $pi);

  my @fov        = map { list atan( $_ / 2.0 / $focal ) * 2.0 } @sensorsize;
  my @remap_size = map { $_ * $px_per_rad } @fov;

  my $img_remapped = double zeros( @remap_size, 3 );

  # I remap my photo (from a perspective camera) to an az-el image. $map is a
  # piddle corresponding to the target az-el image; values of $map are indices
  # into the original photo image. Transformation is:
  #
  # x = (w-1)(f/sx tan(az)         + 1/2 )
  # y = (h-1)(f/sy tan(el)/cos(az) + 1/2 )
  #
  # Derivation is in my notebook and probably in many other places
  my $az = $img_remapped->(:,:,(0))->xlinvals( -$fov[0]/2, $fov[0]/2 );
  my $el = $img_remapped->(:,:,(0))->ylinvals( -$fov[1]/2, $fov[1]/2 );

  my $wh  = pdl( $img->dim(0), $img->dim(1) );
  my $sxy = pdl( @sensorsize );

  my $map = cat( tan($az), tan($el)/cos($az) );
  $map = double( ($map * $focal / $sxy->dummy(0)->dummy(0) + 0.5) * ($wh->dummy(0)->dummy(0) - 1));

  Remap( $img,
         $img_remapped,
         $map->dog,
         1 + 9,                 # CV_INTER_LINEAR+CV_WARP_FILL_OUTLIERS
         zeros(4)->double );

  if( $ARGV{'--saveremapped'} )
  {
    write_true_png( $img_remapped*255, $ARGV{'--saveremapped'} );
  }
  if( $ARGV{'--plot'} eq 'remapped' )
  {
    debugPlot( {clut => 'gray'},
               $img_remapped );
  }

  return ($img_remapped, $pano);
}

# computes the peak of the correlation of img0 and conj(img1)
sub correlate_conj
{
  my @imgs = @_;
  return correlate_conj_mounted( mount_images(@imgs) );



  # computes the peak of the correlation of img0 and conj(img1). img0 and img1
  # are guaranteed to be 0-padded and identically-dimensioned
  sub correlate_conj_mounted
  {
    my @mounted = @_;

    my $Npoints = $mounted[0]->dim(1) * $mounted[0]->dim(2);

    my @fft = dog cplx fft2 real cat @mounted;
    my $corr = cplx ifft2( real( $fft[0] * Cconj( $fft[1] )) );


    my ($corr_max, @corr_offset ) = max2d_ind( $corr->re );

    say "best offset: @corr_offset; corr: $corr_max";

    # correlation plot
    if( $ARGV{'--plot'} eq 'corr' )
    {
      debugPlot( re $corr );
    }

    my @mounted_size = $mounted[0]->dims;
    shift @mounted_size;
    return (@corr_offset, @mounted_size);
  }
}

sub fullOptimization
{
  my ($img, $pano, $dx, $dy) = @_;



  sub evalfunc
  {
    my ($state, $step, $cookie) = @_;

    my ($img, $pano) = map {real $_} @{$cookie}{qw(img pano)};

    my ($imgW,  $imgH)  = $img ->shape->(1:2)->list;
    my ($panoW, $panoH) = $pano->shape->(1:2)->list;


    # assume the panorama is a full 360-deg span
    my $pano_px_per_rad = $panoW / (2 * PI);

    # $img and $pano are complex piddles that contain the edge images. I want to
    # maximize re(corr($img(delta),$pano)), so I want to maximize
    #   sum( re0*re1 - im0*im1 )
    my $focal = $state->[0];
    my $r     = pdl( @{$state}[1..3] );

    # pixelcoords referenced from the center of the $img
    my $pxcoords_centerref = $img((0),:,:)->ndcoords - (pdl($imgW,$imgH) - pdl(1,1))/2;

    # the image (x,y,f) tuples
    my $x = $pxcoords_centerref->glue(0, $focal*ones($imgW, $imgH)->dummy(0));

    my $R  = zeros(3,3);
    my $dR = zeros(3,9);
    Rodrigues2( my $retval, $r, $R, $dR );

    my $v = $x x $R->transpose;

    # unroll the az
    my $az_rad = atan2( $v(0), $v(2) )-> squeeze;
    if( $az_rad->max > 0.9*PI && $az_rad->min < -0.9*PI )
    {
      $az_rad->where($az_rad < 0) += PI*2;
    }

    my $az = $pano_px_per_rad * $az_rad;
    my $el = $pano_px_per_rad * asin ( $v((1)) / sqrt(inner($x,$x)) ) -> squeeze;

    my $cellindex = cat($el->zeros, long(floor($az)), long(floor($el)))->mv(-1,0);
    my $azoffset  = $az - $cellindex((1),:,:);
    my $eloffset  = $el - $cellindex((2),:,:);

    # I make sure the mapped data is in-bounds of my rendered pano. The az is
    # always in-bounds since it's periodic. This is not true of the el, however
    # my $valididx =
    #   $cellindex((1),:,:) >= 3 &&
    #   $cellindex((1),:,:) < $panoH-3;
    # my $Nvalid = $valididx->sum;



    # bilinear interpolation
    my $pano00 = $pano;
    my $pano10 = $pano->range(pdl(0,1,0) , $pano->shape, 'fp');
    my $pano01 = $pano->range(pdl(0,0,1) , $pano->shape, 'fp');
    my $pano11 = $pano->range(pdl(0,1,1) , $pano->shape, 'fp');


    my $pano_interpolated =
      $pano00->range($cellindex, pdl(2,1,1), 'fpt')->sever * (1 - $azoffset) * (1 - $eloffset ) +
      $pano10->range($cellindex, pdl(2,1,1), 'fpt')->sever * (    $azoffset) * (1 - $eloffset ) +
      $pano01->range($cellindex, pdl(2,1,1), 'fpt')->sever * (1 - $azoffset) * (    $eloffset ) +
      $pano11->range($cellindex, pdl(2,1,1), 'fpt')->sever * (    $azoffset) * (    $eloffset );
    $pano_interpolated = $pano_interpolated->squeeze->mv(-1,0);


    # store [$img,$pano,$pano00,$pano01,$pano10,$pano11,$azoffset,$eloffset,$cellindex,$az,$el,$pano_interpolated,$x,$R,$dR,$v], 'dat';
    # exit;



    # once again, I want to match up sum( re0*re1 - im0*im1 ). I'm already lined
    # up, so I sum up everything
    my $f = Cmul(cplx($img), cplx($pano_interpolated))->re->sum;



    # Now I get the gradients
    # d(sum( re0*re1 - im0*im1 )) = sum( re0 dre1 - im0 dim1 )
    # (re1,im1) is pano_interpolated
    #
    # dpano_interpolated = pano00 * ( (1-azoffset) * (-deloffset) + (-dazoffset) * (1-deloffset) ) +
    #                      pano10 * ( (  azoffset) * (-deloffset) + ( dazoffset) * (1-deloffset) ) +
    #                      pano01 * ( (1-azoffset) * ( deloffset) + (-dazoffset) * (  deloffset) ) +
    #                      pano11 * ( (  azoffset) * ( deloffset) + ( dazoffset) * (  deloffset) );
    #
    # so all I need is d_azoffset and d_eloffset
    #
    # d_azoffset = pano_px_per_rad * d_az
    # d_eloffset = pano_px_per_rad * d_el

    # d_az_dr = (x y f ) * (dr0_dr * v2 - dr2_dr * v0) / (v2^2 * v0^2)
    # d_az_df = (r02*v2 - v0*r22) / (v2^2 * v0^2)
    # d_el_dr = (x y f ) dr1_dr       /        sqrt(|x|^2 - v1^2)
    # d_el_df = (|x|^2*r12(r) - f*v1) / (|x|^2*sqrt(|x|^2 - v1^2))

    my $dr0_dr = $dR(:,0:2);
    my $dr1_dr = $dR(:,3:5);
    my $dr2_dr = $dR(:,6:8);

    my $d_az_dr =
      PDL::squeeze
        ($x->dummy(1) x ($dr0_dr * $v(2,:,:)->dummy(0) -
                         $dr2_dr * $v(0,:,:)->dummy(0)))
        / ($v(2,:,:) * $v(2,:,:) + $v(0,:,:) * $v(0,:,:));

    my $d_az_df =
      ($R(2,0; -) * $v(2,:,:) - $R(2,2; -) * $v(0,:,:)) /
      ($v(2,:,:) * $v(2,:,:) + $v(0,:,:) * $v(0,:,:) );

    my $d_el_dr =
      PDL::squeeze($x->dummy(1) x $dr1_dr) /
      sqrt( inner($x,$x) - $v((1),:,:)*$v((1),:,:) )->dummy(0);

    my $d_el_df =
      dummy( (inner($x,$x) * $R(2,1;-) - $focal*$v((1),:,:)) /
             (inner($x,$x)*sqrt( inner($x,$x) - $v((1),:,:)*$v((1),:,:) )), 0);

    # makde d_azel_offset dimensions (imwidth, imheight, 4)
    my $d_azoffset = $pano_px_per_rad * $d_az_df->glue(0, $d_az_dr)->mv(0,-1);
    my $d_eloffset = $pano_px_per_rad * $d_el_df->glue(0, $d_el_dr)->mv(0,-1);

    # dpano_interpolated dims are (imwidth, imheight, 4, 2)
    my $dpano_interpolated =
      $pano00->range($cellindex, pdl(2,1,1), 'fpt')->sever->squeeze->dummy(2) *
      ( (- $d_azoffset) * (1 - $eloffset ) + (1 - $azoffset) * (- $d_eloffset ) ) +
      $pano10->range($cellindex, pdl(2,1,1), 'fpt')->sever->squeeze->dummy(2) *
      ( (  $d_azoffset) * (1 - $eloffset ) + (    $azoffset) * (- $d_eloffset ) ) +
      $pano01->range($cellindex, pdl(2,1,1), 'fpt')->sever->squeeze->dummy(2) *
      ( (- $d_azoffset) * (    $eloffset ) + (1 - $azoffset) * (  $d_eloffset ) ) +
      $pano11->range($cellindex, pdl(2,1,1), 'fpt')->sever->squeeze->dummy(2) *
      ( (  $d_azoffset) * (    $eloffset ) + (    $azoffset) * (  $d_eloffset ));

    my $j =
      [list sumover(sumover($img((0),:,:) * $dpano_interpolated(:,:,:,(0)) -
                            $img((1),:,:) * $dpano_interpolated(:,:,:,(1)) )) ];


    # I want to maximize my $f, but the solver wants to minimize. Flip all the
    # signs
    $f *= -1;
    $j = [ map {$_ * -1} @$j ];
    say sprintf "callback returning cost %g", $f;
    return ($f, $j);
  }

  sub testGradient
  {
    my ($testvar, $state, $img, $pano) = @_;

    $state = $state->copy;

    my ($f0, $j0) = evalfunc( [$state->list], 10, {img  => $img ->{edges},
                                                   pano => $pano->{edges}} );

    my $delta = 1e-8;

    $state($testvar) += $delta;
    my ($f1, $j1) = evalfunc( [$state->list], 10, {img  => $img ->{edges},
                                                   pano => $pano->{edges}} );

    my $j_observed = ($f1 - $f0) / $delta;
    my $j_expected = $j0->[$testvar];
    say "observed gradient: $j_observed";
    say "expected gradient: $j_expected";
    say "relative error: " . ($j_observed - $j_expected) / ( (abs($j_observed) + abs($j_expected))/2);
  }





  # (dx,dy) is the coords of the top-left corner of the photo mapped into pano
  # pixel coords. This is mod mounted->dims. I construct my rotation to properly
  # map the center pixel of the photo. The full projection effects are utilized
  # from this point on, so the un-distorted fit I just did won't map 100%

  my $delta      = pdl($dx, $dy);
  my $photo_size = $img ->{edges}->shape->(1:2) ;
  my $pano_size  = $pano->{edges}->shape->(1:2) ;

  # first map the coords to [-h,h] from [0,2h].
  my $mounted_size = 2*pdl($pano_size((0)), $photo_size((1)));

  $delta -= $mounted_size * ($delta >= $mounted_size/2);

  # center pixel of the photo, in pano coords
  my $center_px = $delta + $photo_size/2;

  # pano has full 360-deg horiz view. Assume constant px/rad value
  my $pano_px_per_rad = $pano_size((0)) / (2 * PI);

  my $center_rad = $center_px / $pano_px_per_rad;

  # I need to rotate around y by $center_px(0) and around x by $center_px(1);
  my ($saz,$sel) = sin($center_rad)->list;
  my ($caz,$cel) = cos($center_rad)->list;

  my $Raz = pdl( [ $caz, 0,  $saz],
                 [   0,  1, 0    ],
                 [-$saz, 0,  $caz] );

  my $Rel = pdl( [ 1,   0,      0],
                 [ 0,  $cel, $sel],
                 [ 0, -$sel, $cel] );

  my $rref = zeros(3);
  my $R    = $Raz x $Rel;
  Rodrigues2( my $retval, $R, $rref, null );

  my $focal = $pano_px_per_rad * 0.99;

  # These are tests:
  #
  # say "these should be the same (center pixel azel):";
  # my $v = pdl(0,0,$focal) x $R->transpose;
  # my $az_check = $pano_px_per_rad * atan2( $v(0), $v(2) )               -> squeeze;
  # my $el_check = $pano_px_per_rad * asin ( $v(1) / sqrt(inner($v,$v)) ) -> squeeze;
  # say pdl($az_check, $el_check);
  # say $center_px;
  # say "center-top pixel azel:";
  # $v = pdl(0,-$photo_size((1))/2,$focal) x $R->transpose;
  # $az_check = $pano_px_per_rad * atan2( $v(0), $v(2) )               -> squeeze;
  # $el_check = $pano_px_per_rad * asin ( $v(1) / sqrt(inner($v,$v)) ) -> squeeze;
  # say pdl($az_check, $el_check);


  my $state = pdl( $focal, $rref->list );

  # testGradient( $_, $state, $img, $pano ) for 0..3;
  # exit;


  say "starting state: $state";
  my $lbfgs = Algorithm::LBFGS->new;
  my $out = $lbfgs->fmin( \&evalfunc, [$state(0)->list],
                          'verbose', {img  => $img ->{edges},
                                      pano => $pano->{edges}} );
  say $lbfgs->get_status;
  say "ending state: " . pdl($out);
  exit;
}

sub mount_images
{
  my @imgs = @_;

  # mount the images into larger, equal matrix
  my $sizes = pdl( [$imgs[0]->dims], [$imgs[1]->dims] );
  my @mountsize = PDL::list( $sizes->transpose->maximum->(1:-1) * 2 );

  my @mounted;
  foreach my $img(@imgs)
  {
    # mount the image
    my $mountedimg = cplx zeros( 2, @mountsize );
    $mountedimg->(:, 0:$img->dim(1)-1, 0:$img->dim(2)-1 ) .= $img;
    push @mounted, $mountedimg;
  }

  return @mounted;
}


# This is for testing/debugging. Each piddle in the argument plot is drawn as an
# image. First argument is a hashref to pass to gplot()
sub debugPlot
{
  my $plotoptions;
  $plotoptions = shift if ref $_[0] eq 'HASH';

  my @data = @_;

  my @w;
  for my $x(@data)
  {
    push @w, gpwin;

    my @options = (globalwith => 'image',
                   square => 1);

    if( defined $plotoptions && %$plotoptions )
    {
      if( defined $plotoptions->{yrange} &&
          $plotoptions->{yrange}[0] < $plotoptions->{yrange}[1] )
      {
        $plotoptions->{yrange} = [ reverse @{$plotoptions->{yrange}}];
      }

      # this i just 'push @options, %$plotoptions', however
      # PDL::Graphics::Gnuplot has a bug where xrange has to appear first. with
      # just that line, the range specs could end up anywhere, confusing PGG
      {
        my %opts_norange = %$plotoptions;
        delete $opts_norange{xrange};
        delete $opts_norange{yrange};
        push @options, %opts_norange;

        my %opts_range = %$plotoptions;
        for (keys %opts_range)
        {
          delete $opts_range{$_} unless /range/;
        }
        push @options, %opts_range;
      }
    }
    else
    {
      push @options, (extracmds => 'set yrange [*:*] reverse');
    }

    $w[-1]->plot( @options, $x );
  }
  sleep(1000000);
  exit;
}



__END__

=head1 NAME

fit - prototype for the image aligner

=head1 REQUIRED ARGUMENTS

=head1 OPTIONS

=over

=item --pano <pano>

Panorama render image. Required if no --cache

=for Euclid:
  pano.type: readable

=item --photo <photo>

Photo being annotated.  Required if no --cache

=for Euclid:
  photo.type: readable

=item --cache

Read cache data from a file 'cache'

=item --only1

Read only the first stage of the cache

=item --saveremapped <remappedfile>

Save a remapped photo to a given file

=for Euclid
  remappedfile.type: writable

=item --plot <what>

Selects what should be plotted at the end. Could be

C<remapped> to plot just the remapped photo, without fitting anything

C<corr> for the correlation map

C<alignpair> to show the aligned original pair of images

C<alignpair_smoothed> to show the aligned smoothed pair of images

C<regions> to show which regions of the image aligned the best in the best-case alignment

=for Euclid:
    what.type: /remapped|corr|alignpair|alignpair_smoothed|regions/
    what.default: ''

=item --s[moothradius] <smoothradius>

The radius of the initial smoothing filter. The default is smoothradius.default.
This must be an odd integer >= 3

=for Euclid:
    smoothradius.type:       int, smoothradius >= 3 && smoothradius % 2 == 1
    smoothradius.type.error: smoothradius must be odd integer >= 3
    smoothradius.default:    7

=item --forcerightanswer

Pretend I found the right solution for my Iron-mt photo

=item --doremap

Convert the photo projection to the cylindrical projection in the pano

=item --help

Print the usual program information

=back

=head1 AUTHOR

Dima Kogan, C<< <dima@secretsauce.net> >>

=head1 COPYRIGHT

Copyright (c) 2013, Dima Kogan

This module is free software. It may be used, redistributed and/or modified
under the terms of the GNU public license or the Perl Artistic License
