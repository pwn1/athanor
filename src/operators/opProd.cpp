#include "operators/opProd.h"
#include <algorithm>
#include <cassert>
#include <unordered_map>
#include "operators/flatten.h"
#include "operators/previousValueCache.h"
#include "operators/shiftViolatingIndices.h"
#include "operators/simpleOperator.hpp"
#include "utils/ignoreUnused.h"
using namespace std;
using OperandsSequenceTrigger = OperatorTrates<OpProd>::OperandsSequenceTrigger;

OpProd::OpProd(OpProd&& other)
    : SimpleUnaryOperator<IntView, SequenceView, OpProd>(std::move(other)),
      evaluationComplete(std::move(other.evaluationComplete)),
      cachedValues(move(other.cachedValues)) {}
void OpProd::addSingleValue(Int exprValue) {
    if (exprValue == 0) {
        ++numberZeros;
    } else {
        cachedValue *= exprValue;
    }
    value *= exprValue;
    assert((numberZeros > 0 && value == 0) || (value != 0 && numberZeros == 0));
}
void OpProd::removeSingleValue(Int exprValue) {
    if (exprValue == 0) {
        --numberZeros;
        if (numberZeros == 0) {
            value = cachedValue;
        }
    } else {
        value /= exprValue;
        cachedValue /= exprValue;
    }
}

class OperatorTrates<OpProd>::OperandsSequenceTrigger : public SequenceTrigger {
   public:
    OpProd* op;
    OperandsSequenceTrigger(OpProd* op) : op(op) {}
    void valueAdded(UInt index, const AnyExprRef& exprIn) final {
        if (!op->evaluationComplete) {
            return;
        }
        auto& expr = mpark::get<ExprRef<IntView>>(exprIn);
        auto view = expr->getViewIfDefined();
        if (!view) {
            op->cachedValues.insert(index, 1);
            if (op->operand->view().get().numberUndefined == 1) {
                op->setDefined(false);
                visitTriggers([&](auto& t) { t->hasBecomeUndefined(); },
                              op->triggers, true);
            }
            return;
        }
        Int operandValue = (*view).value;
        op->cachedValues.insert(index, operandValue);
        op->changeValue([&]() {
            op->addSingleValue(operandValue);
            return true;
        });
    }

    void valueRemoved(UInt index, const AnyExprRef& exprIn) final {
        if (!op->evaluationComplete) {
            return;
        }
        const auto& expr = mpark::get<ExprRef<IntView>>(exprIn);

        Int operandValue = op->cachedValues.erase(index);

        if (!expr->appearsDefined()) {
            if (op->operand->view().get().numberUndefined == 0) {
                op->setDefined(true);
                visitTriggers([&](auto& t) { t->hasBecomeDefined(); },
                              op->triggers, true);
            }
            return;
        }
        op->changeValue([&]() {
            op->removeSingleValue(operandValue);
            return true;
        });
    }

    inline void positionsSwapped(UInt index1, UInt index2) {
        if (!op->evaluationComplete) {
            return;
        }

        swap(op->cachedValues.get(index1), op->cachedValues.get(index2));
    }

    ExprRef<IntView>& getMember(SequenceView& operandView, UInt index) {
        return operandView.getMembers<IntView>()[index];
    }
    Int getValueCatchUndef(SequenceView& operandView, UInt index) {
        auto& member = getMember(operandView, index);
        auto view = member->getViewIfDefined();
        return (view) ? (*view).value : 1;
    }

    inline void handleSingleOperandChange(SequenceView& operandView,
                                          UInt index) {
        debug_code(assert((op->numberZeros > 0 && op->value == 0) ||
                          (op->value != 0 && op->numberZeros == 0)));
        Int newValue = getValueCatchUndef(operandView, index);
        Int oldValue = op->cachedValues.getAndSet(index, newValue);
        op->removeSingleValue(oldValue);
        op->addSingleValue(newValue);
    }

    inline void subsequenceChanged(UInt startIndex, UInt endIndex) final {
        if (!op->evaluationComplete) {
            return;
        }
        auto view = op->operand->view();
        if (!view) {
            hasBecomeUndefined();
            return;
        }
        auto& operandView = *view;
        op->changeValue([&]() {
            for (size_t i = startIndex; i < endIndex; i++) {
                handleSingleOperandChange(operandView, i);
            }
            return true;
        });
    }

    void valueChanged() final {
        bool wasDefined = op->isDefined();
        op->changeValue([&]() {
            op->reevaluate();
            return op->isDefined();
        });
        if (wasDefined && !op->isDefined()) {
            visitTriggers([&](auto& t) { t->hasBecomeUndefined(); },
                          op->triggers, true);
        } else if (!wasDefined && op->isDefined()) {
            visitTriggers([&](auto& t) { t->hasBecomeDefined(); }, op->triggers,
                          true);
        }
    }

    void reattachTrigger() final {
        deleteTrigger(op->operandTrigger);
        auto trigger = make_shared<OperandsSequenceTrigger>(op);
        op->operand->addTrigger(trigger);
        op->operandTrigger = trigger;
    }

    void hasBecomeUndefined() final {
        op->setUndefinedAndTrigger();
        op->evaluationComplete = false;
    }
    void hasBecomeDefined() final { op->reevaluateDefinedAndTrigger(); }

    void memberHasBecomeUndefined(UInt index) {
        if (!op->evaluationComplete) {
            return;
        }
        op->removeSingleValue(op->cachedValues.get(index));
        auto view = op->operand->view();
        if (!view) {
            hasBecomeUndefined();
            return;
        }
        if ((*view).numberUndefined == 1) {
            op->setUndefinedAndTrigger();
        }
    }

    void memberHasBecomeDefined(UInt index) {
        auto operandView = op->operand->view();
        if (!operandView) {
            hasBecomeUndefined();
            return;
        }
        if (!op->evaluationComplete) {
            if ((*operandView).numberUndefined == 0) {
                op->reevaluateDefinedAndTrigger();
            }
            return;
        }

        Int operandValue = getValueCatchUndef(*operandView, index);
        op->addSingleValue(operandValue);
        op->cachedValues.set(index, operandValue);
        if ((*operandView).numberUndefined == 0) {
            op->setDefined(true);
            visitTriggers([&](auto& t) { t->hasBecomeDefined(); }, op->triggers,
                          true);
        }
    }
};

void OpProd::reevaluateImpl(SequenceView& operandView) {
    setDefined(true);
    value = 1;
    cachedValue = 1;
    cachedValues.clear();
    numberZeros = 0;
    auto& members = operandView.getMembers<IntView>();
    for (size_t index = 0; index < members.size(); index++) {
        auto& operandChild = members[index];
        auto operandChildView = operandChild->getViewIfDefined();
        if (!operandChildView) {
            setDefined(false);
            cachedValues.insert(index, 1);
        } else {
            Int operandValue = (*operandChildView).value;
            addSingleValue(operandValue);
            cachedValues.insert(index, operandValue);
        }
    }
    evaluationComplete = true;
}

void OpProd::updateVarViolationsImpl(const ViolationContext& vioContext,
                                     ViolationContainer& vioContainer) {
    auto operandView = operand->view();
    if (!operandView) {
        operand->updateVarViolations(vioContext, vioContainer);
        return;
    }
    for (auto& operandChild : (*operandView).getMembers<IntView>()) {
        operandChild->updateVarViolations(vioContext, vioContainer);
    }
}

void OpProd::copy(OpProd& newOp) const {
    newOp.value = value;
    newOp.cachedValues = cachedValues;
    newOp.cachedValue = cachedValue;
    newOp.numberZeros = numberZeros;
    newOp.evaluationComplete = this->evaluationComplete;
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
