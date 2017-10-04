
#ifndef SRC_TYPES_INT_H_
#define SRC_TYPES_INT_H_
#include <utility>
#include <vector>

#include "forwardDecls/typesAndDomains.h"
#include "operators/intProducing.h"

inline auto intBound(int64_t a, int64_t b) { return std::make_pair(a, b); }
struct IntDomain {
    std::vector<std::pair<int64_t, int64_t>> bounds;
    IntDomain(std::vector<std::pair<int64_t, int64_t>> bounds)
        : bounds(std::move(bounds)) {}
};

struct IntValue : DirtyFlag {
    int64_t value;
    size_t containingBoundIndex;  // a cache of which bound this value lies in
    std::vector<std::shared_ptr<IntTrigger>> triggers;

    template <typename Func>
    inline void changeValue(Func&& func) {
        func();
        if (state != ValueState::UNDEFINED) {
            for (std::shared_ptr<IntTrigger>& t : triggers) {
                t->postValueChange();
            }
        }
    }
};

#endif /* SRC_TYPES_INT_H_ */
