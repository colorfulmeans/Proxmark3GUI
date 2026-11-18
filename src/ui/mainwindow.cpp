#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QJsonDocument>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QJsonParseError>
#include <QSaveFile>
#include <QToolBar>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QUuid>
#include "common/apppaths.h"
#include "common/offlinepolicy.h"
#include "common/localdictionary.h"
#include "ui/localresourcesdialog.h"

namespace {
QByteArray fileDigest(const QString& path, QString* error)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error) *error = file.errorString();
        return QByteArray();
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&file))
    {
        if(error) *error = QStringLiteral("Cannot read the complete client executable.");
        return QByteArray();
    }
    return hash.result().toHex();
}
QString clientPinKey(const QString& path)
{
    return "Offline/clients/" + QString::fromLatin1(
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
}
}

MainWindow::MainWindow(QWidget *parent):
    QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    dockAllWindows = new QAction(tr("Dock all windows"), this);
    myInfo = new QAction(tr("About offline GUI"), this);
    currVersion = new QAction(tr("Ver: ") + QApplication::applicationVersion(), this); // ignore the 4th version number
    checkUpdate = new QAction(tr("Client details"), this);
    connect(dockAllWindows, &QAction::triggered, [ = ]()
    {
        for(int i = 0; i < dockList.size(); i++)
            dockList[i]->setFloating(false);
    });
    connect(myInfo, &QAction::triggered, [ = ]()
    {
        QMessageBox::information(this, tr("About offline GUI"),
            tr("Proxmark3GUI Offline\nBased on wh201906/Proxmark3GUI.\n"
               "Local serial connections and local data tools. No account, update check or upload service.\n"
               "Use a trusted local PM3 client; the GUI input policy is not an operating-system sandbox."));
    });
    connect(checkUpdate, &QAction::triggered, [ = ]()
    {
        showClientDetails();
    });

    settings = new QSettings(AppPaths::settingsFile(), QSettings::IniFormat);
    settings->setIniCodec("UTF-8");

    pm3Thread = new QThread(this);
    connect(QApplication::instance(), &QApplication::aboutToQuit, pm3Thread, &QThread::quit);
    pm3 = new PM3Process(pm3Thread);
    connect(pm3Thread, &QThread::finished, pm3, &PM3Process::deleteLater);
    pm3Thread->start();
    pm3state = false;
    clientWorkingDir = new QDir;

    util = new Util(this);
    Util::setUI(ui);
    mifare = new Mifare(ui, util, this);
    lf = new LF(ui, util, this);
    t55xxTab = new T55xxTab(util);
    connect(lf, &LF::LFfreqConfChanged, this, &MainWindow::onLFfreqConfChanged);
    connect(t55xxTab, &T55xxTab::setParentGUIState, this, &MainWindow::setState);
    ui->funcTab->insertTab(2, t55xxTab, tr("T55xx"));

    keyEventFilter = new MyEventFilter(QEvent::KeyPress);
    resizeEventFilter = new MyEventFilter(QEvent::Resize);

    // hide unused tabs
//    ui->funcTab->removeTab(1);
    ui->funcTab->removeTab(3);

    portSearchTimer = new QTimer(this);
    portSearchTimer->setInterval(2000);
    connect(portSearchTimer, &QTimer::timeout, this, &MainWindow::on_portSearchTimer_timeout);
    portSearchTimer->start();

    contextMenu = new QMenu();
    contextMenu->addAction(dockAllWindows);
    contextMenu->addSeparator();
    contextMenu->addAction(myInfo);
    currVersion->setEnabled(false);
    contextMenu->addAction(currVersion);
    contextMenu->addAction(checkUpdate);

}

MainWindow::~MainWindow()
{
    if(pm3Thread->isRunning())
    {
        QMetaObject::invokeMethod(pm3, "killPM3", Qt::BlockingQueuedConnection);
        pm3Thread->quit();
        pm3Thread->wait();
    }
    delete pm3Thread;
    // Worker signals are queued while the GUI thread waits in this destructor.
    // Do not rely on onPM3StateChanged(false) running during window teardown.
    for(const QString& path : stagedDictionaryFiles)
        QFile::remove(path);
    stagedDictionaryFiles.clear();
    delete clientWorkingDir;
    delete settings;
    delete ui;
}

bool MainWindow::loadConfig()
{
    QString filename = ui->Set_Client_configFileBox->currentData().toString();
    if(filename == "(ext)")
    {
        QString error;
        if(!OfflinePolicy::validateLocalFilePath(ui->Set_Client_configPathEdit->text(), &filename, &error))
        {
            showOfflineError(error);
            return false;
        }
    }
    QFile file(filename);
    if(!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
    {
        showOfflineError(tr("Cannot read the selected local client configuration."));
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    const QJsonObject object = document.object();
    if(parseError.error != QJsonParseError::NoError || !document.isObject()
       || !object.value("mifare classic").isObject() || !object.value("lf").isObject()
       || !object.value("t55xx").isObject())
    {
        showOfflineError(tr("Invalid client configuration. Select a matching local profile."));
        return false;
    }
    mifare->setConfigMap(object["mifare classic"].toObject().toVariantMap());
    lf->setConfigMap(object["lf"].toObject().toVariantMap());
    t55xxTab->setConfigMap(object["t55xx"].toObject().toVariantMap());
    return true;
}

void MainWindow::initUI() // will be called by main.app
{
    ui->retranslateUi(this);
    uiInit();
    signalInit();
    setState(false);
    dockInit();
    initLocalTools();
    setWindowTitle(tr("Proxmark3GUI Offline — local tools"));
}

void MainWindow::initLocalTools()
{
    QToolBar* tools = addToolBar(tr("Local tools"));
    tools->setObjectName("localToolsBar");
    tools->setMovable(false);
    QAction* choose = tools->addAction(tr("Choose local client"));
    choose->setObjectName("chooseLocalClientAction");
    connect(choose, &QAction::triggered, this, &MainWindow::chooseLocalClient);
    tools->addAction(checkUpdate);
    QAction* dictionaries = tools->addAction(tr("Local dictionaries"));
    dictionaries->setObjectName("localDictionariesAction");
    connect(dictionaries, &QAction::triggered, this, &MainWindow::manageLocalDictionaries);
    clearDictionaryAction = tools->addAction(tr("Clear dictionary selection"));
    connect(clearDictionaryAction, &QAction::triggered, this, [this]() {
        selectedDictionary.clear();
        settings->remove("Offline/dictionary");
        dictionaryAction->setText(tr("Dictionary: built-in defaults"));
        ui->MF_Attack_chkButton->setToolTip(QString());
        clearDictionaryAction->setEnabled(false);
    });
    QAction* log = tools->addAction(tr("Save session log"));
    connect(log, &QAction::triggered, this, &MainWindow::saveSessionLog);
    tools->addSeparator();
    dictionaryAction = tools->addAction(tr("Dictionary: built-in defaults"));
    dictionaryAction->setEnabled(false);
    clearDictionaryAction->setEnabled(false);
    selectedDictionary = settings->value("Offline/dictionary").toString();
    if(!selectedDictionary.isEmpty())
    {
        LocalDictionary::ParseResult result;
        if(LocalDictionary::readFile(selectedDictionary, &result))
        {
            dictionaryAction->setText(tr("Dictionary: %1").arg(QFileInfo(selectedDictionary).fileName()));
            ui->MF_Attack_chkButton->setToolTip(tr("Use the selected local dictionary."));
            clearDictionaryAction->setEnabled(true);
        }
        else
            selectedDictionary.clear();
    }
    ui->PM3_pathBox->setEditable(false);
    ui->PM3_pathBox->setToolTip(tr("Choose a trusted native client with the local client button."));
}

void MainWindow::chooseLocalClient()
{
    if(pm3state)
    {
        QMessageBox::information(this, tr("Local client"), tr("Disconnect before changing the client."));
        return;
    }
#ifdef Q_OS_WIN
    const QString filter = tr("Native executable (*.exe)");
#else
    const QString filter = tr("Native executable (*)");
#endif
    const QString selected = QFileDialog::getOpenFileName(this, tr("Choose a trusted local PM3 client"),
        QApplication::applicationDirPath(), filter);
    if(selected.isEmpty()) return;
    QString path, error;
    if(!OfflinePolicy::validateExecutablePath(selected, &path, &error))
    {
        QMessageBox::warning(this, tr("Local client"), error);
        return;
    }
    const QByteArray digest = fileDigest(path, &error);
    if(digest.isEmpty())
    {
        QMessageBox::warning(this, tr("Local client"), error);
        return;
    }
    settings->setValue(clientPinKey(path), digest);
    settings->sync();
    addClientPath(path);
    ui->statusbar->showMessage(tr("Local client selected. Its SHA-256 will be checked before each launch."), 8000);
}

void MainWindow::showClientDetails()
{
    QString path, error;
    QByteArray digest;
    if(OfflinePolicy::validateExecutablePath(ui->PM3_pathBox->currentText(), &path, &error))
        digest = fileDigest(path, &error);
    const QByteArray recorded = path.isEmpty() ? QByteArray() : settings->value(clientPinKey(path)).toByteArray();
    QString details = tr("Client: %1\nSHA-256: %2\nRecorded SHA-256: %3\nProfile: %4\nData directory: %5\nClient workspace: %6\n\n")
        .arg(path.isEmpty() ? tr("Not selected") : path,
             QString::fromLatin1(digest), QString::fromLatin1(recorded),
             ui->Set_Client_configFileBox->currentText(), AppPaths::dataDirectory(), AppPaths::workspaceDirectory());
    details += tr("The checksum detects changes to the selected executable. It does not authenticate its origin or its DLLs.\n"
                  "Use a reviewed RRG client and a matching firmware/profile. This GUI does not download or update them.\n"
                  "Only local serial startup and reviewed commands are accepted. External clients remain separate programs; "
                  "use operating-system outbound rules when enforcing network isolation.\n");
    if(!error.isEmpty()) details += "\n" + error;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Client details"));
    dialog.resize(760, 420);
    QVBoxLayout layout(&dialog);
    QPlainTextEdit text;
    text.setReadOnly(true);
    text.setPlainText(details);
    layout.addWidget(&text);
    QDialogButtonBox buttons(QDialogButtonBox::Close);
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout.addWidget(&buttons);
    dialog.exec();
}

void MainWindow::manageLocalDictionaries()
{
    LocalResourcesDialog dialog(AppPaths::dataDirectory(), this);
    connect(&dialog, &LocalResourcesDialog::dictionarySelected, this, [this](const QString& path) {
        selectedDictionary = path;
        settings->setValue("Offline/dictionary", path);
        dictionaryAction->setText(tr("Dictionary: %1").arg(QFileInfo(path).fileName()));
        ui->MF_Attack_chkButton->setToolTip(tr("Use the selected local dictionary."));
        clearDictionaryAction->setEnabled(true);
        ui->statusbar->showMessage(tr("Dictionary selected. Use the existing key-check button when connected."), 8000);
    });
    dialog.exec();
}

void MainWindow::saveSessionLog()
{
    const QString name = QDir(AppPaths::dataDirectory()).filePath("logs/session-"
        + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".txt");
    const QString selected = QFileDialog::getSaveFileName(this, tr("Save local session log"), name,
                                                         tr("Text files (*.txt)"));
    if(selected.isEmpty()) return;
    QString path, error;
    if(!LocalDictionary::validateLocalPath(selected, &path, &error))
    {
        showOfflineError(error);
        return;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    const QByteArray contents = ui->Raw_outputEdit->toPlainText().toUtf8();
    if(!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
    {
        showOfflineError(tr("Cannot save the session log: %1").arg(file.errorString()));
        return;
    }
    ui->statusbar->showMessage(tr("Session log saved locally."), 6000);
}

void MainWindow::showOfflineError(const QString& error)
{
    refreshOutput("\n[Offline GUI] " + error + "\n");
    ui->statusbar->showMessage(error, 12000);
}

// ******************** basic functions ********************

void MainWindow::on_portSearchTimer_timeout()
{
    QStringList newPortList; // for actural port name
    QStringList newPortNameList; // for display name
    const QString hint = " *";

    foreach(const QSerialPortInfo &info, QSerialPortInfo::availablePorts())
    {
//        qDebug() << info.isNull() << info.portName() << info.description() << info.serialNumber() << info.manufacturer();
        if(!info.isNull())
        {
            QString idString = (info.description() + info.serialNumber() + info.manufacturer()).toLower();
            QString portName = info.portName();

#ifdef Q_OS_WIN
            newPortList << info.portName();
#else
            newPortList << info.systemLocation();
#endif
            if(info.hasVendorIdentifier() && info.hasProductIdentifier())
            {
                quint16 vid = info.vendorIdentifier();
                quint16 pid = info.productIdentifier();
                if(vid == 0x9AC4 && pid == 0x4B8F)
                    portName += hint;
                else if(vid == 0x2D2D && pid == 0x504D)
                    portName += hint;
            }
            else if(idString.contains("proxmark") || idString.contains("iceman"))
                portName += hint;
            newPortNameList << portName;
        }
    }
    if(newPortList != portList) // update PM3_portBox when available ports changed
    {
        portList = newPortList;
        ui->PM3_portBox->clear();
        int selectId = -1;
        for(int i = 0; i < portList.size(); i++)
        {
            ui->PM3_portBox->addItem(newPortNameList[i], newPortList[i]);
            if(selectId == -1 && newPortNameList[i].endsWith(hint))
                selectId = i;
        }
        if(selectId != -1)
            ui->PM3_portBox->setCurrentIndex(selectId);
    }
}

void MainWindow::on_PM3_connectButton_clicked()
{
    if(pm3state)
        return;
    const QComboBox* box = ui->PM3_portBox;
    const QString port = box->currentText() == box->itemText(box->currentIndex())
        ? box->currentData().toString() : box->currentText();
    QString path, error;
    QStringList arguments;
    if(!OfflinePolicy::validateExecutablePath(ui->PM3_pathBox->currentText(), &path, &error)
       || !OfflinePolicy::buildLaunchArguments(port, &arguments, &error))
    {
        QMessageBox::warning(this, tr("Local client"), error);
        return;
    }
    const QByteArray expected = settings->value(clientPinKey(path)).toByteArray();
    const QByteArray actual = fileDigest(path, &error);
    if(expected.isEmpty() || actual.isEmpty() || actual != expected)
    {
        QMessageBox::warning(this, tr("Client file changed"),
            tr("Choose the local client again to record its current SHA-256. "
               "A checksum detects changes; it does not certify the source of an executable."));
        return;
    }
    if(!AppPaths::initialize(&error) || !loadConfig())
    {
        if(!error.isEmpty()) showOfflineError(error);
        return;
    }
    pinnedClientHash = expected;
    clientWorkingDir->setPath(AppPaths::workspaceDirectory());
    emit setWorkingDir(clientWorkingDir->absolutePath());
    emit connectPM3(path, arguments, expected);
    if(!keepClientActive)
        emit setSerialListener(arguments.first(), true);
}

void MainWindow::onPM3ErrorOccurred(QProcess::ProcessError error)
{
    qDebug() << "PM3 Error:" << error << pm3->errorString();
    if(error == QProcess::FailedToStart)
        QMessageBox::information(this, tr("Info"), tr("Failed to start the client") + "\n" + pm3->errorString());
}

void MainWindow::onPM3HWConnectFailed()
{
    QMessageBox::information(this, tr("Info"), tr("Failed to connect to the hardware"));
}

void MainWindow::onPM3StateChanged(bool st, const QString& info)
{
    pm3state = st;
    setState(st);
    if(st == true)
    {
        portSearchTimer->stop();
        setStatusBar(PM3VersionBar, info);
        setStatusBar(connectStatusBar, tr("Connected"));
    }
    else
    {
        portSearchTimer->start();
        for(const QString& path : stagedDictionaryFiles)
            QFile::remove(path);
        stagedDictionaryFiles.clear();
        setStatusBar(PM3VersionBar, "");
        setStatusBar(connectStatusBar, tr("Not Connected"));
    }
}

void MainWindow::on_PM3_disconnectButton_clicked()
{
    emit killPM3();
    emit setSerialListener(false);
}

void MainWindow::refreshOutput(const QString& output)
{
//    qDebug() << "MainWindow::refresh:" << output;
    ui->Raw_outputEdit->moveCursor(QTextCursor::End);
    ui->Raw_outputEdit->insertPlainText(output);
    ui->Raw_outputEdit->moveCursor(QTextCursor::End);
}

void MainWindow::on_stopButton_clicked()
{
    // A process stop is not a protocol-level cancellation. Reconnecting is an
    // explicit user action so an interrupted operation cannot silently resume.
    on_PM3_disconnectButton_clicked();
}
// *********************************************************

// ******************** raw command ********************

void MainWindow::on_Raw_CMDEdit_textChanged(const QString &arg1)
{
    stashedCMDEditText = arg1;
}

void MainWindow::on_Raw_sendCMDButton_clicked()
{
    util->execCMD(ui->Raw_CMDEdit->text());
    refreshCMD(ui->Raw_CMDEdit->text());
}

void MainWindow::on_Raw_clearOutputButton_clicked()
{
    ui->Raw_outputEdit->clear();
}

void MainWindow::on_Raw_CMDHistoryBox_stateChanged(int arg1)
{
    if(arg1 == Qt::Checked)
    {
        ui->Raw_CMDHistoryWidget->setVisible(true);
        ui->Raw_clearHistoryButton->setVisible(true);
        ui->Raw_CMDHistoryBox->setText(tr("History:"));
    }
    else
    {
        ui->Raw_CMDHistoryWidget->setVisible(false);
        ui->Raw_clearHistoryButton->setVisible(false);
        ui->Raw_CMDHistoryBox->setText("");
    }
}

void MainWindow::on_Raw_clearHistoryButton_clicked()
{
    ui->Raw_CMDHistoryWidget->clear();
}

void MainWindow::on_Raw_CMDHistoryWidget_itemDoubleClicked(QListWidgetItem *item)
{
    ui->Raw_CMDEdit->setText(item->text());
    ui->Raw_CMDEdit->setFocus();
}

void MainWindow::sendMSG() // send command when pressing Enter
{
    if(ui->Raw_CMDEdit->hasFocus())
        on_Raw_sendCMDButton_clicked();
}


void MainWindow::refreshCMD(const QString& cmd)
{
    ui->Raw_CMDEdit->blockSignals(true);
    ui->Raw_CMDEdit->setText(cmd);
    if(cmd != "" && (ui->Raw_CMDHistoryWidget->count() == 0 || ui->Raw_CMDHistoryWidget->item(ui->Raw_CMDHistoryWidget->count() - 1)->text() != cmd))
        ui->Raw_CMDHistoryWidget->addItem(cmd);
    stashedCMDEditText = cmd;
    stashedIndex = -1;
    ui->Raw_CMDEdit->blockSignals(false);
}

void MainWindow::on_Raw_keyPressed(QObject* obj_addr, QEvent& event)
{
    if(event.type() == QEvent::KeyPress)
    {
        QKeyEvent& keyEvent = static_cast<QKeyEvent&>(event);
        if(obj_addr == ui->Raw_CMDEdit)
        {
            if(keyEvent.key() == Qt::Key_Up)
            {
                if(stashedIndex > 0)
                    stashedIndex--;
                else if(stashedIndex == -1)
                    stashedIndex = ui->Raw_CMDHistoryWidget->count() - 1;
            }
            else if(keyEvent.key() == Qt::Key_Down)
            {
                if(stashedIndex < ui->Raw_CMDHistoryWidget->count() - 1 && stashedIndex != -1)
                    stashedIndex++;
                else if(stashedIndex == ui->Raw_CMDHistoryWidget->count() - 1)
                    stashedIndex = -1;
            }
            if(keyEvent.key() == Qt::Key_Up || keyEvent.key() == Qt::Key_Down)
            {
                ui->Raw_CMDEdit->blockSignals(true);
                if(stashedIndex == -1)
                    ui->Raw_CMDEdit->setText(stashedCMDEditText);
                else
                    ui->Raw_CMDEdit->setText(ui->Raw_CMDHistoryWidget->item(stashedIndex)->text());
                ui->Raw_CMDEdit->blockSignals(false);
            }
        }
        else if(obj_addr == ui->Raw_outputEdit)
        {
            if(keyEvent.key() == Qt::Key_Up || keyEvent.key() == Qt::Key_Down)
                ui->Raw_CMDEdit->setFocus();
        }
    }
}
// *****************************************************

// ******************** mifare ********************
void MainWindow::on_MF_keyWidget_resized(QObject* obj_addr, QEvent& event)
{
    if(obj_addr == ui->MF_keyWidget && event.type() == QEvent::Resize)
    {
        QTableWidget* widget = (QTableWidget*)obj_addr;
        int keyItemWidth = widget->width();
        keyItemWidth -= widget->verticalScrollBar()->width();
        keyItemWidth -= 2 * widget->frameWidth();
        keyItemWidth -= widget->horizontalHeader()->sectionSize(0);
        widget->horizontalHeader()->resizeSection(1, keyItemWidth / 2);
        widget->horizontalHeader()->resizeSection(2, keyItemWidth / 2);
    }
}

void MainWindow::MF_onMFCardTypeChanged(int id, bool st)
{
    MFCardTypeBtnGroup->blockSignals(true);
    qDebug() << id << MFCardTypeBtnGroup->checkedId();
    if(!st)
    {
        int result;
        if(id > MFCardTypeBtnGroup->checkedId()) // id is specified in uiInit() with a proper order, so I can compare the size by id.
        {
            result = QMessageBox::question(this, tr("Info"), tr("Some of the data and key will be cleared.") + "\n" + tr("Continue?"), QMessageBox::Yes | QMessageBox::No);
        }
        else
        {
            result = QMessageBox::Yes;
        }
        if(result == QMessageBox::Yes)
        {
            qDebug() << "Yes";
            mifare->setCardType(MFCardTypeBtnGroup->checkedId());
            MF_widgetReset();
            mifare->data_syncWithDataWidget();
            mifare->data_syncWithKeyWidget();
        }
        else
        {
            qDebug() << "No";
            MFCardTypeBtnGroup->button(id)->setChecked(true);
        }
    }
    MFCardTypeBtnGroup->blockSignals(false);
}

void MainWindow::on_MF_selectAllBox_stateChanged(int arg1)
{
    ui->MF_dataWidget->blockSignals(true);
    ui->MF_selectAllBox->blockSignals(true);
    ui->MF_selectTrailerBox->blockSignals(true);
    if(arg1 == Qt::PartiallyChecked)
    {
        ui->MF_selectAllBox->setTristate(false);
        ui->MF_selectAllBox->setCheckState(Qt::Checked);
    }
    for(int i = 0; i < mifare->cardType.block_size; i++)
    {
        ui->MF_dataWidget->item(i, 1)->setCheckState(ui->MF_selectAllBox->checkState());
    }
    for(int i = 0; i < mifare->cardType.sector_size; i++)
    {
        ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(ui->MF_selectAllBox->checkState());
    }
    ui->MF_selectTrailerBox->setCheckState(ui->MF_selectAllBox->checkState());
    ui->MF_dataWidget->blockSignals(false);
    ui->MF_selectAllBox->blockSignals(false);
    ui->MF_selectTrailerBox->blockSignals(false);
}


void MainWindow::on_MF_selectTrailerBox_stateChanged(int arg1)
{
    int selectedSubBlocks = 0;

    ui->MF_dataWidget->blockSignals(true);
    ui->MF_selectAllBox->blockSignals(true);
    ui->MF_selectTrailerBox->blockSignals(true);
    if(arg1 == Qt::PartiallyChecked)
    {
        ui->MF_selectTrailerBox->setTristate(false);
        ui->MF_selectTrailerBox->setCheckState(Qt::Checked);
    }
    for(int i = 0; i < mifare->cardType.sector_size; i++)
    {
        ui->MF_dataWidget->item(mifare->cardType.blks[i] + mifare->cardType.blk[i] - 1, 1)->setCheckState(ui->MF_selectTrailerBox->checkState());
        selectedSubBlocks = 0;
        for(int j = 0; j < mifare->cardType.blk[i]; j++)
        {
            if(ui->MF_dataWidget->item(j + mifare->cardType.blks[i], 1)->checkState() == Qt::Checked)
                selectedSubBlocks++;
        }
        if(selectedSubBlocks == 0)
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(Qt::Unchecked);
        }
        else if(selectedSubBlocks == mifare->cardType.blk[i])
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(Qt::PartiallyChecked);
        }
    }

    ui->MF_dataWidget->blockSignals(false);
    ui->MF_selectAllBox->blockSignals(false);
    ui->MF_selectTrailerBox->blockSignals(false);
}


void MainWindow::on_MF_data2KeyButton_clicked()
{
    mifare->data_data2Key();
}

void MainWindow::on_MF_key2DataButton_clicked()
{
    mifare->data_key2Data();
}

void MainWindow::on_MF_fillKeysButton_clicked()
{
    mifare->data_fillKeys();
}

void MainWindow::on_MF_trailerDecoderButton_clicked()
{
    decDialog = new MF_trailerDecoderDialog(this);
    decDialog->show();
}

void MainWindow::on_MF_dataWidget_itemChanged(QTableWidgetItem *item)
{
    ui->MF_dataWidget->blockSignals(true);
    ui->MF_selectAllBox->blockSignals(true);
    ui->MF_selectTrailerBox->blockSignals(true);
    if(item->column() == 0)
    {
        int selectedSectors = 0;
        for(int i = 0; i < mifare->cardType.blk[Mifare::data_b2s(item->row())]; i++)
        {
            ui->MF_dataWidget->item(i + item->row(), 1)->setCheckState(item->checkState());
            qDebug() << i << mifare->cardType.blk[item->row()] << i + item->row() << ui->MF_dataWidget->item(i + item->row(), 1)->text();
        }
        for(int i = 0; i < mifare->cardType.sector_size; i++)
        {
            if(ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->checkState() == Qt::Checked)
            {
                selectedSectors++;
            }
        }
        if(selectedSectors == 0)
        {
            ui->MF_selectAllBox->setCheckState(Qt::Unchecked);
            ui->MF_selectTrailerBox->setCheckState(Qt::Unchecked);
        }
        else if(selectedSectors == mifare->cardType.sector_size)
        {
            ui->MF_selectAllBox->setCheckState(Qt::Checked);
            ui->MF_selectTrailerBox->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_selectAllBox->setCheckState(Qt::PartiallyChecked);
            ui->MF_selectTrailerBox->setCheckState(Qt::PartiallyChecked);
        }
    }
    else if(item->column() == 1)
    {
        int selectedSubBlocks = 0;
        int selectedBlocks = 0;
        int selectedTrailers = 0;

        for(int i = 0; i < mifare->cardType.block_size; i++)
        {
            if(ui->MF_dataWidget->item(i, 1)->checkState() == Qt::Checked)
                selectedBlocks++;
        }
        for(int i = 0; i < mifare->cardType.blk[Mifare::data_b2s(item->row())]; i++)
        {
            if(ui->MF_dataWidget->item(i + mifare->cardType.blks[Mifare::data_b2s(item->row())], 1)->checkState() == Qt::Checked)
                selectedSubBlocks++;
        }
        for(int i = 0; i < mifare->cardType.sector_size; i++)
        {
            int targetBlock = mifare->cardType.blks[i] + mifare->cardType.blk[i] - 1;
            if(ui->MF_dataWidget->item(targetBlock, 1)->checkState() == Qt::Checked)
                selectedTrailers++;
        }
        if(selectedBlocks == 0)
        {
            ui->MF_selectAllBox->setCheckState(Qt::Unchecked);
        }
        else if(selectedBlocks == mifare->cardType.block_size)
        {
            ui->MF_selectAllBox->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_selectAllBox->setCheckState(Qt::PartiallyChecked);
        }
        if(selectedSubBlocks == 0)
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[Mifare::data_b2s(item->row())], 0)->setCheckState(Qt::Unchecked);
        }
        else if(selectedSubBlocks == mifare->cardType.blk[Mifare::data_b2s(item->row())])
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[Mifare::data_b2s(item->row())], 0)->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[Mifare::data_b2s(item->row())], 0)->setCheckState(Qt::PartiallyChecked);
        }
        if(selectedTrailers == 0)
        {
            ui->MF_selectTrailerBox->setCheckState(Qt::Unchecked);
        }
        else if(selectedTrailers == mifare->cardType.sector_size)
        {
            ui->MF_selectTrailerBox->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_selectTrailerBox->setCheckState(Qt::PartiallyChecked);
        }
    }
    else if(item->column() == 2)
    {
        QString data = item->text().remove(" ").toUpper();
        if(data == "" || mifare->data_isDataValid(data) == Mifare::DATA_NOSPACE)
        {
            mifare->data_setData(item->row(), data);
        }
        else
        {
            QMessageBox::information(this, tr("Info"), tr("Data must consists of 32 Hex symbols(Whitespace is allowed)"));
        }
        mifare->data_syncWithDataWidget(false, item->row());
    }
    ui->MF_dataWidget->blockSignals(false);
    ui->MF_selectAllBox->blockSignals(false);
    ui->MF_selectTrailerBox->blockSignals(false);
}

void MainWindow::on_MF_keyWidget_itemChanged(QTableWidgetItem *item)
{
    if(item->column() == 1)
    {
        QString key = item->text().remove(" ").toUpper();
        if(key == "" || mifare->data_isKeyValid(key))
        {
            mifare->data_setKey(item->row(), Mifare::KEY_A, key);
        }
        else
        {
            QMessageBox::information(this, tr("Info"), tr("Key must consists of 12 Hex symbols(Whitespace is allowed)"));
        }
        mifare->data_syncWithKeyWidget(false, item->row(), Mifare::KEY_A);
    }
    else if(item->column() == 2)
    {
        QString key = item->text().remove(" ").toUpper();
        if(key == "" || mifare->data_isKeyValid(key))
        {
            mifare->data_setKey(item->row(), Mifare::KEY_B, key);
        }
        else
        {
            QMessageBox::information(this, tr("Info"), tr("Key must consists of 12 Hex symbols(Whitespace is allowed)"));
        }
        mifare->data_syncWithKeyWidget(false, item->row(), Mifare::KEY_B);
    }
}

void MainWindow::on_MF_File_loadButton_clicked()
{
    QString title = "";
    QString filename = "";
    if(ui->MF_File_dataButton->isChecked())
    {
        title = tr("Plz select the data file:");
        filename = QFileDialog::getOpenFileName(this, title, "./", tr("Binary Data Files(*.bin *.dump)") + ";;" + tr("Text Data Files(*.txt *.eml)") + ";;" + tr("All Files(*.*)"));
        qDebug() << filename;
        if(filename != "")
        {
            if(!mifare->data_loadDataFile(filename))
            {
                QMessageBox::information(this, tr("Info"), tr("Failed to open") + "\n" + filename);
            }
        }
    }
    else if(ui->MF_File_keyButton->isChecked())
    {
        title = tr("Plz select the key file:");
        filename = QFileDialog::getOpenFileName(this, title, "./", tr("Binary Key Files(*.bin *.dump)") + ";;" + tr("All Files(*.*)"));
        qDebug() << filename;
        if(filename != "")
        {
            if(!mifare->data_loadKeyFile(filename))
            {
                QMessageBox::information(this, tr("Info"), tr("Failed to open") + "\n" + filename);
            }
        }
    }

}

void MainWindow::on_MF_File_saveButton_clicked()
{

    QString title = "";
    QString filename = "";
    QString selectedType = "";
    QString defaultName = mifare->data_getUID();
    if(defaultName != "")
        defaultName += "_";
    defaultName += QDateTime::currentDateTime().toString("yyyy-MM-dd-hh-mm-ss");

    if(ui->MF_File_dataButton->isChecked())
    {
        title = tr("Plz select the location to save data file:");
        filename = QFileDialog::getSaveFileName(this, title, "./data_" + defaultName, tr("Binary Data Files(*.bin *.dump)") + ";;" + tr("Text Data Files(*.txt *.eml)"), &selectedType);
        qDebug() << filename;
        if(filename != "")
        {
            if(!mifare->data_saveDataFile(filename, selectedType == tr("Binary Data Files(*.bin *.dump)")))
            {
                QMessageBox::information(this, tr("Info"), tr("Failed to save to") + "\n" + filename);
            }
        }
    }
    else if(ui->MF_File_keyButton->isChecked())
    {
        title = tr("Plz select the location to save key file:");
        filename = QFileDialog::getSaveFileName(this, title, "./key_" + defaultName, tr("Binary Key Files(*.bin *.dump)"), &selectedType);
        qDebug() << filename;
        if(filename != "")
        {
            if(!mifare->data_saveKeyFile(filename, selectedType == tr("Binary Key Files(*.bin *.dump)")))
            {
                QMessageBox::information(this, tr("Info"), tr("Failed to save to") + "\n" + filename);
            }
        }
    }
    qDebug() << filename << selectedType;
}

void MainWindow::on_MF_File_clearButton_clicked()
{
    if(ui->MF_File_keyButton->isChecked())
    {
        mifare->data_clearKey();
        mifare->data_syncWithKeyWidget();
    }
    else if(ui->MF_File_dataButton->isChecked())
    {
        mifare->data_clearData();
        mifare->data_syncWithDataWidget();
    }
}

void MainWindow::on_MF_Attack_infoButton_clicked()
{
    mifare->info();
}

void MainWindow::on_MF_Attack_chkButton_clicked()
{
    if(!pm3state) return;
    QString dictionaryName;
    if(!selectedDictionary.isEmpty())
    {
        if(!mifare->supportsLocalDictionary())
        {
            QMessageBox::information(this, tr("Local dictionary"),
                tr("The selected profile has no verified dictionary option. "
                   "Use the RRG v4.16717 profile with its matching client, or clear the dictionary selection."));
            return;
        }
        LocalDictionary::ParseResult result;
        QString error;
        if(!LocalDictionary::readFile(selectedDictionary, &result, &error))
        {
            showOfflineError(error);
            return;
        }
        // Keep the filename ASCII and pass it relative to the private workspace.
        // This avoids putting user directory names into the client's command line.
        dictionaryName = "gui-dictionary-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".dic";
        const QString staged = QDir(AppPaths::workspaceDirectory()).filePath(dictionaryName);
        if(!LocalDictionary::writeFile(staged, result.keys, &error))
        {
            showOfflineError(error);
            return;
        }
        // Keep the file until the process closes: a command can still be queued
        // in the client's stdin after the GUI output collector times out.
        stagedDictionaryFiles.append(staged);
    }
    setState(false);
    mifare->chk(dictionaryName);
    setState(true);
}

void MainWindow::on_MF_Attack_nestedButton_clicked()
{
    setState(false);
    mifare->nested();
    setState(true);
}

void MainWindow::on_MF_Attack_hardnestedButton_clicked()
{
    mifare->hardnested();
}

void MainWindow::on_MF_RW_readSelectedButton_clicked()
{
    setState(false);
    mifare->readSelected(Mifare::TARGET_MIFARE);
    setState(true);
}

void MainWindow::on_MF_RW_readBlockButton_clicked()
{
    setState(false);
    mifare->readOne(Mifare::TARGET_MIFARE);
    setState(true);
}

void MainWindow::on_MF_RW_writeBlockButton_clicked()
{
    setState(false);
    mifare->writeOne();
    setState(true);
}

void MainWindow::on_MF_RW_writeSelectedButton_clicked()
{
    setState(false);
    mifare->writeSelected(Mifare::TARGET_MIFARE);
    setState(true);
}

void MainWindow::on_MF_RW_dumpButton_clicked()
{
    mifare->dump();
}

void MainWindow::on_MF_RW_restoreButton_clicked()
{
    mifare->restore();
}

void MainWindow::on_MF_UID_readSelectedButton_clicked()
{
    setState(false);
    mifare->readSelected(Mifare::TARGET_UID);
    setState(true);
}

void MainWindow::on_MF_UID_readBlockButton_clicked()
{
    setState(false);
    mifare->readOne(Mifare::TARGET_UID);
    setState(true);
}

void MainWindow::on_MF_UID_writeSelectedButton_clicked()
{
    setState(false);
    mifare->writeSelected(Mifare::TARGET_UID);
    setState(true);
}

void MainWindow::on_MF_UID_writeBlockButton_clicked()
{
    setState(false);
    mifare->writeOne(Mifare::TARGET_UID);
    setState(true);
}

void MainWindow::on_MF_UID_wipeButton_clicked()
{
    mifare->wipeC();
}

void MainWindow::on_MF_UID_aboutUIDButton_clicked()
{
    QString msg;
    msg += tr("    Normally, the Block 0 of a typical Mifare card, which contains the UID, is locked during the manufacture. Users cannot write anything to Block 0 or set a new UID to a normal Mifare card.") + "\n";
    msg += tr("    Chinese Magic Cards(aka UID Cards) are some special cards whose Block 0 are writeable. And you can change UID by writing to it.") + "\n";
    msg += "\n";
    msg += tr("There are two versions of Chinese Magic Cards, the Gen1 and the Gen2.") + "\n";
    msg += tr("    Gen1:") + "\n" + tr("    also called UID card in China. It responses to some backdoor commands so you can access any blocks without password. The Proxmark3 has a bunch of related commands(csetblk, cgetblk, ...) to deal with this type of card, and my GUI also support these commands.") + "\n";
    msg += tr("    Gen2:") + "\n" + tr("    doesn't response to the backdoor commands, which means that a reader cannot detect whether it is a Chinese Magic Card or not by sending backdoor commands.") + "\n";
    msg += "\n";
    msg += tr("There are some types of Chinese Magic Card Gen2.") + "\n";
    msg += tr("    CUID Card:") + "\n" + tr("    the Block 0 is writeable, you can write to this block repeatedly by normal wrbl command.") + "\n";
    msg += tr("    (hf mf wrbl 0 A FFFFFFFFFFFF <the data you want to write>)") + "\n";
    msg += tr("    FUID Card:") + "\n" + tr("    you can only write to Block 0 once. After that, it seems like a typical Mifare card(Block 0 cannot be written to).") + "\n";
    msg += tr("    (some readers might try changing the Block 0, which could detect the CUID Card. In that case, you should use FUID card.)") + "\n";
    msg += tr("    UFUID Card:") + "\n" + tr("    It behaves like a CUID card(or UID card? I'm not sure) before you send some special command to lock it. Once it is locked, you cannot change its Block 0(just like a typical Mifare card).") + "\n";
    msg += "\n";
    msg += tr("    Seemingly, these Chinese Magic Cards are more easily to be compromised by Nested Attack(it takes little time to get an unknown key).") + "\n";
    QMessageBox::information(this, tr("About UID Card"), msg);
}

void MainWindow::on_MF_UID_setParaButton_clicked()
{
    setState(false);
    mifare->setParameterC();
    setState(true);
}

void MainWindow::on_MF_UID_lockButton_clicked()
{
    mifare->lockC();
}

void MainWindow::on_MF_Sim_readSelectedButton_clicked()
{
    setState(false);
    mifare->readSelected(Mifare::TARGET_EMULATOR);
    setState(true);
}

void MainWindow::on_MF_Sim_writeSelectedButton_clicked()
{
    setState(false);
    mifare->writeSelected(Mifare::TARGET_EMULATOR);
    setState(true);
}

void MainWindow::on_MF_Sim_clearButton_clicked()
{
    mifare->wipeE();
}

void MainWindow::on_MF_Sim_simButton_clicked()
{
    mifare->simulate();
}

void MainWindow::on_MF_Sniff_loadButton_clicked() // use a tmp file to support complicated path
{
    QString title = "";
    QString filename = "";
    QString defaultExtension;
    QDir clientTracePath;

    if(Util::getClientType() == Util::CLIENTTYPE_OFFICIAL)
        defaultExtension = ".trc";
    else if(Util::getClientType() == Util::CLIENTTYPE_ICEMAN)
        defaultExtension = ".trace";

    QString userTraceSavePath = mifare->getTraceSavePath();
    if(userTraceSavePath.isEmpty())
        clientTracePath = *clientWorkingDir;
    else
        clientTracePath = QDir(userTraceSavePath); // For v4.16717 and later

    title = tr("Plz select the trace file:");
    filename = QFileDialog::getOpenFileName(this, title, clientTracePath.absolutePath(), tr("Trace Files") + "(*" + defaultExtension + ")" + ";;" + tr("All Files(*.*)"));
    qDebug() << filename;
    if(filename != "")
    {
        QString tmpFile = "tmp" + QString::number(QDateTime::currentDateTimeUtc().toTime_t()) + defaultExtension;
        if(QFile::copy(filename, clientTracePath.absolutePath() + "/" + tmpFile))
        {
            mifare->loadSniff(tmpFile);
            util->delay(3000);
            QFile::remove(clientTracePath.absolutePath() + "/" + tmpFile);
        }
        else
        {
            QMessageBox::information(this, tr("Info"), tr("Failed to open") + "\n" + filename);
        }
    }
}

void MainWindow::on_MF_Sniff_saveButton_clicked()
{
    QString title = "";
    QString filename = "";
    QString defaultExtension;
    QDir clientTracePath;

    if(Util::getClientType() == Util::CLIENTTYPE_OFFICIAL)
        defaultExtension = ".trc";
    else if(Util::getClientType() == Util::CLIENTTYPE_ICEMAN)
        defaultExtension = ".trace";

    QString userTraceSavePath = mifare->getTraceSavePath();
    if(userTraceSavePath.isEmpty())
        clientTracePath = *clientWorkingDir;
    else
        clientTracePath = QDir(userTraceSavePath); // For v4.16717 and later

    title = tr("Plz select the location to save trace file:");
    filename = QFileDialog::getSaveFileName(this, title, clientTracePath.absolutePath(), tr("Trace Files") + "(*" + defaultExtension + ")");
    qDebug() << filename;
    if(filename != "")
    {
        QString tmpFile = "tmp" + QString::number(QDateTime::currentDateTimeUtc().toTime_t()) + defaultExtension;
        mifare->saveSniff(tmpFile);
        for(int i = 0; i < 100; i++)
        {
            util->delay(100);
            if(QFile::exists(clientTracePath.absolutePath() + "/" + tmpFile))
                break;
        }
        // filename is not empty -> the user has chosen to overwrite the existing file
        if(QFile::exists(filename))
            QFile::remove(filename);
        if(!QFile::copy(clientTracePath.absolutePath() + "/" + tmpFile, filename))
        {
            QMessageBox::information(this, tr("Info"), tr("Failed to save to") + "\n" + filename);
        }
        QFile::remove(clientTracePath.absolutePath() + "/" + tmpFile);
    }

}

void MainWindow::on_MF_Sniff_sniffButton_clicked()
{
    setState(false);
    mifare->sniff();
    setState(true);
}

void MainWindow::on_MF_14aSniff_snoopButton_clicked()
{
    setState(false);
    mifare->sniff14a();
    setState(true);
}

void MainWindow::on_MF_Sniff_listButton_clicked()
{
    mifare->list();
}

void MainWindow::MF_widgetReset()
{
    int secs = mifare->cardType.sector_size;
    int blks = mifare->cardType.block_size;
    QBrush trailerItemForeColor = QBrush(QColor(0, 160, 255));
    ui->MF_RW_blockBox->clear();
    ui->MF_keyWidget->setRowCount(secs);
    ui->MF_dataWidget->setRowCount(blks);

    ui->MF_dataWidget->blockSignals(true);
    ui->MF_keyWidget->blockSignals(true);
    ui->MF_selectAllBox->blockSignals(true);
    ui->MF_selectTrailerBox->blockSignals(true);

    for(int i = 0; i < blks; i++)
    {
        setTableItem(ui->MF_dataWidget, i, 0, "");
        setTableItem(ui->MF_dataWidget, i, 1, QString::number(i));
        ui->MF_dataWidget->item(i, 1)->setCheckState(Qt::Checked);
        setTableItem(ui->MF_dataWidget, i, 2, "");
        ui->MF_RW_blockBox->addItem(QString::number(i));
    }

    for(int i = 0; i < secs; i++)
    {
        setTableItem(ui->MF_keyWidget, i, 0, QString::number(i));
        setTableItem(ui->MF_keyWidget, i, 1, "");
        setTableItem(ui->MF_keyWidget, i, 2, "");
        setTableItem(ui->MF_dataWidget, mifare->cardType.blks[i], 0, QString::number(i));
        ui->MF_dataWidget->item(mifare->cardType.blks[i] + mifare->cardType.blk[i] - 1, 2)->setForeground(trailerItemForeColor);
        ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(Qt::Checked);
    }
    ui->MF_dataWidget->item(0, 2)->setForeground(QBrush(QColor(255, 160, 0)));
    ui->MF_selectAllBox->setCheckState(Qt::Checked);
    ui->MF_selectTrailerBox->setCheckState(Qt::Checked);

    ui->MF_dataWidget->blockSignals(false);
    ui->MF_keyWidget->blockSignals(false);
    ui->MF_selectAllBox->blockSignals(false);
    ui->MF_selectTrailerBox->blockSignals(false);
}
// ************************************************


// ******************** other ********************

void MainWindow::uiInit()
{
    connect(ui->Raw_CMDEdit, &QLineEdit::returnPressed, this, &MainWindow::sendMSG);
    ui->Raw_CMDEdit->installEventFilter(keyEventFilter);
    connect(keyEventFilter, &MyEventFilter::eventHappened, this, &MainWindow::on_Raw_keyPressed);
    ui->MF_keyWidget->installEventFilter(resizeEventFilter);
    connect(resizeEventFilter, &MyEventFilter::eventHappened, this, &MainWindow::on_MF_keyWidget_resized);
    ui->Raw_outputEdit->installEventFilter(keyEventFilter);

    connectStatusBar = new QLabel(this);
    programStatusBar = new QLabel(this);
    PM3VersionBar = new QLabel(this);
    stopButton = new QPushButton(this);
    setStatusBar(connectStatusBar, tr("Not Connected"));
    setStatusBar(programStatusBar, tr("Idle"));
    setStatusBar(PM3VersionBar, "");
    stopButton->setText(tr("Stop client"));
    stopButton->setToolTip(tr("Stops the client process. Reconnect explicitly after an interrupted operation."));
    ui->statusbar->addPermanentWidget(PM3VersionBar, 1);
    ui->statusbar->addPermanentWidget(connectStatusBar, 1);
    ui->statusbar->addPermanentWidget(programStatusBar, 1);
    ui->statusbar->addPermanentWidget(stopButton);

    ui->MF_dataWidget->setColumnWidth(0, 55);
    ui->MF_dataWidget->setColumnWidth(1, 55);

    ui->MF_keyWidget->setColumnWidth(0, 45);

    MF_widgetReset();
    MFCardTypeBtnGroup = new QButtonGroup(this);
    MFCardTypeBtnGroup->addButton(ui->MF_Type_miniButton, 0);
    MFCardTypeBtnGroup->addButton(ui->MF_Type_1kButton, 1);
    MFCardTypeBtnGroup->addButton(ui->MF_Type_2kButton, 2);
    MFCardTypeBtnGroup->addButton(ui->MF_Type_4kButton, 4);
    connect(MFCardTypeBtnGroup, QOverload<int, bool>::of(&QButtonGroup::buttonToggled), this, &MainWindow::MF_onMFCardTypeChanged);

    ui->MF_keyWidget->installEventFilter(this);
    ui->MF_dataWidget->installEventFilter(this);

    ui->Set_UI_Theme_nameBox->addItem(tr("(None)"), "(none)");
    ui->Set_UI_Theme_nameBox->addItem(tr("Dark"), "qdss_dark");
    ui->Set_UI_Theme_nameBox->addItem(tr("Light"), "qdss_light");

    settings->beginGroup("UI_grpbox_preference");

    QStringList boxNames = settings->allKeys();
    QGroupBox * boxptr;
    foreach(QString name, boxNames)
    {
        boxptr = this->findChild<QGroupBox*>(name);
        if(boxptr == nullptr)
            continue;
        if(settings->value(name, true).toBool())
        {
            boxptr->setMaximumHeight(16777215);
            boxptr->setChecked(true);
        }
        else
        {
            boxptr->setMaximumHeight(20);
            boxptr->setChecked(false);
        }
    }
    settings->endGroup();

    loadClientPathList();

    ui->Set_Client_GUIWorkingDirLabel->setText(AppPaths::dataDirectory());

    settings->beginGroup("Client_Args");
    ui->Set_Client_startArgsEdit->setText("<local serial port> -f");
    ui->Set_Client_startArgsEdit->setReadOnly(true);
    settings->endGroup();

    settings->beginGroup("Client_forceButtonsEnabled");
    keepButtonsEnabled = settings->value("state", false).toBool();
    settings->endGroup();
    ui->Set_Client_forceEnabledBox->setChecked(keepButtonsEnabled);

    // the disconnect detection doesn't work well on Linux/macOS
    // So it should be disabled on these platforms
    // https://github.com/wh201906/Proxmark3GUI/issues/22
    // #22, #26, #40, #41
    settings->beginGroup("Client_keepClientActive");
#ifdef Q_OS_WIN
    keepClientActive = settings->value("state", false).toBool();
#else
    keepClientActive = settings->value("state", true).toBool();
#endif
    settings->endGroup();
    ui->Set_Client_keepClientActiveBox->setChecked(keepClientActive);

    QDir configFiles(":/config/");
    configFiles.setSorting(QDir::Name);
    const QFileInfoList configFileList = configFiles.entryInfoList();
    ui->Set_Client_configFileBox->blockSignals(true);
    for(const auto& file : configFileList)
    {
        ui->Set_Client_configFileBox->addItem(file.fileName(), file.filePath());
    }

    // Use the last one as the default one
    ui->Set_Client_configFileBox->setCurrentIndex(ui->Set_Client_configFileBox->count() - 1);
    ui->Set_Client_configFileBox->addItem(tr("External file"), "(ext)");

    int configId = -1;
    settings->beginGroup("Client_Env");
    ui->Set_Client_envScriptEdit->clear();
    ui->Set_Client_envScriptEdit->setEnabled(false);
    ui->Set_Client_workingDirEdit->setText(AppPaths::workspaceDirectory());
    ui->Set_Client_workingDirEdit->setReadOnly(true);
    configId = ui->Set_Client_configFileBox->findData(settings->value("configFile"));
    ui->Set_Client_configPathEdit->setText(settings->value("extConfigFilePath", "config.json").toString());
    settings->endGroup();
    if(configId != -1)
        ui->Set_Client_configFileBox->setCurrentIndex(configId);
    ui->Set_Client_configFileBox->blockSignals(false);
    on_Set_Client_configFileBox_currentIndexChanged(ui->Set_Client_configFileBox->currentIndex());

    // setValue() will trigger valueChanged()
    // setValue(settings->value()) will create a nested group
    // call endGroup() before apply the value
    settings->beginGroup("UI");
    int opacity = settings->value("Opacity", 100).toInt();
    int themeId = ui->Set_UI_Theme_nameBox->findData(settings->value("Theme_Name", "(none)").toString());
    settings->endGroup();
    ui->Set_UI_Opacity_Box->setValue(opacity);
    ui->Set_UI_Theme_nameBox->setCurrentIndex((themeId == -1) ? 0 : themeId);

    settings->beginGroup("UI");
    // QApplication::font() might return wrong result
    // If fonts are not specified in config file, don't touch them.
    QString tmpFontName;
    int tmpFontSize;
    bool fontValid = false, dataFontValid = false, CMDFontValid = false;
    tmpFontName = settings->value("Font_Name", "").toString();
    tmpFontSize = settings->value("Font_Size", -1).toInt();
    if(!tmpFontName.isEmpty() && tmpFontSize != -1 && tmpFontName == QFont(tmpFontName).family())
    {
        ui->Set_UI_Font_nameBox->setCurrentFont(QFont(tmpFontName));
        ui->Set_UI_Font_sizeBox->setValue(tmpFontSize);
        fontValid = true;
    }
    // The default font should be the same as MF_dataWidget's and MF_keyWidget's.
    tmpFontName = settings->value("DataFont_Name", "Consolas").toString();
    tmpFontSize = settings->value("DataFont_Size", 12).toInt();
    if(!tmpFontName.isEmpty() && tmpFontSize != -1 && tmpFontName == QFont(tmpFontName).family())
    {
        ui->Set_UI_DataFont_nameBox->setCurrentFont(QFont(tmpFontName));
        ui->Set_UI_DataFont_sizeBox->setValue(tmpFontSize);
        dataFontValid = true;
    }
    tmpFontName = settings->value("CMDFont_Name", "").toString();
    tmpFontSize = settings->value("CMDFont_Size", -1).toInt();
    if(!tmpFontName.isEmpty() && tmpFontSize != -1 && tmpFontName == QFont(tmpFontName).family())
    {
        ui->Set_UI_CMDFont_nameBox->setCurrentFont(QFont(tmpFontName));
        ui->Set_UI_CMDFont_sizeBox->setValue(tmpFontSize);
        CMDFontValid = true;
    }
    settings->endGroup();

    if(fontValid)
        on_Set_UI_Font_setButton_clicked();
    if(dataFontValid)
        on_Set_UI_DataFont_setButton_clicked();
    if(CMDFontValid)
        on_Set_UI_CMDFont_setButton_clicked();

    ui->MF_RW_keyTypeBox->addItem("A", Mifare::KEY_A);
    ui->MF_RW_keyTypeBox->addItem("B", Mifare::KEY_B);

    on_Raw_CMDHistoryBox_stateChanged(Qt::Unchecked);

}

void MainWindow::signalInit()
{
    connect(pm3, &PM3Process::newOutput, util, &Util::processOutput);
    connect(pm3, &PM3Process::changeClientType, util, &Util::setClientType);
    connect(util, &Util::refreshOutput, this, &MainWindow::refreshOutput);

    connect(this, &MainWindow::connectPM3, pm3, &PM3Process::connectPM3);
    connect(this, &MainWindow::reconnectPM3, pm3, &PM3Process::reconnectPM3);
    connect(pm3, &PM3Process::PM3StatedChanged, this, &MainWindow::onPM3StateChanged);
    connect(pm3, &PM3Process::PM3StatedChanged, util, &Util::setRunningState);
    connect(pm3, &PM3Process::errorOccurred, this, &MainWindow::onPM3ErrorOccurred);
    connect(pm3, &PM3Process::HWConnectFailed, this, &MainWindow::onPM3HWConnectFailed);
    connect(this, &MainWindow::killPM3, pm3, &PM3Process::killPM3);
    connect(pm3, &PM3Process::operationBlocked, this, &MainWindow::showOfflineError);
    connect(util, &Util::operationBlocked, this, &MainWindow::showOfflineError);
    connect(this, &MainWindow::setWorkingDir, pm3, &PM3Process::setWorkingDir);
    connect(this, QOverload<bool>::of(&MainWindow::setSerialListener), pm3, QOverload<bool>::of(&PM3Process::setSerialListener));
    connect(this, QOverload<const QString&, bool>::of(&MainWindow::setSerialListener), pm3, QOverload<const QString&, bool>::of(&PM3Process::setSerialListener));

    connect(util, &Util::write, pm3, &PM3Process::write);

    connect(ui->MF_typeGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);
    connect(ui->MF_fileGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);
    connect(ui->MF_RWGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);
    connect(ui->MF_normalGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);
    connect(ui->MF_UIDGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);
    connect(ui->MF_simGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);
    connect(ui->MF_sniffGroupBox, &QGroupBox::clicked, this, &MainWindow::on_GroupBox_clicked);

    connect(stopButton, &QPushButton::clicked, this, &MainWindow::on_stopButton_clicked);

    connect(ui->Set_UI_Opacity_slider, &QSlider::valueChanged, ui->Set_UI_Opacity_Box, &QSpinBox::setValue);
}

void MainWindow::setStatusBar(QLabel * target, const QString& text)
{
    if(target == PM3VersionBar)
        target->setText(tr("HW Version:") + text);
    else if(target == connectStatusBar)
        target->setText(tr("PM3:") + text);
    else if(target == programStatusBar)
        target->setText(tr("State:") + text);
}

void MainWindow::setTableItem(QTableWidget * widget, int row, int column, const QString& text)
{
    if(widget->item(row, column) == nullptr)
        widget->setItem(row, column, new QTableWidgetItem());
    widget->item(row, column)->setText(text);
}

bool MainWindow::eventFilter(QObject * watched, QEvent * event) // drag support
{
    if(event->type() == QEvent::DragEnter)
    {
        QDragEnterEvent* dragEvent = static_cast<QDragEnterEvent*>(event);
        dragEvent->acceptProposedAction();
        return true;
    }
    else if(event->type() == QEvent::Drop)
    {
        QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
        if(watched == ui->MF_keyWidget)
        {
            const QMimeData* mime = dropEvent->mimeData();
            if(mime->hasUrls())
            {
                QList<QUrl> urls = mime->urls();
                if(urls.length() == 1)
                {
                    mifare->data_loadKeyFile(urls[0].toLocalFile());
                    return true;
                }
            }
        }
        else if(watched == ui->MF_dataWidget)
        {
            const QMimeData* mime = dropEvent->mimeData();
            if(mime->hasUrls())
            {
                QList<QUrl> urls = mime->urls();
                if(urls.length() == 1)
                {
                    mifare->data_loadDataFile(urls[0].toLocalFile());
                    return true;
                }
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setState(bool st)
{
    if(!st && pm3state)
    {
        setStatusBar(programStatusBar, tr("Running"));
    }
    else
    {
        setStatusBar(programStatusBar, tr("Idle"));
    }
    setButtonsEnabled((st && pm3state) || keepButtonsEnabled);
}

void MainWindow::setButtonsEnabled(bool st)
{
    ui->MF_attackGroupBox->setEnabled(st);
    ui->MF_normalGroupBox->setEnabled(st);
    ui->MF_UIDGroupBox->setEnabled(st);
    ui->MF_simGroupBox->setEnabled(st);
    ui->MF_sniffGroupBox->setEnabled(st);
    ui->Raw_CMDEdit->setEnabled(st);
    ui->Raw_sendCMDButton->setEnabled(st);
    ui->LF_LFconfigGroupBox->setEnabled(st);
    ui->LF_operationGroupBox->setEnabled(st);
}

void MainWindow::on_GroupBox_clicked(bool checked)
{
    QGroupBox* box = dynamic_cast<QGroupBox*>(sender());

    settings->beginGroup("UI_grpbox_preference");
    if(checked)
    {
        box->setMaximumHeight(16777215);
        settings->setValue(box->objectName(), true);
    }
    else
    {
        box->setMaximumHeight(20);
        settings->setValue(box->objectName(), false);
    }
    settings->endGroup();
}

void MainWindow::addClientPath(const QString& path)
{
    m_clientPathList.removeAll(path);
    m_clientPathList.prepend(path);
    while(m_clientPathList.size() > 32) // the maximum count of path items
        m_clientPathList.removeLast();
    // sync to the storage
    saveClientPathList();
    // sync to the UI
    loadClientPathList();
}

void MainWindow::loadClientPathList()
{
    m_clientPathList.clear();
    settings->beginGroup("Client_Path");
    int len = settings->beginReadArray("pathList");
    settings->endArray();
    if(settings->contains("path") && len == 0)
    {
        qDebug() << "Using old client path storage";
        m_clientPathList += settings->value("path", "proxmark3").toString();
    }
    else
    {
        int arrayLen = settings->beginReadArray("pathList");
        for(int i = 0; i < arrayLen; i++)
        {
            settings->setArrayIndex(i);
            QString path = settings->value("path").toString();
            if(!path.isEmpty())
                m_clientPathList += path;
        }
        settings->endArray();
    }
    settings->endGroup();

    ui->PM3_pathBox->clear();
    for(const QString& clientPath : qAsConst(m_clientPathList))
        ui->PM3_pathBox->addItem(clientPath);
}

void MainWindow::saveClientPathList()
{
    settings->beginGroup("Client_Path");
    if(settings->contains("path"))
    {
        qDebug() << "Upgrading client path storage";
        QString oldPath = settings->value("path").toString();
        if(!oldPath.isEmpty() && !m_clientPathList.contains(oldPath))
            m_clientPathList.append(oldPath);
        settings->remove("path");
    }

    settings->beginWriteArray("pathList");
    for(int i = 0; i < m_clientPathList.size(); i++)
    {
        settings->setArrayIndex(i);
        settings->setValue("path", m_clientPathList[i]);
    }
    settings->endArray();
    settings->endGroup();
}
// ***********************************************



void MainWindow::on_MF_Attack_darksideButton_clicked()
{
    setState(false);
    mifare->darkside();
    setState(true);
}

void MainWindow::on_Set_Client_startArgsEdit_editingFinished()
{
    settings->beginGroup("Client_Args");
    settings->setValue("args", ui->Set_Client_startArgsEdit->text());
    settings->endGroup();
}

void MainWindow::on_Set_Client_forceEnabledBox_stateChanged(int arg1)
{
    settings->beginGroup("Client_forceButtonsEnabled");
    keepButtonsEnabled = (arg1 == Qt::Checked);
    settings->setValue("state", keepButtonsEnabled);
    settings->endGroup();
    if(keepButtonsEnabled)
        setButtonsEnabled(true);
}

void MainWindow::on_Set_UI_setLanguageButton_clicked()
{
    Util::chooseLanguage(settings, this);
}

void MainWindow::on_PM3_refreshPortButton_clicked()
{
    on_portSearchTimer_timeout();
}

void MainWindow::on_Set_Client_envScriptEdit_editingFinished()
{
    settings->beginGroup("Client_Env");
    settings->setValue("scriptPath", ui->Set_Client_envScriptEdit->text());
    settings->endGroup();
}

void MainWindow::on_Set_Client_workingDirEdit_editingFinished()
{
    settings->beginGroup("Client_Env");
    settings->setValue("workingDir", ui->Set_Client_workingDirEdit->text());
    settings->endGroup();
}


void MainWindow::on_Set_Client_configPathEdit_editingFinished()
{
    settings->beginGroup("Client_Env");
    settings->setValue("extConfigFilePath", ui->Set_Client_configPathEdit->text());
    settings->endGroup();
}

void MainWindow::on_Set_Client_keepClientActiveBox_stateChanged(int arg1)
{
    settings->beginGroup("Client_keepClientActive");
    keepClientActive = (arg1 == Qt::Checked);
    settings->setValue("state", keepClientActive);
    settings->endGroup();
    emit setSerialListener(!keepClientActive);
}

void MainWindow::on_LF_LFConf_freqSlider_valueChanged(int value)
{
    onLFfreqConfChanged(value, true);
}

void MainWindow::onLFfreqConfChanged(int divisor, bool isCustomized)
{
    ui->LF_LFConf_freqDivisorBox->blockSignals(true);
    ui->LF_LFConf_freqSlider->blockSignals(true);

    if(isCustomized)
        ui->LF_LFConf_freqOtherButton->setChecked(true);
    else if(divisor == 95)
        ui->LF_LFConf_freq125kButton->setChecked(true);
    else if(divisor == 88)
        ui->LF_LFConf_freq134kButton->setChecked(true);
    ui->LF_LFConf_freqLabel->setText(tr("Actural Freq: ") + QString("%1kHz").arg(LF::divisor2Freq(divisor), 0, 'f', 3));
    ui->LF_LFConf_freqDivisorBox->setValue(divisor);
    ui->LF_LFConf_freqSlider->setValue(divisor);

    ui->LF_LFConf_freqDivisorBox->blockSignals(false);
    ui->LF_LFConf_freqSlider->blockSignals(false);
}

void MainWindow::on_LF_LFConf_freqDivisorBox_valueChanged(int arg1)
{
    onLFfreqConfChanged(arg1, true);
}

void MainWindow::on_LF_LFConf_freq125kButton_clicked()
{
    onLFfreqConfChanged(95, false);
}

void MainWindow::on_LF_LFConf_freq134kButton_clicked()
{
    onLFfreqConfChanged(88, false);
}

void MainWindow::on_LF_Op_searchButton_clicked()
{
    setState(false);
    lf->search();
    setState(true);
}

void MainWindow::on_LF_Op_readButton_clicked()
{
    setState(false);
    lf->read();
    setState(true);
}

void MainWindow::on_LF_Op_tuneButton_clicked()
{
    setState(false);
    lf->tune();
    setState(true);
}

void MainWindow::on_LF_Op_sniffButton_clicked()
{
    setState(false);
    lf->sniff();
    setState(true);
}

void MainWindow::dockInit()
{
    setDockNestingEnabled(true);
    QDockWidget* dock;
    QWidget* widget;
    int count = ui->funcTab->count();
    qDebug() << "dock count" << count;
    for(int i = 0; i < count; i++)
    {
        dock = new QDockWidget(ui->funcTab->tabText(0), this);
        qDebug() << "dock name" << ui->funcTab->tabText(0);
        dock->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);// movable is necessary, otherwise the dock cannot be dragged
        dock->setAllowedAreas(Qt::BottomDockWidgetArea);
        dock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        widget = ui->funcTab->widget(0);
        dock->setWidget(widget);
        if(widget->objectName() == "rawTab")
            Util::setRawTab(dock, i);
        addDockWidget(Qt::BottomDockWidgetArea, dock);
        if(!dockList.isEmpty())
            tabifyDockWidget(dockList[0], dock);
        dockList.append(dock);
    }
    ui->funcTab->setVisible(false);
    dockList[0]->setVisible(true);
    dockList[0]->raise();
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    contextMenu->exec(event->globalPos());
}

void MainWindow::on_LF_LFConf_getButton_clicked()
{
    setState(false);
    lf->getLFConfig();
    setState(true);
}

void MainWindow::on_LF_LFConf_setButton_clicked()
{
    LF::LFConfig config;
    setState(false);
    config.divisor = ui->LF_LFConf_freqDivisorBox->value();
    config.bitsPerSample = ui->LF_LFConf_bitsPerSampleBox->value();
    config.decimation = ui->LF_LFConf_decimationBox->value();
    config.averaging = ui->LF_LFConf_averagingBox->isChecked();
    config.triggerThreshold = ui->LF_LFConf_thresholdBox->value();
    config.samplesToSkip = ui->LF_LFConf_skipsBox->value();
    lf->setLFConfig(config);
    Util::gotoRawTab();
    setState(true);
}

void MainWindow::on_LF_LFConf_resetButton_clicked()
{
    setState(false);
    lf->resetLFConfig();
    setState(true);
}

void MainWindow::on_Set_Client_configFileBox_currentIndexChanged(int index)
{
    ui->Set_Client_configPathEdit->setVisible(ui->Set_Client_configFileBox->itemData(index).toString() == "(ext)");
    settings->beginGroup("Client_Env");
    settings->setValue("configFile", ui->Set_Client_configFileBox->currentData());
    settings->endGroup();
}


void MainWindow::on_Set_UI_Opacity_Box_valueChanged(int arg1)
{
    ui->Set_UI_Opacity_slider->blockSignals(true);
    ui->Set_UI_Opacity_slider->setValue(arg1);
    setWindowOpacity(arg1 / 100.0);
    settings->beginGroup("UI");
    settings->setValue("Opacity", ui->Set_UI_Opacity_Box->value());
    settings->endGroup();
    ui->Set_UI_Opacity_slider->blockSignals(false);
}


void MainWindow::on_Set_UI_Theme_setButton_clicked()
{
    settings->beginGroup("UI");
    settings->setValue("Theme_Name", ui->Set_UI_Theme_nameBox->currentData().toString());
    settings->endGroup();
}


void MainWindow::on_Set_UI_Font_setButton_clicked()
{
    QFont font = ui->Set_UI_Font_nameBox->currentFont();
    font.setPointSize(ui->Set_UI_Font_sizeBox->value());
    QApplication::setFont(font, "QWidget");

    settings->beginGroup("UI");
    settings->setValue("Font_Name", ui->Set_UI_Font_nameBox->currentFont().family());
    settings->setValue("Font_Size", ui->Set_UI_Font_sizeBox->value());
    settings->endGroup();
}


void MainWindow::on_Set_UI_DataFont_setButton_clicked()
{
    QFont font = ui->Set_UI_DataFont_nameBox->currentFont();
    font.setPointSize(ui->Set_UI_DataFont_sizeBox->value());
    ui->MF_dataWidget->setFont(font);
    ui->MF_keyWidget->setFont(font);

    settings->beginGroup("UI");
    settings->setValue("DataFont_Name", ui->Set_UI_DataFont_nameBox->currentFont().family());
    settings->setValue("DataFont_Size", ui->Set_UI_DataFont_sizeBox->value());
    settings->endGroup();
}


void MainWindow::on_Set_UI_CMDFont_setButton_clicked()
{
    QFont font = ui->Set_UI_CMDFont_nameBox->currentFont();
    font.setPointSize(ui->Set_UI_CMDFont_sizeBox->value());
    ui->Raw_outputEdit->setFont(font);

    settings->beginGroup("UI");
    settings->setValue("CMDFont_Name", ui->Set_UI_CMDFont_nameBox->currentFont().family());
    settings->setValue("CMDFont_Size", ui->Set_UI_CMDFont_sizeBox->value());
    settings->endGroup();
}

