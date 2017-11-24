#include "operators/opSetIntersect.h"
#include <iostream>
#include <memory>
#include "types/forwardDecls/hash.h"
#include "utils/hashUtils.h"

using namespace std;
SetMembersVector evaluate(OpSetIntersect& op) {
    SetMembersVector leftVec = evaluate(op.left);
    SetMembersVector rightVec = evaluate(op.right);
    op.cachedHashTotal = 0;
    op.memberHashes.clear();
    SetView& leftSetView = getView<SetView>(op.left);
    SetView& rightSetView = getView<SetView>(op.right);
    SetView& smallSetView =
        (leftSetView.memberHashes.size() < rightSetView.memberHashes.size())
            ? leftSetView
            : rightSetView;
    SetView& largeSetView =
        (leftSetView.memberHashes.size() < rightSetView.memberHashes.size())
            ? rightSetView
            : leftSetView;
    for (u_int64_t hash : smallSetView.memberHashes) {
        if (largeSetView.memberHashes.count(hash)) {
            op.memberHashes.insert(hash);
            op.cachedHashTotal += hash;
        }
    }
    return mpark::visit(
        [&](auto& leftVecImpl) -> SetMembersVector {
            typedef BaseType<decltype(leftVecImpl)> VecType;
            auto rightVecImpl = mpark::get<VecType>(rightVec);
            VecType returnVec;
            auto& smallerVec = (leftVecImpl.size() > rightVecImpl.size())
                                   ? leftVecImpl
                                   : rightVecImpl;
            for (auto& ref : smallerVec) {
                if (op.memberHashes.count(mix(getValueHash(*ref)))) {
                    returnVec.emplace_back(std::move(ref));
                }
            }
            return returnVec;
        },
        leftVec);
}

template <bool left>
class OpSetIntersectTrigger : public SetTrigger {
   public:
    OpSetIntersect* op;
    unordered_set<u_int64_t>::iterator oldHashIter;

   public:
    OpSetIntersectTrigger(OpSetIntersect* op) : op(op) {}
    inline void valueRemoved(const AnyValRef& member) final {
        u_int64_t hash = mix(getValueHash(member));
        unordered_set<u_int64_t>::iterator hashIter;
        if ((hashIter = op->memberHashes.find(hash)) !=
            op->memberHashes.end()) {
            op->memberHashes.erase(hashIter);
            op->cachedHashTotal -= hash;
            visitTriggers([&](auto& trigger) { trigger->valueRemoved(member); },
                          op->triggers);
        }
    }

    inline void valueAdded(const AnyValRef& member) final {
        SetReturning& unchanged = (left) ? op->right : op->left;
        u_int64_t hash = mix(getValueHash(member));
        if (op->memberHashes.count(hash)) {
            return;
        }
        SetView& viewOfUnchangedSet = getView<SetView>(unchanged);
        if (viewOfUnchangedSet.memberHashes.count(hash)) {
            op->memberHashes.insert(hash);
            op->cachedHashTotal += hash;
            visitTriggers([&](auto& trigger) { trigger->valueAdded(member); },
                          op->triggers);
        }
    }
    inline void possibleValueChange(const AnyValRef& member) final {
        u_int64_t hash = mix(getValueHash(member));
        oldHashIter = op->memberHashes.find(hash);
        if (oldHashIter != op->memberHashes.end()) {
            visitTriggers(
                [&](auto& trigger) { trigger->possibleValueChange(member); },
                op->triggers);
        }
    }

    inline void valueChanged(const AnyValRef& member) final {
        u_int64_t newHashOfMember = mix(getValueHash(member));
        if (oldHashIter != op->memberHashes.end() &&
            newHashOfMember == *oldHashIter) {
            return;
        }
        SetReturning& unchanged = (left) ? op->right : op->left;
        bool containedInUnchangedSet =
            getView<SetView>(unchanged).memberHashes.count(newHashOfMember);
        if (oldHashIter != op->memberHashes.end()) {
            op->cachedHashTotal -= *oldHashIter;
            op->memberHashes.erase(oldHashIter);
            if (containedInUnchangedSet) {
                op->memberHashes.insert(newHashOfMember);
                op->cachedHashTotal += newHashOfMember;
                visitTriggers(
                    [&](auto& trigger) { trigger->valueChanged(member); },
                    op->triggers);
            } else {
                visitTriggers(
                    [&](auto& trigger) { trigger->valueRemoved(member); },
                    op->triggers);
            }
        } else if (containedInUnchangedSet) {
            op->memberHashes.insert(newHashOfMember);
            op->cachedHashTotal += newHashOfMember;
            visitTriggers([&](auto& trigger) { trigger->valueAdded(member); },
                          op->triggers);
        }
    }
};

OpSetIntersect::OpSetIntersect(OpSetIntersect&& other)
    : SetView(std::move(other)),
      left(std::move(other.left)),
      right(std::move(other.right)),
      leftTrigger(std::move(other.leftTrigger)),
      rightTrigger(std::move(other.rightTrigger)) {
    setTriggerParent(this, leftTrigger, rightTrigger);
}

void startTriggering(OpSetIntersect& op) {
    op.leftTrigger = make_shared<OpSetIntersectTrigger<true>>(&op);
    op.rightTrigger = make_shared<OpSetIntersectTrigger<false>>(&op);
    addTrigger<SetTrigger>(getView<SetView>(op.left).triggers, op.leftTrigger);
    addTrigger<SetTrigger>(getView<SetView>(op.right).triggers,
                           op.rightTrigger);
    startTriggering(op.left);
    startTriggering(op.right);
}

void stopTriggering(OpSetIntersect& op) {
    if (op.leftTrigger) {
        deleteTrigger(op.leftTrigger);
    }
    if (op.rightTrigger) {
        deleteTrigger<SetTrigger>(op.rightTrigger);
    }
    stopTriggering(op.left);
    stopTriggering(op.right);
}

void updateViolationDescription(const OpSetIntersect& op,
                                u_int64_t parentViolation,
                                ViolationDescription& vioDesc) {
    updateViolationDescription(op.left, parentViolation, vioDesc);
    updateViolationDescription(op.right, parentViolation, vioDesc);
}

std::shared_ptr<OpSetIntersect> deepCopyForUnroll(const OpSetIntersect&,
                                                  const AnyIterRef&) {
    assert(false);
    abort();
}
