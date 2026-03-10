#include <gtest/gtest.h>
#include <vesper/vesper.h>

TEST(UrlDecoding, decodeURL) {
    EXPECT_EQ(decodeURL("%20"), " ");
    EXPECT_NE(decodeURL("% 20"), " ");
    EXPECT_EQ(decodeURL("test"), "test");
}