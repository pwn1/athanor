#include "types/function.h"
#include <algorithm>
#include <cassert>
#include <tuple>
#include <utility>
#include "common/common.h"
#include "types/bool.h"
#include "types/int.h"
#include "types/tuple.h"
#include "utils/ignoreUnused.h"
using namespace std;
struct NoSupportException {};
Dimension intDomainToDimension(IntDomain& dom) {
    if (dom.bounds.size() > 1) {
        cerr << "Error: this function does not support int domains with holes "
                "in it.\n";
        throw NoSupportException();
    }
    return Dimension(dom.bounds.front().first, dom.bounds.front().second);
}
DimensionVec FunctionView::makeDimensionVecFromDomain(
    const AnyDomainRef& domain) {
    const shared_ptr<IntDomain>* intDomainTest;
    const shared_ptr<TupleDomain>* tupleDomainTest;
    try {
        if ((intDomainTest = mpark::get_if<shared_ptr<IntDomain>>(&domain)) !=
            NULL) {
            return {intDomainToDimension(**intDomainTest)};
        } else if ((tupleDomainTest = mpark::get_if<shared_ptr<TupleDomain>>(
                        &domain)) != NULL) {
            DimensionVec dimVec;
            for (auto& innerDomain : (*tupleDomainTest)->inners) {
                auto innerIntDomain =
                    mpark::get<shared_ptr<IntDomain>>(innerDomain);
                dimVec.emplace_back(intDomainToDimension(*innerIntDomain));
            }
            return dimVec;
        } else {
            throw NoSupportException();
        }
    } catch (...) {
        cerr << "Currently no support for building DimensionVecs from function "
                "domain: ";
        prettyPrint(cerr, domain) << endl;
        abort();
    }
}

template <typename Op>
struct OpMaker;

struct OpTupleLit;

template <>
struct OpMaker<OpTupleLit> {
    static ExprRef<TupleView> make(std::vector<AnyExprRef> members);
};

lib::optional<UInt> translateValueFromDimension(Int value,
                                                const Dimension& dimension) {
    if (value >= dimension.lower && value <= dimension.upper) {
        return value - dimension.lower;
    }
    return lib::nullopt;
}

lib::optional<Int> getAsIntForFunctionIndex(const AnyExprRef& expr) {
    const ExprRef<IntView>* intTest = mpark::get_if<ExprRef<IntView>>(&expr);
    if (intTest) {
        auto view = (*intTest)->getViewIfDefined();
        if (view) {
            return (*view).value;
        } else {
            return lib::nullopt;
        }
    }

    const ExprRef<BoolView>* boolTest = mpark::get_if<ExprRef<BoolView>>(&expr);
    if (boolTest) {
        auto view = (*boolTest)->getViewIfDefined();
        if (view) {
            return (*view).violation == 0;
        } else {
            return lib::nullopt;
        }
    }
    cerr << "Error: sorry only handling function from int, bool or tuples of "
            "int/bool\n";
    abort();
}

lib::optional<UInt> FunctionView::domainToIndex(const IntView& intV) {
    debug_code(assert(dimensions.size() == 1));
    return translateValueFromDimension(intV.value, dimensions[0]);
}

lib::optional<UInt> FunctionView::domainToIndex(const TupleView& tupleV) {
    debug_code(assert(dimensions.size() == tupleV.members.size()));
    size_t indexTotal = 0;
    for (size_t i = 0; i < tupleV.members.size(); i++) {
        auto asInt = getAsIntForFunctionIndex(tupleV.members[i]);
        if (!asInt) {
            return lib::nullopt;
        }
        auto index = translateValueFromDimension(*asInt, dimensions[i]);
        if (!index) {
            return lib::nullopt;
        }
        indexTotal += dimensions[i].blockSize * (*index);
    }
    debug_code(assert(indexTotal < rangeSize()));
    return indexTotal;
}

template <>
ExprRef<IntView> functionIndexToDomain<IntView>(const DimensionVec& dimensions,
                                                UInt index) {
    debug_code(assert(dimensions.size() == 1));
    auto val = make<IntValue>();
    val->value = dimensions[0].lower + index;
    return val.asExpr();
}

template <>
void functionIndexToDomain<IntView>(const DimensionVec& dimensions, UInt index,
                                    IntView& view) {
    debug_code(assert(dimensions.size() == 1));
    view.changeValue([&]() {
        view.value = dimensions[0].lower + index;
        return true;
    });
}

template <>
ExprRef<TupleView> functionIndexToDomain<TupleView>(
    const DimensionVec& dimensions, UInt index) {
    vector<AnyExprRef> tupleMembers;
    for (auto& dim : dimensions) {
        auto intVal = make<IntValue>();
        UInt row = index / dim.blockSize;
        index %= dim.blockSize;
        intVal->value = row + dim.lower;
        tupleMembers.emplace_back(intVal.asExpr());
    }
    return OpMaker<OpTupleLit>::make(move(tupleMembers));
}

template <>
void functionIndexToDomain<TupleView>(const DimensionVec& dimensions,
                                      UInt index, TupleView& view) {
    for (size_t dimIndex = 0; dimIndex < dimensions.size(); dimIndex++) {
        auto& dim = dimensions[dimIndex];
        auto& memberExpr = mpark::get<ExprRef<IntView>>(view.members[dimIndex]);
        auto& memberView = memberExpr->view().get();
        UInt row = index / dim.blockSize;
        index %= dim.blockSize;
        memberView.changeValue([&]() {
            memberView.value = row + dim.lower;
            return true;
        });
    }
}

template <>
HashType getValueHash<FunctionView>(const FunctionView& val) {
    todoImpl(val);
}

template <>
ostream& prettyPrint<FunctionView>(ostream& os, const FunctionView& v) {
    os << "function(";
    return os << ")";
    mpark::visit(
        [&](auto& rangeImpl) {
            for (size_t i = 0; i < rangeImpl.size(); i++) {
                if (i > 0) {
                    os << ",\n";
                }
                if (v.dimensions.size() == 1) {
                    auto from = v.indexToDomain<IntView>(i);
                    prettyPrint(os, from->view());
                } else {
                    auto from = v.indexToDomain<TupleView>(i);
                    prettyPrint(os, from->view());
                }
                os << " --> ";
                prettyPrint(os, rangeImpl[i]->view());
            }
        },
        v.range);
    os << ")";
    return os;
}

template <typename InnerViewType>
void deepCopyImpl(const FunctionValue&,
                  const ExprRefVec<InnerViewType>& srcMemnersImpl,
                  FunctionValue& target) {
    todoImpl(srcMemnersImpl, target);
}

template <>
void deepCopy<FunctionValue>(const FunctionValue& src, FunctionValue& target) {
    assert(src.range.index() == target.range.index());
    return visit(
        [&](auto& srcMembersImpl) {
            return deepCopyImpl(src, srcMembersImpl, target);
        },
        src.range);
}

template <>
ostream& prettyPrint<FunctionDomain>(ostream& os, const FunctionDomain& d) {
    os << "function(";
    prettyPrint(os, d.from) << " --> ";
    prettyPrint(os, d.to) << ")";
    return os;
}

void matchInnerType(const FunctionValue& src, FunctionValue& target) {
    mpark::visit(
        [&](auto& srcMembersImpl) {
            target.setInnerType<viewType(srcMembersImpl)>();
        },
        src.range);
}

void matchInnerType(const FunctionDomain& domain, FunctionValue& target) {
    mpark::visit(
        [&](auto& innerDomainImpl) {
            target.setInnerType<typename AssociatedViewType<
                typename AssociatedValueType<typename BaseType<decltype(
                    innerDomainImpl)>::element_type>::type>::type>();
        },
        domain.to);
}

template <>
UInt getDomainSize<FunctionDomain>(const FunctionDomain& domain) {
    todoImpl(domain);
}

void evaluateImpl(FunctionValue&) {}
void startTriggeringImpl(FunctionValue&) {}
void stopTriggering(FunctionValue&) {}

template <typename InnerViewType>
void normaliseImpl(FunctionValue&, ExprRefVec<InnerViewType>& valMembersImpl) {
    for (auto& v : valMembersImpl) {
        normalise(*assumeAsValue(v));
    }
}

template <>
void normalise<FunctionValue>(FunctionValue& val) {
    mpark::visit(
        [&](auto& valMembersImpl) { normaliseImpl(val, valMembersImpl); },
        val.range);
}

template <>
bool smallerValue<FunctionView>(const FunctionView& u, const FunctionView& v);
template <>
bool largerValue<FunctionView>(const FunctionView& u, const FunctionView& v);

template <>
bool smallerValue<FunctionView>(const FunctionView& u, const FunctionView& v) {
    return mpark::visit(
        [&](auto& uMembersImpl) {
            auto& vMembersImpl =
                mpark::get<BaseType<decltype(uMembersImpl)>>(v.range);
            if (uMembersImpl.size() < vMembersImpl.size()) {
                return true;
            } else if (uMembersImpl.size() > vMembersImpl.size()) {
                return false;
            }
            for (size_t i = 0; i < uMembersImpl.size(); ++i) {
                if (smallerValue(uMembersImpl[i]->view(),
                                 vMembersImpl[i]->view())) {
                    return true;
                } else if (largerValue(uMembersImpl[i]->view(),
                                       vMembersImpl[i]->view())) {
                    return false;
                }
            }
            return false;
        },
        u.range);
}

template <>
bool largerValue<FunctionView>(const FunctionView& u, const FunctionView& v) {
    return mpark::visit(
        [&](auto& uMembersImpl) {
            auto& vMembersImpl =
                mpark::get<BaseType<decltype(uMembersImpl)>>(v.range);
            if (uMembersImpl.size() > vMembersImpl.size()) {
                return true;
            } else if (uMembersImpl.size() < vMembersImpl.size()) {
                return false;
            }
            for (size_t i = 0; i < uMembersImpl.size(); ++i) {
                if (largerValue(uMembersImpl[i]->view(),
                                vMembersImpl[i]->view())) {
                    return true;
                } else if (smallerValue(uMembersImpl[i]->view(),
                                        vMembersImpl[i]->view())) {
                    return false;
                }
            }
            return false;
        },
        u.range);
}

void FunctionView::assertValidState() {
    // no state to validate currently
}

void FunctionValue::assertValidVarBases() {
    mpark::visit(
        [&](auto& valMembersImpl) {
            if (valMembersImpl.empty()) {
                return;
            }
            bool success = true;
            for (size_t i = 0; i < valMembersImpl.size(); i++) {
                const ValBase& base =
                    valBase(*assumeAsValue(valMembersImpl[i]));
                if (base.container != this) {
                    success = false;
                    cerr << "member " << i
                         << "'s container does not point to this function."
                         << endl;
                } else if (base.id != i) {
                    success = false;
                    cerr << "function member " << i << "has id " << base.id
                         << " but it should be " << i << endl;
                }
            }
            if (!success) {
                cerr << "Members: " << valMembersImpl << endl;
                this->printVarBases();
                assert(false);
            }
        },
        range);
}

void FunctionValue::printVarBases() {
    mpark::visit(
        [&](auto& valMembersImpl) {
            cout << "parent is constant: " << (this->container == &constantPool)
                 << endl;
            for (auto& member : valMembersImpl) {
                cout << "val id: " << valBase(*assumeAsValue(member)).id
                     << endl;
                cout << "is constant: "
                     << (valBase(*assumeAsValue(member)).container ==
                         &constantPool)
                     << endl;
            }
        },
        range);
}
