#include "tracking/kalman_filter.h"

void KalmanFilter::initiate(const QRectF &box) {
    stateBox_ = box;
    velocity_ = QPointF();
    initialized_ = true;
}

QRectF KalmanFilter::predict() const {
    if (!initialized_) {
        return QRectF();
    }
    return stateBox_.translated(velocity_);
}

void KalmanFilter::update(const QRectF &box) {
    if (!initialized_) {
        initiate(box);
        return;
    }

    velocity_ = box.center() - stateBox_.center();
    stateBox_ = box;
}
