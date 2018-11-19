#include <cmath>
#include <random>
#include "neighbourhoods/neighbourhoods.h"
#include "search/statsContainer.h"
#include "types/sequence.h"
#include "utils/random.h"

using namespace std;
static ViolationContainer emptyViolations;

template <typename InnerDomainPtrType>
void assignRandomValueInDomainImpl(const SequenceDomain& domain,
                                   const InnerDomainPtrType& innerDomainPtr,
                                   SequenceValue& val, StatsContainer& stats) {
    typedef typename AssociatedValueType<
        typename InnerDomainPtrType::element_type>::type InnerValueType;
    size_t newNumberElements =
        globalRandom(domain.sizeAttr.minSize, domain.sizeAttr.maxSize);
    // clear sequence and populate with new random elements
    while (val.numberElements() > 0) {
        val.removeMember<InnerValueType>(val.numberElements() - 1);
        ++stats.minorNodeCount;
    }
    while (newNumberElements > val.numberElements()) {
        auto newMember = constructValueFromDomain(*innerDomainPtr);
        assignRandomValueInDomain(*innerDomainPtr, *newMember, stats);
        val.addMember(val.numberElements(), newMember);
        // add member may reject elements, not to worry, while loop will simply
        // continue
    }
}

template <>
void assignRandomValueInDomain<SequenceDomain>(const SequenceDomain& domain,
                                               SequenceValue& val,
                                               StatsContainer& stats) {
    mpark::visit(
        [&](auto& innerDomainPtr) {
            assignRandomValueInDomainImpl(domain, innerDomainPtr, val, stats);
        },
        domain.inner);
}

template <typename InnerDomainPtrType>
void sequenceLiftSingleGenImpl(const SequenceDomain& domain,
                               const InnerDomainPtrType&,
                               int numberValsRequired,
                               std::vector<Neighbourhood>& neighbourhoods) {
    std::vector<Neighbourhood> innerDomainNeighbourhoods;
    generateNeighbourhoods(1, domain.inner, innerDomainNeighbourhoods);
    typedef typename AssociatedValueType<
        typename InnerDomainPtrType::element_type>::type InnerValueType;
    for (auto& innerNh : innerDomainNeighbourhoods) {
        neighbourhoods.emplace_back(
            "sequenceLiftSingle_" + innerNh.name, numberValsRequired,
            [innerNhApply{std::move(innerNh.apply)}](
                NeighbourhoodParams& params) {
                auto& val = *(params.getVals<SequenceValue>().front());
                if (val.numberElements() == 0) {
                    ++params.stats.minorNodeCount;
                    return;
                }
                ViolationContainer& vioContainerAtThisLevel =
                    params.vioContainer.hasChildViolation(val.id)
                        ? params.vioContainer.childViolations(val.id)
                        : emptyViolations;
                UInt indexToChange = vioContainerAtThisLevel.selectRandomVar(
                    val.numberElements() - 1);
                HashType previousSubsequenceHash;
                std::vector<HashType> subsequenceHashes;
                previousSubsequenceHash =
                    val.notifyPossibleSubsequenceChange<InnerValueType>(
                        indexToChange, indexToChange + 1, subsequenceHashes);
                ParentCheckCallBack parentCheck =
                    [&](const AnyValVec& newValue) {
                        if (val.injective) {
                            if (val.containsMember(
                                    mpark::get<ValRefVec<InnerValueType>>(
                                        newValue)
                                        .front())) {
                                return false;
                            }
                        }
                        return val.trySubsequenceChange<InnerValueType>(
                            indexToChange, indexToChange + 1, subsequenceHashes,
                            previousSubsequenceHash,
                            [&]() { return params.parentCheck(params.vals); });
                    };
                bool requiresRevert = false;
                AcceptanceCallBack changeAccepted = [&]() {
                    requiresRevert = !params.changeAccepted();
                    if (requiresRevert) {
                        previousSubsequenceHash =
                            val.notifyPossibleSubsequenceChange<InnerValueType>(
                                indexToChange, indexToChange + 1,
                                subsequenceHashes);
                    }
                    return !requiresRevert;
                };
                AnyValVec changingMembers;
                auto& changingMembersImpl =
                    changingMembers.emplace<ValRefVec<InnerValueType>>();
                changingMembersImpl.emplace_back(
                    val.member<InnerValueType>(indexToChange));

                NeighbourhoodParams innerNhParams(
                    changeAccepted, parentCheck, 1, changingMembers,
                    params.stats, vioContainerAtThisLevel);
                innerNhApply(innerNhParams);
                if (requiresRevert) {
                    val.trySubsequenceChange<InnerValueType>(
                        indexToChange, indexToChange + 1, subsequenceHashes,
                        previousSubsequenceHash, [&]() { return true; });
                }
            });
    }
}

void sequenceLiftSingleGen(const SequenceDomain& domain, int numberValsRequired,
                           std::vector<Neighbourhood>& neighbourhoods) {
    mpark::visit(
        [&](const auto& innerDomainPtr) {
            sequenceLiftSingleGenImpl(domain, innerDomainPtr,
                                      numberValsRequired, neighbourhoods);
        },
        domain.inner);
}

template <typename InnerDomainPtrType>
void sequenceAddGenImpl(const SequenceDomain& domain,
                        InnerDomainPtrType& innerDomainPtr,
                        int numberValsRequired,
                        std::vector<Neighbourhood>& neighbourhoods) {
    typedef typename AssociatedValueType<
        typename InnerDomainPtrType::element_type>::type InnerValueType;

    neighbourhoods.emplace_back(
        "sequenceAdd", numberValsRequired,
        [&domain, &innerDomainPtr](NeighbourhoodParams& params) {
            auto& val = *(params.getVals<SequenceValue>().front());
            if (val.numberElements() == domain.sizeAttr.maxSize) {
                ++params.stats.minorNodeCount;
                return;
            }
            auto newMember = constructValueFromDomain(*innerDomainPtr);
            int numberTries = 0;
            const int tryLimit = params.parentCheckTryLimit;
            debug_neighbourhood_action("Looking for value to add");
            bool success;
            size_t indexOfNewMember;
            do {
                assignRandomValueInDomain(*innerDomainPtr, *newMember,
                                          params.stats);
                indexOfNewMember = globalRandom<UInt>(0, val.numberElements());
                success = val.tryAddMember(indexOfNewMember, newMember, [&]() {
                    return params.parentCheck(params.vals);
                });
            } while (!success && ++numberTries < tryLimit);
            if (!success) {
                debug_neighbourhood_action(
                    "Couldn't find value, number tries=" << tryLimit);
                return;
            }
            debug_neighbourhood_action("Added value: " << newMember);
            if (!params.changeAccepted()) {
                debug_neighbourhood_action("Change rejected");
                val.tryRemoveMember<InnerValueType>(indexOfNewMember,
                                                    []() { return true; });
            }
        });
}

void sequenceAddGen(const SequenceDomain& domain, int numberValsRequired,
                    std::vector<Neighbourhood>& neighbourhoods) {
    if (domain.sizeAttr.sizeType == SizeAttr::SizeAttrType::EXACT_SIZE) {
        return;
    }
    mpark::visit(
        [&](const auto& innerDomainPtr) {
            sequenceAddGenImpl(domain, innerDomainPtr, numberValsRequired,
                               neighbourhoods);
        },
        domain.inner);
}

template <typename InnerDomainPtrType>
void sequenceRemoveGenImpl(const SequenceDomain& domain, InnerDomainPtrType&,
                           int numberValsRequired,
                           std::vector<Neighbourhood>& neighbourhoods) {
    typedef typename AssociatedValueType<
        typename InnerDomainPtrType::element_type>::type InnerValueType;
    neighbourhoods.emplace_back(
        "sequenceRemove", numberValsRequired, [&](NeighbourhoodParams& params) {
            ++params.stats.minorNodeCount;
            auto& val = *(params.getVals<SequenceValue>().front());
            if (val.numberElements() == domain.sizeAttr.minSize) {
                ++params.stats.minorNodeCount;
                return;
            }
            size_t indexToRemove;
            int numberTries = 0;
            ValRef<InnerValueType> removedMember(nullptr);
            bool success;
            debug_neighbourhood_action("Looking for value to remove");

            do {
                ++params.stats.minorNodeCount;
                indexToRemove =
                    globalRandom<size_t>(0, val.numberElements() - 1);
                std::pair<bool, ValRef<InnerValueType>> removeStatus =
                    val.tryRemoveMember<InnerValueType>(indexToRemove, [&]() {
                        return params.parentCheck(params.vals);
                    });
                success = removeStatus.first;
                if (success) {
                    removedMember = std::move(removeStatus.second);
                    debug_neighbourhood_action("Removed " << removedMember);
                }
            } while (!success && ++numberTries < params.parentCheckTryLimit);
            if (!success) {
                debug_neighbourhood_action(
                    "Couldn't find value, number tries=" << numberTries);
                return;
            }
            if (!params.changeAccepted()) {
                debug_neighbourhood_action("Change rejected");
                val.tryAddMember(indexToRemove, std::move(removedMember),
                                 []() { return true; });
            }
        });
}

void sequenceRemoveGen(const SequenceDomain& domain, int numberValsRequired,
                       std::vector<Neighbourhood>& neighbourhoods) {
    if (domain.sizeAttr.sizeType == SizeAttr::SizeAttrType::EXACT_SIZE) {
        return;
    }
    mpark::visit(
        [&](const auto& innerDomainPtr) {
            sequenceRemoveGenImpl(domain, innerDomainPtr, numberValsRequired,
                                  neighbourhoods);
        },
        domain.inner);
}
template <typename InnerViewType>
static void insureSize(ExprRefVec<InnerViewType>& newValues,
                       size_t subseqSize) {
    typedef typename AssociatedValueType<InnerViewType>::type InnerValueType;
    if (subseqSize < newValues.size()) {
        newValues.erase(newValues.begin() + subseqSize, newValues.end());
    }
    while (newValues.size() < subseqSize) {
        newValues.emplace_back(make<InnerValueType>().asExpr());
    }
}

template <typename InnerDomainType, typename InnerValueType,
          typename InnerViewType>
void assignNewValues(InnerDomainType& innerDomain, InnerValueType& val,
                     vector<HashType>& oldHashes,
                     ExprRefVec<InnerViewType>& newValues,
                     StatsContainer& stats) {
    if (val.injective) {
        for (auto& hash : oldHashes) {
            val.memberHashes.erase(hash);
        }
    }
    vector<HashType> newHashes;
    for (auto& expr : newValues) {
        while (true) {
            assignRandomValueInDomain(innerDomain, *assumeAsValue(expr), stats);
            if (!val.injective) {
                break;
            }
            HashType hash = getValueHash(expr);
            if (!val.memberHashes.count(hash)) {
                val.memberHashes.insert(hash);
                newHashes.emplace_back(hash);
                break;
            }
        }
    }
    if (val.injective) {
        for (auto& hash : newHashes) {
            val.memberHashes.erase(hash);
        }
        for (auto& hash : oldHashes) {
            val.memberHashes.insert(hash);
        }
    }
}

template <typename InnerViewType>
void swapSub(ExprRefVec<InnerViewType>& members,
             ExprRefVec<InnerViewType>& newValues, UInt startIndex,
             UInt endIndex) {
    UInt subseqSize = endIndex - startIndex;
    for (size_t i = 0; i < subseqSize; i++) {
        swap(members[startIndex + i], newValues[i]);
    }
}

template <typename InnerViewType>
void swapAssignedValuesOfSub(ExprRefVec<InnerViewType>& members,
                             ExprRefVec<InnerViewType>& newValues,
                             UInt startIndex, UInt endIndex) {
    typedef typename AssociatedValueType<InnerViewType>::type InnerValueType;

    UInt subseqSize = endIndex - startIndex;
    for (size_t i = 0; i < subseqSize; i++) {
        swapValAssignments(*assumeAsValue(newValues[i]),
                           *assumeAsValue(members[startIndex + i]));
    }
}

template <typename InnerDomainPtrType>
void sequenceRelaxSubGenImpl(const SequenceDomain&,
                             InnerDomainPtrType& innerDomainPtr,
                             int numberValsRequired,
                             std::vector<Neighbourhood>& neighbourhoods) {
    typedef typename AssociatedValueType<
        typename InnerDomainPtrType::element_type>::type InnerValueType;
    typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
    neighbourhoods.emplace_back(
        "sequenceRelaxSub", numberValsRequired,
        [&innerDomainPtr](NeighbourhoodParams& params) {
            auto& val = *(params.getVals<SequenceValue>().front());
            if (val.numberElements() < 2) {
                ++params.stats.minorNodeCount;
                return;
            }
            int numberTries = 0;
            const int tryLimit = params.parentCheckTryLimit;
            debug_neighbourhood_action(
                "Looking for indices of subsequence to relax");

            bool success;
            UInt startIndex, endIndex;
            ExprRefVec<InnerViewType> newValues;
            vector<HashType> oldHashes;
            auto& members = val.getMembers<InnerViewType>();
            do {
                startIndex = globalRandom<UInt>(0, val.numberElements() - 1);
                endIndex =
                    globalRandom<UInt>(startIndex + 1, val.numberElements());
                oldHashes.clear();
                HashType previousSubseqHash =
                    val.notifyPossibleSubsequenceChange<InnerValueType>(
                        startIndex, endIndex, oldHashes);
                insureSize(newValues, endIndex - startIndex);

                assignNewValues(*innerDomainPtr, val, oldHashes, newValues,
                                params.stats);

                // temperarily swap values in, and if parent type accepts it,
                // swap it back and do a propper deepCopy
                swapSub(members, newValues, startIndex, endIndex);

                success = val.trySubsequenceChange<InnerValueType>(
                    startIndex, endIndex, oldHashes, previousSubseqHash, [&]() {
                        // swap it back
                        swapSub(members, newValues, startIndex, endIndex);
                        if (params.parentCheck(params.vals)) {
                            // swap values of the variables not the variables
                            // and trigger change
                            swapAssignedValuesOfSub(members, newValues,
                                                    startIndex, endIndex);
                            return true;
                        }
                        return false;
                    });
            } while (!success && ++numberTries < tryLimit);

            if (!success) {
                debug_neighbourhood_action(
                    "Couldn't find new values for relaxed subsequence, number "
                    "tries="
                    << tryLimit);
                return;
            }
            debug_neighbourhood_action(
                "subsequence relaxd: " << startIndex << " and " << endIndex);
            if (!params.changeAccepted()) {
                debug_neighbourhood_action("Change rejected");
                oldHashes.clear();
                HashType previousSubseqHash =
                    val.notifyPossibleSubsequenceChange<InnerValueType>(
                        startIndex, endIndex, oldHashes);
                swapAssignedValuesOfSub(members, newValues, startIndex,
                                        endIndex);
                val.trySubsequenceChange<InnerValueType>(
                    startIndex, endIndex, oldHashes, previousSubseqHash,
                    []() { return true; });
            }
        });
}

void sequenceRelaxSubGen(const SequenceDomain& domain, int numberValsRequired,
                         std::vector<Neighbourhood>& neighbourhoods) {
    mpark::visit(overloaded([&](const auto&) {},
                            [&](const shared_ptr<IntDomain>& innerDomainPtr) {
                                sequenceRelaxSubGenImpl(domain, innerDomainPtr,
                                                        numberValsRequired,
                                                        neighbourhoods);
                            }),
                 domain.inner);
}

template <typename InnerDomainPtrType>
void sequenceReverseSubGenImpl(const SequenceDomain&, InnerDomainPtrType&,
                               int numberValsRequired,
                               std::vector<Neighbourhood>& neighbourhoods) {
    typedef typename AssociatedValueType<
        typename InnerDomainPtrType::element_type>::type InnerValueType;

    neighbourhoods.emplace_back(
        "sequenceReverseSub", numberValsRequired,
        [](NeighbourhoodParams& params) {
            auto& val = *(params.getVals<SequenceValue>().front());
            if (val.numberElements() < 2) {
                ++params.stats.minorNodeCount;
                return;
            }
            int numberTries = 0;
            const int tryLimit = params.parentCheckTryLimit;
            debug_neighbourhood_action(
                "Looking for indices of subsequence to reverse");

            bool success;
            UInt index1, index2;
            do {
                index1 = globalRandom<UInt>(0, val.numberElements() - 2);
                index2 =
                    globalRandom<UInt>(index1 + 1, val.numberElements() - 1);
                params.stats.minorNodeCount +=
                    (index2 - index1) / 2 + ((index2 - index1) % 2 != 0);
                success = val.trySubsequenceReverse<InnerValueType>(
                    index1, index2,
                    [&]() { return params.parentCheck(params.vals); });
            } while (!success && ++numberTries < tryLimit);
            if (!success) {
                debug_neighbourhood_action(
                    "Couldn't find subsequence to reverse, number tries="
                    << tryLimit);
                return;
            }
            debug_neighbourhood_action(
                "subsequence reversed: " << index1 << " and " << index2);
            if (!params.changeAccepted()) {
                debug_neighbourhood_action("Change rejected");
                val.trySubsequenceReverse<InnerValueType>(
                    index1, index2, []() { return true; });
            }
        });
}

void sequenceReverseSubGen(const SequenceDomain& domain, int numberValsRequired,
                           std::vector<Neighbourhood>& neighbourhoods) {
    mpark::visit(
        [&](const auto& innerDomainPtr) {
            sequenceReverseSubGenImpl(domain, innerDomainPtr,
                                      numberValsRequired, neighbourhoods);
        },
        domain.inner);
}

void generateSwap(UInt size, vector<UInt>& swaps) {
    swaps.resize(size);
    for (size_t i = 0; i < swaps.size(); i++) {
        swaps[i] = globalRandom<UInt>(i, swaps.size() - 1);
    }
}

template <typename InnerDomainPtrType>
void sequenceShuffleSubGenImpl(const SequenceDomain&, InnerDomainPtrType&,
                               int numberValsRequired,
                               std::vector<Neighbourhood>& neighbourhoods) {
    typedef typename AssociatedValueType<
        typename InnerDomainPtrType::element_type>::type InnerValueType;

    neighbourhoods.emplace_back(
        "sequenceShuffleSub", numberValsRequired,
        [](NeighbourhoodParams& params) {
            auto& val = *(params.getVals<SequenceValue>().front());
            if (val.numberElements() < 2) {
                ++params.stats.minorNodeCount;
                return;
            }
            int numberTries = 0;
            const int tryLimit = params.parentCheckTryLimit;
            debug_neighbourhood_action(
                "Looking for indices of subsequence to Shuffle");

            bool success;
            UInt index1, index2;
            vector<UInt> swaps;
            do {
                index1 = globalRandom<UInt>(0, val.numberElements() - 2);
                index2 =
                    globalRandom<UInt>(index1 + 1, val.numberElements() - 1);
                generateSwap((index2 - index1) + 1, swaps);

                params.stats.minorNodeCount += (index2 - index1) + 1;
                success = val.trySubsequenceShuffle<InnerValueType>(
                    index1, swaps,
                    [&]() { return params.parentCheck(params.vals); });
            } while (!success && ++numberTries < tryLimit);
            if (!success) {
                debug_neighbourhood_action(
                    "Couldn't find subsequence to Shuffle, number tries="
                    << tryLimit);
                return;
            }
            debug_neighbourhood_action(
                "subsequence Shuffled: " << index1 << " and " << index2);
            if (!params.changeAccepted()) {
                debug_neighbourhood_action("Change rejected");
                val.trySubsequenceShuffle<InnerValueType>(
                    index1, swaps, []() { return true; });
            }
        });
}

void sequenceShuffleSubGen(const SequenceDomain& domain, int numberValsRequired,
                           std::vector<Neighbourhood>& neighbourhoods) {
    mpark::visit(
        [&](const auto& innerDomainPtr) {
            sequenceShuffleSubGenImpl(domain, innerDomainPtr,
                                      numberValsRequired, neighbourhoods);
        },
        domain.inner);
}

template <typename InnerDomainPtrType>
void sequencePositionsSwapGenImpl(const SequenceDomain&, InnerDomainPtrType&,
                                  int numberValsRequired,
                                  std::vector<Neighbourhood>& neighbourhoods) {
    typedef typename AssociatedValueType<
        typename InnerDomainPtrType::element_type>::type InnerValueType;

    neighbourhoods.emplace_back(
        "sequencePositionsSwap", numberValsRequired,
        [](NeighbourhoodParams& params) {
            auto& val = *(params.getVals<SequenceValue>().front());
            if (val.numberElements() < 2) {
                ++params.stats.minorNodeCount;
                return;
            }
            int numberTries = 0;
            const int tryLimit = params.parentCheckTryLimit;
            debug_neighbourhood_action("Looking for indices to swap");

            bool success;
            UInt index1, index2;
            do {
                ++params.stats.minorNodeCount;
                index1 = globalRandom<UInt>(0, val.numberElements() - 2);
                index2 =
                    globalRandom<UInt>(index1 + 1, val.numberElements() - 1);
                success = val.trySwapPositions<InnerValueType>(
                    index1, index2,
                    [&]() { return params.parentCheck(params.vals); });
            } while (!success && ++numberTries < tryLimit);
            if (!success) {
                debug_neighbourhood_action(
                    "Couldn't find positions to swap, number tries="
                    << tryLimit);
                return;
            }
            debug_neighbourhood_action(
                "positions swapped: " << index1 << " and " << index2);
            if (!params.changeAccepted()) {
                debug_neighbourhood_action("Change rejected");
                val.trySwapPositions<InnerValueType>(index1, index2,
                                                     []() { return true; });
            }
        });
}

void sequencePositionsSwapGen(const SequenceDomain& domain,
                              int numberValsRequired,
                              std::vector<Neighbourhood>& neighbourhoods) {
    mpark::visit(
        [&](const auto& innerDomainPtr) {
            sequencePositionsSwapGenImpl(domain, innerDomainPtr,
                                         numberValsRequired, neighbourhoods);
        },
        domain.inner);
}

template <typename InnerDomainPtrType>
void sequenceMoveGenImpl(const SequenceDomain& domain, InnerDomainPtrType&,
                         int numberValsRequired,
                         std::vector<Neighbourhood>& neighbourhoods) {
    typedef typename AssociatedValueType<
        typename InnerDomainPtrType::element_type>::type InnerValueType;
    typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
    UInt innerDomainSize = getDomainSize(domain.inner);

    neighbourhoods.emplace_back(
        "sequenceMove", numberValsRequired,
        [innerDomainSize, &domain](NeighbourhoodParams& params) {
            auto& vals = params.getVals<SequenceValue>();
            debug_code(assert(vals.size() == 2));
            auto& fromVal = *(vals[0]);
            auto& toVal = *(vals[1]);
            if (fromVal.numberElements() == domain.sizeAttr.minSize ||
                toVal.numberElements() == domain.sizeAttr.maxSize) {
                ++params.stats.minorNodeCount;
                return;
            }
            int numberTries = 0;
            const int tryLimit = params.parentCheckTryLimit *
                                 calcNumberInsertionAttempts(
                                     toVal.numberElements(), innerDomainSize);
            debug_neighbourhood_action("Looking for value to move");
            bool success = false;
            UInt indexToMove, destIndex;
            do {
                ++params.stats.minorNodeCount;
                indexToMove =
                    globalRandom<UInt>(0, fromVal.numberElements() - 1);
                destIndex = globalRandom<UInt>(0, toVal.numberElements());
                auto memberToMove = assumeAsValue(
                    fromVal.getMembers<InnerViewType>()[indexToMove]);
                success = toVal.tryAddMember(destIndex, memberToMove, [&]() {
                    return fromVal
                        .tryRemoveMember<InnerValueType>(
                            indexToMove,
                            [&]() { return params.parentCheck(params.vals); })
                        .first;
                });
            } while (!success && ++numberTries < tryLimit);
            if (!success) {
                debug_neighbourhood_action(
                    "Couldn't find value, number tries=" << tryLimit);
                return;
            }
            debug_neighbourhood_action(
                "Moved value: "
                << toVal.getMembers<InnerViewType>()[destIndex]);
            if (!params.changeAccepted()) {
                debug_neighbourhood_action("Change rejected");
                auto member = toVal
                                  .tryRemoveMember<InnerValueType>(
                                      destIndex, []() { return true; })
                                  .second;
                fromVal.tryAddMember(indexToMove, member,
                                     [&]() { return true; });
            }
        });
}

void sequenceMoveGen(const SequenceDomain& domain, int numberValsRequired,
                     std::vector<Neighbourhood>& neighbourhoods) {
    if (domain.sizeAttr.sizeType == SizeAttr::SizeAttrType::EXACT_SIZE) {
        return;
    }
    mpark::visit(
        [&](const auto& innerDomainPtr) {
            sequenceMoveGenImpl(domain, innerDomainPtr, numberValsRequired,
                                neighbourhoods);
        },
        domain.inner);
}

template <typename InnerValueType, typename Func>
static bool performCrossOver(SequenceValue& fromVal, SequenceValue& toVal,
                             UInt indexToCrossOver,
                             std::vector<HashType>& fromValMemberHashes,
                             std::vector<HashType>& toValMemberHashes,
                             ValRef<InnerValueType> member1,
                             ValRef<InnerValueType> member2,
                             Func&& parentCheck) {
    fromValMemberHashes.clear();
    toValMemberHashes.clear();
    HashType fromValSubseqHash =
        fromVal.notifyPossibleSubsequenceChange<InnerValueType>(
            indexToCrossOver, indexToCrossOver + 1, fromValMemberHashes);
    HashType toValSubseqHash =
        toVal.notifyPossibleSubsequenceChange<InnerValueType>(
            indexToCrossOver, indexToCrossOver + 1, toValMemberHashes);
    swapValAssignments(*member1, *member2);
    return fromVal.trySubsequenceChange<InnerValueType>(
        indexToCrossOver, indexToCrossOver + 1, fromValMemberHashes,
        fromValSubseqHash, [&]() {
            return toVal.trySubsequenceChange<InnerValueType>(
                indexToCrossOver, indexToCrossOver + 1, toValMemberHashes,
                toValSubseqHash, parentCheck);
        });
}

template <typename InnerDomainPtrType>
void sequenceCrossOverGenImpl(const SequenceDomain& domain, InnerDomainPtrType&,
                              int numberValsRequired,
                              std::vector<Neighbourhood>& neighbourhoods) {
    typedef typename AssociatedValueType<
        typename InnerDomainPtrType::element_type>::type InnerValueType;
    typedef typename AssociatedViewType<InnerValueType>::type InnerViewType;
    UInt innerDomainSize = getDomainSize(domain.inner);

    neighbourhoods.emplace_back(
        "sequenceCrossOver", numberValsRequired,
        [innerDomainSize, &domain](NeighbourhoodParams& params) {
            auto& vals = params.getVals<SequenceValue>();
            debug_code(assert(vals.size() == 2));
            auto& fromVal = *(vals[0]);
            auto& toVal = *(vals[1]);
            if (fromVal.numberElements() == 0 || toVal.numberElements() == 0) {
                ++params.stats.minorNodeCount;
                return;
            }
            int numberTries = 0;
            const int tryLimit =
                (domain.injective)
                    ? params.parentCheckTryLimit *
                          calcNumberInsertionAttempts(fromVal.numberElements(),
                                                      innerDomainSize) *
                          calcNumberInsertionAttempts(toVal.numberElements(),
                                                      innerDomainSize)
                    : params.parentCheckTryLimit;
            debug_neighbourhood_action("Looking for values to cross over");
            bool success = false;
            UInt indexToCrossOver;
            const UInt maxCrossOverIndex =
                min(fromVal.numberElements(), toVal.numberElements()) - 1;
            ValRef<InnerValueType> member1 = nullptr, member2 = nullptr;
            std::vector<HashType> fromValMemberHashes, toValMemberHashes;

            do {
                ++params.stats.minorNodeCount;
                indexToCrossOver = globalRandom<UInt>(0, maxCrossOverIndex);
                if (domain.injective &&
                    (toVal.containsMember(
                         fromVal.member<InnerValueType>(indexToCrossOver)) ||
                     fromVal.containsMember(
                         toVal.member<InnerValueType>(indexToCrossOver)))) {
                    continue;
                }
                member1 = assumeAsValue(
                    fromVal.getMembers<InnerViewType>()[indexToCrossOver]);
                member2 = assumeAsValue(
                    toVal.getMembers<InnerViewType>()[indexToCrossOver]);
                success = performCrossOver(
                    fromVal, toVal, indexToCrossOver, fromValMemberHashes,
                    toValMemberHashes, member1, member2,
                    [&]() { return params.parentCheck(params.vals); });
            } while (!success && ++numberTries < tryLimit);

            if (!success) {
                debug_neighbourhood_action(
                    "Couldn't find values to cross over, number tries="
                    << tryLimit);
                return;
            }

            debug_neighbourhood_action(
                "CrossOverd values: "
                << toVal.getMembers<InnerViewType>()[indexToCrossOver]
                << " and "
                << fromVal.getMembers<InnerViewType>()[indexToCrossOver]);
            if (!params.changeAccepted()) {
                debug_neighbourhood_action("Change rejected");
                performCrossOver(fromVal, toVal, indexToCrossOver,
                                 fromValMemberHashes, toValMemberHashes,
                                 member1, member2, [&]() { return true; });
            }
        });
}

void sequenceCrossOverGen(const SequenceDomain& domain, int numberValsRequired,
                          std::vector<Neighbourhood>& neighbourhoods) {
    if (domain.sizeAttr.sizeType == SizeAttr::SizeAttrType::EXACT_SIZE) {
        return;
    }
    mpark::visit(
        [&](const auto& innerDomainPtr) {
            sequenceCrossOverGenImpl(domain, innerDomainPtr, numberValsRequired,
                                     neighbourhoods);
        },
        domain.inner);
}

void sequenceAssignRandomGen(const SequenceDomain& domain,
                             int numberValsRequired,
                             std::vector<Neighbourhood>& neighbourhoods) {
    neighbourhoods.emplace_back(
        "sequenceAssignRandom", numberValsRequired,
        [&domain](NeighbourhoodParams& params) {
            auto& val = *(params.getVals<SequenceValue>().front());
            int numberTries = 0;
            const int tryLimit = params.parentCheckTryLimit;
            debug_neighbourhood_action(
                "Assigning random value: original value is " << asView(val));
            auto backup = deepCopy(val);
            backup->container = val.container;
            auto newValue = constructValueFromDomain(domain);
            newValue->container = val.container;
            bool success;
            do {
                assignRandomValueInDomain(domain, *newValue, params.stats);
                success = val.tryAssignNewValue(*newValue, [&]() {
                    return params.parentCheck(params.vals);
                });
                if (success) {
                    debug_neighbourhood_action("New value is " << asView(val));
                }
            } while (!success && ++numberTries < tryLimit);
            if (!success) {
                debug_neighbourhood_action(
                    "Couldn't find value, number tries=" << tryLimit);
                return;
            }
            if (!params.changeAccepted()) {
                debug_neighbourhood_action("Change rejected");
                deepCopy(*backup, val);
            }
        });
}

const NeighbourhoodVec<SequenceDomain>
    NeighbourhoodGenList<SequenceDomain>::value = {
        {1, sequenceLiftSingleGen}, {1, sequenceAddGen},
        {1, sequenceRemoveGen},     {1, sequencePositionsSwapGen},
        {1, sequenceReverseSubGen},
        {1, sequenceShuffleSubGen},
        {1, sequenceRelaxSubGen},   {2, sequenceMoveGen},
        {2, sequenceCrossOverGen}

};
