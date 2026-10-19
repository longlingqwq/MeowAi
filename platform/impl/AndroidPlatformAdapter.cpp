#include "platform/impl/AndroidPlatformAdapter.h"

#include <QDir>
#include <QStandardPaths>

#if defined(Q_OS_ANDROID)
#include <QJniEnvironment>
#include <QJniObject>
#endif

namespace {

bool notImplemented(QString *error, const QString &what)
{
    if (error) {
        *error = QStringLiteral("Android 平台层 %1 尚未实现").arg(what);
    }
    return false;
}

bool unsupported(QString *error, const QString &what)
{
    if (error) {
        *error = QStringLiteral("当前构建不含 Android 密钥库能力：%1").arg(what);
    }
    return false;
}

#if defined(Q_OS_ANDROID)

/// AndroidKeyStore 中的密钥别名（与 storage/ 的保险库文件名无关）
const char *kSecretKeyAlias = "meowai.secret.v1";
/// AES-GCM 认证标签长度（bit）
constexpr int kGcmTagBits = 128;

jbyteArray toJByteArray(QJniEnvironment &env, const QByteArray &data)
{
    jbyteArray arr = env->NewByteArray(static_cast<jsize>(data.size()));
    if (!arr) {
        return nullptr;
    }
    env->SetByteArrayRegion(arr, 0, static_cast<jsize>(data.size()),
                            reinterpret_cast<const jbyte *>(data.constData()));
    return arr;
}

QByteArray fromJByteArray(QJniEnvironment &env, jbyteArray arr)
{
    if (!arr) {
        return {};
    }
    const jsize len = env->GetArrayLength(arr);
    QByteArray out(static_cast<int>(len), Qt::Uninitialized);
    env->GetByteArrayRegion(arr, 0, len, reinterpret_cast<jbyte *>(out.data()));
    return out;
}

/// 构造单元素 String[]，用于 setBlockModes / setEncryptionPaddings（Java 可变参数）
jobjectArray singleStringArray(JNIEnv *env, const char *value)
{
    jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass) {
        return nullptr;
    }
    jstring str = env->NewStringUTF(value);
    jobjectArray arr = env->NewObjectArray(1, stringClass, str);
    env->DeleteLocalRef(str);
    env->DeleteLocalRef(stringClass);
    return arr;
}

/// 从 AndroidKeyStore 取出 AES 密钥；首次使用时生成（含 GCM/NoPadding/256bit 参数）
bool loadOrCreateSecretKey(QJniEnvironment &env, QJniObject *key, QString *error)
{
    QJniObject keyStore = QJniObject::callStaticObjectMethod(
        "java/security/KeyStore", "getInstance", "(Ljava/lang/String;)Ljava/security/KeyStore;",
        QJniObject::fromString(QStringLiteral("AndroidKeyStore")).object<jstring>());
    if (!keyStore.isValid()) {
        if (error) {
            *error = QStringLiteral("无法加载 AndroidKeyStore");
        }
        return false;
    }

    keyStore.callMethod<void>("load", "(Ljava/security/KeyStore$LoadStoreParameter;)V", nullptr);

    const QJniObject alias = QJniObject::fromString(QString::fromLatin1(kSecretKeyAlias));
    const jboolean exists = keyStore.callMethod<jboolean>("containsAlias", "(Ljava/lang/String;)Z", alias.object<jstring>());

    if (!exists) {
        QJniObject generator = QJniObject::callStaticObjectMethod(
            "javax/crypto/KeyGenerator", "getInstance",
            "(Ljava/lang/String;Ljava/lang/String;)Ljavax/crypto/KeyGenerator;",
            QJniObject::fromString(QStringLiteral("AES")).object<jstring>(),
            QJniObject::fromString(QStringLiteral("AndroidKeyStore")).object<jstring>());
        if (!generator.isValid()) {
            if (error) {
                *error = QStringLiteral("无法创建 AndroidKeyStore 密钥生成器");
            }
            return false;
        }

        // KeyGenParameterSpec.Builder(alias, PURPOSE_ENCRYPT | PURPOSE_DECRYPT = 3)
        QJniObject builder("android/security/keystore/KeyGenParameterSpec$Builder",
                           "(Ljava/lang/String;I)V", alias.object<jstring>(), 3);
        if (!builder.isValid()) {
            if (error) {
                *error = QStringLiteral("无法创建密钥参数构建器");
            }
            return false;
        }

        jobjectArray modes = singleStringArray(env.jniEnv(), "GCM");
        jobjectArray paddings = singleStringArray(env.jniEnv(), "NoPadding");
        if (!modes || !paddings) {
            if (error) {
                *error = QStringLiteral("无法构造密钥参数（GCM / NoPadding）");
            }
            return false;
        }

        builder = builder.callObjectMethod("setBlockModes",
                                           "([Ljava/lang/String;)Landroid/security/keystore/KeyGenParameterSpec$Builder;",
                                           modes);
        env->DeleteLocalRef(modes);

        builder = builder.callObjectMethod("setEncryptionPaddings",
                                           "([Ljava/lang/String;)Landroid/security/keystore/KeyGenParameterSpec$Builder;",
                                           paddings);
        env->DeleteLocalRef(paddings);
        builder = builder.callObjectMethod("setKeySize",
                                           "(I)Landroid/security/keystore/KeyGenParameterSpec$Builder;", 256);
        builder = builder.callObjectMethod("setUserAuthenticationRequired",
                                           "(Z)Landroid/security/keystore/KeyGenParameterSpec$Builder;", JNI_FALSE);

        QJniObject spec = builder.callObjectMethod(
            "build", "()Landroid/security/keystore/KeyGenParameterSpec;");
        generator.callMethod<void>("init", "(Ljava/security/spec/AlgorithmParameterSpec;)V", spec.object<jobject>());
        QJniObject generated = generator.callObjectMethod("generateKey", "()Ljavax/crypto/SecretKey;");
        if (!generated.isValid()) {
            if (error) {
                *error = QStringLiteral("AndroidKeyStore 密钥生成失败");
            }
            return false;
        }
    }

    QJniObject fetched = keyStore.callMethod<QJniObject>(
        "getKey", "(Ljava/lang/String;[C)Ljava/security/Key;", alias.object<jstring>(), nullptr);
    if (!fetched.isValid()) {
        if (error) {
            *error = QStringLiteral("AndroidKeyStore 中未找到可用密钥");
        }
        return false;
    }

    if (env.checkAndClearExceptions(QJniEnvironment::OutputMode::Silent)) {
        if (error) {
            *error = QStringLiteral("AndroidKeyStore 调用异常");
        }
        return false;
    }

    *key = std::move(fetched);
    return true;
}

QJniObject aesGcmCipher()
{
    return QJniObject::callStaticObjectMethod(
        "javax/crypto/Cipher", "getInstance", "(Ljava/lang/String;)Ljavax/crypto/Cipher;",
        QJniObject::fromString(QStringLiteral("AES/GCM/NoPadding")).object<jstring>());
}

#endif // Q_OS_ANDROID

} // namespace

QString AndroidPlatformAdapter::platformId() const
{
    return QStringLiteral("android");
}

QString AndroidPlatformAdapter::appDataRoot() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return path.isEmpty() ? QDir::homePath() + QStringLiteral("/.meowai") : path;
}

QString AndroidPlatformAdapter::cacheRoot() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return path.isEmpty() ? appDataRoot() + QStringLiteral("/cache") : path;
}

QString AndroidPlatformAdapter::bundledSkillsRoot() const
{
    return QStringLiteral(":/skills");
}

bool AndroidPlatformAdapter::isNetworkAvailable() const
{
    return true;
}

bool AndroidPlatformAdapter::supportsSecretEncryption() const
{
#if defined(Q_OS_ANDROID)
    return true; // AndroidKeyStore 可用
#else
    return false;
#endif
}

bool AndroidPlatformAdapter::protectSecret(const QByteArray &plain, QByteArray *blob, QString *error)
{
    if (!blob) {
        if (error) {
            *error = QStringLiteral("内部错误：输出缓冲区为空");
        }
        return false;
    }
    blob->clear();

    if (plain.isEmpty()) {
        if (error) {
            *error = QStringLiteral("待加密内容为空");
        }
        return false;
    }

#if defined(Q_OS_ANDROID)
    QJniEnvironment env;
    QJniObject key;
    if (!loadOrCreateSecretKey(env, &key, error)) {
        return false;
    }

    QJniObject cipher = aesGcmCipher();
    if (!cipher.isValid()) {
        if (error) {
            *error = QStringLiteral("无法创建 AES/GCM 密码器");
        }
        return false;
    }

    // ENCRYPT_MODE = 1；Keystore 侧随机生成 IV
    cipher.callMethod<void>("init", "(ILjava/security/Key;)V", 1, key.object<jobject>());

    QJniObject ivArray = cipher.callObjectMethod("getIV", "()[B");
    const QByteArray iv = fromJByteArray(env, ivArray.object<jbyteArray>());
    if (iv.isEmpty()) {
        if (error) {
            *error = QStringLiteral("AndroidKeyStore 未返回 IV");
        }
        return false;
    }

    jbyteArray plainArray = toJByteArray(env, plain);
    if (!plainArray) {
        if (error) {
            *error = QStringLiteral("输入数据转换失败");
        }
        return false;
    }

    QJniObject encrypted = cipher.callObjectMethod("doFinal", "([B)[B", plainArray);
    env->DeleteLocalRef(plainArray);
    if (env.checkAndClearExceptions(QJniEnvironment::OutputMode::Silent) || !encrypted.isValid()) {
        if (error) {
            *error = QStringLiteral("AndroidKeyStore 加密失败");
        }
        return false;
    }

    // 密文格式：IV || ciphertext（IV 长度随实现变化，解密时按记录长度切分）
    *blob = QByteArray(1, static_cast<char>(iv.size())) + iv
            + fromJByteArray(env, encrypted.object<jbyteArray>());
    return true;
#else
    return unsupported(error, QStringLiteral("Android Keystore 加密"));
#endif
}

bool AndroidPlatformAdapter::unprotectSecret(const QByteArray &blob, QByteArray *plain, QString *error)
{
    if (!plain) {
        if (error) {
            *error = QStringLiteral("内部错误：输出缓冲区为空");
        }
        return false;
    }
    plain->clear();

    if (blob.isEmpty()) {
        if (error) {
            *error = QStringLiteral("密文为空，无法解密");
        }
        return false;
    }

#if defined(Q_OS_ANDROID)
    const int ivLen = static_cast<unsigned char>(blob.at(0));
    if (ivLen <= 0 || blob.size() <= 1 + ivLen) {
        if (error) {
            *error = QStringLiteral("密文格式非法或已损坏");
        }
        return false;
    }
    const QByteArray iv = blob.mid(1, ivLen);
    const QByteArray body = blob.mid(1 + ivLen);

    QJniEnvironment env;
    QJniObject key;
    if (!loadOrCreateSecretKey(env, &key, error)) {
        return false;
    }

    QJniObject cipher = aesGcmCipher();
    if (!cipher.isValid()) {
        if (error) {
            *error = QStringLiteral("无法创建 AES/GCM 密码器");
        }
        return false;
    }

    jbyteArray ivArray = toJByteArray(env, iv);
    if (!ivArray) {
        if (error) {
            *error = QStringLiteral("IV 转换失败");
        }
        return false;
    }
    // DECRYPT_MODE = 2，需显式提供 IV（GCMParameterSpec(tagBits, iv)）
    QJniObject gcmSpec("javax/crypto/spec/GCMParameterSpec", "(I[B)V", kGcmTagBits, ivArray);
    cipher.callMethod<void>("init",
                            "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V",
                            2, key.object<jobject>(), gcmSpec.object<jobject>());
    env->DeleteLocalRef(ivArray);

    jbyteArray bodyArray = toJByteArray(env, body);
    if (!bodyArray) {
        if (error) {
            *error = QStringLiteral("密文转换失败");
        }
        return false;
    }
    QJniObject decrypted = cipher.callObjectMethod("doFinal", "([B)[B", bodyArray);
    env->DeleteLocalRef(bodyArray);

    if (env.checkAndClearExceptions(QJniEnvironment::OutputMode::Silent) || !decrypted.isValid()) {
        if (error) {
            *error = QStringLiteral("密文解密失败：文件可能已损坏（AndroidKeyStore 校验未通过）");
        }
        return false;
    }

    *plain = fromJByteArray(env, decrypted.object<jbyteArray>());
    return true;
#else
    return unsupported(error, QStringLiteral("Android Keystore 解密"));
#endif
}

bool AndroidPlatformAdapter::supportsSelfUpdate() const
{
    return true;
}

bool AndroidPlatformAdapter::installUpdate(const QString &packagePath, QString *error)
{
    Q_UNUSED(packagePath)
    return notImplemented(error, QStringLiteral("Apk 覆盖安装"));
}
