#include "operators/intRange.h"
#include <iostream>
#include <memory>
#include "operators/simpleOperator.hpp"

using namespace std;

void IntRange::reevaluate() {
    cachedLower = left->view().value;
    cachedUpper = right->view().value;
    this->silentClear();
    for (Int i = cachedLower; i <= cachedUpper; i++) {
        auto val = make<IntValue>();
        val->value = i;
        this->addMember(this->numberElements(), val.asExpr());
    }
}
template <bool isLeft>
struct OperatorTrates<IntRange>::Trigger : public IntTrigger {
    IntRange* op;
    Trigger(IntRange* op) : op(op) {}
    void valueChanged() final {
        if (!op->isDefined()) {
            return;
        }
        if (isLeft) {
            adjustLower(true);
        } else {
            adjustUpper(true);
        }
    }

    void hasBecomeUndefined() final {
        if (op->isDefined()) {
            op->setDefined(false, false);
            visitTriggers([&](auto& t) { t->hasBecomeUndefined(); },
                          op->triggers, true);
        }
    }
    void hasBecomeDefined() {
        op->setDefined((isLeft && !op->right->isUndefined()) ||
                           (!isLeft && !op->left->isUndefined()),
                       false);
        if (op->isDefined()) {
            if (isLeft) {
                adjustLower(false);
            } else {
                adjustUpper(false);
            }
            visitTriggers([&](auto& t) { t->hasBecomeDefined(); }, op->triggers,
                          true);
        }
    }
    inline void adjustLower(bool trigger) {
        Int newLower = op->left->view().value;
        while (op->cachedLower > newLower) {
            --op->cachedLower;
            if (op->cachedLower > op->cachedUpper) {
                continue;
            }
            auto val = make<IntValue>();
            val->value = op->cachedLower;
            if (trigger) {
                op->addMemberAndNotify(0, val.asExpr());
            } else {
                op->addMember(0, val.asExpr());
            }
        }
        while (op->cachedLower < newLower) {
            ++op->cachedLower;
            if (op->cachedLower > op->cachedUpper + 1) {
                continue;
            }
            if (trigger) {
                op->removeMemberAndNotify<IntView>(0);
            } else {
                op->removeMember<IntView>(0);
            }
        }
    }
    inline void adjustUpper(bool trigger) {
        Int newUpper = op->right->view().value;
        while (op->cachedUpper < newUpper) {
            ++op->cachedUpper;
            if (op->cachedUpper < op->cachedLower) {
                continue;
            }
            auto val = make<IntValue>();
            val->value = op->cachedUpper;
            if (trigger) {
                op->addMemberAndNotify(op->numberElements(), val.asExpr());
            } else {
                op->addMember(op->numberElements(), val.asExpr());
            }
        }
        while (op->cachedUpper > newUpper) {
            --op->cachedUpper;
            if (op->cachedUpper < op->cachedLower - 1) {
                continue;
            }
            if (trigger) {
                op->removeMemberAndNotify<IntView>(op->numberElements() - 1);
            } else {
                op->removeMember<IntView>(op->numberElements() - 1);
            }
        }
    }

    inline void reattachTrigger() final {
        if (isLeft) {
            reassignLeftTrigger();
        } else {
            reassignRightTrigger();
        }
    }
    inline void reassignLeftTrigger() {
        deleteTrigger(op->leftTrigger);
        auto newTrigger = make_shared<Trigger<true>>(op);
        op->left->addTrigger(newTrigger);
        op->leftTrigger = newTrigger;
    }
    inline void reassignRightTrigger() {
        deleteTrigger(op->rightTrigger);
        auto newTrigger = make_shared<Trigger<false>>(op);
        op->right->addTrigger(newTrigger);
        op->rightTrigger = newTrigger;
    }
};

void IntRange::updateVarViolationsImpl(const ViolationContext& vioContext,
                                       ViolationContainer& vioContainer) {
    left->updateVarViolations(vioContext, vioContainer);
    right->updateVarViolations(vioContext, vioContainer);
}

void IntRange::copy(IntRange& newOp) const {
    newOp.members = this->members;
    newOp.cachedLower = cachedLower;
    newOp.cachedUpper = cachedUpper;
}

ostream& IntRange::dumpState(ostream& os) const {
    os << "IntRange(\nleft: ";
    left->dumpState(os);
    os << ",\nright: ";
    right->dumpState(os);
    os << ")";
    return os;
    return os;
}

template <typename Op>
struct OpMaker;
template <>
struct OpMaker<IntRange> {
    static ExprRef<SequenceView> make(ExprRef<IntView> l, ExprRef<IntView> r);
};

ExprRef<SequenceView> OpMaker<IntRange>::make(ExprRef<IntView> l,
                                              ExprRef<IntView> r) {
    return make_shared<IntRange>(move(l), move(r));
}

template <>
bool isInstanceOf<IntRange, SequenceView>(const ExprRef<SequenceView>& expr) {
    return dynamic_cast<IntRange*>(&(*expr)) != NULL;
}
