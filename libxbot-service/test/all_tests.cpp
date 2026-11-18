//
// Created by clemens on 3/22/24.
//
#include "CppUTest/CommandLineTestRunner.h"

IMPORT_TEST_GROUP(BufferTests);
IMPORT_TEST_GROUP(DispatcherTests);
IMPORT_TEST_GROUP(ServiceTests);

int main(int argc, char** argv) {
  return RUN_ALL_TESTS(argc, argv);
}
