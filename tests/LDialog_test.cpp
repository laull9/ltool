#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "LDialog.hpp"

TEST_CASE("LDialog exposes bundled portable-file-dialogs capability", "[LDialog]") {
    REQUIRE(LDialog::Capabilities::portable_file_dialogs);
    REQUIRE(LDialog::Capabilities::bundled);
}

TEST_CASE("LDialog converts file filters without showing UI", "[LDialog]") {
    const auto filters = LDialog::detail::to_native_filters({
        {"Images", {"*.png", "*.jpg"}},
        {"Text", "*.txt"},
        {"", {}},
    });

    REQUIRE(filters == std::vector<std::string> {
        "Images", "*.png *.jpg",
        "Text", "*.txt",
        "Files", "*",
    });
}

TEST_CASE("LDialog option structs have useful defaults", "[LDialog]") {
    LDialog::OpenFileOptions open;
    LDialog::SaveFileOptions save;
    LDialog::SelectFolderOptions folder;
    LDialog::MessageOptions message;

    REQUIRE(open.title == "Open file");
    REQUIRE_FALSE(open.multiselect);
    REQUIRE(save.title == "Save file");
    REQUIRE_FALSE(save.force_overwrite);
    REQUIRE(folder.title == "Select folder");
    REQUIRE_FALSE(folder.force_path);
    REQUIRE(message.choice == LDialog::Choice::OkCancel);
    REQUIRE(message.icon == LDialog::Icon::Info);
}

TEST_CASE("LDialog async handles are default-safe and typed", "[LDialog]") {
    LDialog::OpenFileAsync open_handle;
    LDialog::PathAsync path_handle;
    LDialog::MessageAsync message_handle;
    LDialog::VoidAsync void_handle;

    REQUIRE_FALSE(open_handle.valid());
    REQUIRE_FALSE(path_handle.ready());
    REQUIRE_FALSE(message_handle.ready());
    REQUIRE_FALSE(void_handle.valid());

    LDialog::OpenFileCallback open_callback = [](const std::vector<std::string>&) {};
    LDialog::PathCallback path_callback = [](const std::string&) {};
    LDialog::MessageCallback message_callback = [](LDialog::Button) {};
    LDialog::ConfirmCallback confirm_callback = [](bool) {};
    LDialog::VoidCallback void_callback = []() {};

    REQUIRE(open_callback);
    REQUIRE(path_callback);
    REQUIRE(message_callback);
    REQUIRE(confirm_callback);
    REQUIRE(void_callback);

    static_assert(std::is_same<
        decltype(LDialog::open_file_async(std::declval<LDialog::OpenFileOptions>(),
                                          std::declval<LDialog::OpenFileCallback>())),
        LDialog::OpenFileAsync>::value, "open_file_async should return OpenFileAsync");

    static_assert(std::is_same<
        decltype(LDialog::open_single_file_async(std::declval<LDialog::OpenFileOptions>(),
                                                 std::declval<LDialog::PathCallback>())),
        LDialog::PathAsync>::value, "open_single_file_async should return PathAsync");

    static_assert(std::is_same<
        decltype(LDialog::save_file_async(std::declval<LDialog::SaveFileOptions>(),
                                          std::declval<LDialog::PathCallback>())),
        LDialog::PathAsync>::value, "save_file_async should return PathAsync");

    static_assert(std::is_same<
        decltype(LDialog::select_folder_async(std::declval<LDialog::SelectFolderOptions>(),
                                              std::declval<LDialog::PathCallback>())),
        LDialog::PathAsync>::value, "select_folder_async should return PathAsync");

    static_assert(std::is_same<
        decltype(LDialog::message_async(std::declval<LDialog::MessageOptions>(),
                                        std::declval<LDialog::MessageCallback>())),
        LDialog::MessageAsync>::value, "message_async should return MessageAsync");

    static_assert(std::is_same<
        decltype(LDialog::confirm_async(std::declval<std::string>(),
                                        std::declval<std::string>(),
                                        std::declval<LDialog::ConfirmCallback>())),
        LDialog::MessageAsync>::value, "confirm_async should return MessageAsync");

    static_assert(std::is_same<
        decltype(LDialog::notify_async(std::declval<LDialog::NotifyOptions>(),
                                       std::declval<LDialog::VoidCallback>())),
        LDialog::VoidAsync>::value, "notify_async should return VoidAsync");
}
