#include "operators/opLess.h"
#include "operators/simpleOperator.hpp"
using namespace std;
void OpLess::reevaluate() {
    Int diff = (right->view().value - 1) - left->view().value;
    violation = abs(min<Int>(diff, 0));
}

void OpLess::updateVarViolationsImpl(const ViolationContext&,
                                     ViolationContainer& vioContainer) {
    if (violation == 0) {
        return;
    } else if (allOperandsAreDefined()) {
        left->updateVarViolations(
            IntViolationContext(violation,
                                IntViolationContext::Reason::TOO_LARGE),
            vioContainer);
        right->updateVarViolations(
            IntViolationContext(violation,
                                IntViolationContext::Reason::TOO_SMALL),
            vioContainer);
    } else {
        left->updateVarViolations(violation, vioContainer);
        right->updateVarViolations(violation, vioContainer);
    }
}
void OpLess::copy(OpLess& newOp) const { newOp.violation = violation; }

ostream& OpLess::dumpState(ostream& os) const {
    os << "OpLess: violation=" << violation << "\nleft: ";
    left->dumpState(os);
    os << "\nright: ";
    right->dumpState(os);
    return os;
}

template <typename Op>
struct OpMaker;

template <>
struct OpMaker<OpLess> {
    static ExprRef<BoolView> make(ExprRef<IntView> l, ExprRef<IntView> r);
};

ExprRef<BoolView> OpMaker<OpLess>::make(ExprRef<IntView> l,
                                        ExprRef<IntView> r) {
    return make_shared<OpLess>(move(l), move(r));
}
