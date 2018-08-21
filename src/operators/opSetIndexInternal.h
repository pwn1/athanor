#ifndef SRC_OPERATORS_OPSETINDEXINTERNAL_H_
#define SRC_OPERATORS_OPSETINDEXINTERNAL_H_

#include "base/base.h"
#include "types/set.h"

template <typename SetMemberViewType>
struct OpSetIndexInternal : public ExprInterface<SetMemberViewType> {
    typedef typename AssociatedTriggerType<SetMemberViewType>::type
        SetMemberTriggerType;
    struct SetOperandTrigger;
    std::vector<std::shared_ptr<TriggerBase>> triggers;
    ExprRef<SetView> setOperand;
    UInt index;
    bool defined = false;
    std::shared_ptr<SetOperandTrigger> setTrigger;
    std::vector<size_t> parentSetMapping;
    std::vector<size_t> setParentMapping;

    OpSetIndexInternal(ExprRef<SetView> setOperand, UInt index)
        : setOperand(std::move(setOperand)), index(index) {}
    OpSetIndexInternal(const OpSetIndexInternal<SetMemberViewType>&) = delete;
    OpSetIndexInternal(OpSetIndexInternal<SetMemberViewType>&& other);
    ~OpSetIndexInternal() { this->stopTriggeringOnChildren(); }
    void addTriggerImpl(const std::shared_ptr<SetMemberTriggerType>& trigger,
                        bool includeMembers, Int memberIndex) final;
    SetMemberViewType& view() final;
    const SetMemberViewType& view() const final;

    void evaluateImpl() final;
    void startTriggeringImpl() final;
    void stopTriggering() final;
    void stopTriggeringOnChildren();

    void updateVarViolationsImpl(const ViolationContext& vioContext,
                                 ViolationContainer&) final;
    ExprRef<SetMemberViewType> deepCopySelfForUnrollImpl(
        const ExprRef<SetMemberViewType>& self,
        const AnyIterRef& iterator) const final;
    std::ostream& dumpState(std::ostream& os) const final;
    void findAndReplaceSelf(const FindAndReplaceFunction&) final;
    bool isUndefined();
    std::pair<bool, ExprRef<SetMemberViewType>> optimise(
        PathExtension path) final;
    ExprRef<SetMemberViewType>& getMember();
    const ExprRef<SetMemberViewType>& getMember() const;
    void reevaluateDefined();
    void notifyPossibleMemberSwap();
    void notifyMemberSwapped();
    void evaluateMappings();
    void handleSetMemberValueChange(UInt index);
    void swapMemberMappings(size_t index1, size_t index2);
    ExprRef<SetMemberViewType>& getMember(size_t index);
    const ExprRef<SetMemberViewType>& getMember(size_t index) const;
};

#endif /* SRC_OPERATORS_OPSETINDEXINTERNAL_H_ */
