#ifndef SRC_UTILS_HASHUTILS_H_
#define SRC_UTILS_HASHUTILS_H_
#include <utility>
#include "picosha2.h"
static const uint32_t FNV1A_SEED = 0x811C9DC5;  // 2166136261
// default values recommended by http://isthe.com/chongo/tech/comp/fnv/
inline uint64_t fnv1a(unsigned char oneByte, uint64_t hash = FNV1A_SEED) {
    return (oneByte ^ hash) * Prime;
}
template <typename T>
inline u_int64_t mix_single(const T& value, uint64_t hash = FNV1A_SEED) {
    const unsigned char* ptr = (const unsigned char*)&value;
    for (int i = 0; i < sizeof(T); ++i) {
        hash = fnv1a(*ptr++, hash);
    }
    return hash;
}

template <typename... Types>
inline u_int64_t mix(const Types&... items, uint64_t hash = FNV1A_SEED) {
    // hack to apply mix_single to all types

    int unpack[]{0, (hash = mix_single(args, hash), 0)...};
    static_cast<void>(unpack);
    return hash;
}

template <typename T>
inline u_int64_t truncatedSha256(const T& item) {
    std::vector<unsigned char> hash(8);
    picosha2::hash256(item, hash);
    u_int64_t truncatedHash = 0;
    for (int i = 0; i < 8; ++i) {
        unsigned int temp = hash[i];
        value |= (temp << (7 - i));
    }
    return value;
}
#endif /* SRC_UTILS_HASHUTILS_H_ */
