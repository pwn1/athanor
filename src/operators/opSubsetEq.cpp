#include "operators/opSubsetEq.h"
#include <iostream>
#include <memory>
#include "operators/simpleOperator.hpp"
#include "types/set.h"
using namespace std;

void OpSubsetEq::reevaluate() {
    violation = 0;
    for (auto& hash : left->view().memberHashes) {
        violation += !right->view().memberHashes.count(hash);
    }
}

struct OperatorTrates<OpSubsetEq>::LeftTrigger : public SetTrigger {
   public:
    OpSubsetEq* op;
    HashType oldHash;
    vector<HashType> oldHashes;

   public:
    LeftTrigger(OpSubsetEq* op) : op(op) {}
    inline void valueRemovedImpl(HashType hash, bool trigger) {
        if (!op->right->view().memberHashes.count(hash)) {
            op->changeValue([&]() {
                --op->violation;
                return trigger;
            });
        }
    }
    void valueRemoved(UInt, HashType hash) { valueRemovedImpl(hash, true); }
    inline void valueAddedImpl(HashType hash, bool triggering) {
        if (!op->right->view().memberHashes.count(hash)) {
            op->changeValue([&]() {
                ++op->violation;
                return triggering;
            });
        }
    }
    void valueAdded(const AnyExprRef& member) {
        valueAddedImpl(getValueHash(member), true);
    }
    void possibleValueChange() final {}
    inline void valueChanged() final {
        op->changeValue([&]() {
            op->reevaluate();
            return true;
        });
    }

    inline void possibleMemberValueChange(UInt index) final {
        mpark::visit(
            [&](auto& members) {
                oldHash = getValueHash(members[index]->view());
            },
            op->left->view().members);
    }

    inline void memberValueChanged(UInt index) final {
        HashType newHash = mpark::visit(
            [&](auto& members) { return getValueHash(members[index]->view()); },
            op->left->view().members);
        op->changeValue([&]() {
            valueRemovedImpl(oldHash, false);
            valueAddedImpl(newHash, false);
            return true;
        });
        oldHash = newHash;
    }

    inline void possibleMemberValuesChange(
        const std::vector<UInt>& indices) final {
        mpark::visit(
            [&](auto& members) {
                oldHashes.resize(indices.size());
                transform(begin(indices), end(indices), begin(oldHashes),
                          [&](UInt index) {
                              return getValueHash(members[index]->view());
                          });
            },
            op->left->view().members);
    }

    inline void memberValuesChanged(const std::vector<UInt>& indices) final {
        op->changeValue([&]() {
            for (HashType hash : oldHashes) {
                valueRemovedImpl(hash, false);
            }
            // repopulate oldHashes with newHashes
            mpark::visit(
                [&](auto& members) {
                    oldHashes.resize(indices.size());
                    transform(begin(indices), end(indices), begin(oldHashes),
                              [&](UInt index) {
                                  return getValueHash(members[index]->view());
                              });
                },
                op->left->view().members);
            for (HashType hash : oldHashes) {
                valueAddedImpl(hash, false);
            }
            return true;
        });
    }

    void reattachTrigger() final {
        deleteTrigger(op->leftTrigger);
        auto trigger = make_shared<OperatorTrates<OpSubsetEq>::LeftTrigger>(op);
        op->left->addTrigger(trigger);
        op->leftTrigger = trigger;
    }
    void hasBecomeUndefined() final { op->setDefined(false, true); }
    void hasBecomeDefined() final { op->setDefined(true, true); }
};

struct OperatorTrates<OpSubsetEq>::RightTrigger : public SetTrigger {
    OpSubsetEq* op;
    HashType oldHash;
    vector<HashType> oldHashes;

    RightTrigger(OpSubsetEq* op) : op(op) {}
    inline void valueRemovedImpl(HashType hash, bool triggering) {
        if (op->left->view().memberHashes.count(hash)) {
            op->changeValue([&]() {
                ++op->violation;
                return triggering;
            });
        }
    }
    void valueRemoved(UInt, HashType hash) { valueRemovedImpl(hash, true); }

    inline void valueAddedImpl(HashType hash, bool triggering) {
        if (op->left->view().memberHashes.count(hash)) {
            op->changeValue([&]() {
                --op->violation;
                return triggering;
            });
        }
    }
    void valueAdded(const AnyExprRef& member) {
        valueAddedImpl(getValueHash(member), true);
    }

    void possibleValueChange() final {}
    inline void valueChanged() final {
        op->changeValue([&]() {
            op->reevaluate();
            return true;
        });
    }

    inline void possibleMemberValueChange(UInt index) final {
        mpark::visit(
            [&](auto& members) {
                oldHash = getValueHash(members[index]->view());
            },
            op->right->view().members);
    }

    inline void memberValueChanged(UInt index) final {
        HashType newHash = mpark::visit(
            [&](auto& members) { return getValueHash(members[index]->view()); },
            op->right->view().members);
        op->changeValue([&]() {
            valueRemovedImpl(oldHash, false);
            valueAddedImpl(newHash, false);
            return true;
        });
        oldHash = newHash;
    }

    inline void possibleMemberValuesChange(
        const std::vector<UInt>& indices) final {
        mpark::visit(
            [&](auto& members) {
                oldHashes.resize(indices.size());
                transform(begin(indices), end(indices), begin(oldHashes),
                          [&](UInt index) {
                              return getValueHash(members[index]->view());
                          });
            },
            op->right->view().members);
    }

    inline void memberValuesChanged(const std::vector<UInt>& indices) final {
        op->changeValue([&]() {
            for (HashType hash : oldHashes) {
                valueRemovedImpl(hash, false);
            }
            // repopulate oldHashes with newHashes
            mpark::visit(
                [&](auto& members) {
                    oldHashes.resize(indices.size());
                    transform(begin(indices), end(indices), begin(oldHashes),
                              [&](UInt index) {
                                  return getValueHash(members[index]->view());
                              });
                },
                op->right->view().members);
            for (HashType hash : oldHashes) {
                valueAddedImpl(hash, false);
            }
            return true;
        });
    }
    void reattachTrigger() final {
        deleteTrigger(op->rightTrigger);
        auto trigger =
            make_shared<OperatorTrates<OpSubsetEq>::RightTrigger>(op);
        op->left->addTrigger(trigger);
        op->rightTrigger = trigger;
    }
    void hasBecomeUndefined() final { op->setDefined(false, true); }
    void hasBecomeDefined() final { op->setDefined(true, true); }
};

void OpSubsetEq::updateVarViolations(const ViolationContext&,
                                     ViolationContainer& vioDesc) {
    left->updateVarViolations(violation, vioDesc);
    right->updateVarViolations(violation, vioDesc);
}

void OpSubsetEq::copy(OpSubsetEq& newOp) const { newOp.violation = violation; }

std::ostream& OpSubsetEq::dumpState(std::ostream& os) const {
    os << "OpSubsetEq: violation=" << violation << "\nLeft: ";
    left->dumpState(os);
    os << "\nRight: ";
    right->dumpState(os);
    return os;
}
template <typename Op>
struct OpMaker;

template <>
struct OpMaker<OpSubsetEq> {
    static ExprRef<BoolView> make(ExprRef<SetView> l, ExprRef<SetView> r);
};

ExprRef<BoolView> OpMaker<OpSubsetEq>::make(ExprRef<SetView> l,
                                            ExprRef<SetView> r) {
    return make_shared<OpSubsetEq>(move(l), move(r));
}
