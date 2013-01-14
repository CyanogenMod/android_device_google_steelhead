#include <stdio.h>
#include <linux/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

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

int main(int argc, char *argv[]) {

    int fd = open("/dev/leds",O_RDWR);
    int res = 0;
    int value = 0;

    res = ioctl(fd,AVR_LED_GET_COUNT,&value);

    if (!res) {
        printf("This device has %d LEDs\n",value);
    } else {
        printf("Failed to get LED count\n");
        return 1;
    }

    if (argc==2) {
        unsigned int rgb = atoi(argv[1]);
        struct avr_led_rgb_vals reg = prepare_leds(rgb);
        int val = 1;

        printf("Setting all LEDs to %x\n",rgb);
        printf("Component breakdown is %x/%x/%x\n",reg.rgb[0],reg.rgb[1],reg.rgb[2]);
        ioctl(fd,AVR_LED_SET_MUTE,&reg);
        res = ioctl(fd,AVR_LED_SET_ALL_VALS,&reg);
    } else if (argc>2) {
        unsigned int ledcount = argc-1;
        unsigned int rgb;
        unsigned int colorstrip = value;
        unsigned int padding = 0;
        unsigned int val = 0;
        unsigned int i = 0;
        struct avr_led_rgb_vals color;
        struct avr_led_set_range_vals *reg;

        reg = malloc(sizeof(reg));

        /* Limit to 8 colors */
        if (ledcount > 8) ledcount = 8;
        colorstrip = (unsigned int)(value / ledcount);
        padding = value - (ledcount * colorstrip);

        /* Set the mute LED to the first listed color */
        color = prepare_leds(atoi(argv[1]));
        ioctl(fd,AVR_LED_SET_MUTE,&color);

        for (val=0; val < ledcount; val++) {
            rgb = (atoi(argv[val+1]));
            color = prepare_leds(rgb);
            for (i=0; i<colorstrip; i++) {
                //printf("Setting %d to 0x%.6x\n",((val*colorstrip)+i),rgb);
                reg->rgb_vals[(val*colorstrip)+i] = color;
            }
        }
        for (val=padding; val > 0; val--) {
            reg->rgb_vals[value-val] = color;
            //printf("Setting %d to 0x%.6x\n",value-val,rgb);
        }

        reg->start = 0;
        reg->count = value;
        reg->rgb_triples = value;
        res = ioctl(fd,AVR_LED_SET_RANGE_VALS,reg);
        free(reg);
    }

    close(fd);
    return (res ? 1 : 0);
}

