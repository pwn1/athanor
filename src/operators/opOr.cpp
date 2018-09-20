
#include "operators/opOr.h"
#include <algorithm>
#include <cassert>
#include "operators/flatten.h"
#include "operators/shiftViolatingIndices.h"
#include "operators/simpleOperator.hpp"
#include "utils/ignoreUnused.h"
using namespace std;
using OperandsSequenceTrigger = OperatorTrates<OpOr>::OperandsSequenceTrigger;
OpOr::OpOr(OpOr&& other)
    : SimpleUnaryOperator<BoolView, SequenceView, OpOr>(move(other)),
      minViolationIndices(move(other.minViolationIndices)) {}
UInt findNewMinViolation(OpOr& op, UInt minViolation);
void OpOr::reevaluateImpl(SequenceView& operandView) {
    minViolationIndices.clear();
    if (operandView.numberElements() == 0) {
        setDefined(false);
        return;
    }
    violation = operandView.getMembers<BoolView>()[0]->view().get().violation;
    minViolationIndices.insert(0);
    violation = findNewMinViolation(*this, violation);
}

inline UInt findNewMinViolation(OpOr& op, UInt minViolation) {
    auto operandView = op.operand->view();
    if (!operandView) {
        op.setDefined(false);
        op.minViolationIndices.clear();
        return op.violation;
    }
    auto& members = (*operandView).getMembers<BoolView>();
    for (size_t i = 0; i < members.size(); ++i) {
        auto& operandChild = members[i];
        UInt violation = operandChild->view().get().violation;
        if (violation < minViolation) {
            minViolation = violation;
            op.minViolationIndices.clear();
            op.minViolationIndices.insert(i);
        } else if (violation == minViolation) {
            op.minViolationIndices.insert(i);
        }
    }
    return minViolation;
}

// returns true if the function did a full revaluate of the OpOr node
inline bool handleOperandValueChange(OpOr& op, UInt index) {
    auto operandView = op.operand->view();
    if (!operandView) {
        op.setDefined(false);
        op.minViolationIndices.clear();
        return true;
    }
    const ExprRef<BoolView> expr = (*operandView).getMembers<BoolView>()[index];
    bool fullReevaluate = false;
    if (expr->view().get().violation < op.violation) {
        op.violation = expr->view().get().violation;
        op.minViolationIndices.clear();
        op.minViolationIndices.insert(index);
    } else if (expr->view().get().violation == op.violation) {
        op.minViolationIndices.insert(index);
    } else {
        // otherwise violation is greater, needs to be removed
        op.minViolationIndices.erase(index);
        if (op.minViolationIndices.size() == 0) {
            // new min needs to be found
            fullReevaluate = true;
            op.reevaluate();
        }
    }
    return fullReevaluate;
}

class OperatorTrates<OpOr>::OperandsSequenceTrigger : public SequenceTrigger {
   public:
    OpOr* op;
    OperandsSequenceTrigger(OpOr* op) : op(op) {}
    void valueAdded(UInt index, const AnyExprRef& exprIn) final {
        auto& expr = mpark::get<ExprRef<BoolView>>(exprIn);
        if (expr->view().get().violation > op->violation) {
            shiftIndicesUp(index, op->operand->view().get().numberElements(),
                           op->minViolationIndices);
            return;
        } else if (expr->view().get().violation < op->violation) {
            op->minViolationIndices.clear();
            op->minViolationIndices.insert(index);
            op->changeValue([&]() {
                op->violation = expr->view().get().violation;
                return true;
            });
            return;
        } else {
            // violation is equal to min violation
            // need to add this index to set of min violation indices.
            // must shift other indices up
            shiftIndicesUp(index, op->operand->view().get().numberElements(),
                           op->minViolationIndices);
            op->minViolationIndices.insert(index);
        }
    }

    void valueRemoved(UInt index, const AnyExprRef&) final {
        if (op->minViolationIndices.count(index)) {
            op->minViolationIndices.erase(index);
        }
        if (op->minViolationIndices.size() > 0) {
            shiftIndicesDown(index, op->operand->view().get().numberElements(),
                             op->minViolationIndices);
        } else {
            op->changeValue([&]() {
                op->reevaluate();
                return true;
            });
        }
    }

    inline void positionsSwapped(UInt index1, UInt index2) {
        if (op->minViolationIndices.count(index1)) {
            if (!op->minViolationIndices.count(index2)) {
                op->minViolationIndices.erase(index1);
                op->minViolationIndices.insert(index2);
            }
        } else {
            if (op->minViolationIndices.count(index2)) {
                op->minViolationIndices.erase(index2);
                op->minViolationIndices.insert(index1);
            }
        }
    }
    inline void subsequenceChanged(UInt startIndex, UInt endIndex) final {
        op->changeValue([&]() {
            for (size_t i = startIndex; i < endIndex; i++) {
                bool fullReevaluated = handleOperandValueChange(*op, i);
                if (fullReevaluated) {
                    break;
                }
            }
            return true;
        });
    }

    void valueChanged() final {
        op->changeValue([&]() {
            op->reevaluate();
            return true;
        });
    }
    void reattachTrigger() final {
        deleteTrigger(op->operandTrigger);
        auto trigger = make_shared<OperandsSequenceTrigger>(op);
        op->operand->addTrigger(trigger);
        op->operandTrigger = trigger;
    }
    void hasBecomeUndefined() final { op->setUndefinedAndTrigger(); }
    void hasBecomeDefined() final { op->reevaluateDefinedAndTrigger(); }
    void memberHasBecomeUndefined(UInt) final { shouldNotBeCalledPanic; }
    void memberHasBecomeDefined(UInt) final { shouldNotBeCalledPanic; }
};

void OpOr::updateVarViolationsImpl(const ViolationContext&,
                                   ViolationContainer& vioContainer) {
    auto operandView = operand->view();
    if (!operandView) {
        operand->updateVarViolations(violation, vioContainer);
        return;
    }
    for (auto& operandChild : (*operandView).getMembers<BoolView>()) {
        operandChild->updateVarViolations(violation, vioContainer);
    }
}

void OpOr::copy(OpOr& newOp) const {
    newOp.violation = violation;
    newOp.minViolationIndices = minViolationIndices;
}

std::ostream& OpOr::dumpState(std::ostream& os) const {
    os << "OpOr: violation=" << violation << endl;
    vector<UInt> sortedViolatingOperands(minViolationIndices.begin(),
                                         minViolationIndices.end());
    sort(sortedViolatingOperands.begin(), sortedViolatingOperands.end());
    os << "Min violating indices: " << sortedViolatingOperands << endl;
    return operand->dumpState(os);
}

bool OpOr::optimiseImpl() { return flatten<BoolView>(*this); }
template <typename Op>
struct OpMaker;

template <>
struct OpMaker<OpOr> {
    static ExprRef<BoolView> make(ExprRef<SequenceView>);
};

ExprRef<BoolView> OpMaker<OpOr>::make(ExprRef<SequenceView> o) {
    return make_shared<OpOr>(move(o));
}
