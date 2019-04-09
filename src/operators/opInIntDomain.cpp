#include "operators/opInIntDomain.h"
#include <iostream>
#include <memory>
#include <tuple>
#include "operators/simpleOperator.hpp"

using namespace std;

static const auto TO_SMALL = IntViolationContext::Reason::TOO_SMALL;
static const auto TO_LARGE = IntViolationContext::Reason::TOO_LARGE;

void OpInDomain<IntView>::reevaluateImpl(IntView& operandView) {
    const auto& bounds = domain->bounds;
    if (bounds.empty()) {
        cerr << "Error: empty domain.\n";
        abort();
    }
    debug_code(assert(closestBound >= 0 && closestBound < bounds.size()));

    Int v = operandView.value;
    if (bounds[closestBound].first <= v && bounds[closestBound].second >= v) {
        this->violation = 0;
        return;
    }

    if (v < bounds.front().first) {
        tie(violation, closestBound, reason) =
            make_tuple(bounds.front().first - v, 0, TO_SMALL);
        return;
    }

    if (v > bounds.back().second) {
        tie(violation, closestBound, reason) =
            make_tuple(v - bounds.back().second, bounds.size() - 1, TO_LARGE);
        return;
    }

    for (size_t i = 0; i < bounds.size() - 1; i++) {
        const auto& bound = bounds[i];
        const auto& nextBound = bounds[i + 1];
        if (bound.first <= v && bound.second >= v) {
            tie(violation, i) = make_tuple(0, i);
            return;
        }
        if (v > bound.second && v < nextBound.first) {
            Int distanceToBound = v - bound.second;
            Int distanceToNextBound = nextBound.second - v;
            tie(violation, closestBound, reason) =
                (distanceToBound < distanceToNextBound)
                    ? make_tuple(distanceToBound, i, TO_LARGE)
                    : make_tuple(distanceToNextBound, i + 1, TO_SMALL);
            return;
        }
    }
    assert(false);
    abort();
}

void OpInDomain<IntView>::updateVarViolationsImpl(
    const ViolationContext&, ViolationContainer& vioContainer) {
    operand->updateVarViolations(IntViolationContext(this->violation, reason),
                                 vioContainer);
}

void OpInDomain<IntView>::copy(OpInDomain<IntView>& newOp) const {
    newOp.domain = domain;
    newOp.violation = violation;
    newOp.closestBound = closestBound;
    newOp.reason = reason;
}

std::ostream& OpInDomain<IntView>::dumpState(std::ostream& os) const {
    os << "OpInDomain<IntView>: violation=" << violation << "\noperand: ";
    operand->dumpState(os);
    return os;
}

string OpInDomain<IntView>::getOpName() const { return "OpInDomain<IntView>"; }
void OpInDomain<IntView>::debugSanityCheckImpl() const {
    operand->debugSanityCheck();

    auto view = operand->getViewIfDefined();
    if (!view) {
        sanityLargeViolationCheck(violation);
        return;
    }
    auto& operandView = *view;

    const auto& bounds = domain->bounds;
    if (bounds.empty()) {
        cerr << "Error: empty domain.\n";
        abort();
    }
    sanityCheck(closestBound >= 0 && closestBound < bounds.size(),
                "closestBound out of range.\n");

    Int v = operandView.value;
    if (bounds[closestBound].first <= v && bounds[closestBound].second >= v) {
        sanityEqualsCheck(violation, 0);
        return;
    }

    if (v < bounds.front().first) {
        sanityEqualsCheck(tie(violation, closestBound, reason),
                          make_tuple(bounds.front().first - v, 0, TO_SMALL));
        return;
    }

    if (v > bounds.back().second) {
        sanityEqualsCheck(
            tie(violation, closestBound, reason),
            make_tuple(v - bounds.back().second, bounds.size() - 1, TO_LARGE));
        return;
    }

    for (size_t i = 0; i < bounds.size() - 1; i++) {
        const auto& bound = bounds[i];
        const auto& nextBound = bounds[i + 1];
        if (bound.first <= v && bound.second >= v) {
            sanityEqualsCheck(tie(violation, i), make_tuple(0, i));
            return;
        }
        if (v > bound.second && v < nextBound.first) {
            Int distanceToBound = v - bound.second;
            Int distanceToNextBound = nextBound.second - v;
            sanityEqualsCheck(
                tie(violation, closestBound, reason),
                ((distanceToBound < distanceToNextBound)
                     ? make_tuple(distanceToBound, i, TO_LARGE)
                     : make_tuple(distanceToNextBound, i + 1, TO_SMALL)));
            return;
        }
    }
    assert(false);
    abort();
}

template <typename Op>
struct OpMaker;

template <>
struct OpMaker<OpInDomain<IntView>> {
    static ExprRef<BoolView> make(shared_ptr<IntDomain>, ExprRef<IntView> o);
};

ExprRef<BoolView> OpMaker<OpInDomain<IntView>>::make(
    shared_ptr<IntDomain> domain, ExprRef<IntView> o) {
    auto op = make_shared<OpInDomain<IntView>>(move(o));
    op->domain = move(domain);
    return op;
}