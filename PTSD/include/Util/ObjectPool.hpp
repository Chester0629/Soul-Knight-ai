#ifndef UTIL_OBJECT_POOL_HPP
#define UTIL_OBJECT_POOL_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Util {
/**
 * @brief A generic, reusable pool of heap-allocated objects.
 *
 * The ObjectPool class template recycles objects of type T instead of
 * repeatedly allocating and freeing them. This is useful for high-churn
 * entities such as bullets or particles, where the cost of construction and
 * destruction would otherwise dominate.
 *
 * Objects are handed out through Acquire() and returned through Release(). A
 * released object is kept on an internal free list so that the next Acquire()
 * can reuse it (preserving pointer identity) rather than allocating a new one.
 * When the free list is empty, a user-supplied @c Factory creates a fresh
 * object.
 *
 * The pool does not own active objects in the sense of forcing their lifetime;
 * it holds shared ownership of every object it has created, so an object stays
 * alive as long as either the caller or the pool's free list references it.
 *
 * @note This type holds no global state and uses no randomness, so its
 * behaviour is fully deterministic.
 *
 * @tparam T The type of object managed by the pool.
 */
template <typename T>
class ObjectPool {
public:
    /**
     * @brief The signature of the function used to create new objects.
     *
     * The factory is invoked whenever Acquire() needs to grow the pool because
     * the free list is empty. It must return a non-null shared pointer.
     */
    using Factory = std::function<std::shared_ptr<T>()>;

    /**
     * @brief Constructs an ObjectPool, optionally pre-allocating objects.
     *
     * @param prewarm The number of objects to create up front and place on the
     * free list. After construction FreeCount() equals @p prewarm and
     * ActiveCount() is zero.
     * @param factory The function used to create new objects of type T. Defaults
     * to @c std::make_shared<T>(). The factory must return a non-null pointer.
     */
    explicit ObjectPool(std::size_t prewarm = 0, Factory factory = nullptr)
        : m_Factory(factory ? std::move(factory) : DefaultFactory()) {
        m_Free.reserve(prewarm);
        for (std::size_t i = 0; i < prewarm; ++i) {
            m_Free.push_back(m_Factory());
        }
    }

    /**
     * @brief Acquires an object from the pool.
     *
     * If the free list is non-empty, the most recently released object is
     * popped and returned, reusing its existing storage (the returned pointer
     * has the same identity it had before being released). Otherwise the
     * factory is invoked to create a new object.
     *
     * @return A shared pointer to a ready-to-use object. The active count is
     * incremented by one.
     */
    std::shared_ptr<T> Acquire() {
        std::shared_ptr<T> obj;
        if (!m_Free.empty()) {
            obj = std::move(m_Free.back());
            m_Free.pop_back();
        } else {
            obj = m_Factory();
        }
        ++m_ActiveCount;
        return obj;
    }

    /**
     * @brief Returns an object to the pool for future reuse.
     *
     * The object is pushed onto the free list and the active count is
     * decremented. Passing a null pointer is a safe no-op.
     *
     * @param obj The object to release. The caller may keep its own copy of the
     * shared pointer, but should not continue to mutate the object after
     * releasing it.
     */
    void Release(const std::shared_ptr<T> &obj) {
        if (obj == nullptr) {
            return;
        }
        m_Free.push_back(obj);
        if (m_ActiveCount > 0) {
            --m_ActiveCount;
        }
    }

    /**
     * @brief Returns the number of objects available for reuse.
     *
     * @return The size of the free list.
     */
    std::size_t FreeCount() const { return m_Free.size(); }

    /**
     * @brief Returns the number of objects currently handed out.
     *
     * @return The number of Acquire() calls that have not yet been balanced by
     * a Release().
     */
    std::size_t ActiveCount() const { return m_ActiveCount; }

    /**
     * @brief Returns the total number of objects tracked by the pool.
     *
     * @return The sum of FreeCount() and ActiveCount().
     */
    std::size_t Capacity() const { return m_Free.size() + m_ActiveCount; }

    /**
     * @brief Drops all pooled objects from the free list.
     *
     * The free list is emptied and its objects are released. Objects that are
     * currently active are unaffected; ActiveCount() is left unchanged so the
     * Capacity() invariant continues to hold.
     */
    void Clear() { m_Free.clear(); }

private:
    /**
     * @brief The fallback factory used when the constructor is given none.
     *
     * Provided as a named helper rather than a default-argument lambda because
     * MSVC (C2440) rejects converting a lambda to @c std::function in a default
     * argument. The behaviour is identical: @c std::make_shared<T>().
     */
    static Factory DefaultFactory() {
        return [] { return std::make_shared<T>(); };
    }

    Factory m_Factory;
    std::vector<std::shared_ptr<T>> m_Free;
    std::size_t m_ActiveCount = 0;
};
} // namespace Util

#endif /* UTIL_OBJECT_POOL_HPP */
