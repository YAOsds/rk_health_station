#include "action/stgcn_preprocessor.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kExpectedFrames = 45;
constexpr int kExpectedJoints = 17;
constexpr float kEpsilon = 1e-6f;

constexpr int kNoseIndex = 0;
constexpr int kLeftShoulderIndex = 5;
constexpr int kRightShoulderIndex = 6;
constexpr int kLeftHipIndex = 11;
constexpr int kRightHipIndex = 12;
constexpr int kLeftKneeIndex = 13;
constexpr int kRightKneeIndex = 14;
constexpr int kLeftAnkleIndex = 15;
constexpr int kRightAnkleIndex = 16;

bool isValidKeypoint(const PoseKeypoint &keypoint) {
    return keypoint.score > 0.0f;
}

QPointF midpoint(const PoseKeypoint &a, const PoseKeypoint &b) {
    return QPointF((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
}

bool midHipForPerson(const PosePerson &person, QPointF *midHip) {
    if (!midHip || person.keypoints.size() < kExpectedJoints) {
        return false;
    }

    const PoseKeypoint &leftHip = person.keypoints.at(kLeftHipIndex);
    const PoseKeypoint &rightHip = person.keypoints.at(kRightHipIndex);
    if (!isValidKeypoint(leftHip) || !isValidKeypoint(rightHip)) {
        return false;
    }

    *midHip = midpoint(leftHip, rightHip);
    return true;
}

float distance(const QPointF &a, const QPointF &b) {
    const double dx = a.x() - b.x();
    const double dy = a.y() - b.y();
    return static_cast<float>(std::sqrt((dx * dx) + (dy * dy)));
}

float estimateBodyHeight(const PosePerson &person) {
    if (person.keypoints.size() < kExpectedJoints) {
        return 0.0f;
    }

    const PoseKeypoint &nose = person.keypoints.at(kNoseIndex);
    const PoseKeypoint &leftAnkle = person.keypoints.at(kLeftAnkleIndex);
    const PoseKeypoint &rightAnkle = person.keypoints.at(kRightAnkleIndex);
    const PoseKeypoint &leftKnee = person.keypoints.at(kLeftKneeIndex);
    const PoseKeypoint &rightKnee = person.keypoints.at(kRightKneeIndex);
    const PoseKeypoint &leftShoulder = person.keypoints.at(kLeftShoulderIndex);
    const PoseKeypoint &rightShoulder = person.keypoints.at(kRightShoulderIndex);

    if (isValidKeypoint(nose) && isValidKeypoint(leftAnkle) && isValidKeypoint(rightAnkle)) {
        return distance(QPointF(nose.x, nose.y), midpoint(leftAnkle, rightAnkle));
    }
    if (isValidKeypoint(nose) && isValidKeypoint(leftKnee) && isValidKeypoint(rightKnee)) {
        return distance(QPointF(nose.x, nose.y), midpoint(leftKnee, rightKnee));
    }

    QPointF midHip;
    if (midHipForPerson(person, &midHip)
        && isValidKeypoint(leftShoulder) && isValidKeypoint(rightShoulder)) {
        return distance(midpoint(leftShoulder, rightShoulder), midHip);
    }

    return 0.0f;
}

int tensorIndex(int channel, int frame, int joint) {
    return ((channel * kExpectedFrames) + frame) * kExpectedJoints + joint;
}
}

bool buildStgcnInputTensor(
    const QVector<PosePerson> &sequence, StgcnInputTensor *tensor, QString *error) {
    if (error) {
        error->clear();
    }
    if (!tensor) {
        if (error) {
            *error = QStringLiteral("stgcn_tensor_missing");
        }
        return false;
    }
    if (sequence.size() != kExpectedFrames) {
        if (error) {
            *error = QStringLiteral("stgcn_sequence_length_mismatch");
        }
        return false;
    }

    QVector<float> scales;
    scales.reserve(sequence.size());
    for (const PosePerson &person : sequence) {
        if (person.keypoints.size() < kExpectedJoints) {
            if (error) {
                *error = QStringLiteral("stgcn_keypoints_incomplete");
            }
            return false;
        }

        QPointF midHip;
        if (!midHipForPerson(person, &midHip)) {
            if (error) {
                *error = QStringLiteral("stgcn_mid_hip_missing");
            }
            return false;
        }

        const float scale = estimateBodyHeight(person);
        if (scale > kEpsilon) {
            scales.push_back(scale);
        }
    }

    if (scales.isEmpty()) {
        if (error) {
            *error = QStringLiteral("stgcn_scale_unavailable");
        }
        return false;
    }

    std::sort(scales.begin(), scales.end());
    const float windowScale = scales.at(scales.size() / 2);
    if (windowScale <= kEpsilon) {
        if (error) {
            *error = QStringLiteral("stgcn_scale_unavailable");
        }
        return false;
    }

    tensor->channels = 3;
    tensor->frames = kExpectedFrames;
    tensor->joints = kExpectedJoints;
    tensor->values.fill(0.0f, tensor->channels * tensor->frames * tensor->joints);

    for (int frame = 0; frame < sequence.size(); ++frame) {
        QPointF midHip;
        midHipForPerson(sequence.at(frame), &midHip);

        for (int joint = 0; joint < kExpectedJoints; ++joint) {
            const PoseKeypoint &keypoint = sequence.at(frame).keypoints.at(joint);
            tensor->values[tensorIndex(0, frame, joint)] = (keypoint.x - midHip.x()) / windowScale;
            tensor->values[tensorIndex(1, frame, joint)] = (keypoint.y - midHip.y()) / windowScale;
            tensor->values[tensorIndex(2, frame, joint)] = keypoint.score;
        }
    }

    return true;
}

QVector<float> flattenSkeletonSequenceForLstm(const StgcnInputTensor &tensor) {
    QVector<float> output;
    if (tensor.channels != 3 || tensor.frames != kExpectedFrames || tensor.joints != kExpectedJoints) {
        return output;
    }

    output.resize(tensor.frames * tensor.joints * tensor.channels);
    int outputIndex = 0;
    for (int frame = 0; frame < tensor.frames; ++frame) {
        for (int joint = 0; joint < tensor.joints; ++joint) {
            for (int channel = 0; channel < tensor.channels; ++channel) {
                output[outputIndex++] = tensor.values[tensorIndex(channel, frame, joint)];
            }
        }
    }
    return output;
}
