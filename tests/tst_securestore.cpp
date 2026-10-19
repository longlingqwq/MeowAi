#include "platform/impl/DesktopPlatformAdapter.h"
#include "platform/impl/WebPlatformAdapter.h"
#include "storage/SecureStore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

/**
 * 安全机制（功能 #5）测试：加解密往返、多 Provider 独立 Key、明文绝不落盘，
 * 以及"缺文件 / 密文损坏 / 非法 Key 名 / 平台不支持 / 空值"等异常流均不得崩溃。
 */
class TstSecureStore : public QObject
{
    Q_OBJECT

private slots:
    void availabilityReflectsBackend();
    void nullPlatformIsSafe();
    void roundTripSaveLoadHasErase();
    void multipleProviderKeysAreIndependent();
    void ciphertextIsNeverPlaintextOnDisk();
    void missingKeyReturnsError();
    void corruptedVaultReturnsError();
    void emptyValueIsRejected();
    void invalidKeyNamesAreRejected();
    void unavailableBackendRejectsSave();
    void eraseIsIdempotent();
    void providerKeyIdNaming();

private:
    static QString vaultPath(const QString &root, const QString &key)
    {
        return QDir(QDir(root).filePath(QStringLiteral("secure"))).filePath(key + QStringLiteral(".vault"));
    }
};

void TstSecureStore::availabilityReflectsBackend()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    DesktopPlatformAdapter platform;
    const SecureStore store(&platform, tmp.path());

#if defined(Q_OS_WIN)
    QCOMPARE(store.isAvailable(), true);
    QVERIFY(store.backendName().contains(QStringLiteral("DPAPI")));
#else
    QSKIP("非 Windows 桌面：无 DPAPI，跳过可用性断言");
#endif
}

void TstSecureStore::nullPlatformIsSafe()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    SecureStore store(nullptr, tmp.path());
    QString error;
    QString value;

    QVERIFY(!store.isAvailable());
    QVERIFY(!store.has(QStringLiteral("provider.openai.api_key")));
    QVERIFY(!store.save(QStringLiteral("provider.openai.api_key"), QStringLiteral("x"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!store.load(QStringLiteral("provider.openai.api_key"), &value, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!store.erase(QStringLiteral("provider.openai.api_key"), &error));
    QVERIFY(!error.isEmpty());
}

void TstSecureStore::roundTripSaveLoadHasErase()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    DesktopPlatformAdapter platform;
    SecureStore store(&platform, tmp.path());
    if (!store.isAvailable()) {
        QSKIP("当前平台无系统级加密能力，跳过加解密往返");
    }

    const QString key = SecureStore::providerApiKeyId(QStringLiteral("openai"));
    const QString secret = QStringLiteral("sk-round-trip-测试-1234567890");
    QString error;

    QVERIFY2(store.save(key, secret, &error), qPrintable(error));
    QVERIFY(store.has(key));

    QString loaded;
    QVERIFY2(store.load(key, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded, secret);

    QVERIFY(store.erase(key, &error));
    QVERIFY(!store.has(key));

    QString afterErase;
    QVERIFY(!store.load(key, &afterErase, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(afterErase.isEmpty());
}

void TstSecureStore::multipleProviderKeysAreIndependent()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    DesktopPlatformAdapter platform;
    SecureStore store(&platform, tmp.path());
    if (!store.isAvailable()) {
        QSKIP("当前平台无系统级加密能力，跳过");
    }

    const QString openAiKey = SecureStore::providerApiKeyId(QStringLiteral("openai"));
    const QString anthropicKey = SecureStore::providerApiKeyId(QStringLiteral("anthropic"));
    QString error;

    QVERIFY2(store.save(openAiKey, QStringLiteral("key-openai"), &error), qPrintable(error));
    QVERIFY2(store.save(anthropicKey, QStringLiteral("key-anthropic"), &error), qPrintable(error));

    QString openAiValue;
    QString anthropicValue;
    QVERIFY2(store.load(openAiKey, &openAiValue, &error), qPrintable(error));
    QVERIFY2(store.load(anthropicKey, &anthropicValue, &error), qPrintable(error));
    QCOMPARE(openAiValue, QStringLiteral("key-openai"));
    QCOMPARE(anthropicValue, QStringLiteral("key-anthropic"));

    // 删掉一个不应影响另一个
    QVERIFY(store.erase(openAiKey, &error));
    QVERIFY(!store.has(openAiKey));
    QVERIFY(store.has(anthropicKey));
    QVERIFY2(store.load(anthropicKey, &anthropicValue, &error), qPrintable(error));
    QCOMPARE(anthropicValue, QStringLiteral("key-anthropic"));
}

void TstSecureStore::ciphertextIsNeverPlaintextOnDisk()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    DesktopPlatformAdapter platform;
    SecureStore store(&platform, tmp.path());
    if (!store.isAvailable()) {
        QSKIP("当前平台无系统级加密能力，跳过明文落盘检查");
    }

    const QString key = SecureStore::providerApiKeyId(QStringLiteral("openai"));
    const QString secret = QStringLiteral("sk-plaintext-must-not-appear-abcdef");
    QString error;
    QVERIFY2(store.save(key, secret, &error), qPrintable(error));

    QFile file(vaultPath(tmp.path(), key));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray raw = file.readAll();
    file.close();

    QVERIFY(!raw.isEmpty());
    QVERIFY(!raw.contains(secret.toUtf8()));
    QVERIFY(!raw.contains("plaintext-must-not-appear"));
}

void TstSecureStore::missingKeyReturnsError()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    DesktopPlatformAdapter platform;
    SecureStore store(&platform, tmp.path());
    QString error;
    QString value = QStringLiteral("dirty");

    QVERIFY(!store.has(SecureStore::providerApiKeyId(QStringLiteral("anthropic"))));
    QVERIFY(!store.load(SecureStore::providerApiKeyId(QStringLiteral("anthropic")), &value, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(value.isEmpty());
}

void TstSecureStore::corruptedVaultReturnsError()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    DesktopPlatformAdapter platform;
    SecureStore store(&platform, tmp.path());

    const QString key = SecureStore::providerApiKeyId(QStringLiteral("openai"));
    const QString path = vaultPath(tmp.path(), key);

    QVERIFY(QDir().mkpath(QDir(tmp.path()).filePath(QStringLiteral("secure"))));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray("not-a-valid-ciphertext-\x01\x02\x03"));
    file.close();

    QVERIFY(store.has(key)); // 文件存在

    QString error;
    QString value;
    QVERIFY(!store.load(key, &value, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(value.isEmpty());
}

void TstSecureStore::emptyValueIsRejected()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    DesktopPlatformAdapter platform;
    SecureStore store(&platform, tmp.path());
    if (!store.isAvailable()) {
        QSKIP("当前平台无系统级加密能力，跳过");
    }

    const QString key = SecureStore::providerApiKeyId(QStringLiteral("openai"));
    QString error;
    QVERIFY(!store.save(key, QString(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!store.has(key));
}

void TstSecureStore::invalidKeyNamesAreRejected()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    DesktopPlatformAdapter platform;
    SecureStore store(&platform, tmp.path());

    const QStringList invalidKeys{
        QString(),
        QStringLiteral("../escape"),
        QStringLiteral(".."),
        QStringLiteral("a/b"),
        QStringLiteral("a\\b"),
        QStringLiteral("Upper.Case"),
        QStringLiteral("with space"),
        QStringLiteral("-dash-first"),
        QString(65, QLatin1Char('a')),
    };

    for (const QString &key : invalidKeys) {
        QString error;
        QVERIFY2(!store.save(key, QStringLiteral("value"), &error), qPrintable(key));
        QVERIFY(!error.isEmpty());
        QVERIFY(!store.has(key));
    }

    // 非法 Key 不得在保险库目录留下任何文件（更不得到目录之外）
    const QString vault = QDir(tmp.path()).filePath(QStringLiteral("secure"));
    QDir dir(vault);
    const QStringList leftovers = dir.exists()
            ? dir.entryList(QDir::Files | QDir::NoDotAndDotDot)
            : QStringList{};
    QVERIFY(leftovers.isEmpty());
}

void TstSecureStore::unavailableBackendRejectsSave()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    WebPlatformAdapter platform; // Web 端不支持本地加密存储
    SecureStore store(&platform, tmp.path());
    QVERIFY(!store.isAvailable());

    const QString key = SecureStore::providerApiKeyId(QStringLiteral("openai"));
    QString error;
    QVERIFY(!store.save(key, QStringLiteral("sk-web-should-fail"), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("不支持")));

    // 关键：拒绝时必须什么都不写（绝不降级为明文）
    QVERIFY(!QFile::exists(vaultPath(tmp.path(), key)));

    QString value;
    QVERIFY(!store.load(key, &value, &error));
    QVERIFY(!error.isEmpty());
}

void TstSecureStore::eraseIsIdempotent()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    DesktopPlatformAdapter platform;
    SecureStore store(&platform, tmp.path());

    QString error;
    QVERIFY(store.erase(SecureStore::providerApiKeyId(QStringLiteral("openai")), &error));
    QVERIFY(store.erase(SecureStore::providerApiKeyId(QStringLiteral("openai")), &error));
}

void TstSecureStore::providerKeyIdNaming()
{
    QCOMPARE(SecureStore::providerApiKeyId(QStringLiteral("openai")), QStringLiteral("provider.openai.api_key"));
    QCOMPARE(SecureStore::providerApiKeyId(QStringLiteral("anthropic")),
             QStringLiteral("provider.anthropic.api_key"));
}

QTEST_GUILESS_MAIN(TstSecureStore)
#include "tst_securestore.moc"
