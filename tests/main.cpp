#include <gtest/gtest.h>
#include <vesper/vesper.h>

int main(int argc, char **argv) {
    debugging = false;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}