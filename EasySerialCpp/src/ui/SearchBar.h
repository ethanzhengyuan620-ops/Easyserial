#pragma once

#include <QFrame>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QKeyEvent;
class QTimer;

class SearchBar final : public QFrame
{
    Q_OBJECT
public:
    explicit SearchBar(QWidget *parent = nullptr);
    void showAndFocus();
    void setResult(int current, int count, const QString &error);

signals:
    void searchRequested(const QString &text, bool regex,
                         bool caseSensitive, bool backwards, bool wholeWord);
    void searchClosed();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void request(bool backwards);

    QLineEdit *m_input = nullptr;
    QCheckBox *m_regex = nullptr;
    QCheckBox *m_case = nullptr;
    QCheckBox *m_word = nullptr;
    QTimer *m_debounce = nullptr;
    QLabel *m_status = nullptr;
};
