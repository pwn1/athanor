#include "operators/opTupleIndex.h"
#include "operators/opTupleLit.h"
#include "operators/quantifier.hpp"
#include "types/intVal.h"
#include "types/sequence.h"

template <typename Op>
struct OpMaker;
template <>
struct OpMaker<OpTupleLit> {
    static ExprRef<TupleView> make(std::vector<AnyExprRef> members);
};

using namespace std;
template <>
struct InitialUnroller<SequenceView> {
    template <typename Quant>
    static void initialUnroll(Quant& quantifier, SequenceView& containerView) {
        mpark::visit(
            [&](auto& membersImpl) {
                for (size_t i = 0; i < membersImpl.size(); i++) {
                    auto tupleFirstMember = make<IntValue>();
                    tupleFirstMember->value = i + 1;
                    auto unrolledExpr = OpMaker<OpTupleLit>::make(
                        {tupleFirstMember.asExpr(), membersImpl[i]});
                    unrolledExpr->evaluate();
                    quantifier.template unroll<TupleView>(
                        {false, i, unrolledExpr});
                }
            },
            containerView.members);
    }
};

template <>
struct ContainerTrigger<SequenceView> : public SequenceTrigger {
    Quantifier<SequenceView>* op;

    ContainerTrigger(Quantifier<SequenceView>* op) : op(op) {}
    void valueRemoved(UInt indexOfRemovedValue, const AnyExprRef&) final {
        op->roll(indexOfRemovedValue);
        correctUnrolledTupleIndices(indexOfRemovedValue);
    }
    void valueAdded(UInt index, const AnyExprRef& member) final {
        mpark::visit(
            [&](auto& member) {
                containerSpecificUnroll<viewType(member)>(
                    index, {false, index, member});
            },
            member);
    }

    void subsequenceChanged(UInt, UInt) final{};

    void valueChanged() {
        while (op->numberUnrolled() != 0) {
            this->valueRemoved(op->numberUnrolled() - 1,
                               ExprRef<BoolView>(nullptr));
        }
        auto containerView = op->container->view();
        if (!containerView) {
            return;
        }
        InitialUnroller<SequenceView>::initialUnroll(*op, *containerView);
    }

    void positionsSwapped(UInt index1, UInt index2) final {
        op->notifyContainerMembersSwapped(index1, index2);
        correctUnrolledTupleIndex(index1);
        correctUnrolledTupleIndex(index2);
    }

    template <typename View>
    void containerSpecificUnroll(UInt memberIndex,
                                 QueuedUnrollValue<View> queuedValue) {
        // this is a bit hacky, if queuedValue.directUnrollExpr is true, we do
        // not create a new tuple to hold to the new value, we assume that we
        // have already been given the tuple.
        if (!queuedValue.directUnrollExpr) {
            auto tupleFirstMember = make<IntValue>();
            tupleFirstMember->value = memberIndex + 1;
            auto unrolledExpr = OpMaker<OpTupleLit>::make(
                {tupleFirstMember.asExpr(), queuedValue.value});
            unrolledExpr->evaluate();
            op->template unroll<TupleView>({queuedValue.directUnrollExpr,
                                            queuedValue.index, unrolledExpr});

        } else {
            op->template unroll(queuedValue);
        }
        if (memberIndex + 1 < op->unrolledIterVals.size()) {
            correctUnrolledTupleIndices(memberIndex);
        }
    }

    void reattachTrigger() final {
        deleteTrigger(op->containerTrigger);
        auto trigger = make_shared<ContainerTrigger<SequenceView>>(op);
        op->container->addTrigger(trigger);
        op->containerTrigger = trigger;
    }

    void correctUnrolledTupleIndices(size_t startIndex) {
        debug_log("correct tuple indices from " << startIndex << " onwards.");
        for (size_t i = startIndex; i < op->unrolledIterVals.size(); i++) {
            correctUnrolledTupleIndex(i);
        }
    }
    void correctUnrolledTupleIndex(size_t index) {
        debug_log("Correcting tuple index " << index);
        auto& tuple =
            mpark::get<IterRef<TupleView>>(op->unrolledIterVals[index]);
        auto& intView = mpark::get<ExprRef<IntView>>(tuple->view()->members[0]);
        auto& intVal = static_cast<IntValue&>(*intView);
        intVal.changeValue([&]() {
            intVal.value = index + 1;
            return true;
        });
    }

    void hasBecomeUndefined() {
        op->containerDefined = false;
        if (op->appearsDefined()) {
            op->setAppearsDefined(false);
            op->notifyValueUndefined();
        }
    }
    void hasBecomeDefined() {
        this->valueChanged();
        op->containerDefined = true;
        op->setAppearsDefined(op->numberUndefined == 0);
        op->notifyValueDefined();
    }

    void memberHasBecomeUndefined(UInt) final {}
    void memberHasBecomeDefined(UInt) final {}
};

template <>
struct ContainerSanityChecker<SequenceView> {
    static void debugSanityCheck(const Quantifier<SequenceView>& quant,
                                 const SequenceView& container) {
        if (quant.condition) {
            sanityEqualsCheck(container.numberElements(),
                              quant.unrolledConditions.size());
        } else {
            sanityEqualsCheck(container.numberElements(),
                              quant.numberElements());
        }

        sanityEqualsCheck(container.numberElements(),
                          quant.unrolledIterVals.size());
        for (size_t i = 0; i < quant.unrolledIterVals.size(); i++) {
            auto* iterPtr = mpark::get_if<IterRef<TupleView>>(
                &(quant.unrolledIterVals[i].asVariant()));
            sanityCheck(iterPtr, "Expected tuple type here.");
            auto view = (*iterPtr)->ref->view();
            sanityCheck(view, "view() should not return undefined here.");
            sanityEqualsCheck(2, view->members.size());
            auto* intExprPtr =
                mpark::get_if<ExprRef<IntView>>(&(view->members[0]));
            sanityCheck(intExprPtr,
                        "Expected first element of unrolled tuple to be int.");
            auto intView = (*intExprPtr)->getViewIfDefined();
            sanityCheck(intView,
                        "First member of unrolled tuple should be defined.");
            sanityEqualsCheck((Int)(i + 1), intView->value);
        }
    }
};

template struct Quantifier<SequenceView>;
