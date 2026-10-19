#include "core/skill/SkillRegistry.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

namespace {

bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    return file.write(content) == content.size();
}

} // namespace

/**
 * Skill 装载测试：目录缺失、manifest 损坏、字段缺失都只跳过并告警，不得崩溃。
 */
class TstSkill : public QObject
{
    Q_OBJECT

private slots:
    void missingRootIsReportedWithoutCrash();
    void loadsValidManifest();
    void skipsBrokenManifestButKeepsValid();
    void readDocumentFailsOnMissingEntry();
};

void TstSkill::missingRootIsReportedWithoutCrash()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    SkillRegistry registry(QDir(tmp.path()).filePath(QStringLiteral("not-exist")));
    QStringList warnings;
    QCOMPARE(registry.scan(&warnings), 0);
    QCOMPARE(registry.count(), 0);
    QVERIFY(!warnings.isEmpty());
    QVERIFY(registry.find(QStringLiteral("example")) == nullptr);
}

void TstSkill::loadsValidManifest()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString dir = QDir(tmp.path()).filePath(QStringLiteral("example"));
    QVERIFY(QDir().mkpath(dir));
    QVERIFY(writeFile(QDir(dir).filePath(QStringLiteral("manifest.json")),
                      R"({"id":"example","name":"示例","version":"0.1.0","entry":"skill.md"})"));
    QVERIFY(writeFile(QDir(dir).filePath(QStringLiteral("skill.md")), "hello skill"));

    SkillRegistry registry(tmp.path());
    QStringList warnings;
    QCOMPARE(registry.scan(&warnings), 1);

    const Skill *skill = registry.find(QStringLiteral("example"));
    QVERIFY(skill != nullptr);
    QCOMPARE(skill->manifest().name, QStringLiteral("示例"));

    QString error;
    QCOMPARE(skill->readDocument(&error), QStringLiteral("hello skill"));
    QVERIFY(error.isEmpty());
}

void TstSkill::skipsBrokenManifestButKeepsValid()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString broken = QDir(tmp.path()).filePath(QStringLiteral("broken"));
    QVERIFY(QDir().mkpath(broken));
    QVERIFY(writeFile(QDir(broken).filePath(QStringLiteral("manifest.json")), R"({"id":)"));

    const QString incomplete = QDir(tmp.path()).filePath(QStringLiteral("incomplete"));
    QVERIFY(QDir().mkpath(incomplete));
    QVERIFY(writeFile(QDir(incomplete).filePath(QStringLiteral("manifest.json")),
                      R"({"id":"incomplete","name":"缺版本"})"));

    const QString valid = QDir(tmp.path()).filePath(QStringLiteral("valid"));
    QVERIFY(QDir().mkpath(valid));
    QVERIFY(writeFile(QDir(valid).filePath(QStringLiteral("manifest.json")),
                      R"({"id":"valid","name":"正常","version":"1.0.0","entry":"skill.md"})"));
    QVERIFY(writeFile(QDir(valid).filePath(QStringLiteral("skill.md")), "ok"));

    SkillRegistry registry(tmp.path());
    QStringList warnings;
    QCOMPARE(registry.scan(&warnings), 1);
    QCOMPARE(warnings.size(), 2);
    QCOMPARE(registry.ids(), QStringList{QStringLiteral("valid")});
}

void TstSkill::readDocumentFailsOnMissingEntry()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString dir = QDir(tmp.path()).filePath(QStringLiteral("ghost"));
    QVERIFY(QDir().mkpath(dir));
    QVERIFY(writeFile(QDir(dir).filePath(QStringLiteral("manifest.json")),
                      R"({"id":"ghost","name":"缺文档","version":"1.0.0","entry":"missing.md"})"));

    SkillRegistry registry(tmp.path());
    QStringList warnings;
    QCOMPARE(registry.scan(&warnings), 1);

    const Skill *skill = registry.find(QStringLiteral("ghost"));
    QVERIFY(skill != nullptr);

    QString error;
    QVERIFY(skill->readDocument(&error).isEmpty());
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(TstSkill)
#include "tst_skill.moc"
