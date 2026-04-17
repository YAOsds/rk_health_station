#pragma once

#include <QVector>

template <typename T>
class SequenceBuffer {
public:
    explicit SequenceBuffer(int capacity)
        : capacity_(capacity) {
    }

    void push(const T &value) {
        if (capacity_ <= 0) {
            return;
        }
        if (values_.size() == capacity_) {
            values_.removeFirst();
        }
        values_.push_back(value);
    }

    QVector<T> values() const {
        return values_;
    }

    bool isFull() const {
        return values_.size() == capacity_;
    }

    void clear() {
        values_.clear();
    }

private:
    int capacity_ = 0;
    QVector<T> values_;
};
