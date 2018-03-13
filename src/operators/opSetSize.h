
#ifndef SRC_OPERATORS_OPSETSIZE_H_
#define SRC_OPERATORS_OPSETSIZE_H_
#include "types/int.h"
class OpSetSizeTrigger;
struct OpSetSize : public IntView {
    ExprRef<SetView> operand;
    std::shared_ptr<OpSetSizeTrigger> operandTrigger;
    OpSetSize(ExprRef<SetView> operandIn) : operand(operandIn) {}
    OpSetSize(const OpSetSize&) = delete;
    OpSetSize(OpSetSize&& other);
    ~OpSetSize() { this->stopTriggering(); }
    void evaluate() final;
    void startTriggering() final;
    void stopTriggering() final;
    void updateViolationDescription(u_int64_t parentViolation,
                                    ViolationDescription&) final;
    ExprRef<IntView> deepCopyForUnroll(const ExprRef<IntView>& op,
                                       const AnyIterRef& iterator) const final;
    std::ostream& dumpState(std::ostream& os) const final;
};

#endif /* SRC_OPERATORS_OPSETSIZE_H_ */
