#ifndef LOCALRESOURCESDIALOG_H
#define LOCALRESOURCESDIALOG_H

#include <QDialog>
#include "../common/localdictionary.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;

class LocalResourcesDialog : public QDialog
{
    Q_OBJECT
public:
    // Dictionaries are kept in dataDirectory/dictionaries. The parent chooses
    // dataDirectory; this dialog never uses a seller endpoint or account.
    explicit LocalResourcesDialog(const QString& dataDirectory, QWidget* parent = nullptr);

signals:
    void dictionarySelected(QString absolutePath);

public slots:
    void refresh();

private slots:
    void importDictionary();
    void exportDictionary();
    void updatePreview();
    void useSelectedDictionary();

private:
    QString selectedPath() const;
    void selectPath(const QString& path);
    void showStatus(const QString& message, bool error = false);

    LocalDictionary m_dictionaries;
    QTableWidget* m_table;
    QPlainTextEdit* m_preview;
    QLabel* m_status;
    QPushButton* m_exportButton;
    QPushButton* m_useButton;
};

#endif // LOCALRESOURCESDIALOG_H
