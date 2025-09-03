#pragma once
#ifndef HUGO_LIB_STACK_H
#define HUGO_LIB_STACK_H

#include <cstddef> // for size_t
#include <stdint.h>

template <typename T, size_t Capacity_>
class stack
{
public:
    stack() { clear(); }

    bool push(const T& item)
    {
        if(full_)
            return false;
        data_[top_++] = item;
        if(top_ >= Capacity_)
            full_ = true;
        return true;
    }

    bool pop(T& out)
    {
        if(empty())
            return false;
        top_--;
        full_ = false;
        out = data_[top_];
        return true;
    }

    bool peek(T& out) const
    {
        if(empty())
            return false;
        out = data_[top_ - 1];
        return true;
    }

    bool peek(T& out, size_t idx) const
    {
        if(empty() || idx >= top_)
            return false;
        out = data_[top_ - 1 - idx];
        return true;
    }

    void clear()
    {
        top_  = 0;
        full_ = false;
    }

    bool empty() const { return top_ == 0; }
    bool full() const { return full_; }
    size_t size() const { return top_; }
    constexpr size_t capacity() const { return Capacity_; }

private:
    T data_[Capacity_];
    size_t top_;
    bool full_;
};

#endif // HUGO_LIB_STACK_H