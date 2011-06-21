#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct
{
	uint16_t slot;
	uint16_t point[100];
	uint16_t adc0[100];
	uint16_t adc1[100];
	uint16_t adc2[100];
	uint16_t adc3[100];
} cald_response_t;
