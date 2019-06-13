#include "operators/opBoolEq.h"
#include "operators/simpleOperator.hpp"
using namespace std;
void OpBoolEq::reevaluateImpl(BoolView& leftView, BoolView& rightView) {
    UInt leftVio = leftView.violation;
    UInt rightVio = rightView.violation;
    if (leftVio > rightVio) {
        swap(leftVio, rightVio);
    }
    if (leftVio == 0) {
        violation = rightVio;
    } else {
        violation = (rightVio == 0) ? 1 : 0;
    }
}

void OpBoolEq::updateVarViolationsImpl(const ViolationContext&,
                                       ViolationContainer& vioContainer) {
    if (violation == 0) {
        return;
    } else if (allOperandsAreDefined()) {
        // either one of left or right is true, other is false
        bool leftIsTrue = (left->view()->violation == 0);
        bool rightIsTrue = (right->view()->violation == 0);
        left->updateVarViolations(BoolViolationContext(violation, leftIsTrue),
                                  vioContainer);
        right->updateVarViolations(BoolViolationContext(violation, rightIsTrue),
                                   vioContainer);
    } else {
        cerr << "internal error, bool operators claiming to be undefined.\n";
        shouldNotBeCalledPanic;
    }
}

void OpBoolEq::copy(OpBoolEq& newOp) const { newOp.violation = violation; }

ostream& OpBoolEq::dumpState(ostream& os) const {
    os << "OpBoolEq: violation=" << violation << "\nleft: ";
    left->dumpState(os);
    os << "\nright: ";
    right->dumpState(os);
    return os << ")";
}

string OpBoolEq::getOpName() const { return "OpBoolEq"; }
void OpBoolEq::debugSanityCheckImpl() const {
    left->debugSanityCheck();
    right->debugSanityCheck();
    auto leftOption = left->getViewIfDefined();
    auto rightOption = right->getViewIfDefined();

    sanityCheck(leftOption && rightOption,
                "bool operators cannot be undefined.");

    auto& leftView = *leftOption;
    auto& rightView = *rightOption;

    UInt checkViolation;
    UInt leftVio = leftView.violation;
    UInt rightVio = rightView.violation;
    if (leftVio > rightVio) {
        swap(leftVio, rightVio);
    }
    if (leftVio == 0) {
        checkViolation = rightVio;
    } else {
        checkViolation = (rightVio == 0) ? 1 : 0;
    }
    sanityEqualsCheck(checkViolation, violation);
}

template <typename>
struct OpMaker;
template <>
struct OpMaker<OpBoolEq> {
    static ExprRef<BoolView> make(ExprRef<BoolView> l, ExprRef<BoolView> r);
};

ExprRef<BoolView> OpMaker<OpBoolEq>::make(ExprRef<BoolView> l,
                                          ExprRef<BoolView> r) {
    return make_shared<OpBoolEq>(move(l), move(r));
}