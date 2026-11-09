#pragma once

#include <QPointer>
#include <QWidget>

#include "Settings.h"

class QAction;
class QLabel;
class QPropertyAnimation;
class QScreen;
class QSlider;
class QToolButton;
class QVBoxLayout;
class ReceiveView;

// A window shell only. Serial I/O and the single ReceiveView remain owned by
// the main application; close requests restore that view, never disconnect it.
class MonitorWindow final : public QWidget
{
    Q_OBJECT
public:
    explicit MonitorWindow(Settings &settings);
    void attachView(ReceiveView *view);
    ReceiveView *releaseView();
    void openOnScreen(QScreen *preferred);
    void saveState();
    void setConnectionStatus(const QString &text, bool connected, bool busy = false);
    void setAutoScrollEnabled(bool enabled);
    void setAutoSnap(bool enabled);
    void dockToCorner(Qt::Corner corner, bool animated = true);
    int backgroundOpacity() const { return m_opacity; }
    bool autoSnap() const { return m_autoSnap; }
    static QRect constrainedGeometry(QRect geometry, const QRect &available);
    static QRect cornerGeometry(QSize size, const QRect &available, Qt::Corner corner);
    static Qt::Corner nearestCorner(const QRect &geometry, const QRect &available);

signals:
    void restoreRequested();
    void connectionToggleRequested();
    void autoScrollToggled(bool enabled);
    void clearRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QScreen *currentScreen() const;
    void watchScreen(QScreen *screen);
    void ensureOnScreen();
    void moveInto(const QRect &geometry, bool animated);
    void finishDrag(const QPoint &globalPosition);
    void updateStatusLabel();
    Qt::Edges edgesAt(const QPoint &point) const;
    void updateResizeCursor(Qt::Edges edges);

    Settings &m_settings;
    QPointer<ReceiveView> m_view;
    QVBoxLayout *m_layout = nullptr;
    QWidget *m_titleBar = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_status = nullptr;
    QToolButton *m_connectionButton = nullptr;
    QToolButton *m_autoScrollButton = nullptr;
    QLabel *m_opacityLabel = nullptr;
    QLabel *m_fontLabel = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QSlider *m_fontSlider = nullptr;
    QAction *m_autoAction = nullptr;
    QAction *m_freeAction = nullptr;
    QPropertyAnimation *m_snapAnimation = nullptr;
    QMetaObject::Connection m_fontConnection;
    QString m_statusText;
    int m_opacity = 90;
    bool m_autoSnap = true;
    bool m_dragging = false;
    bool m_resizing = false;
    Qt::Corner m_corner = Qt::BottomRightCorner;
    Qt::Edges m_resizeEdges;
    QPoint m_pressGlobal;
    QRect m_pressGeometry;
};
