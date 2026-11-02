#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>
#include <vector>

#include "LTable.hpp"
#include "LTool.hpp"

TEST_CASE("LTable builds and prints tables", "[LTable]") {
    LTable table;
    table.header({"Name", "Score"})
        .add_values("Alice", 98)
        .add_row(std::vector<std::string> {"Bob", "87"});

    const auto shape = table.shape();
    REQUIRE(shape.first == 3);
    REQUIRE(shape.second == 2);

    const auto text = table.str();
    REQUIRE(text.find("Name") != std::string::npos);
    REQUIRE(text.find("Alice") != std::string::npos);
    REQUIRE(text.find("98") != std::string::npos);
    REQUIRE(text.find("Bob") != std::string::npos);

    std::ostringstream out;
    out << table;
    REQUIRE(out.str() == text);
}

TEST_CASE("LTable keeps tabulate escape hatches", "[LTable]") {
    auto row = LTable::make_row_values("Package", "Enabled");

    LTable table(row);
    table.add_values("tabulate", true);

    table.native().format().padding(0);
    table.column(1).format().font_align(tabulate::FontAlign::right);

    const auto markdown = table.markdown();
    REQUIRE(markdown.find("Package") != std::string::npos);
    REQUIRE(markdown.find("tabulate") != std::string::npos);
}

TEST_CASE("LTable uses unicode borders and folds long cells", "[LTable]") {
    LTable table;
    table.max_cell_width(8).max_cell_rows(3);
    table.header({"Name", "Note"})
        .add_values("Alice", "abcdefghijklmnopqrstuvwxyz")
        .add_values("中文", "一二三四五六七八九十");

    const auto text = table.str();
    REQUIRE(text.find("┌") != std::string::npos);
    REQUIRE(text.find("┬") != std::string::npos);
    REQUIRE(text.find("└") != std::string::npos);
    REQUIRE(text.find("…") != std::string::npos);

    REQUIRE(text.find("qrstuvw…") != std::string::npos);
    REQUIRE(text.find("一二三四") != std::string::npos);
}
