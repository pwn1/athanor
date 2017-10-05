#ifndef SRC_TYPES_SET_H_
#define SRC_TYPES_SET_H_
#include <unordered_set>
#include <vector>
#include "utils/hashUtils.h"

#include "operators/setProducing.h"
#include "types/forwardDecls/typesAndDomains.h"
struct SetDomain {
    SizeAttr sizeAttr;
    Domain inner;
    // template hack to accept only domains
    template <
        typename DomainType,
        typename std::enable_if<IsDomainType<DomainType>::value, int>::type = 0>
    SetDomain(SizeAttr sizeAttr, DomainType&& inner)
        : sizeAttr(sizeAttr),
          inner(std::make_shared<
                typename std::remove_reference<DomainType>::type>(
              std::forward<DomainType>(inner))) {}

    // template hack to accept only pointers to domains
    template <typename DomainPtrType,
              typename std::enable_if<IsDomainPtrType<DomainPtrType>::value,
                                      int>::type = 0>
    SetDomain(SizeAttr sizeAttr, DomainPtrType&& inner)
        : sizeAttr(sizeAttr), inner(std::forward<DomainPtrType>(inner)) {}
};

std::vector<std::shared_ptr<SetTrigger>>& getSetTriggers(SetValue& v);
template <typename Inner>
struct SetValueImpl {
    std::vector<Inner> members;
    std::unordered_set<u_int64_t> memberHashes;
    u_int64_t cachedHashTotal = 0;
    inline bool containsMember(const Inner& member) {
        return memberHashes.count(mix(getValueHash(*member)));
    }

    template <typename Func>
    inline void changeMemberValue(Func&& func, SetValue& value,
                                  size_t memberIndex) {
        u_int64_t hash = mix(getValueHash(*members[memberIndex]));
        for (std::shared_ptr<SetTrigger>& t : getSetTriggers(value)) {
            t->possibleValueChange(members[memberIndex]);
        }
        func();
        memberHashes.erase(hash);
        cachedHashTotal -= hash;
        hash = mix(getValueHash(*members[memberIndex]));
        memberHashes.insert(hash);
        cachedHashTotal += hash;
        for (std::shared_ptr<SetTrigger>& t : getSetTriggers(value)) {
            t->valueChanged(members[memberIndex]);
        }
    }

    inline void removeValue(SetValue& value, size_t memberIndex) {
        Inner member = std::move(members[memberIndex]);
        members[memberIndex] = std::move(members.back());
        members.pop_back();
        u_int64_t hash = mix(getValueHash(*member));
        memberHashes.erase(hash);
        cachedHashTotal -= hash;
        for (std::shared_ptr<SetTrigger>& t : getSetTriggers(value)) {
            t->valueRemoved(member);
        }
    }

    inline void addValue(SetValue& value, const Inner& member) {
        members.push_back(member);
        u_int64_t hash = mix(getValueHash(*member));
        memberHashes.insert(hash);
        cachedHashTotal += hash;
        for (std::shared_ptr<SetTrigger>& t : getSetTriggers(value)) {
            t->valueAdded(members.back());
        }
    }
};

#define variantValues(T) SetValueImpl<std::shared_ptr<T##Value>>
typedef mpark::variant<buildForAllTypes(variantValues, MACRO_COMMA)>
    SetValueImplWrapper;
#undef variantValues

struct SetValue {
    SetValueImplWrapper setValueImpl =
        SetValueImpl<std::shared_ptr<IntValue>>();
    std::vector<std::shared_ptr<SetTrigger>> triggers;
};

inline std::vector<std::shared_ptr<SetTrigger>>& getSetTriggers(SetValue& v) {
    return v.triggers;
}
#endif /* SRC_TYPES_SET_H_ */
