#include "il2bridge/loader/hooks.h"
#include <catch2/catch_test_macros.hpp>

namespace {
HookEntry make_entry(void* target) {
    HookEntry e{};
    e.target = target;
    e.detour = nullptr;
    e.type = HOOK_TYPE_BREAKPOINT;
    e.original_len = 1;
    e.disabled = false;
    return e;
}
}

TEST_CASE("adds an entry and retrieves it by handle", "[hooks_registry]") {
    hook_registry_reset_for_testing();
    int dummy;
    HookEntry entry = make_entry(&dummy);
    HookHandle handle{};
    REQUIRE(hook_registry_add(&entry, &handle));

    const HookEntry* got = hook_registry_get(handle);
    REQUIRE(got != nullptr);
    REQUIRE(got->target == &dummy);
}

TEST_CASE("find_by_address locates the right entry among several", "[hooks_registry]") {
    hook_registry_reset_for_testing();
    int a, b, c;
    HookHandle ha{}, hb{}, hc{};
    HookEntry ea = make_entry(&a);
    HookEntry eb = make_entry(&b);
    HookEntry ec = make_entry(&c);
    REQUIRE(hook_registry_add(&ea, &ha));
    REQUIRE(hook_registry_add(&eb, &hb));
    REQUIRE(hook_registry_add(&ec, &hc));

    const HookEntry* found = hook_registry_find_by_address(&b);
    REQUIRE(found != nullptr);
    REQUIRE(found->target == &b);
}

TEST_CASE("disable marks the entry unavailable without reusing the slot", "[hooks_registry]") {
    hook_registry_reset_for_testing();
    int dummy;
    HookEntry entry = make_entry(&dummy);
    HookHandle handle{};
    REQUIRE(hook_registry_add(&entry, &handle));
    REQUIRE(hook_registry_disable(handle));

    REQUIRE(hook_registry_get(handle) == nullptr);
    REQUIRE(hook_registry_find_by_address(&dummy) == nullptr);

    // A second disable on the same handle must fail (already disabled).
    REQUIRE_FALSE(hook_registry_disable(handle));
}

TEST_CASE("get and find_by_address reject an out-of-range handle/address", "[hooks_registry]") {
    hook_registry_reset_for_testing();
    HookHandle bogus{};
    bogus.slot = 9999;
    REQUIRE(hook_registry_get(bogus) == nullptr);

    int not_hooked;
    REQUIRE(hook_registry_find_by_address(&not_hooked) == nullptr);
}

TEST_CASE("disabling a slot never lets a later install reuse it", "[hooks_registry]") {
    hook_registry_reset_for_testing();
    int a, b;
    HookEntry ea = make_entry(&a);
    HookHandle h1{};
    REQUIRE(hook_registry_add(&ea, &h1));
    REQUIRE(hook_registry_disable(h1));

    HookEntry eb = make_entry(&b);
    HookHandle h2{};
    REQUIRE(hook_registry_add(&eb, &h2));

    REQUIRE(h1.slot != h2.slot);
}

TEST_CASE("the registry reports how many slots are used", "[hooks_registry]") {
    hook_registry_reset_for_testing();
    REQUIRE(hook_registry_used() == 0u);

    int a, b;
    HookHandle ha{}, hb{};
    HookEntry ea = make_entry(&a);
    HookEntry eb = make_entry(&b);
    REQUIRE(hook_registry_add(&ea, &ha));
    REQUIRE(hook_registry_add(&eb, &hb));
    REQUIRE(hook_registry_used() == 2u);

    // Disabling frees no slot: the count is monotonic by design.
    REQUIRE(hook_registry_disable(ha));
    REQUIRE(hook_registry_used() == 2u);

    hook_registry_reset_for_testing();
}

TEST_CASE("the registry refuses to overflow its capacity", "[hooks_registry]") {
    hook_registry_reset_for_testing();

    // Distinct, in-bounds addresses: find_by_address rejects duplicates, and
    // walking past a single object would be undefined behaviour under UBSan.
    static char keys[HOOK_REGISTRY_CAPACITY + 1];
    for (uint32_t i = 0; i < (uint32_t)HOOK_REGISTRY_CAPACITY; ++i) {
        HookEntry entry = make_entry(&keys[i]);
        HookHandle handle{};
        REQUIRE(hook_registry_add(&entry, &handle));
    }
    REQUIRE(hook_registry_used() == (uint32_t)HOOK_REGISTRY_CAPACITY);

    HookEntry overflow = make_entry(&keys[HOOK_REGISTRY_CAPACITY]);
    HookHandle handle{};
    REQUIRE_FALSE(hook_registry_add(&overflow, &handle));
    REQUIRE(hook_registry_used() == (uint32_t)HOOK_REGISTRY_CAPACITY);

    hook_registry_reset_for_testing();
}
