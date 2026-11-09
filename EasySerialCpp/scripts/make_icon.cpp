// Deterministically render our vector artwork into a multi-resolution Windows icon.
#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QVector>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 3) return 1;
    QSvgRenderer renderer(args[1]);
    if (!renderer.isValid()) return 2;
    const QList<int> sizes{16, 24, 32, 48, 64, 128, 256};
    QVector<QByteArray> images;
    for (const int size : sizes) {
        // Supersample for legible, antialiased taskbar and title-bar icons.
        QImage large(size * 4, size * 4, QImage::Format_ARGB32_Premultiplied);
        large.fill(Qt::transparent);
        QPainter painter(&large);
        renderer.render(&painter);
        painter.end();
        const QImage image = large.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QByteArray png;
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return 3;
        images.append(png);
    }
    QFile output(args[2]);
    if (!output.open(QIODevice::WriteOnly)) return 4;
    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * sizes.size();
    for (qsizetype i = 0; i < sizes.size(); ++i) {
        const quint8 dimension = sizes[i] == 256 ? 0 : sizes[i];
        stream << dimension << dimension << quint8(0) << quint8(0)
               << quint16(1) << quint16(32) << quint32(images[i].size()) << offset;
        offset += images[i].size();
    }
    for (const auto &png : images) stream.writeRawData(png.data(), png.size());
    return stream.status() == QDataStream::Ok && output.flush() ? 0 : 5;
}
