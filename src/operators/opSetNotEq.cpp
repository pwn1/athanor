#include "operators/opSetNotEq.h"
#include <iostream>
#include <memory>
#include "types/set.h"

using namespace std;

inline void setViolation(OpSetNotEq& op, bool trigger) {
    SetView& leftSetView = getView<SetView>(op.left);
    SetView& rightSetView = getView<SetView>(op.right);
    u_int64_t oldViolation = op.violation;
    op.violation =
        (leftSetView.cachedHashTotal == rightSetView.cachedHashTotal) ? 1 : 0;
    if (trigger && op.violation != oldViolation) {
        for (auto& trigger : op.triggers) {
            trigger->possibleValueChange(oldViolation);
        }
        for (auto& trigger : op.triggers) {
            trigger->valueChanged(op.violation);
        }
    }
}

void evaluate(OpSetNotEq& op) {
    evaluate(op.left);
    evaluate(op.right);
    setViolation(op, false);
}

class OpSetNotEqTrigger : public SetTrigger {
    OpSetNotEq& op;

   public:
    OpSetNotEqTrigger(OpSetNotEq& op) : op(op) {}
    inline void valueRemoved(const Value&) final { setViolation(op, true); }

    inline void valueAdded(const Value&) final { setViolation(op, true); }

    inline void possibleValueChange(const Value&) final {}

    inline void valueChanged(const Value&) final { setViolation(op, true); }
};

void startTriggering(OpSetNotEq& op) {
    getView<SetView>(op.left).triggers.emplace_back(
        make_shared<OpSetNotEqTrigger>(op));
    getView<SetView>(op.right).triggers.emplace_back(
        make_shared<OpSetNotEqTrigger>(op));
    startTriggering(op.left);
    startTriggering(op.right);
}

void stopTriggering(OpSetNotEq& op) {
    assert(false);  // todo
    stopTriggering(op.left);
    stopTriggering(op.right);
}

void updateViolationDescription(const OpSetNotEq& op, u_int64_t,
                                ViolationDescription& vioDesc) {
    updateViolationDescription(op.left, op.violation, vioDesc);
    updateViolationDescription(op.right, op.violation, vioDesc);
}