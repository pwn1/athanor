#ifndef SRC_TYPES_SET_H_
#define SRC_TYPES_SET_H_
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "base/base.h"
#include "common/common.h"
#include "types/sizeAttr.h"
#include "utils/hashUtils.h"
#include "utils/ignoreUnused.h"

struct SetDomain {
    SizeAttr sizeAttr;
    AnyDomainRef inner;
    // template hack to accept only domains
    template <typename DomainType,
              typename std::enable_if<IsDomainType<BaseType<DomainType>>::value,
                                      int>::type = 0>
    SetDomain(SizeAttr sizeAttr, DomainType&& inner)
        : sizeAttr(sizeAttr),
          inner(std::make_shared<
                typename std::remove_reference<DomainType>::type>(
              std::forward<DomainType>(inner))) {
        trimMaxSize();
    }

    // template hack to accept only pointers to domains
    template <
        typename DomainPtrType,
        typename std::enable_if<IsDomainPtrType<BaseType<DomainPtrType>>::value,
                                int>::type = 0>
    SetDomain(SizeAttr sizeAttr, DomainPtrType&& inner)
        : sizeAttr(sizeAttr), inner(std::forward<DomainPtrType>(inner)) {
        trimMaxSize();
    }
    template <typename AnyDomainRefType,
              typename std::enable_if<
                  std::is_same<BaseType<AnyDomainRefType>, AnyDomainRef>::value,
                  int>::type = 0>
    SetDomain(SizeAttr sizeAttr, AnyDomainRefType&& inner)
        : sizeAttr(sizeAttr), inner(std::forward<AnyDomainRefType>(inner)) {
        trimMaxSize();
    }

   private:
    inline void trimMaxSize() {
        u_int64_t innerDomainSize = getDomainSize(inner);
        if (innerDomainSize < sizeAttr.maxSize) {
            sizeAttr.maxSize = innerDomainSize;
        }
    }
};

struct SetTrigger : public IterAssignedTrigger<SetView> {
    virtual void valueRemoved(u_int64_t indexOfRemovedValue,
                              HashType hashOfRemovedValue) = 0;
    virtual void valueAdded(const AnyExprRef& member) = 0;
    virtual void possibleMemberValueChange(u_int64_t index,
                                           const AnyExprRef& member) = 0;
    virtual void memberValueChanged(u_int64_t index,
                                    const AnyExprRef& member) = 0;

    virtual void setValueChanged(const SetView& newValue) = 0;
};
struct SetView : public ExprInterface<SetView> {
    friend SetValue;
    std::unordered_map<u_int64_t, u_int64_t> hashIndexMap;
    AnyExprVec members;
    HashType cachedHashTotal;
    HashType hashOfPossibleChange;
    std::vector<std::shared_ptr<SetTrigger>> triggers;

   private:
    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline bool addMember(const ExprRef<InnerViewType>& member) {
        auto& members = getMembers<InnerViewType>();
        if (containsMember(member)) {
            return false;
        }

        HashType hash = getValueHash(*member);
        debug_code(assert(!hashIndexMap.count(hash)));
        members.emplace_back(member);
        hashIndexMap.emplace(hash, members.size() - 1);
        cachedHashTotal += mix(hash);
        debug_code(assertValidState());
        return true;
    }

    inline void notifyMemberAdded(const AnyExprRef& newMember) {
        debug_code(assertValidState());
        visitTriggers([&](auto& t) { t->valueAdded(newMember); }, triggers);
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline ExprRef<InnerViewType> removeMember(u_int64_t index) {
        auto& members = getMembers<InnerViewType>();
        debug_code(assert(index < members.size()));

        HashType hash = getValueHash(*members[index]);
        debug_code(assert(hashIndexMap.count(hash)));
        hashIndexMap.erase(hash);
        cachedHashTotal -= mix(hash);
        auto removedMember = std::move(members[index]);
        members[index] = std::move(members.back());
        members.pop_back();
        if (index < members.size()) {
            debug_code(
                assert(hashIndexMap.count(getValueHash(*members[index]))));
            hashIndexMap[getValueHash(*members[index])] = index;
        }
        return removedMember;
    }

    inline void notifyMemberRemoved(u_int64_t index,
                                    HashType hashOfRemovedMember) {
        debug_code(assertValidState());
        visitTriggers(
            [&](auto& t) { t->valueRemoved(index, hashOfRemovedMember); },
            triggers);
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline u_int64_t memberChanged(HashType oldHash, u_int64_t index) {
        auto& members = getMembers<InnerViewType>();
        HashType newHash = getValueHash(*members[index]);
        if (newHash != oldHash) {
            debug_code(assert(!hashIndexMap.count(newHash)));
            hashIndexMap[newHash] = hashIndexMap[oldHash];
            hashIndexMap.erase(oldHash);
            cachedHashTotal -= mix(oldHash);
            cachedHashTotal += mix(newHash);
        }
        debug_code(assertValidState());
        return newHash;
    }

    inline void notifyMemberChanged(size_t index,
                                    const AnyExprRef& changedMember) {
        debug_code(assertValidState());
        visitTriggers(
            [&](auto& t) { t->memberValueChanged(index, changedMember); },
            triggers);
    }

    void silentClear() {
        mpark::visit(
            [&](auto& membersImpl) {
                cachedHashTotal = 0;
                hashIndexMap.clear();
                membersImpl.clear();

            },
            members);
    }

   public:
    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline bool addMemberAndNotify(const ExprRef<InnerViewType>& member) {
        if (addMember(member)) {
            notifyMemberAdded(getMembers<InnerViewType>().back());
            return true;
        } else {
            return false;
        }
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline ExprRef<InnerViewType> removeMemberAndNotify(u_int64_t index) {
        ExprRef<InnerViewType> removedValue =
            removeMember<InnerViewType>(index);
        notifyMemberRemoved(index, getValueHash(*removedValue));
        return removedValue;
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline void notifyPossibleMemberChange(u_int64_t index) {
        auto& members = getMembers<InnerViewType>();
        debug_code(assertValidState());
        hashOfPossibleChange = getValueHash(*members[index]);
        AnyExprRef triggerMember = members[index];
        visitTriggers(
            [&](auto& t) {
                t->possibleMemberValueChange(index, triggerMember);
            },
            triggers);
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline void memberChangedAndNotify(size_t index) {
        HashType oldHash = hashOfPossibleChange;
        hashOfPossibleChange = memberChanged<InnerViewType>(oldHash, index);
        if (oldHash == hashOfPossibleChange) {
            return;
        }
        notifyMemberChanged(index, getMembers<InnerViewType>()[index]);
    }

    inline void notifyEntireSetChange() {
        debug_code(assertValidState());
        visitTriggers([&](auto& t) { t->setValueChanged(*this); }, triggers);
    }

    inline void initFrom(SetView& other) {
        hashIndexMap = other.hashIndexMap;
        members = other.members;
        cachedHashTotal = other.cachedHashTotal;
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline ExprRefVec<InnerViewType>& getMembers() {
        return mpark::get<ExprRefVec<InnerViewType>>(members);
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline const ExprRefVec<InnerViewType>& getMembers() const {
        return mpark::get<ExprRefVec<InnerViewType>>(members);
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline bool containsMember(const ExprRef<InnerViewType>& member) const {
        return containsMember(*member);
    }
    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline bool containsMember(const InnerViewType& member) const {
        return hashIndexMap.count(getValueHash(member));
    }

    inline u_int64_t numberElements() const { return hashIndexMap.size(); }
    void assertValidState();
};

struct SetValue : public SetView, public ValBase {
    using SetView::silentClear;
    template <typename InnerValueType, EnableIfValue<InnerValueType> = 0>
    inline ValRef<InnerValueType> member(u_int64_t index) {
        return assumeAsValue(
            getMembers<
                typename AssociatedViewType<InnerValueType>::type>()[index]
                .asViewRef());
    }

    template <typename InnerValueType, EnableIfValue<InnerValueType> = 0>
    inline bool addMember(const ValRef<InnerValueType>& member) {
        typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
        if (SetView::addMember(ExprRef<InnerViewType>(getViewPtr(member)))) {
            valBase(*member).container = this;
            valBase(*member).id = numberElements() - 1;
            debug_code(assertValidVarBases());
            return true;
        } else {
            return false;
        }
    }

    template <typename InnerValueType, typename Func,
              EnableIfValue<InnerValueType> = 0>
    inline bool tryAddMember(const ValRef<InnerValueType>& member,
                             Func&& func) {
        typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
        if (SetView::addMember(ExprRef<InnerViewType>(getViewPtr(member)))) {
            if (func()) {
                valBase(*member).container = this;
                valBase(*member).id = numberElements() - 1;
                SetView::notifyMemberAdded(getViewPtr(member));
                return true;
            } else {
                typedef typename AssociatedViewType<InnerValueType>::type
                    InnerViewType;
                SetView::removeMember<InnerViewType>(numberElements() - 1);
                valBase(*member).container = NULL;
            }
        }
        return false;
    }

    template <typename InnerValueType, EnableIfValue<InnerValueType> = 0>
    inline ValRef<InnerValueType> removeMember(u_int64_t index) {
        typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
        auto removedMember = assumeAsValue(
            SetView::removeMember<InnerViewType>(index).asViewRef());
        valBase(*removedMember).container = NULL;
        if (index < numberElements()) {
            valBase(*member<InnerValueType>(index)).id = index;
        }
        debug_code(assertValidVarBases());
        return removedMember;
    }

    template <typename InnerValueType, typename Func,
              EnableIfValue<InnerValueType> = 0>
    inline std::pair<bool, ValRef<InnerValueType>> tryRemoveMember(
        u_int64_t index, Func&& func) {
        typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
        auto removedMember = assumeAsValue(
            SetView::removeMember<InnerViewType>(index).asViewRef());
        if (func()) {
            valBase(*removedMember).container = NULL;
            if (index < numberElements()) {
                valBase(*member<InnerValueType>(index)).id = index;
            }
            debug_code(assertValidVarBases());
            SetView::notifyMemberRemoved(index,
                                         getValueHash(asView(*removedMember)));
            return std::make_pair(true, std::move(removedMember));
        } else {
            SetView::addMember<InnerViewType>(getViewPtr(removedMember));
            auto& members = getMembers<InnerViewType>();
            std::swap(members[index], members.back());
            hashIndexMap[getValueHash(*members[index])] = index;
            hashIndexMap[getValueHash(*members.back())] = members.size() - 1;
            debug_code(assertValidState());
            debug_code(assertValidVarBases());
            return std::make_pair(false, ValRef<InnerValueType>(nullptr));
        }
    }

    template <typename InnerValueType, EnableIfValue<InnerValueType> = 0>
    inline void notifyPossibleMemberChange(u_int64_t index) {
        return SetView::notifyPossibleMemberChange<
            typename AssociatedViewType<InnerValueType>::type>(index);
    }

    template <typename InnerValueType, typename Func,
              EnableIfValue<InnerValueType> = 0>
    inline bool tryMemberChange(size_t index, Func&& func) {
        typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
        HashType oldHash = hashOfPossibleChange;
        hashOfPossibleChange = memberChanged<InnerViewType>(oldHash, index);
        if (func()) {
            SetView::notifyMemberChanged(index,
                                         getMembers<InnerViewType>()[index]);
            return true;
        } else {
            if (oldHash != hashOfPossibleChange) {
                hashIndexMap[oldHash] = hashIndexMap[hashOfPossibleChange];
                hashIndexMap.erase(hashOfPossibleChange);
                cachedHashTotal -= mix(hashOfPossibleChange);
                cachedHashTotal += mix(oldHash);
                hashOfPossibleChange = oldHash;
            }
            return false;
        }
    }

    template <typename Func>
    bool tryAssignNewValue(SetValue& newvalue, Func&& func) {
        // fake putting in the value first untill func()verifies that it is
        // happy with the change
        std::swap(*this, newvalue);
        bool allowed = func();
        std::swap(*this, newvalue);
        if (allowed) {
            deepCopy(newvalue, *this);
        }
        return allowed;
    }
    void assertValidVarBases();

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    void setInnerType() {
        if (mpark::get_if<ExprRefVec<InnerViewType>>(&(members)) == NULL) {
            members.emplace<ExprRefVec<InnerViewType>>();
        }
    }

    void printVarBases();
    void evaluate() final;
    void startTriggering() final;
    void stopTriggering() final;
    void updateViolationDescription(u_int64_t parentViolation,
                                    ViolationDescription&) final;
    ExprRef<SetView> deepCopySelfForUnroll(
        const AnyIterRef& iterator) const final;

    std::ostream& dumpState(std::ostream& os) const final;
};

template <>
struct DefinedTrigger<SetValue> : public SetTrigger {
    ValRef<SetValue> val;
    DefinedTrigger(const ValRef<SetValue>& val) : val(val) {}
    inline void valueRemoved(u_int64_t indexOfRemovedValue,
                             HashType hashOfRemovedValue) {
        todoImpl(indexOfRemovedValue, hashOfRemovedValue);
    }
    inline void valueAdded(const AnyExprRef& member) { todoImpl(member); }
    virtual inline void possibleMemberValueChange(u_int64_t index,
                                                  const AnyExprRef& member) {
        todoImpl(index, member);
    }
    virtual void memberValueChanged(u_int64_t index, const AnyExprRef& member) {
        todoImpl(index, member);
        ;
    }

    void setValueChanged(const SetView& newValue) { todoImpl(newValue); }
    void iterHasNewValue(const SetView&, const ExprRef<SetView>&) final {
        assert(false);
        abort();
    }
};

#endif /* SRC_TYPES_SET_H_ */
