#include "operators/opProd.h"
#include <algorithm>
#include <cassert>
#include <unordered_map>
#include "operators/flatten.h"
#include "operators/shiftViolatingIndices.h"
#include "operators/simpleOperator.hpp"
#include "utils/ignoreUnused.h"
using namespace std;
using OperandsSequenceTrigger = OperatorTrates<OpProd>::OperandsSequenceTrigger;
class OperatorTrates<OpProd>::OperandsSequenceTrigger : public SequenceTrigger {
   public:
    Int previousValue;
    unordered_map<UInt, Int> previousValues;
    OpProd* op;
    OperandsSequenceTrigger(OpProd* op) : op(op) {}
    void valueAdded(UInt, const AnyExprRef& exprIn) final {
        if (!op->evaluated) {
            return;
        }
        auto& expr = mpark::get<ExprRef<IntView>>(exprIn);
        if (expr->isUndefined()) {
            if (op->operand->view().numberUndefined == 1) {
                op->setDefined(false, false);
                visitTriggers([&](auto& t) { t->hasBecomeUndefined(); },
                              op->triggers);
            }
            return;
        }

        op->changeValue([&]() {
            op->value *= expr->view().value;
            return true;
        });
    }

    void valueRemoved(UInt, const AnyExprRef& exprIn) final {
        if (!op->evaluated) {
            return;
        }

        const auto& expr = mpark::get<ExprRef<IntView>>(exprIn);
        if (expr->isUndefined()) {
            if (op->operand->view().numberUndefined == 0) {
                op->setDefined(true, false);
                visitTriggers([&](auto& t) { t->hasBecomeDefined(); },
                              op->triggers);
            }
            return;
        }

        op->changeValue([&]() {
            op->value /= expr->view().value;
            return true;
        });
    }

    inline void beginSwaps() final {}
    inline void endSwaps() final {}
    inline void positionsSwapped(UInt, UInt) {}

    ExprRef<IntView>& getMember(UInt index) {
        return op->operand->view().getMembers<IntView>()[index];
    }
    Int getValueCatchUndef(UInt index) {
        auto& member = getMember(index);
        return (member->isUndefined()) ? 1 : member->view().value;
    }

    inline void possibleSubsequenceChange(UInt startIndex,
                                          UInt endIndex) final {
        if (!op->evaluated) {
            return;
        }

        if (endIndex - startIndex == 1) {
            previousValues[startIndex] = getValueCatchUndef(startIndex);
            return;
        }

        previousValue = 1;
        for (size_t i = startIndex; i < endIndex; i++) {
            previousValue *= getValueCatchUndef(i);
        }
    }

    inline void subsequenceChanged(UInt startIndex, UInt endIndex) final {
        if (!op->evaluated) {
            return;
        }

        Int newValue = 1;
        Int valueToRemove;
        if (endIndex - startIndex == 1) {
            debug_code(assert(previousValues.count(startIndex)));
            valueToRemove = previousValues[startIndex];
            previousValues.erase(startIndex);
        } else {
            valueToRemove = previousValue;
        }

        for (size_t i = startIndex; i < endIndex; i++) {
            newValue *= getValueCatchUndef(i);
        }
        if (!op->isDefined()) {
            op->value /= valueToRemove;
            op->value *= newValue;
        } else {
            op->changeValue([&]() {
                op->value /= valueToRemove;
                op->value *= newValue;
                return true;
            });
        }
    }
    void possibleValueChange() final {}
    void valueChanged() final {
        if (!op->isDefined()) {
            op->reevaluate();
            if (op->isDefined()) {
                visitTriggers([&](auto& t) { t->hasBecomeDefined(); },
                              op->triggers);
            }
            return;
        }
        bool isDefined = op->changeValue([&]() {
            op->reevaluate();
            return op->isDefined();
        });
        if (!isDefined) {
            visitTriggers([&](auto& t) { t->hasBecomeUndefined(); },
                          op->triggers);
        }
    }

    void reattachTrigger() final {
        deleteTrigger(op->operandTrigger);
        auto trigger = make_shared<OperandsSequenceTrigger>(op);
        op->operand->addTrigger(trigger);
        op->operandTrigger = trigger;
    }

    void hasBecomeUndefined() final { op->setDefined(false, true); }
    void hasBecomeDefined() final { op->setDefined(true, true); }

    void memberHasBecomeUndefined(UInt index) {
        if (!op->evaluated) {
            return;
        }
        debug_code(assert(previousValues.count(index)));
        op->value /= previousValues[index];
        if (op->operand->view().numberUndefined == 1) {
            op->setDefined(false, false);
            visitTriggers([&](auto& t) { t->hasBecomeUndefined(); },
                          op->triggers);
        }
    }

    void memberHasBecomeDefined(UInt index) {
        if (!op->evaluated) {
            if (op->operand->view().numberUndefined == 0) {
                op->setDefined(true, true);
            }
            return;
        }

        op->value *=
            op->operand->view().getMembers<IntView>()[index]->view().value;
        if (op->operand->view().numberUndefined == 0) {
            op->setDefined(true, false);
            visitTriggers([&](auto& t) { t->hasBecomeDefined(); },
                          op->triggers);
        }
    }
};

void OpProd::reevaluate() {
    setDefined(true, false);
    value = 1;
    for (auto& operandChild : operand->view().getMembers<IntView>()) {
        if (operandChild->isUndefined()) {
            setDefined(false, false);
        } else {
            value *= operandChild->view().value;
        }
    }
    evaluated = true;
}

void OpProd::updateVarViolations(UInt parentViolation,
                                        ViolationContainer& vioDesc) {
    for (auto& operandChild : operand->view().getMembers<IntView>()) {
        operandChild->updateVarViolations(parentViolation, vioDesc);
    }
}

void OpProd::copy(OpProd& newOp) const {
    newOp.value = value;
    newOp.evaluated = this->evaluated;
}

std::ostream& OpProd::dumpState(std::ostream& os) const {
    os << "OpProd: value=" << value << endl;
    return operand->dumpState(os);
}

bool OpProd::optimiseImpl() { return flatten<IntView>(*this); }
template <typename Op>
struct OpMaker;

template <>
struct OpMaker<OpProd> {
    static ExprRef<IntView> make(ExprRef<SequenceView>);
};

ExprRef<IntView> OpMaker<OpProd>::make(ExprRef<SequenceView> o) {
    return make_shared<OpProd>(move(o));
}
