#!/usr/bin/perl
use strict;
use warnings;
use feature qw(say);

use blib '/home/dima/PDL';

use PDL;
use PDL::IO::GD;
use PDL::Graphics::Gnuplot;
use PDL::NiceSlice;
use PDL::OpenCV qw(Smooth Sobel %cvdef);
use PDL::LinearAlgebra;
use PDL::Complex;
use PDL::FFTW;

my %image;

# for my $name_file ( [qw(img  /tmp/tst1.png)],
#                     [qw(pano /tmp/tst2.png)] )
for my $name_file ( [qw(img  ironcut.png)],
                    [qw(pano pano.png)] )
{
  my ($name, $file) = @$name_file;

  my $img   = PDL::IO::GD->new( $file )->to_pdl->float / 255.0;

#  $img = float random(4,8,3); # test code

  my $gradx = $img->copy;
  my $grady = $img->copy;

  Smooth( $img, $img, $cvdef{CV_GAUSSIAN}, 9, 9, 0, 0 );
  $img /= 3*3;

  $image{$name}{orig} = $img;
  $image{$name}{size} = [$img->dims];
  pop $image{$name}{size};

  # edges looking at each channel separately
  $image{$name}{edgecomponents}{x} = $gradx;
  $image{$name}{edgecomponents}{y} = $grady;

  Sobel( $img, $gradx, 1, 0, $cvdef{CV_SCHARR} );
  Sobel( $img, $grady, 0, 1, $cvdef{CV_SCHARR} );

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
my ($dx,$dy) = correlate_conj( $image{pano}{edges},
                               $image{img} {edges} );




# computes the peak of the correlation of img0 and conj(img1)
sub correlate_conj
{
  my @imgs = @_;

  # mount the images into larger, equal matrix
  my $sizes = pdl( [$imgs[0]->dims], [$imgs[1]->dims] );
  my @mountsize = PDL::list( $sizes->transpose->maximum->(1:-1) * 2 );

  say "mounted size: @mountsize";

  my @mounted;
  foreach my $img(@imgs)
  {
    # mount the image
    my $mountedimg = cplx zeros( 2, @mountsize );
    $mountedimg->(:, 0:$img->dim(1)-1, 0:$img->dim(2)-1 ) .= $img;
    push @mounted, $mountedimg;
  }

  return correlate_conj_mounted( @mounted );



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

    my @fft = map { cplx fftw($_) } @mounted;
    my $corr = cplx ifftw( $fft[0] * Cconj( $fft[1] ) ) / $Npoints;






    gplot( globalwith => 'image',
           square => 1,
           extracmds => 'set yrange [*:*] reverse',
           re $corr
         );
    sleep(1000);





    # say $corr(:,0:1,0:1);
    # say PDL::Complex::sum( $mounted[0] * Cconj $mounted[1] );
    # my $panoshift = $mounted[0]->(:,1:-1,:)->glue(1, $mounted[0]->(:,0,:) ) ;
    # say PDL::Complex::sum($panoshift * Cconj $mounted[1] );


  }
}

__END__
need     avgover alias to average
similarly minover and maxover
