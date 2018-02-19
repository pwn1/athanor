
/*contains definitions for functions that use the same code for every type,
 * using macros to do the specialisation to avoid having to include this file,
 * which includes all the types */
#include "operators/operatorBase.h"
#include "search/violationDescription.h"
#include "types/allTypes.h"
#define quote(x) #x

ValBase constantPool;
bool currentlyProcessingDelayedTriggerStack = false;
std::vector<std::shared_ptr<DelayedTrigger>> delayedTriggerStack;

inline ViolationDescription& registerViolations(const ValBase* val,
                                                const u_int64_t violation,
                                                ViolationDescription& vioDesc);
#define specialised(name)                                                      \
    template <>                                                                \
    std::shared_ptr<name##Value> makeShared<name##Value>() {                   \
        return std::make_shared<name##Value>();                                \
    }                                                                          \
    const std::string TypeAsString<name##Value>::value = quote(name##Value);   \
    const std::string TypeAsString<name##Domain>::value = quote(name##Domain); \
    const std::string TypeAsString<name##Returning>::value = quote(name##Returning); \
    template <>                                                                \
    ValBase& valBase<name##Value>(name##Value & v) {                           \
        return v;                                                              \
    }                                                                          \
    template <>                                                                \
    const ValBase& valBase<name##Value>(const name##Value& v) {                \
        return v;                                                              \
    }                                                                          \
    void updateViolationDescription(const name##Value& val,                    \
                                    u_int64_t parentViolation,                 \
                                    ViolationDescription& vioDesc) {           \
        registerViolations(&val, parentViolation, vioDesc);                    \
    }                                                                          \
                                                                               \
    std::ostream& dumpState(std::ostream& os, const name##Value& val) {        \
        return prettyPrint(os, val);                                           \
    }

buildForAllTypes(specialised, )
#undef specialised

    std::vector<std::shared_ptr<DelayedTrigger>> emptyEndOfTriggerQueue;

inline ViolationDescription& registerViolations(const ValBase* val,
                                                const u_int64_t violation,
                                                ViolationDescription& vioDesc) {
    if (val->container == &constantPool) {
        return vioDesc;
    }
    if (val->container == NULL) {
        vioDesc.addViolation(val->id, violation);
        return vioDesc;
    } else {
        ViolationDescription& parentVioDesc =
            registerViolations(val->container, violation, vioDesc);
        ViolationDescription& childVioDesc =
            parentVioDesc.childViolations(val->container->id);
        childVioDesc.addViolation(val->id, violation);
        return childVioDesc;
    }
}
