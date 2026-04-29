#include "widgets/config_editor_window.h"

#include <QCheckBox>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest/QTest>

class HealthConfigUiTest : public QObject {
    Q_OBJECT

private slots:
    void loadsJsonIntoWidgets();
    void marksDirtyAfterEditAndSaves();
};

void HealthConfigUiTest::loadsJsonIntoWidgets() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile file(dir.filePath(QStringLiteral("runtime_config.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({
        "video": { "device_path": "/dev/video55" },
        "analysis": { "transport": "dmabuf" }
    })");
    file.close();

    ConfigEditorWindow window(file.fileName());
    QVERIFY(window.load());
    QCOMPARE(window.valueForField(QStringLiteral("video.device_path")), QStringLiteral("/dev/video55"));
    QCOMPARE(window.valueForField(QStringLiteral("analysis.transport")), QStringLiteral("dmabuf"));
}

void HealthConfigUiTest::marksDirtyAfterEditAndSaves() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QFile file(dir.filePath(QStringLiteral("runtime_config.json")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({
        "video": { "analysis_enabled": true }
    })");
    file.close();

    ConfigEditorWindow window(file.fileName());
    QVERIFY(window.load());

    QWidget *field = window.fieldWidget(QStringLiteral("video.analysis_enabled"));
    QVERIFY(field != nullptr);
    auto *checkBox = qobject_cast<QCheckBox *>(field);
    QVERIFY(checkBox != nullptr);
    QVERIFY(checkBox->isChecked());

    checkBox->click();
    QVERIFY(window.save());

    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QByteArray saved = file.readAll();
    QVERIFY(saved.contains("\"analysis_enabled\": false"));
}

QTEST_MAIN(HealthConfigUiTest)
#include "health_config_ui_test.moc"
