#ifndef SRC_TYPES_BASE_H_
#define SRC_TYPES_BASE_H_
#include <cassert>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>
#include "utils/variantOperations.h"
#define buildForAllTypes(f, sep) f(Bool) sep f(Int) sep f(Set) sep f(MSet)

#define MACRO_COMMA ,
// Want a shared ptr like class that is not convertable to shared ptr
template <typename T>
class StandardSharedPtr {
   public:
    typedef T element_type;

   private:
    std::shared_ptr<T> ref;

   public:
    StandardSharedPtr(std::shared_ptr<T> ref) : ref(std::move(ref)) {}
    inline explicit operator bool() const noexcept {
        return ref.operator bool();
    }
    inline T& operator*() const { return ref.operator*(); }
    inline T* operator->() const noexcept { return ref.operator->(); }
};
template <typename T>
std::shared_ptr<T> makeShared();
template <typename T>
using BaseType =
    typename std::remove_cv<typename std::remove_reference<T>::type>::type;
// forward declare structs
#define declDomainsAndValues(name) \
    struct name##Value;            \
    struct name##View;             \
    struct name##Domain;
buildForAllTypes(declDomainsAndValues, )
#undef declDomainsAndValues

    // associations between values, domains and views

    template <typename T>
    struct AssociatedDomain;
template <typename T>
struct AssociatedValueType;

template <typename T>
struct AssociatedViewType;

template <typename T>
struct IsDomainPtrType : public std::false_type {};
template <typename T>
struct IsDomainType : public std::false_type {};
template <typename T>
struct IsValueType : public std::false_type {};
template <typename T>
struct IsViewType : public std::false_type {};

template <typename T>
struct TypeAsString;
#define makeAssociations(name)                                           \
    void matchInnerType(const name##Domain& domain, name##Value& value); \
    void matchInnerType(const name##Value& other, name##Value& value);   \
    void reset(name##Value& value);                                      \
    template <>                                                          \
    struct AssociatedDomain<name##Value> {                               \
        typedef name##Domain type;                                       \
    };                                                                   \
                                                                         \
    template <>                                                          \
    struct AssociatedValueType<name##Domain> {                           \
        typedef name##Value type;                                        \
    };                                                                   \
    template <>                                                          \
    struct AssociatedViewType<name##Value> {                             \
        typedef name##View type;                                         \
    };                                                                   \
    template <>                                                          \
    struct AssociatedValueType<name##View> {                             \
        typedef name##Value type;                                        \
    };                                                                   \
    template <>                                                          \
    struct TypeAsString<name##Value> {                                   \
        static const std::string value;                                  \
    };                                                                   \
    template <>                                                          \
    struct TypeAsString<name##Domain> {                                  \
        static const std::string value;                                  \
    };                                                                   \
                                                                         \
    template <>                                                          \
    struct IsDomainPtrType<std::shared_ptr<name##Domain>>                \
        : public std::true_type {};                                      \
                                                                         \
    template <>                                                          \
    struct IsDomainType<name##Domain> : public std::true_type {};        \
    template <>                                                          \
    struct IsValueType<name##Value> : public std::true_type {};          \
    template <>                                                          \
    struct IsViewType<name##View> : public std::true_type {};

buildForAllTypes(makeAssociations, );
#undef makeAssociations

template <typename T, typename U>
using EnableIfValueAndReturn =
    typename std::enable_if<IsValueType<BaseType<T>>::value, U>::type;
template <typename T>
using EnableIfValue = EnableIfValueAndReturn<T, int>;

template <typename T, typename U>
using EnableIfViewAndReturn =
    typename std::enable_if<IsViewType<BaseType<T>>::value, U>::type;
template <typename T>
using EnableIfView = EnableIfViewAndReturn<T, int>;

template <typename T>
struct ValRef : public StandardSharedPtr<T> {
    using StandardSharedPtr<T>::StandardSharedPtr;
};

template <typename T>
struct ViewRef : public StandardSharedPtr<T> {
    using StandardSharedPtr<T>::StandardSharedPtr;
};

template <typename T,
          typename std::enable_if<IsValueType<T>::value, int>::type = 0>
ValRef<T> make() {
    return ValRef<T>(makeShared<T>());
}

// short cut for building a variant of any other templated class, where the
// class is templated over a value (SetValue,IntValue, etc.)
#define variantValues(V) T<V##Value>
template <template <typename> class T>
using Variantised =
    mpark::variant<buildForAllTypes(variantValues, MACRO_COMMA)>;
#undef variantValues

// variant for values
typedef Variantised<ValRef> AnyValRef;
// variant for vector of values
template <typename InnerValueType>
using ValRefVec = std::vector<ValRef<InnerValueType>>;
typedef Variantised<ValRefVec> AnyValVec;

template <typename T>
struct ValType;

template <typename T>
struct ValType<ValRef<T>> {
    typedef T type;
};

template <typename T>
struct ValType<std::vector<ValRef<T>>> {
    typedef T type;
};

#define valType(t) typename ValType<BaseType<decltype(t)>>::type

// variant for views
template <typename T>
using ViewRefMaker = ViewRef<typename AssociatedViewType<T>::type>;
typedef Variantised<ViewRefMaker> AnyViewRef;
// variant for vector of views
template <typename InnerViewType>
using ViewRefVec = std::vector<ViewRef<InnerViewType>>;
template <typename T>
using ViewRefVecMaker = ViewRefVec<typename AssociatedViewType<T>::type>;
typedef Variantised<ViewRefVecMaker> AnyViewVec;

template <typename T>
struct ViewType;

template <typename T>
struct ViewType<ViewRef<T>> {
    typedef T type;
};

template <typename T>
struct ViewType<std::vector<ViewRef<T>>> {
    typedef T type;
};

#define viewType(t) typename ViewType<BaseType<decltype(t)>>::type

// variant for domains
#define variantDomains(T) std::shared_ptr<T##Domain>
using AnyDomainRef =
    mpark::variant<buildForAllTypes(variantDomains, MACRO_COMMA)>;
#undef variantDomains

struct ValBase;
extern ValBase constantPool;

struct ValBase {
    u_int64_t id = 0;
    ValBase* container = &constantPool;
};

template <typename Val>
ValBase& valBase(Val& val);
template <typename Val>
const ValBase& valBase(const Val& val);

template <typename T = int>
ValBase& valBase(const AnyValRef& ref, T = 0) {
    return mpark::visit([](auto& val) { return valBase(val); }, ref);
}

template <typename DomainType>
ValRef<typename AssociatedValueType<DomainType>::type> constructValueFromDomain(
    const DomainType& domain) {
    auto val = make<typename AssociatedValueType<DomainType>::type>();
    matchInnerType(domain, *val);
    return val;
}
template <typename ValueType>
ValRef<ValueType> constructValueOfSameType(const ValueType& other) {
    auto val = make<ValueType>();
    matchInnerType(other, *val);
    return val;
}

template <typename ViewType,
          typename ValueType = typename AssociatedValueType<ViewType>::type,
          typename std::enable_if<IsViewType<ViewType>::value, int>::type = 0>
inline ValRef<ValueType>& assumeAsValue(ViewRef<ViewType>& viewPtr) {
    return reinterpret_cast<ValRef<ValueType>&>(viewPtr);
}

template <typename ViewType,
          typename ValueType = typename AssociatedValueType<ViewType>::type,
          typename std::enable_if<IsViewType<ViewType>::value, int>::type = 0>
inline const ValRef<ValueType>& assumeAsValue(
    const ViewRef<ViewType>& viewPtr) {
    return reinterpret_cast<const ValRef<ValueType>&>(viewPtr);
}

template <typename ViewType,
          typename ValueType = typename AssociatedValueType<ViewType>::type,
          typename std::enable_if<IsViewType<ViewType>::value, int>::type = 0>
inline ValRef<ValueType>&& assumeAsValue(ViewRef<ViewType>&& viewPtr) {
    return reinterpret_cast<ValRef<ValueType>&&>(std::move(viewPtr));
}

template <typename ValueType,
          typename ViewType = typename AssociatedViewType<ValueType>::type,
          typename std::enable_if<IsValueType<ValueType>::value, int>::type = 0>
inline ViewRef<ViewType>& getViewPtr(ValRef<ValueType>& value) {
    return reinterpret_cast<ViewRef<ViewType>&>(value);
}

template <typename ValueType,
          typename ViewType = typename AssociatedViewType<ValueType>::type,
          typename std::enable_if<IsValueType<ValueType>::value, int>::type = 0>
inline const ViewRef<ViewType>& getViewPtr(const ValRef<ValueType>& value) {
    return reinterpret_cast<const ViewRef<ViewType>&>(value);
}

template <typename ValueType,
          typename ViewType = typename AssociatedViewType<ValueType>::type,
          typename std::enable_if<IsValueType<ValueType>::value, int>::type = 0>
inline ViewRef<ViewType>&& getViewPtr(ValRef<ValueType>&& value) {
    return reinterpret_cast<ViewRef<ViewType>&&>(std::move(value));
}

struct TriggerBase {
    bool active;
};
struct DelayedTrigger : public virtual TriggerBase {
    virtual void trigger() = 0;
};
template <typename UnrollingValue>
struct IterAssignedTrigger : public virtual TriggerBase {
    typedef UnrollingValue ValueType;
    virtual void iterHasNewValue(const UnrollingValue& oldValue,
                                 const ValRef<UnrollingValue>& newValue) = 0;
};

template <typename ValueType>
struct DefinedTrigger;

extern std::vector<std::shared_ptr<DelayedTrigger>> delayedTriggerStack;
extern bool currentlyProcessingDelayedTriggerStack;

template <typename Trigger>
void cleanNullTriggers(std::vector<std::shared_ptr<Trigger>>& triggers) {
    for (size_t i = 0; i < triggers.size(); ++i) {
        if (triggers[i]) {
            continue;
        }
        while (!triggers.empty() && !triggers.back()) {
            triggers.pop_back();
        }
        if (i >= triggers.size()) {
            break;
        } else {
            triggers[i] = std::move(triggers.back());
            triggers.pop_back();
        }
    }
}

template <typename Visitor, typename Trigger>
void visitTriggers(Visitor&& func,
                   std::vector<std::shared_ptr<Trigger>>& triggers) {
    size_t triggerNullCount = 0;
    for (auto& trigger : triggers) {
        if (trigger && trigger->active) {
            func(trigger);
        } else {
            ++triggerNullCount;
            if (trigger && !trigger->active) {
                trigger = nullptr;
            }
        }
    }
    if (((double)triggerNullCount) / triggers.size() > 0.2) {
        cleanNullTriggers(triggers);
    }
    if (!currentlyProcessingDelayedTriggerStack) {
        currentlyProcessingDelayedTriggerStack = true;
        while (!delayedTriggerStack.empty()) {
            auto trigger = std::move(delayedTriggerStack.back());
            delayedTriggerStack.pop_back();
            if (trigger && trigger->active) {
                trigger->trigger();
            }
        }
        currentlyProcessingDelayedTriggerStack = false;
    }
}

template <typename View, typename Operator>
View& getView(const Operator& op);
template <typename T>
class IterRef;
template <typename>
struct QuantifierView;

template <typename ExprType, typename Trigger>
inline void saveTriggerOverload(
    std::shared_ptr<QuantifierView<ExprType>>& quant,
    const std::shared_ptr<Trigger>& trigger) {
    quant->triggers.emplace_back(trigger);
}
template <typename Op, typename Trigger>
inline void saveTriggerOverload(Op& op,
                                const std::shared_ptr<Trigger>& trigger) {
    getView(op).triggers.emplace_back(trigger);

    mpark::visit(overloaded(
                     [&](IterRef<typename Trigger::ValueType>& ref) {
                         ref.getIterator().unrollTriggers.emplace_back(trigger);
                     },
                     [](auto&) {}),
                 op);
}

template <typename Op, typename Trigger>
void addTrigger(Op& op, const std::shared_ptr<Trigger>& trigger) {
    saveTriggerOverload(op, trigger);
    trigger->active = true;
}

template <typename Trigger>
void addDelayedTrigger(const std::shared_ptr<Trigger>& trigger) {
    delayedTriggerStack.emplace_back(trigger);
    delayedTriggerStack.back()->active = true;
}

template <typename Trigger>
void deleteTrigger(const std::shared_ptr<Trigger>& trigger) {
    trigger->active = false;
}

template <typename Op, typename Trigger>
void setTriggerParentImpl(Op* op, std::shared_ptr<Trigger>& trigger) {
    if (trigger) {
        trigger->op = op;
    }
}
template <typename Op, typename Trigger>
void setTriggerParentImpl(Op* op,
                          std::vector<std::shared_ptr<Trigger>>& triggers) {
    for (auto& trigger : triggers) {
        trigger->op = op;
    }
}

template <typename Op, typename... Triggers>
void setTriggerParent(Op* op, Triggers&... triggers) {
    int unpack[] = {0, (setTriggerParentImpl(op, triggers), 0)...};
    static_cast<void>(unpack);
}
#endif
