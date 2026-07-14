#include "qrscanner.h"

#include <QImage>
#include <QVideoFrame>

#include <ZXing/ReadBarcode.h>

namespace app {

namespace {

// Intervalle minimal entre deux décodages (ms).
constexpr qint64 kThrottleMs = 150;

// Au-delà, l'image est réduite : un QR reste lisible bien en dessous de la
// résolution d'une caméra moderne, et le décodage coûte en O(pixels).
constexpr int kMaxWidth = 720;

} // namespace

QrScanner::QrScanner(QObject* parent)
    : QObject(parent)
{
    m_since.start();
}

void QrScanner::setVideoSink(QVideoSink* sink)
{
    if (m_sink == sink)
        return;

    if (m_sink)
        disconnect(m_sink, nullptr, this, nullptr);

    m_sink = sink;

    if (m_sink) {
        connect(m_sink, &QVideoSink::videoFrameChanged,
                this,   &QrScanner::onFrame);
    }

    emit videoSinkChanged();
}

void QrScanner::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (m_active)
        m_found = false;   // nouveau scan : réarmer
    emit activeChanged();
}

void QrScanner::onFrame(const QVideoFrame& frame)
{
    if (!m_active || m_found || !frame.isValid())
        return;

    if (m_since.elapsed() < kThrottleMs)
        return;
    m_since.restart();

    QImage image = frame.toImage();
    if (image.isNull())
        return;

    if (image.width() > kMaxWidth)
        image = image.scaledToWidth(kMaxWidth, Qt::FastTransformation);

    // ZXing travaille en luminance : la conversion explicite évite qu'il redécode
    // le format à chaque appel, et rend le résultat indépendant de la caméra.
    image = image.convertToFormat(QImage::Format_Grayscale8);

    const ZXing::ImageView view(image.constBits(),
                                image.width(),
                                image.height(),
                                ZXing::ImageFormat::Lum,
                                static_cast<int>(image.bytesPerLine()));

    ZXing::ReaderOptions options;
    options.setFormats(ZXing::BarcodeFormat::QRCode);
    options.setTryHarder(true);
    options.setTryRotate(true);

    const auto result = ZXing::ReadBarcode(view, options);
    if (!result.isValid())
        return;

    const QString text = QString::fromStdString(result.text());
    if (text.isEmpty())
        return;

    m_found = true;
    emit codeDetected(text);
}

} // namespace app
