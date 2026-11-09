#include "ui/SearchBar.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QKeyEvent>
#include <QShortcut>
#include <QTimer>

SearchBar::SearchBar(QWidget *parent)
    : QFrame(parent)
{
    setVisible(false);
    setFrameShape(QFrame::StyledPanel);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("Find in received data…"));
    m_regex = new QCheckBox(QStringLiteral("Regex"), this);
    m_case = new QCheckBox(QStringLiteral("Case sensitive"), this);
    m_word = new QCheckBox(QStringLiteral("Whole word"), this);
    m_status = new QLabel(QStringLiteral("0 / 0"), this);
    m_status->setObjectName(QStringLiteral("searchCount"));
    auto *previous = new QPushButton(QStringLiteral("↑"), this);
    auto *next = new QPushButton(QStringLiteral("↓"), this);
    auto *close = new QPushButton(QStringLiteral("×"), this);
    previous->setFixedWidth(34);
    next->setFixedWidth(34);
    close->setFixedWidth(34);
    layout->addWidget(m_input, 1);
    layout->addWidget(m_regex);
    layout->addWidget(m_case);
    layout->addWidget(m_word);
    layout->addWidget(m_status);
    layout->addWidget(previous);
    layout->addWidget(next);
    layout->addWidget(close);

    connect(m_input, &QLineEdit::returnPressed, this, [this] { request(false); });
    connect(previous, &QPushButton::clicked, this, [this] { request(true); });
    connect(next, &QPushButton::clicked, this, [this] { request(false); });
    connect(close, &QPushButton::clicked, this, [this] { hide(); emit searchClosed(); });
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(150);
    connect(m_debounce, &QTimer::timeout, this, [this] {
        if (isVisible()) { emit searchClosed(); request(false); }
    });
    connect(m_input, &QLineEdit::textChanged, this, [this] { m_debounce->start(); });
    for (auto *option : {m_regex, m_case, m_word})
        connect(option, &QCheckBox::toggled, this, [this] { m_debounce->start(); });
    auto *previousShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Return), this);
    previousShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previousShortcut, &QShortcut::activated, this, [this] { request(true); });
}

void SearchBar::showAndFocus()
{
    show();
    raise();
    m_input->setFocus();
    m_input->selectAll();
    emit searchClosed();
    request(false);
}

void SearchBar::request(bool backwards)
{
    emit searchRequested(m_input->text(), m_regex->isChecked(),
                         m_case->isChecked(), backwards, m_word->isChecked());
}

void SearchBar::setResult(int current, int count, const QString &error)
{
    m_status->setText(error.isEmpty() ? QStringLiteral("%1 / %2").arg(current).arg(count) : error);
    m_status->setStyleSheet(error.isEmpty() ? QString{} : QStringLiteral("color:#DC2626;"));
}

void SearchBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit searchClosed();
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}
