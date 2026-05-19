#include <gtest/gtest.h>

#include "../src/cli/cli_parser.h"

TEST(CliParserTest, ParsesNegativeNumericValueAsOptionValue) {
    const std::vector<std::string> argv = {
        "gis-cli",
        "projection",
        "reproject",
        "--x",
        "-123.5",
        "--dst_srs",
        "EPSG:4326"
    };

    const auto args = gis::cli::parseArgs(argv);
    ASSERT_EQ(args.pluginName, "projection");
    ASSERT_EQ(args.positional.size(), 1u);
    EXPECT_EQ(args.positional[0], "reproject");
    ASSERT_EQ(args.params.count("x"), 1u);
    EXPECT_EQ(args.params.at("x"), "-123.5");
    ASSERT_EQ(args.params.count("dst_srs"), 1u);
    EXPECT_EQ(args.params.at("dst_srs"), "EPSG:4326");
}

TEST(CliParserTest, JoinsMultiTokenOptionValueUntilNextOption) {
    const std::vector<std::string> argv = {
        "gis-cli",
        "raster_manage",
        "overviews",
        "--levels",
        "2",
        "4",
        "8",
        "16",
        "--resampling",
        "average"
    };

    const auto args = gis::cli::parseArgs(argv);
    ASSERT_EQ(args.pluginName, "raster_manage");
    ASSERT_EQ(args.positional.size(), 1u);
    EXPECT_EQ(args.positional[0], "overviews");
    ASSERT_EQ(args.params.count("levels"), 1u);
    EXPECT_EQ(args.params.at("levels"), "2 4 8 16");
    ASSERT_EQ(args.params.count("resampling"), 1u);
    EXPECT_EQ(args.params.at("resampling"), "average");
}
