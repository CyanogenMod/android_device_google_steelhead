#include <stdio.h>
#include <stdlib.h>
#include <linux/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cutils/properties.h>

typedef uint8_t  u8;

#include "steelhead_avr.h"

struct avr_led_rgb_vals prepare_leds(unsigned int rgb) {
    struct avr_led_rgb_vals reg;
    int red, green, blue;

    reg.rgb[0] = (rgb >> 16) & 0xFF;
    reg.rgb[1] = (rgb >> 8) & 0xFF;
    reg.rgb[2] = rgb & 0xFF;

    return reg;
}

void usage()
{
    puts("USAGE: avrlights [start] [acount] [color1] [color2] ...\n"
        "avrlights allows for the updating of ring LEDs on the Nexus Q.\n"
        "start -- start LED (0 is the mute LED, 1 is the 'top' LED\n"
        "count -- the number of LEDs to update\n"
        "color -- a RGB color in base 8, 10, or 16\n\n"                                                                                                                                                                           
        "EXAMPLES:\n\n"
        "Set all LEDs to a bright teal\n"
        "  avrlights 65535\n"
        "Make a small rainbow\n"
        "  avrlights 1 5 0xff 0x8888 0xff00 0x888800 0xff0000\n"
        "Alternate the black and blue, ending with a long blue steak\n"
        "  avrlights 4 16 0x88 0x880000 0x88 0x880000 0x88\n"
        "Turn off all LEDs\n"
        "  avrlights 0\n"
        "Reset to default colors and print usage\n"
        "  avrlights");
}

int main( int argc, char *argv[] ) {

    int fd = open( "/dev/leds", O_RDWR );

    // On the heap
    unsigned int * colors = (unsigned int *) NULL;
    struct avr_led_set_range_vals *reg =
        (struct avr_led_set_range_vals *) NULL;

    // On the stack
    u8 start = 0;
    u8 count = 0;
    int res = 0;
    char ** vptr = NULL;
    char * endptr = NULL;
    struct avr_led_rgb_vals color;

    if ( fd < 0 )
    {
        perror( "open" );
        res = 1;
        goto __bail;
    }

    unsigned int maxled = 0;
    char property[PROPERTY_VALUE_MAX];
    unsigned int rgb = 14428;   // 14428 is our default color

    ioctl( fd, AVR_LED_GET_MODE, &maxled );
    if ( maxled != 0x1 ) {
        maxled = 0x1; //AVR_LED_MODE_HOST_AUTO_COMMIT
        ioctl( fd, AVR_LED_SET_MODE, &maxled );
    }

    res = ioctl( fd, AVR_LED_GET_COUNT, &maxled );

    if ( res ) {
        fprintf( stderr, "Failed to get LED count\n" );
        goto __closeshop;
    }

    // Don't forget about the mute LED
    maxled++;

    // Make an array AFTER we know the LED count
    colors = calloc( maxled, sizeof( unsigned int ) );
    if ( colors == NULL )
    {
        perror("calloc");
        res = 1;
        goto __liberate;
    }

    // If we have < 2 arguments (remember that argv[0] is always there)
    if ( argc < 3 )
    {
        if ( argc == 1 )
            usage();

        start = 0;
        count = maxled;
        // Set vptr to point to either our color or NULL.
        // If NULL, then we stick with the default.
        vptr = argv+1;
    }
    else
    {
        // Safely pull in start and count; truncate the input, if needed
        errno = 0;
        start = (u8) strtol( argv[1], &endptr, 10 );

        // Validate our start
        if( (*endptr) || (errno == EINVAL) )
        {
            fprintf( stderr, "START is invalid\n" );
            usage();
            res = 1;
            goto __liberate;
        }

        errno = 0;
        count = (u8) strtol( argv[2], &endptr, 10 );

        // Validate our count
        if( (*endptr) || (errno == EINVAL) )
        {
            fprintf( stderr, "COUNT is invalid\n" );
            usage();
            res = 1;
            goto __liberate;
        }

        // Start LED sanity check
        if ( start > maxled )
        {
            fprintf( stderr, "Start LED [%d] out of range [0, %d]\n",
                    start, maxled );
            res = 1;
            goto __liberate;
        }

        // If there's no work to do
        if ( count == 0 )
        {
            res = 0;
            goto __liberate; // we're done here
        }

        // Make sure not to overshoot our LEDs
        if ( start + count > maxled )
        {
            fprintf( stderr, "Operation will overshoot maxled. (%d > %d)\n",
                    start+count, maxled );
            res = 1;
            goto __liberate;
        }

        // Set vptr to point to either our color or NULL.
        // If NULL, then we stick with the default.
        vptr = argv+3;
    }

    // The user shouldn't have to worry about the fact that
    // the mute LED is updated differently, so let's tweak things
    if ( start == 0 )
    {

        // If we've actually got a color argument
        if ( *vptr )
        {
            // strtol treats base 0 as base 8, 10, or 16
            rgb = strtol( *vptr, (char**)NULL, 0 ); // take in color
            vptr++; // next color
        }
        // Else, just the default RGB value
        color = prepare_leds( rgb ); // turn it into a struct
        res = ioctl( fd, AVR_LED_SET_MUTE, &color ); // update the mute LED

        count--; // we've got one less LED to touch
        start++; // start on the next LED in our sequence

        // If setting the mute LED doesn't go so well
        if ( res )
        {
            fprintf( stderr, "Failed to update the mute LED\n" );
            goto __liberate;
        }

    }

    // If there's no more work to do
    if ( count == 0 )
    {
        res = 0;
        goto __liberate; // we're done here
    }

    // This struct can get kinda large.
    // We allocate space for the struct itself and all of the RGB
    // structs that it may contain.
    //
    // space required = space for bare update struct + 
    //  ( number of LEDs to update * space for each color struct )
    //
    // See http://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html

    reg = ( struct avr_led_set_range_vals * ) 
        malloc( sizeof( struct avr_led_set_range_vals ) +
                sizeof( struct avr_led_rgb_vals ) * count );

    if ( reg == NULL )
    {
        perror("malloc");
        res = 1;
        goto __liberate;
    }

    // Correct for the decision to represent the mute LED as LED[0]
    // to make sure that the AVR stays happy. Our LED[1] is the
    // AVR's LED[0]
    reg->start = start - 1;

    reg->count = count;
    reg->rgb_triples = count;

    int i;
    for ( i = 0; i < count; i++ )
    {
        // If we've still got color arguments
        if( *vptr )
        {
            // strtol treats base 0 as base 8, 10, or 16
            rgb = strtol( *vptr, (char**)NULL, 0 );
            vptr++;
        }
        // Else: just repeat the last RGB value we had
        // If we aren't passed any at all, we just use the default one
        reg->rgb_vals[i] = prepare_leds( rgb );
    }

    res = ioctl( fd, AVR_LED_SET_RANGE_VALS, reg );

__liberate:
    // POSIX dictates that freeing NULL pointers results in no action taken.
    // Therefore, if we free something already freed, we just waste a syscall.
    free( colors );
    colors = (unsigned int *) NULL;
    free( reg );
    reg = (struct avr_led_set_range_vals *) NULL;


__closeshop:
    close( fd );

__bail:
    // !! reduces a number to either 0 or 1 while maintaining truthiness
    return !!res;

}
