#pragma once
#include <QWidget>
#include <QStringList>
#include <memory>

class Settings;
class ProtoSchema;

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class PbDecoderWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit PbDecoderWidget(Settings &settings, QWidget *parent = nullptr);
    ~PbDecoderWidget() override;
    void importFiles(const QStringList &paths);
    void setIncludeFolders(const QStringList &paths);
private:
    void loadSchema();
    void editIncludeFolders();
    void saveCatalog();
    void scheduleParse();
    void parse();
    Settings &m_settings;
    std::unique_ptr<ProtoSchema> m_schema;
    QStringList m_includeFolders;
    QString m_schemaError;
    QComboBox *m_file = nullptr;
    QLabel *m_schemaStatus = nullptr;
    QPushButton *m_remove = nullptr;
    QComboBox *m_type = nullptr;
    QComboBox *m_mode = nullptr;
    QSpinBox *m_offset = nullptr;
    QCheckBox *m_live = nullptr;
    QCheckBox *m_defaults = nullptr;
    QPlainTextEdit *m_input = nullptr;
    QPlainTextEdit *m_output = nullptr;
    QLineEdit *m_prefix = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_copy = nullptr;
    QTimer *m_timer = nullptr;
};
