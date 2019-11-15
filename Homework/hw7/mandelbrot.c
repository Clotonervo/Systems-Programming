// Compile: gcc -o mandelbrot -fopenmp mandelbrot.c

/*
  a) 4

  b) ./mandelbrot 0.27085 0.27100 0.004640 0.004810 1000 8192 pic.ppm

   - 1: 40.576540

   - 2: 21.348374

   - 4: 11.166523

   - 8: 10.948165

*/


/*
  This program is an adaptation of the Mandelbrot program
  from the Programming Rosetta Stone, see
  http://rosettacode.org/wiki/Mandelbrot_set
  Compile the program with:
  gcc -o mandelbrot -O4 mandelbrot.c
  Usage:
 
  ./mandelbrot <xmin> <xmax> <ymin> <ymax> <maxiter> <xres> <out.ppm>
  Example:
  ./mandelbrot 0.27085 0.27100 0.004640 0.004810 1000 1024 pic.ppm
  The interior of Mandelbrot set is black, the levels are gray.
  If you have very many levels, the picture is likely going to be quite
  dark. You can postprocess it to fix the palette. For instance,
  with ImageMagick you can do (assuming the picture was saved to pic.ppm):
  convert -normalize pic.ppm pic.png
  The resulting pic.png is still gray, but the levels will be nicer. You
  can also add colors, for instance:
  convert -negate -normalize -fill blue -tint 100 pic.ppm pic.png
  See http://www.imagemagick.org/Usage/color_mods/ for what ImageMagick
  can do. It can do a lot.
*/
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <omp.h>
#include <string.h>
int main(int argc, char* argv[])
{
/* Parse the command line arguments. */
if (argc != 8) {
printf("Usage:   %s <xmin> <xmax> <ymin> <ymax> <maxiter> <xres> <out.ppm>\n", argv[0]);
printf("Example: %s 0.27085 0.27100 0.004640 0.004810 1000 1024 pic.ppm\n", argv[0]);
exit(EXIT_FAILURE);
  }

/* The window in the plane. */
const double xmin = atof(argv[1]);
const double xmax = atof(argv[2]);
const double ymin = atof(argv[3]);
const double ymax = atof(argv[4]);
/* Maximum number of iterations, at most 65535. */
const uint16_t maxiter = (unsigned short)atoi(argv[5]);
/* Image size, width is given, height is computed. */
const int xres = atoi(argv[6]);
const int yres = (xres*(ymax-ymin))/(xmax-xmin);
/* The output file name */
const char* filename = argv[7];
/* Open the file and write the header. */
FILE * fp = fopen(filename,"wb");
char *comment="# Mandelbrot set";/* comment should start with # */
/*write ASCII header to the file*/
fprintf(fp,
"P6\n# Mandelbrot, xmin=%lf, xmax=%lf, ymin=%lf, ymax=%lf, maxiter=%d\n%d\n%d\n%d\n",
          xmin, xmax, ymin, ymax, maxiter, xres, yres, (maxiter < 256 ? 256 : maxiter));

/* Precompute pixel width and height. */
double dx=(xmax-xmin)/xres;
double dy=(ymax-ymin)/yres;
double x, y; /* Coordinates of the current point in the complex plane. */
double u, v; /* Coordinates of the iterated point. */
int i,j; /* Pixel counters */
int k; /* Iteration counter */
printf("Number of threads= %s\n", getenv("OMP_NUM_THREADS"));

char*** list;
list = calloc(yres, sizeof(char**));

/*-------------------- Create Data Structure --------------*/
for(int q = 0; q < yres; q++){
  list[q] = calloc(xres, sizeof(char*));
    for(int r = 0; r < xres; r++){
     list[q][r] = calloc(6, sizeof(char));
  }
}

/*----------------------- Compute Data --------------------*/
double start = omp_get_wtime();

#pragma omp parallel for private(i,j,k,x)
for (j = 0; j < yres; j++) {
    y = ymax - j * dy;
for(i = 0; i < xres; i++) {
double u = 0.0;
double v= 0.0;
double u2 = u * u;
double v2 = v*v;
      x = xmin + i * dx;

/* iterate the point */
for (k = 1; k < maxiter && (u2 + v2 < 4.0); k++) {
            v = 2 * u * v + y;
            u = u2 - v2 + x;
            u2 = u * u;
            v2 = v * v;
      };

/* compute  pixel color and write it to file */
if (k >= maxiter) {
/* interior */
        list[j][i][0] = 0;
        list[j][i][1] = 0;
        list[j][i][2] = 0;
        list[j][i][3] = 0;
        list[j][i][4] = 0;
        list[j][i][5] = 0;
      }
else {
/* exterior */
unsigned char color[6];
        list[j][i][0] = k >> 8;
        list[j][i][1] = k & 255;
        list[j][i][2] = k >> 8;
        list[j][i][3] = k & 255;
        list[j][i][4] = k >> 8;
        list[j][i][5] = k & 255;
      }
    }
  }

  double end = omp_get_wtime();

printf("Time for computation: %f\n", end - start);


/*-------------------- Write all data to file ----------------*/
for(int q = 0; q < yres; q++){
  for(int r = 0; r < xres; r++){
    fwrite(list[q][r], 6, 1, fp);
  }
}

fclose(fp);
return 0;
}