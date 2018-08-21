
#ifndef SRC_OPERATORS_OPPROD_H_
#define SRC_OPERATORS_OPPROD_H_
#include "operators/simpleOperator.h"
#include "types/int.h"
#include "types/sequence.h"
#include "utils/fastIterableIntSet.h"

struct OpProd;
template <>
struct OperatorTrates<OpProd> {
    class OperandsSequenceTrigger;
    typedef OperandsSequenceTrigger OperandTrigger;
};

struct OpProd : public SimpleUnaryOperator<IntView, SequenceView, OpProd> {
    using SimpleUnaryOperator<IntView, SequenceView,
                              OpProd>::SimpleUnaryOperator;
    bool evaluationComplete = false;
    UInt numberZeros = 0;
    Int cachedValue;
    OpProd(OpProd&& other);
    void reevaluate();
    void updateVarViolationsImpl(const ViolationContext& vioContext,
                                 ViolationContainer& vioContainer) final;
    void copy(OpProd& newOp) const;
    std::ostream& dumpState(std::ostream& os) const final;
    bool optimiseImpl();
    void addSingleValue(Int exprValue);
    void removeSingleValue(Int exprValue);
};

#endif /* SRC_OPERATORS_OPPROD_H_ */
