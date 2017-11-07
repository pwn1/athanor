#ifndef SRC_OPERATORS_BOOLRETURNING_H_
#define SRC_OPERATORS_BOOLRETURNING_H_
#include "operators/operatorBase.h"
#include "search/violationDescription.h"
#include "types/bool.h"
#include "types/forwardDecls/typesAndDomains.h"
#include "types/typeTrates.h"
#define buildForBoolProducers(f, sep) \
    f(BoolValue) sep f(OpAnd) sep f(OpSetNotEq)

#define structDecls(name) struct name;
buildForBoolProducers(structDecls, );
#undef structDecls

using BoolReturning =
    mpark::variant<ValRef<BoolValue>, QuantRef<BoolValue>,
                   std::shared_ptr<OpAnd>, std::shared_ptr<OpSetNotEq>>;

#define boolProducerFuncs(name)                                                \
    void evaluate(name&);                                                      \
    void startTriggering(name&);                                               \
    void stopTriggering(name&);                                                \
    void updateViolationDescription(const name& op, u_int64_t parentViolation, \
                                    ViolationDescription&);                    \
    std::shared_ptr<name> deepCopyForUnroll(                                   \
        const name&, const QuantValue& unrollingQuantifier);
buildForBoolProducers(boolProducerFuncs, )
#undef boolProducerFuncs

    inline BoolView& getBoolView(const BoolReturning& boolV) {
    return mpark::visit(
        [&](auto& boolImpl) -> BoolView& {
            return reinterpret_cast<BoolView&>(*boolImpl);
        },
        boolV);
}

inline void evaluate(BoolReturning& boolV) {
    mpark::visit([&](auto& boolImpl) { evaluate(*boolImpl); }, boolV);
}

inline void startTriggering(BoolReturning& boolV) {
    mpark::visit([&](auto& boolImpl) { startTriggering(*boolImpl); }, boolV);
}

inline void stopTriggering(BoolReturning& boolV) {
    mpark::visit([&](auto& boolImpl) { stopTriggering(*boolImpl); }, boolV);
}

inline void updateViolationDescription(
    const BoolReturning& boolv, u_int64_t parentViolation,
    ViolationDescription& violationDescription) {
    mpark::visit(
        [&](auto& boolImpl) {
            updateViolationDescription(*boolImpl, parentViolation,
                                       violationDescription);
        },
        boolv);
}

#endif /* SRC_OPERATORS_BOOLRETURNING_H_ */
