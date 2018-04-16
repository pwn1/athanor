#include "operators/opSetNotEq.h"
#include <iostream>
#include <memory>

using namespace std;

void OpSetNotEq::reevaluate() {
    violation =
        (left->view().cachedHashTotal == right->view().cachedHashTotal) ? 1 : 0;
}

void OpSetNotEq::updateViolationDescription(UInt,
                                            ViolationDescription& vioDesc) {
    left->updateViolationDescription(violation, vioDesc);
    right->updateViolationDescription(violation, vioDesc);
}

void OpSetNotEq::copy(OpSetNotEq& newOp) const { newOp.violation = violation; }

std::ostream& OpSetNotEq::dumpState(std::ostream& os) const {
    os << "OpSetNotEq: violation=" << violation << "\nleft: ";
    left->dumpState(os);
    os << "\nright: ";
    right->dumpState(os);
    return os;
    return os;
}

template <typename Op>
struct OpMaker;
template <>
struct OpMaker<OpSetNotEq> {
    ExprRef<BoolView> make(ExprRef<SetView> l, ExprRef<SetView> r);
};

ExprRef<BoolView> OpMaker<OpSetNotEq>::make(ExprRef<SetView> l,
                                            ExprRef<SetView> r) {
    return make_shared<OpSetNotEq>(move(l), move(r));
}
