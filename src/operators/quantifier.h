
#ifndef SRC_OPERATORS_QUANTIFIER_H_
#define SRC_OPERATORS_QUANTIFIER_H_

#include "operators/operatorBase.h"
#include "operators/quantifierBase.h"

inline static int nextQuantId() {
    static int id = 0;
    return id++;
}

template <typename ContainerType, typename ContainerValueType,
          typename ReturnType, typename ReturnValueType>
struct Quantifier {
    const int quantId;
    ContainerType container;
    ReturnType expr;
    std::vector<std::pair<ReturnType, IterValue>> unrolledExprs;
    std::unordered_map<u_int64_t, size_t> valueExprMap;

    Quantifier(ContainerType container, const int id = nextQuantId())
        : quantId(id), container(std::move(container)) {}

    inline void setExpression(ReturnType exprIn) { expr = std::move(exprIn); }

    template <typename T>
    inline IterRef<T> newIterRef() {
        return IterRef<T>(quantId);
    }

    inline std::pair<size_t, ReturnType&> unroll(
        const Value& newValue, const bool startTriggeringExpr = true,
        const bool evaluateFreshExpr = false) {
        mpark::visit(
            [&](auto& newValImpl) {
                typedef typename BaseType<decltype(newValImpl)>::element_type
                    IterValueType;
                auto iterRef = newIterRef<IterValueType>();

                if (evaluateFreshExpr || unrolledExprs.size() == 0) {
                    unrolledExprs.emplace_back(deepCopyForUnroll(expr, iterRef),
                                               iterRef);
                    iterRef.getIterator().attachValue(newValImpl);
                    evaluate(unrolledExprs.front().first);
                    if (startTriggeringExpr) {
                        startTriggering(unrolledExprs.back().first);
                    }
                } else {
                    unrolledExprs.emplace_back(
                        deepCopyForUnroll(unrolledExprs.back().first, iterRef),
                        iterRef);
                    if (startTriggeringExpr) {
                        startTriggering(unrolledExprs.back().first);
                    }
                    iterRef.getIterator().attachValue(newValImpl);
                }
                valueExprMap.emplace(getValueHash(newValImpl),
                                     unrolledExprs.size() - 1);
            },
            newValue);
        return std::pair<size_t, ReturnType&>(unrolledExprs.size() - 1,
                                              unrolledExprs.back().first);
    }

    inline std::pair<size_t, ReturnType> roll(const Value& val) {
        u_int64_t hash = getValueHash(val);
        assert(valueExprMap.count(hash));
        size_t index = valueExprMap[hash];
        std::pair<size_t, ReturnType> removedExpr =
            std::make_pair(index, std::move(unrolledExprs[index].first));
        unrolledExprs[index] = std::move(unrolledExprs.back());
        unrolledExprs.pop_back();
        valueExprMap.erase(hash);
        if (unrolledExprs.size() > 0) {
            valueExprMap[getValueHash(unrolledExprs[index].second)] = index;
        }
        return removedExpr;
    }

    Quantifier<ContainerType, ContainerValueType, ReturnType, ReturnValueType>
    deepCopyQuantifierForUnroll(const IterValue& iterator) const {
        Quantifier<ContainerType, ContainerValueType, ReturnType,
                   ReturnValueType>
            newQuantifier(deepCopyForUnroll(container, iterator));

        const IterRef<ContainerValueType>* containerPtr =
            mpark::get_if<IterRef<ContainerValueType>>(
                &(newQuantifier.container));
        const IterRef<ContainerValueType>* iteratorPtr =
            mpark::get_if<IterRef<ContainerValueType>>(&iterator);
        if (containerPtr != NULL && iteratorPtr != NULL &&
            containerPtr->getIterator().id == iteratorPtr->getIterator().id) {
            // this is a new container we are now pointing too
            // no need to populate it with copies of the old unrolled exprs
            return newQuantifier;
        }
        newQuantifier.expr = expr;
        for (auto& expr : unrolledExprs) {
            newQuantifier.unrolledExprs.emplace_back(
                deepCopyForUnroll(expr.first, iterator), expr.second);
        }
        return newQuantifier;
    }
};

struct SetTrigger;
template <typename DerivingQuantifierType, typename ContainerType,
          typename ContainerValueType, typename ExprTrigger>
struct BoolQuantifier : public Quantifier<ContainerType, ContainerValueType,
                                          BoolReturning, BoolValue> {
    using QuantBase =
        Quantifier<ContainerType, ContainerValueType, BoolReturning, BoolValue>;
    using typename QuantBase::Quantifier;
    using QuantBase::unrolledExprs;
    using QuantBase::container;
    FastIterableIntSet violatingOperands = FastIterableIntSet(0, 0);
    std::vector<std::shared_ptr<ExprTrigger>> exprTriggers;

    BoolQuantifier(QuantBase quant, const FastIterableIntSet& violatingOperands)
        : Quantifier(std::move(quant)), violatingOperands(violatingOperands) {}

    inline void attachTriggerToExpr(size_t index) {
        auto trigger = std::make_shared<ExprTrigger>(
            static_cast<DerivingQuantifierType*>(this), index);
        addTrigger<BoolTrigger>(
            getView<BoolView>(unrolledExprs[index].first).triggers, trigger);
        exprTriggers.emplace_back(trigger);
        mpark::visit(overloaded(
                         [&](IterRef<BoolValue>& ref) {
                             addTrigger<IterAssignedTrigger<BoolValue>>(
                                 ref.getIterator().unrollTriggers, trigger);
                         },
                         [](auto&) {}),
                     unrolledExprs[index].first);
    }

    inline std::pair<size_t, BoolReturning&> unroll(
        const Value& newValue, const bool startTriggeringExpr = true,
        const bool evaluateFreshExpr = false) {
        auto indexExprPair =
            QuantBase::unroll(newValue, startTriggeringExpr, evaluateFreshExpr);
        if (getView<BoolView>(indexExprPair.second).violation > 0) {
            violatingOperands.insert(indexExprPair.first);
        }
        attachTriggerToExpr(unrolledExprs.size() - 1);
        return indexExprPair;
    }

    inline std::pair<size_t, BoolReturning> roll(const Value& val) {
        auto indexExprPair = QuantBase::roll(val);
        u_int64_t removedViolation =
            getView<BoolView>(indexExprPair.second).violation;
        if (removedViolation > 0) {
            violatingOperands.erase(indexExprPair.first);
        }
        if (violatingOperands.erase(unrolledExprs.size())) {
            violatingOperands.insert(indexExprPair.first);
        }
        deleteTrigger(exprTriggers[indexExprPair.first]);
        exprTriggers[indexExprPair.first] = std::move(exprTriggers.back());
        exprTriggers.pop_back();
        exprTriggers[indexExprPair.first]->index = indexExprPair.first;
        return indexExprPair;
    }

    void startTriggeringBase() {
        auto& op = static_cast<DerivingQuantifierType&>(*this);
        typedef typename decltype(
            op.containerTrigger)::element_type ContainerTrigger;
        op.containerTrigger = std::make_shared<ContainerTrigger>(&op);
        addTrigger<SetTrigger>(getView<SetView>(op.container).triggers,
                               op.containerTrigger);
        startTriggering(container);
        mpark::visit(
            overloaded(
                [&](IterRef<SetValue>& ref) {
                    addTrigger<IterAssignedTrigger<ContainerValueType>>(
                        ref.getIterator().unrollTriggers, op.containerTrigger);
                },
                [](auto&) {}),
            op.container);
        for (size_t i = 0; i < unrolledExprs.size(); ++i) {
            attachTriggerToExpr(i);
            startTriggering(unrolledExprs[i].first);
        }
    }

    void stopTriggeringBase() {
        auto& op = static_cast<DerivingQuantifierType&>(*this);
        if (op.containerTrigger) {
            deleteTrigger(op.containerTrigger);
            op.containerTrigger = nullptr;
        }
        stopTriggering(container);
        while (!exprTriggers.empty()) {
            deleteTrigger(exprTriggers.back());
            exprTriggers.pop_back();
        }
        for (auto& expr : op.unrolledExprs) {
            stopTriggering(expr.first);
        }
    }
};
#endif /* SRC_OPERATORS_QUANTIFIER_H_ */