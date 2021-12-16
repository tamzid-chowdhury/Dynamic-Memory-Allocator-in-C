#include <stdio.h>
#include "sfmm.h"

int main(int argc, char const *argv[]) {
    double* ptr = sf_malloc(120);

    sf_show_heap();

    printf("--------------------------------------------------------\n");

    double* ptr2 = sf_malloc(220);

    sf_show_heap();

    printf("--------------------------------------------------------\n");

    sf_free(ptr);

    sf_show_heap();

    printf("--------------------------------------------------------\n");

    double* ptr3 = sf_malloc(64);

    sf_show_heap();

    printf("--------------------------------------------------------\n");

    double* ptr4 = sf_malloc(550);

    sf_show_heap();

    printf("--------------------------------------------------------\n");

    sf_free(ptr2);

    sf_show_heap();

    printf("--------------------------------------------------------\n");

    sf_free(ptr3);

    sf_show_heap();

    printf("--------------------------------------------------------\n");

    sf_free(ptr4);

    sf_show_heap();

    printf("--------------------------------------------------------\n");

    return EXIT_SUCCESS;
}
