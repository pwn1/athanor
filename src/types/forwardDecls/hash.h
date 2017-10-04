
#ifndef SRC_TYPES_FORWARDDECLS_HASH_H_
#define SRC_TYPES_FORWARDDECLS_HASH_H_
#include "types/forwardDecls/typesAndDomains.h"
#define makeGetHashDecl(valueName) \
    u_int64_t getValueHash(const valueName##Value&);
buildForAllTypes(makeGetHashDecl, )
#undef makeGetHashDecl

    inline u_int64_t getValueHash(const Value& val) {
    return mpark::visit(
        [&](const auto& valImpl) { return getValueHash(*valImpl); }, val);
}
#endif /* SRC_TYPES_FORWARDDECLS_HASH_H_ */
