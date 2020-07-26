#include "operators/opMSetSubsetEq.h"

#include <iostream>
#include <memory>

#include "operators/simpleOperator.hpp"
#include "types/mSet.h"
using namespace std;
static const char* NO_UNDEFINED_IN_SUBMSET =
    "OpSubSet does not yet handle all the cases where mSets become undefined.  "
    "Especially if returned views are undefined\n";

UInt excess(UInt leftCount, UInt rightCount) {
    return (leftCount > rightCount) ? leftCount - rightCount : 0;
}
void OpMsetSubsetEq::reevaluateImpl(MSetView& leftView, MSetView& rightView,
                                    bool, bool) {
    violation = 0;
    for (auto& hashCountPair : leftView.memberCounts) {
        auto leftCount = hashCountPair.second;
        auto rightCount = rightView.memberCount(hashCountPair.first);
        violation += excess(leftCount, rightCount);
    }
}
namespace {
HashType getHashForceDefined(const AnyExprRef& expr) {
    return lib::visit(
        [&](auto& expr) {
            auto view = expr->getViewIfDefined();
            if (!view) {
                myCerr << NO_UNDEFINED_IN_SUBMSET;
                myAbort();
            }
            return getValueHash(*view);
        },
        expr);
}

}  // namespace
// returns a pair, old excess count, new excess count.
// excess means the number of memebrs in left that are not in right with the
// given hash.
struct OperatorTrates<OpMsetSubsetEq>::LeftTrigger : public MSetTrigger {
   public:
    OpMsetSubsetEq* op;

   public:
    LeftTrigger(OpMsetSubsetEq* op) : op(op) {}
    inline void valueRemovedImpl(HashType hash, bool trigger) {
        auto leftCount = op->left->view()
                             .checkedGet(NO_UNDEFINED_IN_SUBMSET)
                             .memberCount(hash);
        auto rightCount = op->right->view()
                              .checkedGet(NO_UNDEFINED_IN_SUBMSET)
                              .memberCount(hash);
        auto oldExcess = excess(leftCount + 1, rightCount);
        auto newExcess = excess(leftCount, rightCount);

        op->changeValue([&]() {
            op->violation -= oldExcess;
            op->violation += newExcess;
            return trigger;
        });
    }
    void valueRemoved(UInt, const AnyExprRef& member) {
        valueRemovedImpl(getHashForceDefined(member), true);
    }
    inline void valueAddedImpl(HashType hash, bool triggering) {
        auto leftCount = op->left->view()
                             .checkedGet(NO_UNDEFINED_IN_SUBMSET)
                             .memberCount(hash);
        auto rightCount = op->right->view()
                              .checkedGet(NO_UNDEFINED_IN_SUBMSET)
                              .memberCount(hash);
        auto oldExcess = excess(leftCount - 1, rightCount);
        auto newExcess = excess(leftCount, rightCount);

        op->changeValue([&]() {
            op->violation -= oldExcess;
            op->violation += newExcess;
            return triggering;
        });
    }
    void valueAdded(const AnyExprRef& member) final {
        valueAddedImpl(getHashForceDefined(member), true);
    }
    inline void valueChanged() final {
        op->changeValue([&]() {
            op->reevaluate(true, false);
            return true;
        });
    }
    void memberReplaced(UInt index, const AnyExprRef& oldMember) {
        memberValueChanged(index, getValueHash(oldMember));
    }
    inline void memberValueChanged(UInt index, HashType oldHash) final {
        auto& leftView =
            op->left->getViewIfDefined().checkedGet(NO_UNDEFINED_IN_SUBMSET);
        HashType newHash = leftView.indexHashMap[index];
        op->changeValue([&]() {
            valueRemovedImpl(oldHash, false);
            valueAddedImpl(newHash, false);
            return true;
        });
    }
    inline void memberValuesChanged(
        const std::vector<UInt>& indices,
        const std::vector<HashType>& oldHashes) final {
        op->changeValue([&]() {
            for (HashType hash : oldHashes) {
                valueRemovedImpl(hash, false);
            }
            auto& indexHashMap = op->left->view()
                                     .checkedGet(NO_UNDEFINED_IN_SUBMSET)
                                     .indexHashMap;
            for (UInt index : indices) {
                valueAddedImpl(indexHashMap[index], false);
            }
            return true;
        });
    }

    void reattachTrigger() final {
        deleteTrigger(op->leftTrigger);
        auto trigger =
            make_shared<OperatorTrates<OpMsetSubsetEq>::LeftTrigger>(op);
        op->left->addTrigger(trigger);
        op->leftTrigger = trigger;
    }
    void hasBecomeUndefined() final { op->setUndefinedAndTrigger(); }
    void hasBecomeDefined() final { op->reevaluateDefinedAndTrigger(); }
};

struct OperatorTrates<OpMsetSubsetEq>::RightTrigger : public MSetTrigger {
    OpMsetSubsetEq* op;
    RightTrigger(OpMsetSubsetEq* op) : op(op) {}
    inline void valueRemovedImpl(HashType hash, bool triggering) {
        auto leftCount = op->left->view()
                             .checkedGet(NO_UNDEFINED_IN_SUBMSET)
                             .memberCount(hash);
        auto rightCount = op->right->view()
                              .checkedGet(NO_UNDEFINED_IN_SUBMSET)
                              .memberCount(hash);
        auto oldExcess = excess(leftCount, rightCount + 1);
        auto newExcess = excess(leftCount, rightCount);

        op->changeValue([&]() {
            op->violation -= oldExcess;
            op->violation += newExcess;
            return triggering;
        });
    }
    void valueRemoved(UInt, const AnyExprRef& member) {
        valueRemovedImpl(getHashForceDefined(member), true);
    }

    inline void valueAddedImpl(HashType hash, bool triggering) {
        auto leftCount = op->left->view()
                             .checkedGet(NO_UNDEFINED_IN_SUBMSET)
                             .memberCount(hash);
        auto rightCount = op->right->view()
                              .checkedGet(NO_UNDEFINED_IN_SUBMSET)
                              .memberCount(hash);
        auto oldExcess = excess(leftCount, rightCount - 1);
        auto newExcess = excess(leftCount, rightCount);

        op->changeValue([&]() {
            op->violation -= oldExcess;
            op->violation += newExcess;
            return triggering;
        });
    }

    void valueAdded(const AnyExprRef& member) final {
        valueAddedImpl(getHashForceDefined(member), true);
    }

    inline void valueChanged() final {
        op->changeValue([&]() {
            op->reevaluate(false, true);
            return true;
        });
    }
    void memberReplaced(UInt index, const AnyExprRef& oldMember) {
        memberValueChanged(index, getHashForceDefined(oldMember));
    }

    inline void memberValueChanged(UInt index, HashType oldHash) final {
        auto& rightView = op->right->view().checkedGet(NO_UNDEFINED_IN_SUBMSET);
        HashType newHash = rightView.indexHashMap[index];
        op->changeValue([&]() {
            valueRemovedImpl(oldHash, false);
            valueAddedImpl(newHash, false);
            return true;
        });
    }

    inline void memberValuesChanged(
        const std::vector<UInt>& indices,
        const std::vector<HashType>& oldHashes) final {
        op->changeValue([&]() {
            for (HashType hash : oldHashes) {
                valueRemovedImpl(hash, false);
            }
            auto& indexHashMap = op->right->view()
                                     .checkedGet(NO_UNDEFINED_IN_SUBMSET)
                                     .indexHashMap;
            for (UInt index : indices) {
                valueAddedImpl(indexHashMap[index], false);
            }

            return true;
        });
    }

    void reattachTrigger() final {
        deleteTrigger(op->rightTrigger);
        auto trigger =
            make_shared<OperatorTrates<OpMsetSubsetEq>::RightTrigger>(op);
        op->right->addTrigger(trigger);
        op->rightTrigger = trigger;
    }
    void hasBecomeUndefined() final { op->setUndefinedAndTrigger(); }
    void hasBecomeDefined() final { op->reevaluateDefinedAndTrigger(); }
};

void OpMsetSubsetEq::updateVarViolationsImpl(const ViolationContext&,
                                             ViolationContainer& vioContainer) {
    left->updateVarViolations(violation, vioContainer);
    right->updateVarViolations(violation, vioContainer);
}

void OpMsetSubsetEq::copy(OpMsetSubsetEq&) const {}

std::ostream& OpMsetSubsetEq::dumpState(std::ostream& os) const {
    os << "OpMsetSubsetEq: violation=" << violation << "\nLeft: ";
    left->dumpState(os);
    os << "\nRight: ";
    right->dumpState(os);
    return os;
}

string OpMsetSubsetEq::getOpName() const { return "OpMsetSubsetEq"; }
void OpMsetSubsetEq::debugSanityCheckImpl() const {
    left->debugSanityCheck();
    right->debugSanityCheck();
    auto leftOption = left->getViewIfDefined();
    auto rightOption = right->getViewIfDefined();
    if (!leftOption || !rightOption) {
        sanityLargeViolationCheck(violation);
        return;
    }
    auto& leftView = *leftOption;
    auto& rightView = *rightOption;
    UInt checkViolation = 0;
    for (auto& hashCountPair : leftView.memberCounts) {
        auto rightCount = rightView.memberCount(hashCountPair.first);
        if (hashCountPair.second > rightCount) {
            checkViolation += hashCountPair.second - rightCount;
        }
    }
    sanityEqualsCheck(checkViolation, violation);
}

template <typename Op>
struct OpMaker;

template <>
struct OpMaker<OpMsetSubsetEq> {
    static ExprRef<BoolView> make(ExprRef<MSetView> l, ExprRef<MSetView> r);
};

ExprRef<BoolView> OpMaker<OpMsetSubsetEq>::make(ExprRef<MSetView> l,
                                                ExprRef<MSetView> r) {
    return make_shared<OpMsetSubsetEq>(move(l), move(r));
}
