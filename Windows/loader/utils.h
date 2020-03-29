#ifndef LIN_UTILS_H_
#define LIN_UTILS_H_	1

#include <stdio.h>
#include <stdlib.h>

/* useful macro for handling error codes */
#define DIE(assertion, call_description)				\
	do {								\
		if (assertion) {					\
			fprintf(stderr, "(%s, %d): ",			\
					__FILE__, __LINE__);		\
			perror(call_description);			\
			exit(EXIT_FAILURE);				\
		}							\
	} while (0)

#define ERR_AP	0.0000000001f

/* echivalentul functie ceil din libraria math.h */
static int ceil_(float x)
{
	int x_int = (int)x;

	if (x - x_int < ERR_AP)
		return x_int;

	return x_int + 1;
}

#endif
