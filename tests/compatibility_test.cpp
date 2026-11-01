#include "sha256.hpp"
#include "compatibility_policy.hpp"
#include <cassert>
#include <cstdio>
int main() {
    BuildSha256 empty;
    assert(empty.hex_digest() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    empty.update("a",1);
    auto partial = empty.hex_digest();
    empty.update("bc",2);
    assert(empty.hex_digest() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(partial != empty.hex_digest());
    const char* multi="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    BuildSha256 boundary;
    for (size_t i=0;i<strlen(multi);++i) boundary.update(multi+i,1);
    assert(boundary.hex_digest() == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    BuildSha256 million;
    std::string chunk(1000,'a');
    for(int i=0;i<1000;++i) million.update(chunk.data(),chunk.size());
    assert(million.hex_digest() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    assert(approved_native_build(kVerifiedIl2CppSha256));
    assert(approved_native_build(kFullTrial1180Sha256));
    assert(std::string(approved_native_label(kFullTrial1180Sha256)) == "1.118.0 / versionCode 487");
    assert(std::string(native_validation_scope(kFullTrial1180Sha256)) == "full_runtime_trial");
    assert(approved_native_label(std::string(64, '0')) == nullptr);
    assert(native_validation_scope("") == nullptr);
    assert(!approved_native_build(""));
    assert(!approved_native_build(std::string(64,'0')));
    assert(!approved_native_build(std::string(kVerifiedIl2CppSha256)+" "));
    auto altered = std::string(kFullTrial1180Sha256); altered[0] = '0';
    assert(!approved_native_build(altered));
    assert(!approved_native_build(std::string(kFullTrial1180Sha256) + " "));

    // Evaluate compatibility: Tier 1 built-in
    auto v_builtin = evaluate_compatibility(kFullTrial1180Sha256);
    assert(v_builtin.approved && v_builtin.status == "approved" && v_builtin.scope == "full_runtime_trial");

    // Evaluate compatibility: Tier 2 custom config
    std::string fake_new_hash = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
    nlohmann::json cfg_tier2 = {
        {"allow_adaptive_compatibility", false},
        {"custom_approved_builds", {{fake_new_hash, "Custom 1.119.0"}}}
    };
    auto v_tier2 = evaluate_compatibility(fake_new_hash, cfg_tier2);
    assert(v_tier2.approved && v_tier2.status == "approved_by_config" && v_tier2.label == "Custom 1.119.0");

    // Evaluate compatibility: Tier 3 adaptive mode
    std::string unknown_hash = "1111111122222222333333334444444455555555666666667777777788888888";
    nlohmann::json cfg_adaptive = {{"allow_adaptive_compatibility", true}};
    auto v_adaptive = evaluate_compatibility(unknown_hash, cfg_adaptive);
    assert(v_adaptive.approved && v_adaptive.status == "adaptive_approved");

    // Evaluate compatibility: unknown build with adaptive disabled
    nlohmann::json cfg_strict = {{"allow_adaptive_compatibility", false}};
    auto v_strict = evaluate_compatibility(unknown_hash, cfg_strict);
    assert(!v_strict.approved && v_strict.status == "unknown_native_build");

    // Evaluate compatibility: malformed hash
    assert(!evaluate_compatibility("not-hex-at-all", cfg_adaptive).approved);
    assert(!evaluate_compatibility(std::string(63, 'a'), cfg_adaptive).approved);

    std::puts("compatibility: SHA-256 vectors/streaming and unknown-build rejection passed");
}
