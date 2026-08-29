#include "il2bridge/loader/handlers.h"
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

// Mirrors the handler's assumed string prefix; storage is allocated separately.
struct FakeIl2CppStringLayout {
    void* klass;
    void* monitor;
    int32_t length;
    uint16_t chars[1];
};

// claimed_length may exceed the allocated UTF-16 storage for clamp tests.
std::vector<unsigned char> make_fake_string(int32_t claimed_length, size_t real_char_count, uint16_t fill_char) {
    size_t bytes = offsetof(FakeIl2CppStringLayout, chars) + real_char_count * sizeof(uint16_t);
    std::vector<unsigned char> buf(bytes, 0);
    auto* str = reinterpret_cast<FakeIl2CppStringLayout*>(buf.data());
    str->length = claimed_length;
    for (size_t i = 0; i < real_char_count; ++i) {
        str->chars[i] = fill_char;
    }
    return buf;
}

// Captures stderr and restores fd 2 if the callback throws.
template <typename Fn>
std::string capture_stderr(Fn&& fn) {
    fflush(stderr);

    FILE* tmp = tmpfile();
    REQUIRE(tmp != nullptr);

    int saved_fd = dup(fileno(stderr));
    if (saved_fd < 0) {
        fclose(tmp);
    }
    REQUIRE(saved_fd >= 0);

    dup2(fileno(tmp), fileno(stderr));

    struct Restore {
        int saved_fd;
        FILE* tmp;
        ~Restore() {
            fflush(stderr);
            dup2(saved_fd, fileno(stderr));
            close(saved_fd);
            fclose(tmp);
        }
    } restore{saved_fd, tmp};

    fn();

    fflush(stderr);
    rewind(tmp);
    std::string result;
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), tmp)) > 0) {
        result.append(chunk, n);
    }
    return result;
}

} // namespace

TEST_CASE("looks up a known handler by name", "[handlers]") {
    const HandlerEntry* entry = handler_lookup("log-1string-arg");
    REQUIRE(entry != nullptr);
    REQUIRE(std::strcmp(entry->name, "log-1string-arg") == 0);
    REQUIRE(entry->function_pointer != nullptr);
}

TEST_CASE("looks up the other two v1 handlers by name", "[handlers]") {
    REQUIRE(handler_lookup("skip-return-void") != nullptr);
    REQUIRE(handler_lookup("skip-return-null-object") != nullptr);
}

TEST_CASE("returns null for an unknown handler name", "[handlers]") {
    REQUIRE(handler_lookup("does-not-exist") == nullptr);
}

TEST_CASE("log-1string-arg decodes ASCII and substitutes ? for non-ASCII chars", "[handlers]") {
    const HandlerEntry* entry = handler_lookup("log-1string-arg");
    REQUIRE(entry != nullptr);
    auto handler = reinterpret_cast<void (*)(void*)>(entry->function_pointer);

    auto buf = make_fake_string(/*claimed_length=*/4, /*real_char_count=*/4, /*fill_char=*/0);
    auto* str = reinterpret_cast<FakeIl2CppStringLayout*>(buf.data());
    str->chars[0] = 'H';
    str->chars[1] = 'i';
    str->chars[2] = 0x00E9; // non-ASCII 'e' with acute accent -> must become '?'
    str->chars[3] = '!';

    std::string output = capture_stderr([&]() { handler(str); });
    REQUIRE(output == "[il2bridge] Hi?!\n");
}

TEST_CASE("log-1string-arg reports a null string without dereferencing it", "[handlers]") {
    const HandlerEntry* entry = handler_lookup("log-1string-arg");
    REQUIRE(entry != nullptr);
    auto handler = reinterpret_cast<void (*)(void*)>(entry->function_pointer);

    std::string output = capture_stderr([&]() { handler(nullptr); });
    REQUIRE(output == "[il2bridge] log-1string-arg: null string\n");
}

TEST_CASE("log-1string-arg bounds an absurd length instead of reading unbounded memory", "[handlers]") {
    const HandlerEntry* entry = handler_lookup("log-1string-arg");
    REQUIRE(entry != nullptr);
    auto handler = reinterpret_cast<void (*)(void*)>(entry->function_pointer);

    constexpr size_t kBackingChars = 65536;

    auto buf = make_fake_string(/*claimed_length=*/1'000'000'000, kBackingChars, /*fill_char=*/'X');
    auto* str = reinterpret_cast<FakeIl2CppStringLayout*>(buf.data());

    std::string output = capture_stderr([&]() { handler(str); });

    // The line is assembled into a bounded buffer and truncated to fit: it is
    // the prefix followed by 'X' runs and a newline, never the full length.
    REQUIRE(output.rfind("[il2bridge] X", 0) == 0);
    REQUIRE(output.back() == '\n');
    REQUIRE(output.size() < 512);
    const std::string body = output.substr(std::strlen("[il2bridge] "), output.size() - std::strlen("[il2bridge] ") - 1);
    REQUIRE(body.find_first_not_of('X') == std::string::npos);
}

TEST_CASE("skip-return-void can be called through its pointer without crashing", "[handlers]") {
    const HandlerEntry* entry = handler_lookup("skip-return-void");
    REQUIRE(entry != nullptr);
    auto handler = reinterpret_cast<void (*)(void)>(entry->function_pointer);
    handler(); // no observable state; must simply not crash
}

TEST_CASE("skip-return-null-object returns NULL through its pointer", "[handlers]") {
    const HandlerEntry* entry = handler_lookup("skip-return-null-object");
    REQUIRE(entry != nullptr);
    auto handler = reinterpret_cast<void* (*)(void)>(entry->function_pointer);
    REQUIRE(handler() == nullptr);
}
