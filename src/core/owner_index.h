#pragma once

#include <unordered_map>
#include <utility>

namespace ScrollerCore {

/**
 * @brief Shared cache for "member key -> owning object" lookups.
 *
 * Both `Lane` and `CanvasLayout` keep a small cache that maps window keys to
 * their current owning stack/lane. The mutation rules are the same in both
 * places: look up a cached owner, drop stale entries eagerly, bulk remember all
 * members of one owner, bulk forget a removed owner, and verify the cache in
 * debug/test code.
 */
template <typename Key, typename Owner>
class OwnerIndex {
public:
    // Lookup behaves like a cache read for callers, but eagerly evicts stale
    // entries so later reads do not keep paying the validation cost.
    Owner *find_valid(const Key &key, auto &&validate) const {
        if (const auto it = owners_.find(key); it != owners_.end()) {
            auto *owner = it->second;
            if (std::forward<decltype(validate)>(validate)(owner))
                return owner;

            owners_.erase(it);
        }

        return nullptr;
    }

    void remember(const Key &key, Owner *owner) {
        if (!owner) {
            forget(key);
            return;
        }

        owners_[key] = owner;
    }

    void forget(const Key &key) {
        owners_.erase(key);
    }

    template <typename VisitKeysFn>
    void remember_owner(Owner *owner, VisitKeysFn &&visitKeys) {
        if (!owner)
            return;

        std::forward<VisitKeysFn>(visitKeys)([&](const Key &key) {
            owners_[key] = owner;
        });
    }

    void forget_owner(Owner *owner) {
        if (!owner)
            return;

        for (auto it = owners_.begin(); it != owners_.end();) {
            if (it->second == owner)
                it = owners_.erase(it);
            else
                ++it;
        }
    }

    void clear() {
        owners_.clear();
    }

    bool matches_expected(auto &&buildExpected) const {
        std::unordered_map<Key, Owner *> expected;
        bool ok = true;

        std::forward<decltype(buildExpected)>(buildExpected)([&](const Key &key, Owner *owner) {
            const auto [it, inserted] = expected.emplace(key, owner);
            if (!inserted || it->second != owner)
                ok = false;
        });

        if (!ok || expected.size() != owners_.size())
            return false;

        for (const auto &[key, owner] : expected) {
            const auto it = owners_.find(key);
            if (it == owners_.end() || it->second != owner)
                return false;
        }

        return true;
    }

private:
    mutable std::unordered_map<Key, Owner *> owners_;
};

} // namespace ScrollerCore
