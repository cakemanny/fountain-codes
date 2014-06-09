#ifndef __RANDOM_H__
#define __RANDOM_H__

/* A better way of handling random numbers - and moreover known and
   consistent, Code is translated from a Matteo Italia stack overeflow answer
*/

#define RAND_MAX 32767

typedef struct randgen_s {
    unsigned long seed;     // Extract an use in the next call
    int result;             // Extract this as the result
} randgen_s;


randgen_s next_rand(unsigned long seed) {
    unsigned long next = seed * 1103515245 + 12345;
    return (randgen_s) {
        .seed = next,
        .result = (unsigned int)(next / 65536) % 32768;
    };
}


#endif
