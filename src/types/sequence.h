#ifndef SRC_TYPES_SEQUENCE_H_
#define SRC_TYPES_SEQUENCE_H_
#include <vector>
#include "base/base.h"
#include "common/common.h"
#include "types/sizeAttr.h"
#include "utils/hashUtils.h"
#include "utils/ignoreUnused.h"
#include "utils/simpleCache.h"

struct SequenceDomain {
    SizeAttr sizeAttr;
    AnyDomainRef inner;
    bool injective;
    template <typename DomainType>
    SequenceDomain(SizeAttr sizeAttr, DomainType&& inner,
                   bool injective = false)
        : sizeAttr(sizeAttr),
          inner(makeAnyDomainRef(std::forward<DomainType>(inner))),
          injective(injective) {
        checkMaxSize();
    }

   private:
    void checkMaxSize() {
        if (sizeAttr.sizeType == SizeAttr::SizeAttrType::NO_SIZE ||
            sizeAttr.sizeType == SizeAttr::SizeAttrType::MIN_SIZE) {
            std::cerr << "Error, Sequence domain must be initialised with "
                         "maxSize() or exactSize()\n";
            abort();
        }
    }
};

struct SequenceView;

struct SequenceOuterTrigger : public virtual TriggerBase {
    virtual void valueRemoved(
        UInt index, const AnyExprRef& removedValueindexOfRemovedValue) = 0;
    virtual void valueAdded(UInt indexOfRemovedValue,
                            const AnyExprRef& member) = 0;
    virtual void beginSwaps() = 0;
    virtual void positionsSwapped(UInt index1, UInt index2) = 0;
    virtual void endSwaps() = 0;
    virtual void memberHasBecomeUndefined(UInt) = 0;
    virtual void memberHasBecomeDefined(UInt) = 0;
};

struct SequenceMemberTrigger : public virtual TriggerBase {
    virtual void possibleSubsequenceChange(UInt startIndex, UInt endIndex) = 0;
    virtual void subsequenceChanged(UInt startIndex, UInt endIndex) = 0;
};

struct SequenceTrigger : public virtual SequenceOuterTrigger,
                         public virtual SequenceMemberTrigger {};
struct SequenceView : public ExprInterface<SequenceView> {
    friend SequenceValue;
    AnyExprVec members;
    std::vector<std::shared_ptr<SequenceMemberTrigger>> memberTriggers;
    std::vector<std::shared_ptr<SequenceOuterTrigger>> triggers;
    SimpleCache<HashType> cachedHashTotal;
    std::unordered_set<HashType> memberHashes;
    bool injective = false;
    HashType hashOfPossibleChange;
    UInt numberUndefined = 0;
    debug_code(bool posSequenceValueChangeCalled = false);

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline HashType calcMemberHash(UInt index,
                                   const ExprRef<InnerViewType>& expr) const {
        HashType input[2];
        HashType result[2];
        input[0] = index;
        input[1] = getValueHash(expr->view());
        MurmurHash3_x64_128(((void*)input), sizeof(input), 0, result);
        return result[0] ^ result[1];
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline HashType calcSubsequenceHash(UInt start, UInt end) const {
        HashType total = 0;
        for (size_t i = start; i < end; ++i) {
            total += calcMemberHash(i, getMembers<InnerViewType>()[i]);
        }
        return total;
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline bool addMember(size_t index, const ExprRef<InnerViewType>& member) {
        if (injective) {
            HashType newMemberHash = getValueHash(member);
            if (memberHashes.count(newMemberHash)) {
                return false;
            } else {
                memberHashes.insert(newMemberHash);
            }
        }
        auto& members = getMembers<InnerViewType>();
        members.insert(members.begin() + index, member);
        bool memberUndefined = member->isUndefined();
        if (!memberUndefined && index == members.size() - 1) {
            cachedHashTotal.applyIfValid([&](auto& value) {
                value += this->calcMemberHash(index, member);
            });
        } else {
            cachedHashTotal.invalidate();
        }
        if (memberUndefined) {
            numberUndefined++;
        }
        debug_code(assertValidState());
        return true;
    }

    inline void notifyMemberAdded(size_t index, const AnyExprRef& newMember) {
        debug_code(assert(posSequenceValueChangeCalled);
                   posSequenceValueChangeCalled = false);

        debug_code(assertValidState());
        visitTriggers([&](auto& t) { t->valueAdded(index, newMember); },
                      triggers);
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline ExprRef<InnerViewType> removeMember(UInt index) {
        auto& members = getMembers<InnerViewType>();
        debug_code(assert(index < members.size()));
        ExprRef<InnerViewType> removedMember = std::move(members[index]);
        members.erase(members.begin() + index);
        if (injective) {
            bool deleted =
                memberHashes.erase(getValueHash(removedMember->view()));
            static_cast<void>(deleted);
            debug_code(assert(deleted));
        }
        if (index == members.size()) {
            cachedHashTotal.applyIfValid([&](auto& value) {
                value -= this->calcMemberHash(index, removedMember);
            });
        } else {
            cachedHashTotal.invalidate();
        }
        if (removedMember->isUndefined()) {
            numberUndefined--;
        }
        return removedMember;
    }

    inline void notifyMemberRemoved(UInt index,
                                    const AnyExprRef& removedMember) {
        debug_code(assert(posSequenceValueChangeCalled);
                   posSequenceValueChangeCalled = false);

        debug_code(assertValidState());
        visitTriggers([&](auto& t) { t->valueRemoved(index, removedMember); },
                      triggers);
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline void swapPositions(UInt index1, UInt index2) {
        auto& members = getMembers<InnerViewType>();
        std::swap(members[index1], members[index2]);
        cachedHashTotal.applyIfValid([&](auto& value) {
            value -= this->calcMemberHash(index1, members[index2]);
            value -= this->calcMemberHash(index2, members[index1]);
            value += this->calcMemberHash(index1, members[index1]);
            value += this->calcMemberHash(index2, members[index2]);
        });
        debug_code(assertValidState());
    }

    inline void notifyPositionsSwapped(UInt index1, UInt index2) {
        visitTriggers([&](auto& t) { t->positionsSwapped(index1, index2); },
                      triggers);
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline HashType changeSubsequence(UInt startIndex, UInt endIndex) {
        HashType newHash = 0;
        cachedHashTotal.applyIfValid([&](auto& value) {
            newHash =
                this->calcSubsequenceHash<InnerViewType>(startIndex, endIndex);
            value -= hashOfPossibleChange;
            value += newHash;
        });
        debug_code(assertValidState());
        return newHash;
    }

    inline void notifySubsequenceChanged(UInt startIndex, UInt endIndex) {
        ignoreUnused(startIndex, endIndex);
        debug_code(assertValidState());
        visitTriggers(
            [&](auto& t) { t->subsequenceChanged(startIndex, endIndex); },
            memberTriggers);
    }
    inline void notifyPossibleSequenceValueChange() {
        visitTriggers([](auto& t) { t->possibleValueChange(); }, triggers);
        debug_code(posSequenceValueChangeCalled = true);
    }

    inline void notifyBeginSwaps() {
        visitTriggers([&](auto& t) { t->beginSwaps(); }, triggers);
    }
    inline void notifyEndSwaps() {
        visitTriggers([&](auto& t) { t->endSwaps(); }, triggers);
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline void swapAndNotify(UInt index1, UInt index2) {
        swapPositions<InnerViewType>(index1, index2);
        notifyPositionsSwapped(index1, index2);
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline void notifyMemberDefined(UInt index) {
        cachedHashTotal.applyIfValid([&](auto& value) {
            value +=
                this->calcMemberHash(index, getMembers<InnerViewType>()[index]);
        });
        debug_code(assert(numberUndefined > 0));
        numberUndefined--;
        visitTriggers([&](auto& t) { t->memberHasBecomeDefined(index); },
                      triggers);
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline void notifyMemberUndefined(UInt index) {
        cachedHashTotal.applyIfValid(
            [&](auto& value) { value -= hashOfPossibleChange; });
        numberUndefined++;
        visitTriggers([&](auto& t) { t->memberHasBecomeUndefined(index); },
                      triggers);
    }

    void silentClear() {
        mpark::visit(
            [&](auto& membersImpl) {
                cachedHashTotal.invalidate();
                membersImpl.clear();
            },
            members);
        numberUndefined = 0;
    }

   public:
    SequenceView() {}
    SequenceView(AnyExprVec members) : members(std::move(members)) {}
    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline bool addMemberAndNotify(UInt index,
                                   const ExprRef<InnerViewType>& member) {
        notifyPossibleSequenceValueChange();
        if (addMember(index, member)) {
            notifyMemberAdded(index, getMembers<InnerViewType>().back());
            return true;
        } else {
            return false;
        }
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline ExprRef<InnerViewType> removeMemberAndNotify(UInt index) {
        notifyPossibleSequenceValueChange();
        ExprRef<InnerViewType> removedValue =
            removeMember<InnerViewType>(index);
        notifyMemberRemoved(index, removedValue);
        return removedValue;
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline void notifyPossibleSubsequenceChange(UInt startIndex,
                                                UInt endIndex) {
        debug_code(assertValidState());
        if (cachedHashTotal.isValid()) {
            hashOfPossibleChange =
                calcSubsequenceHash<InnerViewType>(startIndex, endIndex);
        }
        visitTriggers(
            [&](auto& t) {
                t->possibleSubsequenceChange(startIndex, endIndex);
            },
            memberTriggers);
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline void changeSubsequenceAndNotify(UInt startIndex, UInt endIndex) {
        HashType oldHash = hashOfPossibleChange;
        hashOfPossibleChange =
            changeSubsequence<InnerViewType>(startIndex, endIndex);
        if (cachedHashTotal.isValid() && oldHash == hashOfPossibleChange) {
            return;
        }
        notifySubsequenceChanged(startIndex, endIndex);
    }
    inline void notifyEntireSequenceChange() {
        debug_code(assert(posSequenceValueChangeCalled);
                   posSequenceValueChangeCalled = false);
        cachedHashTotal.invalidate();
        visitTriggers([&](auto& t) { t->valueChanged(); }, triggers);
    }
    inline void initFrom(SequenceView&) {
        std::cerr << "Deprecated, Should never be called\n"
                  << __func__ << std::endl;
        abort();
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline ExprRefVec<InnerViewType>& getMembers() {
        return mpark::get<ExprRefVec<InnerViewType>>(members);
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    inline const ExprRefVec<InnerViewType>& getMembers() const {
        return mpark::get<ExprRefVec<InnerViewType>>(members);
    }

    inline UInt numberElements() const {
        return mpark::visit([](auto& members) { return members.size(); },
                            members);
    }
    void assertValidState();
};

struct SequenceValue : public SequenceView, public ValBase {
    using SequenceView::silentClear;
    template <typename InnerValueType, EnableIfValue<InnerValueType> = 0>
    inline ValRef<InnerValueType> member(UInt index) {
        return assumeAsValue(
            getMembers<
                typename AssociatedViewType<InnerValueType>::type>()[index]);
    }

    template <typename InnerValueType, EnableIfValue<InnerValueType> = 0>
    void reassignIndicesToEnd(UInt start) {
        auto& members =
            getMembers<typename AssociatedViewType<InnerValueType>::type>();
        for (size_t i = start; i < members.size(); i++) {
            valBase(*assumeAsValue(members[i])).id = i;
        }
    }

    template <typename InnerValueType, EnableIfValue<InnerValueType> = 0>
    inline bool addMember(UInt index, const ValRef<InnerValueType>& member) {
        if (SequenceView::addMember(index, member.asExpr())) {
            valBase(*member).container = this;
            reassignIndicesToEnd<InnerValueType>(index);
            debug_code(assertValidVarBases());
            return true;
        } else {
            return false;
        }
    }

    template <typename InnerValueType, typename Func,
              EnableIfValue<InnerValueType> = 0>
    inline bool tryAddMember(UInt index, const ValRef<InnerValueType>& member,
                             Func&& func) {
        notifyPossibleSequenceValueChange();
        bool added = SequenceView::addMember(index, member.asExpr());
        if (!added) {
            return false;
        }
        if (func()) {
            valBase(*member).container = this;
            reassignIndicesToEnd<InnerValueType>(index);
            SequenceView::notifyMemberAdded(index, member.asExpr());
            debug_code(assertValidVarBases());
            return true;
        } else {
            typedef
                typename AssociatedViewType<InnerValueType>::type InnerViewType;
            SequenceView::removeMember<InnerViewType>(index);
            return false;
        }
    }

    template <typename InnerValueType, EnableIfValue<InnerValueType> = 0>
    inline ValRef<InnerValueType> removeMember(UInt index) {
        typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
        auto removedMember =
            assumeAsValue(SequenceView::removeMember<InnerViewType>(index));
        valBase(*removedMember).container = NULL;
        reassignIndicesToEnd<InnerValueType>(index);
        debug_code(assertValidVarBases());
        return removedMember;
    }

    template <typename InnerValueType, typename Func,
              EnableIfValue<InnerValueType> = 0>
    inline std::pair<bool, ValRef<InnerValueType>> tryRemoveMember(
        UInt index, Func&& func) {
        notifyPossibleSequenceValueChange();
        typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
        auto removedMember =
            assumeAsValue(SequenceView::removeMember<InnerViewType>(index));
        if (func()) {
            valBase(*removedMember).container = NULL;
            reassignIndicesToEnd<InnerValueType>(index);
            debug_code(assertValidVarBases());
            SequenceView::notifyMemberRemoved(index, removedMember.asExpr());
            return std::make_pair(true, std::move(removedMember));
        } else {
            SequenceView::addMember<InnerViewType>(index,
                                                   removedMember.asExpr());
            debug_code(assertValidState());
            debug_code(assertValidVarBases());
            return std::make_pair(false, ValRef<InnerValueType>(nullptr));
        }
    }

    template <typename InnerValueType, EnableIfValue<InnerValueType> = 0>
    inline void notifyPossibleSubsequenceChange(UInt start, UInt end) {
        return SequenceView::notifyPossibleSubsequenceChange<
            typename AssociatedViewType<InnerValueType>::type>(start, end);
    }

    template <typename InnerValueType, typename Func,
              EnableIfValue<InnerValueType> = 0>
    inline bool trySwapPositions(UInt index1, UInt index2, Func&& func) {
        typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
        notifyBeginSwaps();
        SequenceView::swapPositions<InnerViewType>(index1, index2);
        if (func()) {
            auto& members = getMembers<InnerViewType>();
            valBase(*assumeAsValue(members[index1])).id = index1;
            valBase(*assumeAsValue(members[index2])).id = index2;
            SequenceView::notifyPositionsSwapped(index1, index2);
            debug_code(assertValidVarBases());
            notifyEndSwaps();
            return true;
        } else {
            SequenceView::swapPositions<InnerViewType>(index1, index2);
            notifyEndSwaps();
            return false;
        }
    }

    template <typename InnerValueType, typename Func,
              EnableIfValue<InnerValueType> = 0>
    inline bool trySubsequenceChange(UInt start, UInt end, Func&& func) {
        typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
        hashOfPossibleChange =
            SequenceView::changeSubsequence<InnerViewType>(start, end);
        if (func()) {
            SequenceView::notifySubsequenceChanged(start, end);
            return true;
        } else {
            SequenceView::changeSubsequence<InnerViewType>(start, end);
            return false;
        }
    }

    template <typename Func>
    bool tryAssignNewValue(SequenceValue& newvalue, Func&& func) {
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
    void evaluateImpl() final;
    void startTriggering() final;
    void stopTriggering() final;
    void updateVarViolations(const ViolationContext& vioContext,
                             ViolationContainer&) final;
    ExprRef<SequenceView> deepCopySelfForUnroll(
        const ExprRef<SequenceView>&, const AnyIterRef& iterator) const final;

    std::ostream& dumpState(std::ostream& os) const final;
    void findAndReplaceSelf(const FindAndReplaceFunction&) final;
    bool isUndefined();
    bool optimise(PathExtension) final;
};

template <typename Child>
struct ChangeTriggerAdapter<SequenceTrigger, Child>
    : public ChangeTriggerAdapterBase<SequenceTrigger, Child> {
    inline void valueRemoved(UInt, const AnyExprRef&) {
        this->forwardValueChanged();
    }
    inline void valueAdded(UInt, const AnyExprRef&) final {
        this->forwardValueChanged();
    }

    inline void possibleSubsequenceChange(UInt, UInt) final {
        this->forwardPossibleValueChange();
    }
    inline void subsequenceChanged(UInt, UInt) final {
        this->forwardValueChanged();
        ;
    }
    inline void beginSwaps() final { this->forwardPossibleValueChange(); }
    inline void positionsSwapped(UInt, UInt) final {}
    inline void endSwaps() final { this->forwardValueChanged(); }
    inline void memberHasBecomeDefined(UInt) {}
    inline void memberHasBecomeUndefined(UInt) {}
};

#endif /* SRC_TYPES_SEQUENCE_H_ */
