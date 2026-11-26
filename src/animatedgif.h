#pragma once
#include <QByteArray>
#include <QImage>
#include <QList>

// GIF stores frame delays in centiseconds. A missing loop extension means play once.
struct GifTiming {
    QList<quint16> delays;
    int repeats = -1;
};
bool readGifTiming(const QByteArray &bytes, GifTiming &timing);

class GifEncoder {
  public:
    GifEncoder(QSize canvas, int repeats);
    void useFrozenBackgroundPalette(const QImage &base);
    bool appendFrame(const QImage &image, quint16 delay);
    QByteArray finish();

  private:
    QSize size;
    QByteArray bytes;
    QList<QRgb> fixedPalette;
};
