
#ifndef SRC_OPERATORS_INTRANGE_H_
#define SRC_OPERATORS_INTRANGE_H_
#include "operators/simpleOperator.h"
#include "types/int.h"
#include "types/sequence.h"
struct IntRange;
template <>
struct OperatorTrates<IntRange> {
    template <bool isLeft>
    struct Trigger;
    typedef Trigger<true> LeftTrigger;
    typedef Trigger<false> RightTrigger;
};

struct IntRange : public SimpleBinaryOperator<SequenceView, IntView, IntView, IntRange> {
    Int cachedLower;
    Int cachedUpper;

    IntRange(ExprRef<IntView> left, ExprRef<IntView> right)
        : SimpleBinaryOperator<SequenceView, IntView,IntView, IntRange>(
              std::move(left), std::move(right)) {
        this->members.emplace<ExprRefVec<IntView>>();
    }

    IntRange(IntRange&&) = delete;
    IntRange(const IntRange&) = delete;

    void reevaluateImpl(IntView& leftView, IntView& rightView, bool, bool);

    void updateVarViolationsImpl(const ViolationContext& vioContext,
                                 ViolationContainer& vioContainer) final;
    void copy(IntRange& newOp) const;
    std::ostream& dumpState(std::ostream& os) const final;
    void debugSanityCheckImpl() const final;
    std::string getOpName() const final;
};
#endif /* SRC_OPERATORS_INTRANGE_H_ */
