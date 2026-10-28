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
    assert(!approved_native_build(""));
    assert(!approved_native_build(std::string(64,'0')));
    assert(!approved_native_build(std::string(kVerifiedIl2CppSha256)+" "));
    std::puts("compatibility: SHA-256 vectors/streaming and unknown-build rejection passed");
}
