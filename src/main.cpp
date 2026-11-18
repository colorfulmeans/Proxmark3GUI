#include "ui/mainwindow.h"

#include <QApplication>
#include <QSettings>
#include <QTranslator>
#include <QMessageBox>
#include <QTextCodec>
#include <QDir>
#include <QLocale>
#include "common/apppaths.h"
#include "common/offlinepolicy.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    QApplication a(argc, argv);
    a.setOrganizationName("colorfulmeans");
    a.setApplicationName("Proxmark3GUIOffline");
    a.setApplicationVersion("0.3.0-offline-dev");
    QString pathError;
    if(!AppPaths::initialize(&pathError) || !QDir::setCurrent(AppPaths::workspaceDirectory()))
    {
        QMessageBox::critical(nullptr, "Local data directory", pathError.isEmpty()
                              ? "Cannot enter the local workspace." : pathError);
        return 1;
    }

    QSettings* settings = new QSettings(AppPaths::settingsFile(), QSettings::IniFormat);
    settings->setIniCodec("UTF-8");
    settings->beginGroup("language");
    QString languageFile = settings->value("extPath").toString();
    QString languageName = settings->value("name").toString();
    settings->endGroup();
    settings->beginGroup("UI");
    QString theme = settings->value("Theme_Name").toString();
    settings->endGroup();
    if(languageName.isEmpty())
    {
        languageName = QLocale::system().language() == QLocale::Chinese ? "zh_CN" : "en_US";
        settings->setValue("language/name", languageName);
    }
    if(languageName == "(ext)")
    {
        settings->beginGroup("language");
        languageFile = settings->value("extPath").toString();
        settings->endGroup();
        QString localTranslation;
        if(OfflinePolicy::validateLocalFilePath(languageFile, &localTranslation))
            languageFile = localTranslation;
        else
            languageFile = ":/i18n/en_US.qm";
    }
    else
        languageFile = ":/i18n/" + languageName + ".qm";

    // Note that the translator must be created before the application's widgets.
    QTranslator* translator = new QTranslator();
    if(translator->load(languageFile))
        a.installTranslator(translator);
    else
        QMessageBox::information(nullptr, "Error", "Can't load " + languageFile + " as translation file.");

    QFile* themeFile = new QFile();
    QTextStream* themeStream = new QTextStream();
    QString qssString = a.styleSheet(); // default behavior
    if(theme == "(none)")
        ;
    else if(theme == "qdss_dark")
    {
        themeFile->setFileName(":/qdarkstyle/dark/darkstyle.qss");
        themeFile->open(QFile::ReadOnly | QFile::Text);
        themeStream->setDevice(themeFile);
        qssString = themeStream->readAll();
    }
    else if(theme == "qdss_light")
    {
        themeFile->setFileName(":/qdarkstyle/light/lightstyle.qss");
        themeFile->open(QFile::ReadOnly | QFile::Text);
        themeStream->setDevice(themeFile);
        qssString = themeStream->readAll();
    }
    a.setStyleSheet(qssString);
    delete themeFile;
    delete themeStream;
    themeFile = nullptr;
    themeStream = nullptr;

    delete settings;
    settings = nullptr;

    MainWindow w;
    w.initUI();
    w.show();
    return a.exec();
}


