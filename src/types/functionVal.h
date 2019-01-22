#ifndef SRC_TYPES_FUNCTIONVAL_H_
#define SRC_TYPES_FUNCTIONVAL_H_
#include <vector>
#include "base/base.h"
#include "common/common.h"
#include "types/function.h"
#include "types/sizeAttr.h"
#include "utils/hashUtils.h"
#include "utils/ignoreUnused.h"
#include "utils/simpleCache.h"
enum class JectivityAttr { NONE, INJECTIVE, SURJECTIVE, BIJECTIVE };
enum class PartialAttr { PARTIAL, TOTAL };
struct FunctionDomain {
    JectivityAttr jectivity;
    PartialAttr partial;
    AnyDomainRef from;
    AnyDomainRef to;

    template <typename FromDomainType, typename ToDomainType>
    FunctionDomain(JectivityAttr jectivity, PartialAttr partial,
                   FromDomainType&& from, ToDomainType&& to)
        : jectivity(jectivity),
          partial(partial),
          from(makeAnyDomainRef(std::forward<FromDomainType>(from))),
          to(makeAnyDomainRef(std::forward<ToDomainType>(to))) {
        checkSupported();
    }

   private:
    void checkSupported() {
        if (jectivity != JectivityAttr::NONE) {
            std::cerr
                << "Sorry, jectivity for functions must currently be None.\n";
            abort();
        }
        if (partial != PartialAttr::TOTAL) {
            std::cerr << "Error, functions must currently be total.\n";
            abort();
        }
    }
};

struct FunctionValue : public FunctionView, public ValBase {
    template <typename InnerValueType, EnableIfValue<InnerValueType> = 0>
    inline ValRef<InnerValueType> member(UInt index) {
        return assumeAsValue(
            getRange<
                typename AssociatedViewType<InnerValueType>::type>()[index]);
    }

    template <typename InnerValueType, EnableIfValue<InnerValueType> = 0>
    inline void assignImage(UInt index, const ValRef<InnerValueType>& member) {
        FunctionView::assignImage(index, member.asExpr());
        valBase(*member).container = this;
        valBase(*member).id = index;
    }

    template <typename InnerViewType, EnableIfView<InnerViewType> = 0>
    void setInnerType() {
        if (mpark::get_if<ExprRefVec<InnerViewType>>(&(range)) == NULL) {
            range.emplace<ExprRefVec<InnerViewType>>();
        }
    }

    template <typename InnerValueType, typename Func,
              EnableIfValue<InnerValueType> = 0>
    inline bool trySwapImages(UInt index1, UInt index2, Func&& func) {
        typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
        FunctionView::swapImages<InnerViewType>(index1, index2);
        if (func()) {
            auto& range = getRange<InnerViewType>();
            valBase(*assumeAsValue(range[index1])).id = index1;
            valBase(*assumeAsValue(range[index2])).id = index2;
            FunctionView::notifyImagesSwapped(index1, index2);
            debug_code(assertValidVarBases());
            return true;
        } else {
            FunctionView::swapImages<InnerViewType>(index1, index2);
            return false;
        }
    }

    template <typename InnerValueType, typename Func,
              EnableIfValue<InnerValueType> = 0>
    inline bool tryImageChange(UInt index, Func&& func) {
        typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
        FunctionView::imageChanged<InnerViewType>(index);
        if (func()) {
            FunctionView::notifyImageChanged(index);
            return true;
        } else {
            FunctionView::imageChanged<InnerViewType>(index);
            return false;
        }
    }
    template <typename InnerValueType, typename Func,
              EnableIfValue<InnerValueType> = 0>
    inline bool tryImagesChange(const std::vector<UInt>& indices, Func&& func) {
        typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
        FunctionView::imagesChanged<InnerViewType>(indices);
        if (func()) {
            FunctionView::notifyImagesChanged(indices);
            return true;
        } else {
            FunctionView::imagesChanged<InnerViewType>(indices);
            return false;
        }
    }
    template <typename Func>
    bool tryAssignNewValue(FunctionValue& newvalue, Func&& func) {
        // fake putting in the value first untill func()verifies that it is
        // happy with the change
        std::swap(*this, newvalue);
        bool allowed = func();
        std::swap(*this, newvalue);
        if (allowed) {
            deepCopy(newvalue, *this);
        }
        return allowed;
    }

    void printVarBases();
    void evaluateImpl() final;
    void startTriggeringImpl() final;
    void stopTriggering() final;
    void updateVarViolationsImpl(const ViolationContext& vioContext,
                                 ViolationContainer&) final;
    ExprRef<FunctionView> deepCopyForUnrollImpl(
        const ExprRef<FunctionView>&, const AnyIterRef& iterator) const final;

    std::ostream& dumpState(std::ostream& os) const final;
    void findAndReplaceSelf(const FindAndReplaceFunction&) final;
    bool isUndefined();
    std::pair<bool, ExprRef<FunctionView>> optimise(PathExtension) final;
    void debugSanityCheckImpl() const final;
    std::string getOpName() const final;

    void assertValidVarBases();
};

#endif /* SRC_TYPES_FUNCTIONVAL_H_ */