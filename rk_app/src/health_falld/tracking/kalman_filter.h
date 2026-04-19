#pragma once

#include <QPointF>
#include <QRectF>

class KalmanFilter {
public:
    void initiate(const QRectF &box);
    QRectF predict() const;
    void update(const QRectF &box);

private:
    QRectF stateBox_;
    QPointF velocity_;
    bool initialized_ = false;
};
