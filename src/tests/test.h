#ifndef TESTS
#define TESTS
#include <stdbool.h>
#define HEAP_SIZE 8192
#define BLOCK_SIZE 8
#define HUGE_BLOCK 131072

bool test_alloc();
bool test_free_one_block();
bool test_free_two_blocks();
bool test_region_extends();
bool test_region_does_not_extend();

int test_run();

#endif
