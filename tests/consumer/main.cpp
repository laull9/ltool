#include "LFile.hpp"
#include "LLruCache.hpp"

int main() {
    LLruCache<int, int> cache(2);
    cache.put(1, 2);
    LTempFile file;
    file.file().atomic_write_text("old");
    file.file().atomic_write_text("abc");
    return cache.get(1) == 2 && file.file().sha256() ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" ? 0 : 1;
}
