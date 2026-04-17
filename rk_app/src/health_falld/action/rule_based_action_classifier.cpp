#include "action/rule_based_action_classifier.h"

#include <QPointF>

#include <cmath>

namespace {
constexpr int kExpectedJoints = 17;
constexpr int kLeftShoulderIndex = 5;
constexpr int kRightShoulderIndex = 6;
constexpr int kLeftHipIndex = 11;
constexpr int kRightHipIndex = 12;

bool isValidKeypoint(const PoseKeypoint &keypoint) {
    return keypoint.score > 0.0f;
}

bool centerOf(const PosePerson &person, int leftIndex, int rightIndex, QPointF *center) {
    if (!center || person.keypoints.size() < kExpectedJoints) {
        return false;
    }

    const PoseKeypoint &left = person.keypoints.at(leftIndex);
    const PoseKeypoint &right = person.keypoints.at(rightIndex);
    if (!isValidKeypoint(left) || !isValidKeypoint(right)) {
        return false;
    }

    *center = QPointF((left.x + right.x) * 0.5f, (left.y + right.y) * 0.5f);
    return true;
}

bool shoulderCenter(const PosePerson &person, QPointF *center) {
    return centerOf(person, kLeftShoulderIndex, kRightShoulderIndex, center);
}

bool hipCenter(const PosePerson &person, QPointF *center) {
    return centerOf(person, kLeftHipIndex, kRightHipIndex, center);
}

float torsoScale(const PosePerson &person) {
    QPointF shoulder;
    QPointF hip;
    if (!shoulderCenter(person, &shoulder) || !hipCenter(person, &hip)) {
        return 0.0f;
    }

    const double dx = shoulder.x() - hip.x();
    const double dy = shoulder.y() - hip.y();
    return static_cast<float>(std::sqrt((dx * dx) + (dy * dy)));
}
}

bool RuleBasedActionClassifier::loadModel(const QString &path, QString *error) {
    Q_UNUSED(path);
    if (error) {
        error->clear();
    }
    return true;
}

ActionClassification RuleBasedActionClassifier::classify(
    const QVector<PosePerson> &sequence, QString *error) {
    if (error) {
        error->clear();
    }

    if (sequence.isEmpty()) {
        return {};
    }

    QPointF firstHip;
    QPointF lastHip;
    QPointF lastShoulder;
    if (!hipCenter(sequence.first(), &firstHip)
        || !hipCenter(sequence.last(), &lastHip)
        || !shoulderCenter(sequence.last(), &lastShoulder)) {
        return {};
    }

    const float scale = torsoScale(sequence.first());
    if (scale <= 1e-3f) {
        return {};
    }

    const float hipDrop = static_cast<float>(lastHip.y() - firstHip.y());
    const float torsoRise = std::fabs(static_cast<float>(lastShoulder.y() - lastHip.y()));
    const bool torsoLooksHorizontal = torsoRise < (scale * 0.5f);

    ActionClassification classification;
    if (hipDrop > scale && torsoLooksHorizontal) {
        classification.label = QStringLiteral("lie");
        classification.confidence = 0.8;
        return classification;
    }

    classification.label = QStringLiteral("monitoring");
    classification.confidence = 0.0;
    return classification;
}
