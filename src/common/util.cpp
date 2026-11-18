#include "util.h"
#include "offlinepolicy.h"
#include <QMessageBox>

Util::ClientType Util::clientType = CLIENTTYPE_OFFICIAL;

int Util::rawTabIndex = 0;
QDockWidget* Util::rawDockPtr = nullptr;
Ui::MainWindow* Util::ui = nullptr;


Util::Util(QObject *parent) : QObject(parent)
{
    isRequiringOutput = false;
    requiredOutput = new QString();
    isRunning = false;
    outputTimer.start();
    qRegisterMetaType<Util::ClientType>("Util::ClientType");
}


void Util::processOutput(const QString& output)
{
//    qDebug() << "Util::processOutput:" << output;
    if(isRequiringOutput)
    {
        requiredOutput->append(output);
        outputTimer.restart();
    }
    emit refreshOutput(output);
}

void Util::execCMD(const QString& cmd)
{
    QString error;
    if(!OfflinePolicy::validateCommand(cmd, &error))
    {
        emit operationBlocked(error);
        return;
    }
    if(isRunning)
        emit write(cmd + "\n");
}

QString Util::execCMDWithOutput(const QString& cmd, ReturnTrigger trigger, bool rawOutput)
{
    QString error;
    if(!OfflinePolicy::validateCommand(cmd, &error))
    {
        emit operationBlocked(error);
        return QString();
    }
    if(!isRunning || isRequiringOutput)
        return QString();
    bool isResultFound = false;
    QRegularExpression re;
    re.setPatternOptions(QRegularExpression::DotMatchesEverythingOption);
    isRequiringOutput = true;
    requiredOutput->clear();
    outputTimer.restart();
    emit write(cmd + "\n");
    while(isRunning && outputTimer.elapsed() < qint64(trigger.waitTime))
    {
        for(const QString& expected : trigger.expectedOutputs)
        {
            re.setPattern(expected);
            if(re.match(*requiredOutput).hasMatch())
            {
                isResultFound = true;
                break;
            }
        }
        if(isResultFound)
        {
            delay(200);
            break;
        }
        // Wait for events without spinning a full CPU core. Monotonic elapsed
        // time also avoids the previous midnight rollover problem.
        delay(20);
    }
    isRequiringOutput = false;
    return (trigger.expectedOutputs.isEmpty() || isResultFound || rawOutput) ? *requiredOutput : QString();
}

void Util::delay(unsigned int msec)
{
    QEventLoop loop;
    QTimer::singleShot(int(msec), &loop, &QEventLoop::quit);
    loop.exec(QEventLoop::AllEvents);
}

Util::ClientType Util::getClientType()
{
    return Util::clientType;
}

void Util::setClientType(Util::ClientType clientType)
{
    Util::clientType = clientType;
}

void Util::setRunningState(bool st)
{
    this->isRunning = st;
}

bool Util::chooseLanguage(QSettings* guiSettings, QMainWindow* window)
{
    // make sure the GUISettings is not in any group
    QSettings* langSettings = new QSettings(":/i18n/languages.ini", QSettings::IniFormat);
    QMap<QString, QString> langMap;
    langSettings->setIniCodec("UTF-8");
    langSettings->beginGroup("Languages");
    QStringList langList = langSettings->allKeys();
    for(int i = 0; i < langList.size(); i++)
        langMap.insert(langSettings->value(langList[i]).toString(), langList[i]);
    langMap.insert(tr("Load from external file"), "(ext)");
    langSettings->endGroup();
    delete langSettings;
    bool isOk = false;
    QString selectedText = QInputDialog::getItem(window, "", tr("Choose a language:"), langMap.keys(), 0, false, &isOk);
    if(!isOk)
        return false;
    if(langMap[selectedText] == "(ext)")
    {
        QString extPath = QFileDialog::getOpenFileName(window, tr("Select the translation file:"));
        if(extPath.isEmpty())
            return false;

        QString localTranslation, error;
        if(!OfflinePolicy::validateLocalFilePath(extPath, &localTranslation, &error))
        {
            QMessageBox::warning(window, tr("Local translation"), error);
            return false;
        }

        guiSettings->beginGroup("language");
        guiSettings->setValue("extPath", localTranslation);
        guiSettings->endGroup();
    }

    guiSettings->beginGroup("language");
    guiSettings->setValue("name", langMap[selectedText]);
    guiSettings->endGroup();
    guiSettings->sync();

    return isOk;
}

void Util::gotoRawTab()
{
    Util::ui->funcTab->setCurrentIndex(Util::rawTabIndex);
    Util::rawDockPtr->setVisible(true);
    Util::rawDockPtr->raise();
}

void Util::setUI(Ui::MainWindow *ui)
{
    Util::ui = ui;
}


void Util::setRawTab(QDockWidget *dockPtr, int tabIndex)
{
    Util::rawDockPtr = dockPtr;
    Util::rawTabIndex = tabIndex;
}
