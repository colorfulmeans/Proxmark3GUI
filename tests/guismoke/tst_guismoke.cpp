#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QUuid>
#include <memory>

#include "common/apppaths.h"
#include "common/localdictionary.h"
#include "common/util.h"
#include "module/mifare.h"
#include "ui/localresourcesdialog.h"
#include "ui/mainwindow.h"

class GuiSmokeTest : public QObject
{
    Q_OBJECT
private:
    QString m_originalWorkingDirectory;
    QString m_testDataDirectory;

    QString createDictionary(QString* error)
    {
        const QString source = QDir(m_testDataDirectory).filePath(QStringLiteral("fixture.txt"));
        QFile file(source);
        if(!file.open(QIODevice::WriteOnly))
        {
            *error = file.errorString();
            return QString();
        }
        const QByteArray fixture("# generated test data\nabcdefabcdef\nABCDEFABCDEF\n001122334455\n");
        if(file.write(fixture) != fixture.size())
        {
            *error = file.errorString();
            return QString();
        }
        file.close();
        LocalDictionary store(QDir(m_testDataDirectory).filePath(QStringLiteral("dictionaries")));
        LocalDictionary::DictionaryInfo imported;
        if(!store.importFile(source, QString::fromUtf8("测试字典"), &imported, error))
            return QString();
        return imported.absolutePath;
    }

    QString selectThroughToolbar(MainWindow& window, QString* error)
    {
        QAction* action = window.findChild<QAction*>(QStringLiteral("localDictionariesAction"));
        if(!action)
        {
            *error = QStringLiteral("The local dictionaries toolbar action is missing.");
            return QString();
        }

        QString selected;
        bool dialogOpened = false;
        QTimer::singleShot(0, &window, [&]() {
            LocalResourcesDialog* dialog = window.findChild<LocalResourcesDialog*>();
            if(!dialog)
            {
                *error = QStringLiteral("The toolbar did not open LocalResourcesDialog.");
                // Always release an unexpected modal dialog instead of hanging
                // the test process on a changed startup/error path.
                for(QWidget* widget : QApplication::topLevelWidgets())
                    if(QDialog* other = qobject_cast<QDialog*>(widget))
                        other->reject();
                return;
            }
            dialogOpened = true;
            QTableWidget* table = dialog->findChild<QTableWidget*>(QStringLiteral("localDictionaryTable"));
            if(!table || table->rowCount() != 1)
            {
                *error = QStringLiteral("The generated dictionary was not listed.");
                dialog->reject();
                return;
            }
            QSignalSpy selectedSpy(dialog, &LocalResourcesDialog::dictionarySelected);
            table->selectRow(0);
            const bool invoked = QMetaObject::invokeMethod(dialog, "useSelectedDictionary", Qt::DirectConnection);
            if(!invoked || selectedSpy.count() != 1)
                *error = QStringLiteral("Selecting a valid dictionary did not emit its path.");
            else
                selected = selectedSpy.takeFirst().at(0).toString();
            if(dialog->isVisible())
                dialog->reject();
        });
        action->trigger(); // Exercises MainWindow::manageLocalDictionaries().
        QCoreApplication::processEvents();
        if(!dialogOpened && error->isEmpty())
            *error = QStringLiteral("The resources dialog did not open.");
        return selected;
    }

    bool setMifareProfile(MainWindow& window, const QString& filename)
    {
        Mifare* mifare = window.findChild<Mifare*>();
        QFile file(filename);
        if(!mifare || !file.open(QIODevice::ReadOnly))
            return false;
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        if(!object.value(QStringLiteral("mifare classic")).isObject())
            return false;
        mifare->setConfigMap(object.value(QStringLiteral("mifare classic")).toObject().toVariantMap());
        return true;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QApplication::setQuitOnLastWindowClosed(false);
        m_originalWorkingDirectory = QDir::currentPath();
    }

    void init()
    {
        QCoreApplication::setOrganizationName(QStringLiteral("Proxmark3GUITests"));
        QCoreApplication::setApplicationName(QStringLiteral("GuiSmoke-") +
                                             QUuid::createUuid().toString(QUuid::WithoutBraces));
        QCoreApplication::setApplicationVersion(QStringLiteral("test"));
        m_testDataDirectory = AppPaths::dataDirectory();
        QString error;
        QVERIFY2(AppPaths::initialize(&error), qPrintable(error));
        QVERIFY(QDir::setCurrent(AppPaths::workspaceDirectory()));
    }

    void cleanup()
    {
        QCoreApplication::processEvents();
        QVERIFY(QDir::setCurrent(m_originalWorkingDirectory));
        QVERIFY(m_testDataDirectory.endsWith(QCoreApplication::applicationName()));
        QVERIFY(QDir(m_testDataDirectory).removeRecursively());
        m_testDataDirectory.clear();
    }

    void mainWindowStartsAndStopsWorkerWithoutLaunchingClient()
    {
        std::unique_ptr<MainWindow> window(new MainWindow);
        QSignalSpy launchSpy(window.get(), &MainWindow::connectPM3);
        window->initUI();
        window->show();
        QCoreApplication::processEvents();

        QVERIFY(window->findChild<QToolBar*>(QStringLiteral("localToolsBar")));
        QComboBox* client = window->findChild<QComboBox*>(QStringLiteral("PM3_pathBox"));
        QLineEdit* args = window->findChild<QLineEdit*>(QStringLiteral("Set_Client_startArgsEdit"));
        QLineEdit* preload = window->findChild<QLineEdit*>(QStringLiteral("Set_Client_envScriptEdit"));
        QLineEdit* workspace = window->findChild<QLineEdit*>(QStringLiteral("Set_Client_workingDirEdit"));
        QPushButton* check = window->findChild<QPushButton*>(QStringLiteral("MF_Attack_chkButton"));
        QVERIFY(client && args && preload && workspace && check);
        QVERIFY(!client->isEditable());
        QVERIFY(args->isReadOnly());
        QVERIFY(!preload->isEnabled());
        QVERIFY(preload->text().isEmpty());
        QVERIFY(workspace->isReadOnly());
        QCOMPARE(workspace->text(), AppPaths::workspaceDirectory());
        QVERIFY(!check->isEnabled());
        QCOMPARE(launchSpy.count(), 0);

        QThread* worker = window->findChild<QThread*>();
        QVERIFY(worker);
        QTRY_VERIFY(worker->isRunning());
        QPointer<QThread> workerGuard(worker);
        QSignalSpy finishedSpy(worker, &QThread::finished);
        window->close();
        window.reset();
        QVERIFY(workerGuard.isNull());
        QCOMPARE(finishedSpy.count(), 1);
    }

    void resourcesToolbarSelectsAndPersistsDictionary()
    {
        QString error;
        const QString dictionary = createDictionary(&error);
        QVERIFY2(!dictionary.isEmpty(), qPrintable(error));
        std::unique_ptr<MainWindow> window(new MainWindow);
        QSignalSpy launchSpy(window.get(), &MainWindow::connectPM3);
        window->initUI();
        window->show();
        QCOMPARE(selectThroughToolbar(*window, &error), dictionary);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(launchSpy.count(), 0);
        window.reset();

        QSettings settings(AppPaths::settingsFile(), QSettings::IniFormat);
        QCOMPARE(settings.value(QStringLiteral("Offline/dictionary")).toString(), dictionary);
        QVERIFY(QFile::exists(dictionary));
    }

    void selectedDictionaryIsStagedAndCleanedOnWindowDestruction()
    {
        QString error;
        const QString dictionary = createDictionary(&error);
        QVERIFY2(!dictionary.isEmpty(), qPrintable(error));
        std::unique_ptr<MainWindow> window(new MainWindow);
        QSignalSpy launchSpy(window.get(), &MainWindow::connectPM3);
        window->initUI();
        QCOMPARE(selectThroughToolbar(*window, &error), dictionary);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(setMifareProfile(*window, QStringLiteral(":/config/config_rrgv4.16717.json")));

        Util* util = window->findChild<Util*>();
        QVERIFY(util);
        QSignalSpy writeSpy(util, &Util::write);
        // Set only the GUI state for this integration test. Util remains stopped
        // and PM3Process is never launched, so the check cannot send commands.
        window->onPM3StateChanged(true, QStringLiteral("test-only GUI state"));
        QVERIFY(QMetaObject::invokeMethod(window.get(), "on_MF_Attack_chkButton_clicked", Qt::DirectConnection));
        QCOMPARE(launchSpy.count(), 0);
        QCOMPARE(writeSpy.count(), 0);

        const QDir workspace(AppPaths::workspaceDirectory());
        const QStringList staged = workspace.entryList({QStringLiteral("gui-dictionary-*.dic")}, QDir::Files);
        QCOMPARE(staged.size(), 1);
        QVERIFY(QRegularExpression(QStringLiteral("\\A[A-Za-z0-9._-]+\\z")).match(staged.first()).hasMatch());
        const QString stagedPath = workspace.filePath(staged.first());
        QFile file(stagedPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("ABCDEFABCDEF\n001122334455\n"));
        file.close();
#ifndef Q_OS_WIN
        const QFile::Permissions nonOwner = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup |
                                           QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
        QVERIFY(!(QFileInfo(AppPaths::workspaceDirectory()).permissions() & nonOwner));
#endif

        window.reset();
        QVERIFY(!QFile::exists(stagedPath));
        QVERIFY(QFile::exists(dictionary));
    }

    void unsupportedProfileDoesNotStageOrSendDictionary()
    {
        QString error;
        QVERIFY2(!createDictionary(&error).isEmpty(), qPrintable(error));
        std::unique_ptr<MainWindow> window(new MainWindow);
        window->initUI();
        QVERIFY2(!selectThroughToolbar(*window, &error).isEmpty(), qPrintable(error));
        QVERIFY(setMifareProfile(*window, QStringLiteral(":/config/config_official.json")));
        Util* util = window->findChild<Util*>();
        QVERIFY(util);
        QSignalSpy writeSpy(util, &Util::write);
        bool rejectedProfile = false;
        QTimer::singleShot(0, window.get(), [&]() {
            if(QMessageBox* message = window->findChild<QMessageBox*>())
            {
                rejectedProfile = message->text().contains(QStringLiteral("dictionary option"));
                message->accept();
            }
        });
        window->onPM3StateChanged(true, QStringLiteral("test-only GUI state"));
        QVERIFY(QMetaObject::invokeMethod(window.get(), "on_MF_Attack_chkButton_clicked", Qt::DirectConnection));
        QVERIFY(rejectedProfile);
        QCOMPARE(writeSpy.count(), 0);
        const QStringList staged = QDir(AppPaths::workspaceDirectory()).entryList(
            {QStringLiteral("gui-dictionary-*.dic")}, QDir::Files);
        QVERIFY(staged.isEmpty());
    }
};

int main(int argc, char** argv)
{
    if(qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication application(argc, argv);
    GuiSmokeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_guismoke.moc"
