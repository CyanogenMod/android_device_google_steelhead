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

    if (argc>1) {
        unsigned int rgb = atoi(argv[1]);
        struct avr_led_rgb_vals reg = prepare_leds(rgb);
        int val = 1;

        printf("Setting LEDs to %x\n",rgb);
        printf("Component breakdown is %x/%x/%x\n",reg.rgb[0],reg.rgb[1],reg.rgb[2]);
        res = ioctl(fd,AVR_LED_SET_ALL_VALS,&reg);
    }

    /*if (argc>1) {
        unsigned int rgb = atoi(argv[1]);
        struct avr_led_rgb_vals color = prepare_leds(rgb);
        struct avr_led_set_range_vals reg;
        int val = 1;

        reg.start = 0;
        reg.count = 20;
        reg.rgb_triples = 2;
        reg.rgb_vals[0] = color;
        reg.rgb_vals[1] = prepare_leds(8388608);
        res = ioctl(fd,AVR_LED_SET_RANGE_VALS,&reg);
    }*/

    close(fd);
    return ( res ? -1 : 0 );

}

