#!/usr/bin/perl
use strict;
use warnings;
use feature qw(say);

set_autopthread_targ(2);
set_autopthread_size(0);

use blib '/home/dima/projects/PDL-FFTW3';

use PDL;
use PDL::IO::GD;
use PDL::Graphics::Gnuplot;
use PDL::NiceSlice;
use PDL::OpenCV qw(Smooth Sobel %cvdef);
use PDL::LinearAlgebra;
use PDL::Complex;
use PDL::FFTW3;
use PDL::Image2D;
use PDL::IO::Storable;
use Storable qw(store retrieve);

my %image;

# for my $name_file ( [qw(img  /tmp/tst1.png)],
#                     [qw(pano /tmp/tst2.png)] )

# for my name_file ( [qw(img  remapped.

if( !$ARGV[0] )
{
  for my $name_file ( [qw(img  ironcut.png)],
                      [qw(pano pano.png)] )
  {
    my ($name, $file) = @$name_file;

    my $img   = PDL::IO::GD->new( $file )->to_pdl->float / 255.0;

  #  $img = float random(4,8,3); # test code

    my $gradx = $img->copy;
    my $grady = $img->copy;

    my $img_smoothed = $img->zeros;
    Smooth( $img, $img_smoothed, $cvdef{CV_GAUSSIAN}, 7, 7, 0, 0 );
    $img_smoothed /= 3*3;

    $image{$name}{orig}     = $img;
    $image{$name}{smoothed} = $img_smoothed;
    $image{$name}{size}     = [$img->dims];
    pop $image{$name}{size};

    # edges looking at each channel separately
    $image{$name}{edgecomponents}{x} = $gradx;
    $image{$name}{edgecomponents}{y} = $grady;

    Sobel( $img_smoothed, $gradx, 1, 0, $cvdef{CV_SCHARR} );
    Sobel( $img_smoothed, $grady, 0, 1, $cvdef{CV_SCHARR} );

    # I now join the channel-independent edges into single-channel edge vectors. I
    # do this by computing the most-aligned direction, weighted by the magnitude
    # of each vector. The magnitude of the result is simply the mean magnitude of
    # the sources

    # the gradients have dimensions (x,y,rgb)
    my $V = cat($gradx, $grady)->mv(3,0); # dims (grad, x,y,rgb )

    my $M = outer( $V, $V );      # dims (M0, M1, x, y, rgb)
    $M = $M->mv(-1,0)->sumover;   # dims (M0, M1, x, y)
    my ($l, $v) = msymeigen( $M, 0, 1 );
    $v = $v((1),:,:,:);           # select the larger eigenvalue
    my $m = sqrt(inner( $V, $V) )->mv(-1,0)->sumover; # sum of the lengths of the gradient vectors

    # Done. I have the normalized directions ($v) and the magnitudes ($m). I now
    # construct the output array of vectors
    $image{$name}{edges} = $v * $m->dummy(0);
  }

  store \%image, "cache";
}
else
{
  %image = %{retrieve $ARGV[0]};
}


my ($dx,$dy, @mounted_size);
if( !$ARGV[1] )
{
  # I want to align the vectors mod pi. I treat these as complex numbers.Given two
  # complex numbers,
  #
  # Re(a*conj(b)) = |a||b| cos( th_a - th_b ). This shows angle differences mod
  # 2pi. To show differences mod pi, I simply double the angles by squaring the
  # numbers: Re( a^2 * conj(b^2) ) = |a|^2 |b|^ cos( 2* (th_a - th_b) )

  # I square each of the vectors
  {
    for my $type (qw(img pano))
    {
      $image{$type}{edges} = cplx $image{$type}{edges};
      $image{$type}{edges} = $image{$type}{edges} * $image{$type}{edges};
    }
  }

  # and correlate
  ($dx,$dy, @mounted_size) = correlate_conj( $image{pano}{edges},
                                             $image{img} {edges} );

  store [$dx,$dy, @mounted_size], "cache2";
}
else
{
  ($dx,$dy, @mounted_size) = @{retrieve $ARGV[1]};
}

# plot the aligned images
my $img0_gray = real $image{img} {orig}->mv(-1,0)->average;
my $img1_gray = real $image{pano}{orig}->mv(-1,0)->average;


my @mounted = map { $_->range( [0,0], \@mounted_size, 'e') } ( $img0_gray, $img1_gray );
gplot( globalwith => 'image',
       square => 1,
       extracmds => 'set yrange [*:*] reverse',
       0.5 * ($mounted[0] + $mounted[1]->range( [$dx,$dy], [$mounted[1]->dims], 'p' ))->(0:1000,0:400)
     );
sleep 1000;






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



    # $mounted[0]->(1,:,:) .= 0; # test code
    # $mounted[1]->(1,:,:) .= 0; # test code
    # say join(' ', $mounted[0]->dims);
    # $mounted[0] = cplx abs real $mounted[0]; # test code
    # $mounted[1] = cplx abs real $mounted[1]; # test code
    # gplot( globalwith => 'image',
    #        square => 1,
    #        extracmds => 'set yrange [*:*] reverse',
    #        re $mounted[0]->glue(1, $mounted[1])
    #      );
    # sleep(1000);



    my $Npoints = $mounted[0]->dim(1) * $mounted[0]->dim(2);

    my @fft = dog cplx fft2 real cat @mounted;
    my $corr = cplx ifft2( real( $fft[0] * Cconj( $fft[1] )) ) / $Npoints;


    my ($corr_max, @corr_offset ) = max2d_ind( $corr->re );

    say "best offset: @corr_offset";

    # exit;
    # # correlation plot
    # gplot( globalwith => 'image',
    #        square => 1,
    #        extracmds => 'set yrange [*:*] reverse',
    #        re $corr
    #      );

    my @mounted_size = $mounted[0]->dims;
    shift @mounted_size;
    return (@corr_offset, @mounted_size);
  }
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

__END__
