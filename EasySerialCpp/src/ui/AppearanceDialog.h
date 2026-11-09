#pragma once

#include <QDialog>
#include <QMap>

#include "Settings.h"

class QComboBox;
class QPushButton;

class AppearanceDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit AppearanceDialog(Settings &settings, QWidget *parent = nullptr);

signals:
    void changed();

private:
    void chooseColor(const QString &key);
    void updateSwatch(const QString &key);

    Settings &m_settings;
    QComboBox *m_font = nullptr;
    QMap<QString, QPushButton *> m_swatches;
};

