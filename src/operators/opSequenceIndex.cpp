#include "operators/opSequenceIndex.h"
#include <iostream>
#include <memory>
#include "utils/ignoreUnused.h"
using namespace std;

template <typename SequenceMemberViewType>
void OpSequenceIndex<SequenceMemberViewType>::addTrigger(
    const shared_ptr<SequenceMemberTriggerType>& trigger) {
    triggers.emplace_back(getTriggerBase(trigger));
    if (locallyDefined) {
        getMember()->addTrigger(trigger);
    }
}

template <typename SequenceMemberViewType>
ExprRef<SequenceMemberViewType>&
OpSequenceIndex<SequenceMemberViewType>::getMember() {
    debug_code(assert(locallyDefined));
    debug_code(
        assert(!indexOperand->isUndefined() && cachedIndex >= 0 &&
               !sequenceOperand->isUndefined() &&
               cachedIndex < (Int)sequenceOperand->view().numberElements()));
    auto& member = sequenceOperand->view()
                       .getMembers<SequenceMemberViewType>()[cachedIndex];
    debug_code(assert(!member->isUndefined()));
    return member;
}

template <typename SequenceMemberViewType>
const ExprRef<SequenceMemberViewType>&
OpSequenceIndex<SequenceMemberViewType>::getMember() const {
    debug_code(assert(locallyDefined));
    debug_code(
        assert(!indexOperand->isUndefined() && cachedIndex >= 0 &&
               !sequenceOperand->isUndefined() &&
               cachedIndex < (Int)sequenceOperand->view().numberElements()));

    const auto& member = sequenceOperand->view()
                             .getMembers<SequenceMemberViewType>()[cachedIndex];
    debug_code(assert(!member->isUndefined()));
    return member;
}

template <typename SequenceMemberViewType>
SequenceMemberViewType& OpSequenceIndex<SequenceMemberViewType>::view() {
    return getMember()->view();
}
template <typename SequenceMemberViewType>
const SequenceMemberViewType& OpSequenceIndex<SequenceMemberViewType>::view()
    const {
    return getMember()->view();
}
template <typename SequenceMemberViewType>
void OpSequenceIndex<SequenceMemberViewType>::reevaluateDefined() {
    locallyDefined =
        !indexOperand->isUndefined() && cachedIndex >= 0 &&
        !sequenceOperand->isUndefined() &&
        cachedIndex < (Int)sequenceOperand->view().numberElements();
    defined = locallyDefined && !getMember()->isUndefined();
}

template <typename SequenceMemberViewType>
void OpSequenceIndex<SequenceMemberViewType>::evaluateImpl() {
    sequenceOperand->evaluate();
    indexOperand->evaluate();
    if (indexOperand->isUndefined()) {
        defined = false;
        locallyDefined = false;
    } else {
        cachedIndex = indexOperand->view().value - 1;
        reevaluateDefined();
    }
}

template <typename SequenceMemberViewType>
OpSequenceIndex<SequenceMemberViewType>::OpSequenceIndex(
    OpSequenceIndex<SequenceMemberViewType>&& other)
    : ExprInterface<SequenceMemberViewType>(move(other)),
      triggers(move(other.triggers)),
      sequenceOperand(move(other.sequenceOperand)),
      indexOperand(move(other.indexOperand)),
      cachedIndex(move(other.cachedIndex)),
      defined(move(other.defined)),
      locallyDefined(move(other.locallyDefined)),
      sequenceTrigger(move(other.sequenceTrigger)),
      indexTrigger(move(other.indexTrigger)) {
    setTriggerParent(this, indexTrigger, sequenceTrigger);
}
template <typename SequenceMemberViewType>
struct OpSequenceIndexTriggerBase {
    OpSequenceIndex<SequenceMemberViewType>* op;
    OpSequenceIndexTriggerBase(OpSequenceIndex<SequenceMemberViewType>* op)
        : op(op) {}
    bool eventForwardedAsDefinednessChange() {
        bool wasDefined = op->defined;
        op->reevaluateDefined();
        if (wasDefined && !op->defined) {
            visitTriggers([&](auto& t) { t->hasBecomeUndefined(); },
                          op->triggers);
            return true;
        } else if (!wasDefined && op->defined) {
            visitTriggers(
                [&](auto& t) {
                    t->hasBecomeDefined();
                    t->reattachTrigger();
                },
                op->triggers);
            return true;
        } else {
            return !op->defined;
        }
    }
    inline void hasBecomeUndefinedBase() {
        op->locallyDefined = false;
        if (!op->defined) {
            return;
        }
        op->defined = false;
        visitTriggers([&](auto& t) { t->hasBecomeUndefined(); }, op->triggers);
    }
    void hasBecomeDefinedBase() {
        op->reevaluateDefined();
        if (!op->defined) {
            return;
        }
        visitTriggers(
            [&](auto& t) {
                t->hasBecomeDefined();
                t->reattachTrigger();
            },
            op->triggers);
    }
};

template <typename SequenceMemberViewType>
struct OpSequenceIndex<SequenceMemberViewType>::SequenceOperandTrigger
    : public SequenceTrigger,
      public OpSequenceIndexTriggerBase<SequenceMemberViewType> {
    using OpSequenceIndexTriggerBase<
        SequenceMemberViewType>::OpSequenceIndexTriggerBase;
    using OpSequenceIndexTriggerBase<SequenceMemberViewType>::op;
    using OpSequenceIndexTriggerBase<
        SequenceMemberViewType>::eventForwardedAsDefinednessChange;
    void possibleValueChange() final {
        if (!op->defined) {
            return;
        }

        visitTriggers([&](auto& t) { t->possibleValueChange(); }, op->triggers);
    }
    void valueChanged() final {
        if (!eventForwardedAsDefinednessChange()) {
            visitTriggers(
                [&](auto& t) {
                    t->valueChanged();
                    t->reattachTrigger();
                },
                op->triggers);
        }
    }

    inline void valueRemoved(UInt index, const AnyExprRef&) {
        if (!op->locallyDefined) {
            return;
        }
        if (!eventForwardedAsDefinednessChange() &&
            (Int)index <= op->cachedIndex) {
            visitTriggers(
                [&](auto& t) {
                    t->valueChanged();
                    t->reattachTrigger();
                },
                op->triggers);
        }
    }

    inline void valueAdded(UInt index, const AnyExprRef&) final {
        if (!eventForwardedAsDefinednessChange() &&
            (Int)index <= op->cachedIndex) {
            visitTriggers(
                [&](auto& t) {
                    t->valueChanged();
                    t->reattachTrigger();
                },
                op->triggers);
        }
    }

    inline void possibleSubsequenceChange(UInt, UInt) final {
        // since the parent will already be directly triggering on the sequence
        // member, this trigger need not be forwarded
    }

    inline void subsequenceChanged(UInt, UInt) final {
        // since the parent will already be directly triggering on the sequence
        // member, this trigger need not be forwarded
    }

    inline void beginSwaps() final {}
    inline void endSwaps() final {}
    inline void positionsSwapped(UInt index1, UInt index2) final {
        if (!op->locallyDefined) {
            return;
        }
        if (((Int)index1 != op->cachedIndex &&
             (Int)index2 != op->cachedIndex) ||
            index1 == index2) {
            return;
        }
        if ((Int)index1 != op->cachedIndex) {
            swap(index1, index2);
        }
        if (op->defined) {
            op->cachedIndex = index2;
            visitTriggers([&](auto& t) { t->possibleValueChange(); },
                          op->triggers);
            op->cachedIndex = index1;
        }
        if (!eventForwardedAsDefinednessChange()) {
            visitTriggers(
                [&](auto& t) {
                    t->valueChanged();
                    t->reattachTrigger();
                },
                op->triggers);
        }
    }

    void reattachTrigger() final {
        deleteTrigger(op->sequenceTrigger);
        auto trigger = make_shared<
            OpSequenceIndex<SequenceMemberViewType>::SequenceOperandTrigger>(
            op);
        op->sequenceOperand->addTrigger(trigger);
        op->sequenceTrigger = trigger;
    }

    void memberHasBecomeUndefined(UInt index) final {
        if (!op->defined || (Int)index != op->cachedIndex) {
            return;
        }
        debug_code(assert(op->defined));
        op->defined = false;
    }

    void memberHasBecomeDefined(UInt index) final {
        if (!op->locallyDefined || (Int)index != op->cachedIndex) {
            return;
        }
        debug_code(assert(!op->defined));
        op->defined = true;
    }

    void hasBecomeDefined() final { this->hasBecomeDefinedBase(); }

    void hasBecomeUndefined() final { this->hasBecomeUndefinedBase(); }
};

template <typename SequenceMemberViewType>
struct OpSequenceIndex<SequenceMemberViewType>::IndexTrigger
    : public IntTrigger,
      public OpSequenceIndexTriggerBase<SequenceMemberViewType> {
    using OpSequenceIndexTriggerBase<
        SequenceMemberViewType>::OpSequenceIndexTriggerBase;
    using OpSequenceIndexTriggerBase<SequenceMemberViewType>::op;
    using OpSequenceIndexTriggerBase<
        SequenceMemberViewType>::eventForwardedAsDefinednessChange;
    void possibleValueChange() final {
        if (!op->defined) {
            return;
        }

        visitTriggers([&](auto& t) { t->possibleValueChange(); }, op->triggers);
    }

    void valueChanged() final {
        op->cachedIndex = op->indexOperand->view().value - 1;
        if (!eventForwardedAsDefinednessChange()) {
            visitTriggers(
                [&](auto& t) {
                    t->valueChanged();
                    t->reattachTrigger();
                },
                op->triggers);
        }
    }

    void reattachTrigger() final {
        deleteTrigger(op->indexTrigger);
        auto trigger =
            make_shared<OpSequenceIndex<SequenceMemberViewType>::IndexTrigger>(
                op);
        op->indexOperand->addTrigger(trigger);
        op->indexTrigger = trigger;
    }
    void hasBecomeDefined() final {
        op->cachedIndex = op->indexOperand->view().value - 1;
        this->hasBecomeDefinedBase();
    }

    void hasBecomeUndefined() final { this->hasBecomeUndefinedBase(); }
};

template <typename SequenceMemberViewType>
void OpSequenceIndex<SequenceMemberViewType>::startTriggering() {
    if (!indexTrigger) {
        indexTrigger =
            make_shared<OpSequenceIndex<SequenceMemberViewType>::IndexTrigger>(
                this);
        sequenceTrigger = make_shared<
            OpSequenceIndex<SequenceMemberViewType>::SequenceOperandTrigger>(
            this);
        indexOperand->addTrigger(indexTrigger);
        sequenceOperand->addTrigger(sequenceTrigger);
        indexOperand->startTriggering();
        sequenceOperand->startTriggering();
    }
}

template <typename SequenceMemberViewType>
void OpSequenceIndex<SequenceMemberViewType>::stopTriggeringOnChildren() {
    if (indexTrigger) {
        deleteTrigger(indexTrigger);
        deleteTrigger(sequenceTrigger);
        indexTrigger = nullptr;
        sequenceTrigger = nullptr;
    }
}
template <typename SequenceMemberViewType>
void OpSequenceIndex<SequenceMemberViewType>::stopTriggering() {
    if (indexTrigger) {
        stopTriggeringOnChildren();
        indexOperand->stopTriggering();
        sequenceOperand->stopTriggering();
    }
}

template <typename SequenceMemberViewType>
void OpSequenceIndex<SequenceMemberViewType>::updateViolationDescription(
    UInt parentViolation, ViolationDescription& vioDesc) {
    indexOperand->updateViolationDescription(parentViolation, vioDesc);
    if (defined) {
        getMember()->updateViolationDescription(parentViolation, vioDesc);
    } else {
        sequenceOperand->updateViolationDescription(parentViolation, vioDesc);
    }
}

template <typename SequenceMemberViewType>
ExprRef<SequenceMemberViewType>
OpSequenceIndex<SequenceMemberViewType>::deepCopySelfForUnroll(
    const ExprRef<SequenceMemberViewType>&, const AnyIterRef& iterator) const {
    auto newOpSequenceIndex =
        make_shared<OpSequenceIndex<SequenceMemberViewType>>(
            sequenceOperand->deepCopySelfForUnroll(sequenceOperand, iterator),
            indexOperand->deepCopySelfForUnroll(indexOperand, iterator));
    newOpSequenceIndex->evaluated = this->evaluated;
    newOpSequenceIndex->cachedIndex = cachedIndex;
    newOpSequenceIndex->defined = defined;
    newOpSequenceIndex->locallyDefined = locallyDefined;
    return newOpSequenceIndex;
}

template <typename SequenceMemberViewType>
std::ostream& OpSequenceIndex<SequenceMemberViewType>::dumpState(
    std::ostream& os) const {
    os << "opSequenceIndex(sequence=";
    sequenceOperand->dumpState(os) << ",\n";
    os << "index=";
    indexOperand->dumpState(os) << ")";
    return os;
}

template <typename SequenceMemberViewType>
void OpSequenceIndex<SequenceMemberViewType>::findAndReplaceSelf(
    const FindAndReplaceFunction& func) {
    this->sequenceOperand = findAndReplace(sequenceOperand, func);
    this->indexOperand = findAndReplace(indexOperand, func);
}

template <typename SequenceMemberViewType>
bool OpSequenceIndex<SequenceMemberViewType>::isUndefined() {
    return !defined;
}
template <typename Op>
struct OpMaker;

template <typename View>
struct OpMaker<OpSequenceIndex<View>> {
    static ExprRef<View> make(ExprRef<SequenceView> sequence,
                              ExprRef<IntView> index);
};

template <typename View>
ExprRef<View> OpMaker<OpSequenceIndex<View>>::make(
    ExprRef<SequenceView> sequence, ExprRef<IntView> index) {
    return make_shared<OpSequenceIndex<View>>(move(sequence), move(index));
}

#define opSequenceIndexInstantiators(name)       \
    template struct OpSequenceIndex<name##View>; \
    template struct OpMaker<OpSequenceIndex<name##View>>;

buildForAllTypes(opSequenceIndexInstantiators, );
#undef opSequenceIndexInstantiators
