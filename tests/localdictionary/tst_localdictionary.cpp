#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "localdictionary.h"

class LocalDictionaryTest : public QObject
{
    Q_OBJECT
private:
    static bool putFile(const QString& path, const QByteArray& data)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }

    static QByteArray contents(const QString& path)
    {
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly))
            return QByteArray();
        return file.readAll();
    }

private slots:
    void parsesCommentsWhitespaceAndBom()
    {
        const QString text = QString(QChar(0xfeff)) +
            QStringLiteral("# header\r\n\r\n  a0b1c2d3e4f5  # note\n; comment\n"
                           "// comment\n001122334455; another note\nabcdefabcdef // note\n");
        const LocalDictionary::ParseResult result = LocalDictionary::parse(text);
        QVERIFY(result.isValid());
        QCOMPARE(result.keys, QStringList({QStringLiteral("A0B1C2D3E4F5"), QStringLiteral("001122334455"), QStringLiteral("ABCDEFABCDEF")}));
        QCOMPARE(result.duplicateCount, 0);
    }

    void deduplicatesCaseInsensitivelyAndPreservesOrder()
    {
        const LocalDictionary::ParseResult result = LocalDictionary::parse(
            QStringLiteral("abcdefabcdef\n001122334455\nABCDEFABCDEF\nabcdefABCDEF\n"));
        QVERIFY(result.isValid());
        QCOMPARE(result.keys, QStringList({QStringLiteral("ABCDEFABCDEF"), QStringLiteral("001122334455")}));
        QCOMPARE(result.duplicateCount, 2);
    }

    void reportsEveryInvalidLine()
    {
        const LocalDictionary::ParseResult result = LocalDictionary::parse(
            QStringLiteral("# header\n001122334455\n00112233445\n0011223344556\nGG1122334455\n001122 334455\n"));
        QVERIFY(!result.isValid());
        QCOMPARE(result.invalidLines, QList<int>({3, 4, 5, 6}));
        QVERIFY(!result.errorString().isEmpty());
        QCOMPARE(result.keys.size(), 1);
    }

    void rejectsNonLocalPaths_data()
    {
        QTest::addColumn<QString>("path");
        QTest::newRow("unc") << QStringLiteral("\\\\server\\share\\keys.dic");
        QTest::newRow("forward-unc") << QStringLiteral("//server/share/keys.dic");
        QTest::newRow("extended-device") << QStringLiteral("\\\\?\\C:\\keys.dic");
        QTest::newRow("device") << QStringLiteral("\\\\.\\PhysicalDrive0");
        QTest::newRow("http") << QStringLiteral("http://example.invalid/keys.dic");
        QTest::newRow("https") << QStringLiteral("https://example.invalid/keys.dic");
        QTest::newRow("file-url") << QStringLiteral("file:///tmp/keys.dic");
        QTest::newRow("smb") << QStringLiteral("smb://server/share/keys.dic");
        QTest::newRow("short-url-scheme") << QStringLiteral("x://server/keys.dic");
        QTest::newRow("drive-relative") << QStringLiteral("C:keys.dic");
        QTest::newRow("newline") << QStringLiteral("keys\n.dic");
    }

    void rejectsNonLocalPaths()
    {
        QFETCH(QString, path);
        QString absolute, error;
        QVERIFY(!LocalDictionary::validateLocalPath(path, &absolute, &error));
        QVERIFY(absolute.isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void acceptsLocalPathsAndReturnsAbsolutePath()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = temporary.filePath(QString::fromUtf8("本地 字典.dic"));
        QString absolute, error;
        QVERIFY2(LocalDictionary::validateLocalPath(path, &absolute, &error), qPrintable(error));
        QVERIFY(QDir::isAbsolutePath(absolute));
        QCOMPARE(absolute, path);
        QVERIFY2(LocalDictionary::validateLocalPath(QStringLiteral("relative-keys.dic"), &absolute, &error), qPrintable(error));
        QVERIFY(QDir::isAbsolutePath(absolute));
    }

    void rejectsUnsafeNames_data()
    {
        QTest::addColumn<QString>("name");
        for(const QString& name : {QStringLiteral(""), QStringLiteral(".."), QStringLiteral("../escape"),
                                  QStringLiteral("a/b"), QStringLiteral("a\\b"), QStringLiteral("a:b"),
                                  QStringLiteral("CON"), QStringLiteral("nul.txt"), QStringLiteral("COM1"),
                                  QStringLiteral("trailing."), QStringLiteral(" space"), QStringLiteral("tab\tname")})
            QTest::newRow(name.isEmpty() ? "empty" : qPrintable(name)) << name;
    }

    void rejectsUnsafeNames()
    {
        QFETCH(QString, name);
        QString error;
        QVERIFY(!LocalDictionary::validateName(name, &error));
        QVERIFY(!error.isEmpty());
    }

    void savesNormalizedKeysAndReadsThemBack()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = temporary.filePath(QStringLiteral("roundtrip.dic"));
        QString error;
        QVERIFY2(LocalDictionary::writeFile(path, {QStringLiteral("abcdefabcdef"), QStringLiteral("001122334455"),
                                                   QStringLiteral("ABCDEFABCDEF")}, &error), qPrintable(error));
        QCOMPARE(contents(path), QByteArray("ABCDEFABCDEF\n001122334455\n"));
        LocalDictionary::ParseResult parsed;
        QVERIFY2(LocalDictionary::readFile(path, &parsed, &error), qPrintable(error));
        QCOMPARE(parsed.keys.size(), 2);
        QCOMPARE(parsed.duplicateCount, 0);
    }

    void invalidSavePreservesExistingFile()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = temporary.filePath(QStringLiteral("existing.dic"));
        const QByteArray original("001122334455\n");
        QVERIFY(putFile(path, original));
        QString error;
        QVERIFY(!LocalDictionary::writeFile(path, {QStringLiteral("ABCDEFABCDEF"), QStringLiteral("invalid")}, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(contents(path), original);
        QVERIFY(!LocalDictionary::writeFile(path, QStringList(), &error));
        QCOMPARE(contents(path), original);
    }

    void importsWithoutChangingSourceOrOverwriting()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.txt"));
        const QByteArray original("# notes\nabcdefabcdef\nABCDEFABCDEF\n001122334455\n");
        QVERIFY(putFile(source, original));
        LocalDictionary store(temporary.filePath(QStringLiteral("managed")));
        LocalDictionary::DictionaryInfo info;
        QString error;
        QVERIFY2(store.importFile(source, QString::fromUtf8("测试字典"), &info, &error), qPrintable(error));
        QCOMPARE(info.keyCount, 2);
        QCOMPARE(info.duplicateCount, 1);
        QVERIFY(QDir::isAbsolutePath(info.absolutePath));
        QCOMPARE(contents(source), original);
        const QByteArray saved = contents(info.absolutePath);
        QVERIFY(!store.importFile(source, info.name, nullptr, &error));
        QCOMPARE(contents(info.absolutePath), saved);
    }

    void invalidImportIsAllOrNothing()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("invalid.txt"));
        QVERIFY(putFile(source, QByteArray("001122334455\nnot-a-key\nABCDEFABCDEF\n")));
        LocalDictionary store(temporary.filePath(QStringLiteral("managed")));
        QString error;
        QVERIFY(!store.importFile(source, QStringLiteral("rejected"), nullptr, &error));
        QVERIFY(error.contains(QStringLiteral("2")));
        QVERIFY(!QFile::exists(temporary.filePath(QStringLiteral("managed/rejected.dic"))));
    }

    void listsInvalidFilesInsteadOfSilentlyDroppingThem()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        LocalDictionary store(temporary.path());
        QVERIFY(putFile(temporary.filePath(QStringLiteral("good.dic")), QByteArray("001122334455\n")));
        QVERIFY(putFile(temporary.filePath(QStringLiteral("bad.dic")), QByteArray("invalid\n")));
        QString error;
        const QList<LocalDictionary::DictionaryInfo> entries = store.dictionaries(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.first().name, QStringLiteral("bad"));
        QVERIFY(!entries.first().isValid());
        QVERIFY(entries.last().isValid());
        QCOMPARE(entries.last().keyCount, 1);
    }

    void rejectsEmptyInvalidUtf8AndOversizedFiles()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = temporary.filePath(QStringLiteral("input.dic"));
        QString error;
        LocalDictionary::ParseResult parsed;
        QVERIFY(putFile(path, QByteArray("# comments only\n")));
        QVERIFY(!LocalDictionary::readFile(path, &parsed, &error));
        QVERIFY(putFile(path, QByteArray("001122334455\n\xff\n")));
        QVERIFY(!LocalDictionary::readFile(path, &parsed, &error));
        QVERIFY(putFile(path, QByteArray("001122334455\n\xc3")));
        QVERIFY(!LocalDictionary::readFile(path, &parsed, &error));
        QFile oversized(path);
        QVERIFY(oversized.open(QIODevice::WriteOnly));
        QVERIFY(oversized.resize(LocalDictionary::maximumFileSize() + 1));
        oversized.close();
        QVERIFY(!LocalDictionary::readFile(path, &parsed, &error));
        QVERIFY(!error.isEmpty());
    }

    void exportsValidatedNormalizedContent()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("source.dic"));
        const QString output = temporary.filePath(QStringLiteral("output.dic"));
        QVERIFY(putFile(source, QByteArray("abcdefabcdef\nABCDEFABCDEF\n")));
        QString error;
        QVERIFY2(LocalDictionary::exportFile(source, output, &error), qPrintable(error));
        QCOMPARE(contents(output), QByteArray("ABCDEFABCDEF\n"));
        QVERIFY(putFile(source, QByteArray("invalid\n")));
        QVERIFY(!LocalDictionary::exportFile(source, output, &error));
        QCOMPARE(contents(output), QByteArray("ABCDEFABCDEF\n"));
    }

    void failedSaveDoesNotReplaceADirectory()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString destination = temporary.filePath(QStringLiteral("folder.dic"));
        QVERIFY(QDir().mkpath(destination));
        const QString marker = QDir(destination).filePath(QStringLiteral("keep.txt"));
        QVERIFY(putFile(marker, QByteArray("keep")));
        QString error;
        QVERIFY(!LocalDictionary::writeFile(destination, {QStringLiteral("001122334455")}, &error));
        QCOMPARE(contents(marker), QByteArray("keep"));
    }

    void rejectsSymlinkFilesAndParents()
    {
#ifdef Q_OS_WIN
        QSKIP("QFile::link creates Windows shortcuts, not filesystem symlinks.");
#else
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString source = temporary.filePath(QStringLiteral("actual.dic"));
        const QString link = temporary.filePath(QStringLiteral("linked.dic"));
        QVERIFY(putFile(source, QByteArray("001122334455\n")));
        QVERIFY(QFile::link(source, link));
        QString error;
        QVERIFY(!LocalDictionary::validateLocalPath(link, nullptr, &error));
        LocalDictionary::ParseResult parsed;
        QVERIFY(!LocalDictionary::readFile(link, &parsed, &error));
        const QString directory = temporary.filePath(QStringLiteral("real"));
        const QString directoryLink = temporary.filePath(QStringLiteral("alias"));
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(QFile::link(directory, directoryLink));
        QVERIFY(!LocalDictionary::writeFile(QDir(directoryLink).filePath(QStringLiteral("new.dic")),
                                           {QStringLiteral("001122334455")}, &error));
        QVERIFY(!QFile::exists(QDir(directory).filePath(QStringLiteral("new.dic"))));
#endif
    }
};

QTEST_GUILESS_MAIN(LocalDictionaryTest)
#include "tst_localdictionary.moc"
