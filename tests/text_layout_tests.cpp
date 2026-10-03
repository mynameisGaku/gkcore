#include "../src/text/TextLayout.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void Require(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

void TestTabsExpandToFourSpaces() {
    gk::String result;
    gk::String error;
    const char source[] = "a\tb\t";
    Require(gk::detail::ExpandTextTabs(source, result, error),
            "tab expansion accepts bounded UTF-8 input");
    Require(std::strcmp(result.CStr(), "a    b    ") == 0,
            "each tab expands to exactly four spaces");
}

void TestUtf8BytesArePreserved() {
    gk::String result;
    gk::String error;
    const char source[] = "日本語\tOK";
    Require(gk::detail::ExpandTextTabs(source, result, error),
            "tab expansion accepts multibyte UTF-8 bytes");
    const char expected[] = "日本語    OK";
    Require(result.Length() == sizeof(expected) - 1 &&
                std::memcmp(result.CStr(), expected, sizeof(expected)) == 0,
            "tab expansion preserves all non-tab UTF-8 bytes");
}

void TestNoTabsAndEmptyInput() {
    gk::String result;
    gk::String error;
    Require(gk::detail::ExpandTextTabs("plain", result, error),
            "tab expansion accepts text without tabs");
    Require(std::strcmp(result.CStr(), "plain") == 0, "text without tabs is unchanged");
    Require(gk::detail::ExpandTextTabs("", result, error), "empty text is accepted");
    Require(result.Empty(), "empty text remains empty");
}

void TestFailurePreservesOutput() {
    gk::String result;
    gk::String error;
    Require(result.Assign("previous"), "test can seed previous output");
    Require(!gk::detail::ExpandTextTabs(nullptr, result, error), "null input is rejected");
    Require(std::strcmp(result.CStr(), "previous") == 0,
            "failed expansion leaves previous output unchanged");

    char oversized[4098]{};
    std::memset(oversized, 'x', sizeof(oversized) - 1);
    Require(!gk::detail::ExpandTextTabs(oversized, result, error),
            "input beyond the bounded length is rejected");
    Require(std::strcmp(result.CStr(), "previous") == 0,
            "oversized input leaves previous output unchanged");
}

}

int main() {
    TestTabsExpandToFourSpaces();
    TestUtf8BytesArePreserved();
    TestNoTabsAndEmptyInput();
    TestFailurePreservesOutput();
    return 0;
}
