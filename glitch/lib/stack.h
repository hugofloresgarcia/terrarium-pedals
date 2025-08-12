#pragma once
#include <deque>

template <class T, class Container = std::deque<T>>
class lifo_queue {
public:
    using container_type  = Container;
    using value_type      = typename Container::value_type;
    using size_type       = typename Container::size_type;
    using reference       = typename Container::reference;
    using const_reference = typename Container::const_reference;

    lifo_queue() = default;
    explicit lifo_queue(const Container& cont) : c(cont) {}
    explicit lifo_queue(Container&& cont) : c(std::move(cont)) {}

    bool empty() const noexcept { return c.empty(); }
    size_type size() const noexcept { return c.size(); }

    reference top() { return c.back(); }
    const_reference top() const { return c.back(); }

    void push(const value_type& value) { c.push_back(value); }
    void push(value_type&& value) { c.push_back(std::move(value)); }

    void pop() { c.pop_back(); }

    void swap(lifo_queue& other) noexcept(noexcept(std::swap(c, other.c))) {
        using std::swap;
        swap(c, other.c);
    }

protected:
    Container c;
};

// non-member swap
template <class T, class Container>
void swap(lifo_queue<T, Container>& lhs, lifo_queue<T, Container>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
}