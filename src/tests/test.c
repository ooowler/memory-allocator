#include "test.h"
#include "print_test.h"


static void printf_tests_result(bool res_test, char *msg) {
    if (res_test) printf("%s: +\n", msg);
    printf("%s: -\n", msg);
}


bool test_alloc() {
    printf("Test 1: allocation... \n");

    void *block = _malloc(BLOCK_SIZE);
    debug_heap(stdout, HEAP_START);

    if (!block) {
        print_error();
        return false;
    }

    _free(block);
    print_success();
    return true;
}

bool test_free_one_block() {
    printf("Test 2: Free one block... \n");

    void *block_1 = _malloc(BLOCK_SIZE);
    void *block_2 = _malloc(BLOCK_SIZE);

    if (!block_1 || !block_2) {
        print_error();
        return false;
    }

    debug_heap(stdout, HEAP_START);
    _free(block_1);
    debug_heap(stdout, HEAP_START); // end of the test

    _free(block_2); // free after the test
    print_success();
    return true;
}

// like a prev test, but first of all _free x2
bool test_free_two_blocks() {
    printf("Test 3: Free one block... \n");

    void *block_1 = _malloc(BLOCK_SIZE);
    void *block_2 = _malloc(BLOCK_SIZE);

    if (!block_1 || !block_2) {
        print_error();
        return false;
    }

    debug_heap(stdout, HEAP_START);

    _free(block_1);
    _free(block_2);

    debug_heap(stdout, HEAP_START);
    print_success();
    return true;
}

bool test_region_extends() {
    printf("Test 4: Region extends... \n");
    void *region1 = _malloc(HEAP_SIZE);
    void *region2 = _malloc(HEAP_SIZE);
    if (!region1 || !region2) {
        print_error();
        return false;
    }

    debug_heap(stdout, HEAP_START);
    _free(region1);
    _free(region2);
    debug_heap(stdout, HEAP_START);

    print_success();
    return true;
}

// like a prev test, but malloc HUGE_BLOCK
bool test_region_does_not_extend() {


    printf("Test 5: Region does not extend... \n");
    void *region1 = _malloc(HEAP_SIZE);
    void *region2 = _malloc(HUGE_BLOCK);
    if (!region1 || !region2) {
        print_error();
        return false;
    }


    debug_heap(stdout, HEAP_START);
    _free(region1);
    _free(region2);
    debug_heap(stdout, HEAP_START);

    print_success();
    return true;

}

int test_run() {
    bool test_1 = test_alloc();
    bool test_2 = test_free_one_block();
    bool test_3 = test_free_two_blocks();
    bool test_4 = test_region_extends();
    bool test_5 = test_region_does_not_extend();

    printf_tests_result(test_1, "tests/test_alloc");
    printf_tests_result(test_1, "test_free_one_block");
    printf_tests_result(test_1, "test_free_two_blocks");
    printf_tests_result(test_1, "test_region_extends");
    printf_tests_result(test_1, "test_region_does_not_extend");

    if (test_1 && test_2 && test_3 && test_4 && test_5) {
        printf("Summary result: SUCCESS\n");
        return 0;
    }

    printf("Summary result: FAILED\n");
    return 1;
}
