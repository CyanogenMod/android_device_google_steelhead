#include <stdio.h>
#include <stdlib.h>


// This is modified code from http://www.cs.rit.edu/~ncs/color/t_convert.html
void HSVtoRGB( double *r, double *g, double *b, double h, double s, double v )
{
    int i;
    double f, p, q, t;
    if( s == 0 ) {
        *r = *g = *b = v;
        return;
    }
    h *= 6;
    i = (int) h;
    f = h - i;
    p = v * ( 1 - s );
    q = v * ( 1 - s * f );
    t = v * ( 1 - s * ( 1 - f ) );
    switch( i ) {
        case 0:
            *r = v;
            *g = t;
            *b = p;
            break;
        case 1:
            *r = q;
            *g = v;
            *b = p;
            break;
        case 2:
            *r = p;
            *g = v;
            *b = t;
            break;
        case 3:
            *r = p;
            *g = q;
            *b = v;
            break;
        case 4:
            *r = t;
            *g = p;
            *b = v;
            break;
        default:
            *r = v;
            *g = p;
            *b = q;
            break;
    }
}

int main ( int argc, char ** argv )
{
    double hin, sin, vin;
    double hmax, smax, vmax;
    double h, s, v;

    double r, g, b;
    // This allows for some cool stuff when being called.
    // We get HSV by pulling *in and *max and calculating
    // the true HSV numbers by dividing *in by *max. This
    // allows for easy calling by BASH, for example, which
    // lacks any semblance of real floating number support.

    if ( argc == 4 )
    {
        h = strtod( argv[1], NULL );
        s = strtod( argv[2], NULL );
        v = strtod( argv[3], NULL );
    }
    else if ( argc == 7 )
    {
        hin = strtod( argv[1], NULL );
        hmax = strtod( argv[2], NULL );

        sin = strtod( argv[3], NULL );
        smax = strtod( argv[4], NULL );

        vin = strtod( argv[5], NULL );
        vmax = strtod( argv[6], NULL );

        h = hin / hmax;
        s = sin / smax;
        v = vin / vmax;
    }
    else return -1;

    HSVtoRGB( &r, &g, &b, h, s, v );

    r = (int) (r * 255) % 256;
    g = (int) (g * 255) % 256;
    b = (int) (b * 255) % 256;

    printf("0x%.2x%.2x%.2x", (int) r, (int) g, (int) b);

    return ( (int) r << 16 ) + ( (int) g << 8 ) + (int) b;
}
