#include "ui/AppearanceDialog.h"

#include <QColorDialog>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

AppearanceDialog::AppearanceDialog(Settings &settings, QWidget *parent)
    : QDialog(parent), m_settings(settings)
{
    setWindowTitle(QStringLiteral("Appearance"));
    setMinimumWidth(320);
    auto *root = new QVBoxLayout(this);
    root->setSpacing(14);
    root->setContentsMargins(20, 20, 20, 20);
    auto *fontRow = new QHBoxLayout;
    fontRow->addWidget(new QLabel(QStringLiteral("Font"), this));
    m_font = new QComboBox(this);
    QStringList fonts;
    for (const QString &family : QFontDatabase::families()) {
        if (QFontDatabase::isFixedPitch(family)) fonts.push_back(family);
    }
    if (fonts.isEmpty()) fonts = QFontDatabase::families();
    m_font->addItems(fonts);
    m_font->setCurrentText(settings.stringValue("font_family", "Consolas"));
    fontRow->addWidget(m_font, 1);
    root->addLayout(fontRow);

    const QList<QPair<QString, QString>> colors{
        {QStringLiteral("Text color"), "col_text"},
        {QStringLiteral("Background color"), "col_bg"},
        {QStringLiteral("Timestamp color"), "col_ts"},
        {QStringLiteral("HEX byte color"), "col_hex"},
        {QStringLiteral("ASCII character color"), "col_ascii"}
    };
    for (const auto &[label, key] : colors) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(label, this));
        row->addStretch();
        auto *button = new QPushButton(this);
        button->setFixedSize(50, 26);
        m_swatches.insert(key, button);
        updateSwatch(key);
        connect(button, &QPushButton::clicked, this, [this, key] { chooseColor(key); });
        row->addWidget(button);
        root->addLayout(row);
    }
    connect(m_font, &QComboBox::currentTextChanged, this, [this](const QString &family) {
        m_settings.setValue("font_family", family);
        emit changed();
    });
    auto *tip = new QLabel(QStringLiteral("Changes apply immediately. Text color applies to ASCII receive and send data. HEX bytes, dual-view ASCII and timestamps use their own colors."), this);
    tip->setWordWrap(true);
    tip->setObjectName(QStringLiteral("dim"));
    root->addWidget(tip);
}

void AppearanceDialog::chooseColor(const QString &key)
{
    const QColor color = QColorDialog::getColor(
        QColor(m_settings.stringValue(key)), this, QStringLiteral("Choose Color"));
    if (!color.isValid()) return;
    m_settings.setValue(key, color.name());
    updateSwatch(key);
    emit changed();
}

void AppearanceDialog::updateSwatch(const QString &key)
{
    m_swatches.value(key)->setStyleSheet(
        QStringLiteral("background:%1;border:1px solid #555;border-radius:5px;")
            .arg(m_settings.stringValue(key)));
}
