#include "localresourcesdialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
QString dictionaryDirectory(const QString& dataDirectory)
{
    QString absolute;
    if(!LocalDictionary::validateLocalPath(dataDirectory, &absolute))
        return dataDirectory; // Keep the invalid input so the store reports it.
    return QDir(absolute).filePath(QStringLiteral("dictionaries"));
}
}

LocalResourcesDialog::LocalResourcesDialog(const QString& dataDirectory, QWidget* parent)
    : QDialog(parent),
      m_dictionaries(dictionaryDirectory(dataDirectory))
{
    setWindowTitle(tr("Local dictionaries"));
    resize(720, 580);

    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* description = new QLabel(tr("Import a local UTF-8 dictionary with one 12-digit hexadecimal key per line. "
                                       "Blank lines and comments beginning with #, ; or // are allowed. "
                                       "Duplicates are removed and keys are saved in uppercase."), this);
    description->setWordWrap(true);
    description->setTextFormat(Qt::PlainText);
    layout->addWidget(description);

    QLabel* folder = new QLabel(tr("Folder: %1").arg(m_dictionaries.directoryPath()), this);
    folder->setTextFormat(Qt::PlainText);
    folder->setTextInteractionFlags(Qt::TextSelectableByMouse);
    folder->setWordWrap(true);
    layout->addWidget(folder);

    m_table = new QTableWidget(0, 3, this);
    m_table->setObjectName(QStringLiteral("localDictionaryTable"));
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Keys"), tr("Status")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);

    QHBoxLayout* actions = new QHBoxLayout;
    QPushButton* importButton = new QPushButton(tr("Import..."), this);
    m_exportButton = new QPushButton(tr("Export..."), this);
    QPushButton* refreshButton = new QPushButton(tr("Refresh"), this);
    actions->addWidget(importButton);
    actions->addWidget(m_exportButton);
    actions->addWidget(refreshButton);
    actions->addStretch();
    layout->addLayout(actions);

    layout->addWidget(new QLabel(tr("Preview"), this));
    m_preview = new QPlainTextEdit(this);
    m_preview->setObjectName(QStringLiteral("localDictionaryPreview"));
    m_preview->setReadOnly(true);
    m_preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(m_preview, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_useButton = buttons->addButton(tr("Use selected dictionary"), QDialogButtonBox::AcceptRole);
    m_useButton->setDefault(false);
    m_useButton->setAutoDefault(false);
    layout->addWidget(buttons);

    connect(importButton, &QPushButton::clicked, this, &LocalResourcesDialog::importDictionary);
    connect(m_exportButton, &QPushButton::clicked, this, &LocalResourcesDialog::exportDictionary);
    connect(refreshButton, &QPushButton::clicked, this, &LocalResourcesDialog::refresh);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &LocalResourcesDialog::updatePreview);
    connect(m_useButton, &QPushButton::clicked, this, &LocalResourcesDialog::useSelectedDictionary);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refresh();
}

void LocalResourcesDialog::showStatus(const QString& message, bool error)
{
    m_status->setText(message);
    m_status->setStyleSheet(error ? QStringLiteral("color: #b00020;") : QString());
}

QString LocalResourcesDialog::selectedPath() const
{
    const int row = m_table->currentRow();
    const QTableWidgetItem* item = row >= 0 ? m_table->item(row, 0) : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void LocalResourcesDialog::selectPath(const QString& path)
{
    for(int row = 0; row < m_table->rowCount(); ++row)
    {
        if(m_table->item(row, 0)->data(Qt::UserRole).toString() == path)
        {
            m_table->selectRow(row);
            return;
        }
    }
}

void LocalResourcesDialog::refresh()
{
    const QString previousPath = selectedPath();
    QString error;
    const QList<LocalDictionary::DictionaryInfo> dictionaries = m_dictionaries.dictionaries(&error);
    {
        const QSignalBlocker blocker(m_table);
        m_table->setRowCount(0);
        for(const LocalDictionary::DictionaryInfo& dictionary : dictionaries)
        {
            const int row = m_table->rowCount();
            m_table->insertRow(row);
            QTableWidgetItem* name = new QTableWidgetItem(dictionary.name);
            name->setData(Qt::UserRole, dictionary.absolutePath);
            name->setToolTip(dictionary.absolutePath);
            m_table->setItem(row, 0, name);
            m_table->setItem(row, 1, new QTableWidgetItem(dictionary.isValid() ? QString::number(dictionary.keyCount) : QStringLiteral("—")));
            QTableWidgetItem* status = new QTableWidgetItem(dictionary.isValid() ? tr("Ready") : tr("Invalid"));
            status->setToolTip(dictionary.error);
            m_table->setItem(row, 2, status);
        }
    }
    selectPath(previousPath);
    updatePreview();
    if(!error.isEmpty())
        showStatus(error, true);
    else if(dictionaries.isEmpty())
        showStatus(tr("No dictionaries yet. Import a local file to get started."));
}

void LocalResourcesDialog::updatePreview()
{
    m_preview->clear();
    m_exportButton->setEnabled(false);
    m_useButton->setEnabled(false);
    const QString path = selectedPath();
    if(path.isEmpty())
    {
        showStatus(tr("Select a dictionary to preview or use it."));
        return;
    }

    LocalDictionary::ParseResult parsed;
    QString error;
    if(!LocalDictionary::readFile(path, &parsed, &error))
    {
        showStatus(error, true);
        return;
    }

    const int previewLimit = 500;
    m_preview->setPlainText(parsed.keys.mid(0, previewLimit).join(QLatin1Char('\n')));
    m_exportButton->setEnabled(true);
    m_useButton->setEnabled(true);
    QString message = tr("%1 unique keys.").arg(parsed.keys.size());
    if(parsed.keys.size() > previewLimit)
        message += tr(" Showing the first %1 keys.").arg(previewLimit);
    if(parsed.duplicateCount)
        message += tr(" %1 duplicate entries will be removed when exporting.").arg(parsed.duplicateCount);
    showStatus(message);
}

void LocalResourcesDialog::importDictionary()
{
    const QFileDialog::Options options = QFileDialog::DontUseNativeDialog | QFileDialog::DontResolveSymlinks;
    const QString source = QFileDialog::getOpenFileName(this, tr("Import a local dictionary"),
                                                       m_dictionaries.directoryPath(),
                                                       tr("Text dictionaries (*.dic *.txt);;All files (*)"),
                                                       nullptr, options);
    if(source.isEmpty())
        return;

    LocalDictionary::ParseResult parsed;
    QString error;
    if(!LocalDictionary::readFile(source, &parsed, &error))
    {
        showStatus(error, true);
        return;
    }

    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Dictionary name"), tr("Name (without extension):"),
                                              QLineEdit::Normal, QFileInfo(source).completeBaseName(), &accepted);
    if(!accepted)
        return;
    LocalDictionary::DictionaryInfo imported;
    if(!m_dictionaries.importFile(source, name, &imported, &error))
    {
        showStatus(error, true);
        return;
    }
    refresh();
    selectPath(imported.absolutePath);
    showStatus(tr("Imported %1 unique keys. Removed %2 duplicate entries.")
               .arg(imported.keyCount).arg(imported.duplicateCount));
}

void LocalResourcesDialog::exportDictionary()
{
    const QString source = selectedPath();
    if(source.isEmpty())
        return;
    const QFileDialog::Options options = QFileDialog::DontUseNativeDialog | QFileDialog::DontResolveSymlinks;
    const QString destination = QFileDialog::getSaveFileName(this, tr("Export a local dictionary"),
                                                            QDir(m_dictionaries.directoryPath()).filePath(QFileInfo(source).fileName()),
                                                            tr("Text dictionaries (*.dic);;All files (*)"),
                                                            nullptr, options);
    if(destination.isEmpty())
        return;
    QString error;
    if(!LocalDictionary::exportFile(source, destination, &error))
    {
        showStatus(error, true);
        return;
    }
    refresh();
    showStatus(tr("Dictionary exported successfully."));
}

void LocalResourcesDialog::useSelectedDictionary()
{
    const QString path = selectedPath();
    LocalDictionary::ParseResult parsed;
    QString error;
    // Revalidate at the point of use in case a local file changed since preview.
    if(path.isEmpty() || !LocalDictionary::readFile(path, &parsed, &error))
    {
        showStatus(error.isEmpty() ? tr("Select a valid dictionary first.") : error, true);
        m_useButton->setEnabled(false);
        return;
    }
    QString absolute;
    if(!LocalDictionary::validateLocalPath(path, &absolute, &error))
    {
        showStatus(error, true);
        return;
    }
    emit dictionarySelected(absolute);
    accept();
}
