#include "animatedgif.h"
#include <QHash>
#include <algorithm>
#include <array>
#include <vector>

namespace {
// Quantize each frame separately. A fixed RGB cube visibly shifts flat meme backgrounds.
struct ColorBin {
    quint32 count = 0;
    std::array<quint64, 3> sum{};
    uchar index = 0;
};
int binKey(QRgb rgb) { return ((qRed(rgb) >> 3) << 10) | ((qGreen(rgb) >> 3) << 5) | (qBlue(rgb) >> 3); }
struct ColorBox {
    std::vector<int> keys;
    int splitShift = 0, splitAt = 0;
    quint64 priority = 0;
    void measure(const std::vector<ColorBin> &histogram) {
        std::array<int, 3> low{31, 31, 31}, high{};
        quint64 pixels = 0;
        for (int key : keys) {
            pixels += histogram[key].count;
            for (int axis = 0; axis < 3; ++axis) {
                const int value = (key >> (axis * 5)) & 31;
                low[axis] = std::min(low[axis], value);
                high[axis] = std::max(high[axis], value);
            }
        }
        priority = 0;
        for (int axis = 0; axis < 3; ++axis) {
            const unsigned range = high[axis] - low[axis];
            const auto score = pixels * range * range;
            if (score > priority) {
                priority = score;
                splitShift = axis * 5;
                splitAt = (high[axis] + low[axis]) / 2;
            }
        }
    }
};
QImage indexedFrame(const QImage &input, size_t maxColors = 256) {
    if (input.format() == QImage::Format_Indexed8 && size_t(input.colorCount()) <= maxColors)
        return input;
    const QImage image = input.convertToFormat(QImage::Format_RGB32);
    QImage indexed(image.size(), QImage::Format_Indexed8);
    if (indexed.isNull())
        return {};
    std::vector<ColorBin> histogram(1 << 15);
    ColorBox all;
    for (int y = 0; y < image.height(); ++y) {
        auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb rgb = row[x];
            const int key = binKey(rgb);
            auto &bin = histogram[key];
            if (!bin.count++)
                all.keys.push_back(key);
            bin.sum[0] += qRed(rgb);
            bin.sum[1] += qGreen(rgb);
            bin.sum[2] += qBlue(rgb);
        }
    }
    all.measure(histogram);
    std::vector<ColorBox> boxes;
    boxes.push_back(std::move(all));
    while (boxes.size() < maxColors) {
        auto box = std::max_element(boxes.begin(), boxes.end(),
                                    [](const auto &a, const auto &b) { return a.priority < b.priority; });
        if (!box->priority)
            break;
        auto middle = std::partition(box->keys.begin(), box->keys.end(), [&](int key) {
            return ((key >> box->splitShift) & 31) <= box->splitAt;
        });
        ColorBox right;
        right.keys.assign(middle, box->keys.end());
        box->keys.erase(middle, box->keys.end());
        right.measure(histogram);
        box->measure(histogram);
        boxes.push_back(std::move(right));
    }
    QList<QRgb> palette;
    for (const auto &box : boxes) {
        std::array<quint64, 3> sum{};
        quint64 count = 0;
        for (int key : box.keys) {
            auto &bin = histogram[key];
            bin.index = uchar(palette.size());
            count += bin.count;
            for (int i = 0; i < 3; ++i)
                sum[i] += bin.sum[i];
        }
        palette.append(qRgb(int(sum[0] / count), int(sum[1] / count), int(sum[2] / count)));
    }
    indexed.setColorTable(palette);
    for (int y = 0; y < image.height(); ++y) {
        auto *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        auto *dest = indexed.scanLine(y);
        for (int x = 0; x < image.width(); ++x)
            dest[x] = histogram[binKey(row[x])].index;
    }
    return indexed;
}

void word(QByteArray &out, int value) {
    out.append(char(value & 255));
    out.append(char((value >> 8) & 255));
}

class GifCodes {
  public:
    explicit GifCodes(QByteArray &target) : out(target) {}
    void emitCode(int code, int width) {
        accumulator |= quint32(code) << bitCount;
        bitCount += width;
        while (bitCount >= 8) {
            emitByte(uchar(accumulator & 255));
            accumulator >>= 8;
            bitCount -= 8;
        }
    }
    void finish() {
        if (bitCount)
            emitByte(uchar(accumulator));
        flush();
        out.append(char(0));
    }

  private:
    void emitByte(uchar b) {
        block.append(char(b));
        if (block.size() == 255)
            flush();
    }
    void flush() {
        if (block.isEmpty())
            return;
        out.append(char(block.size()));
        out.append(block);
        block.clear();
    }
    QByteArray &out;
    QByteArray block;
    quint32 accumulator = 0;
    int bitCount = 0;
};

void encodePixels(QByteArray &out, const QImage &indexed, int bits) {
    out.append(char(bits));
    const int clear = 1 << bits, end = clear + 1;
    int width = bits + 1, next = end + 1, prefix = -1;
    QHash<int, int> dict;
    dict.reserve(4096);
    GifCodes stream(out);
    stream.emitCode(clear, width);
    for (int y = 0; y < indexed.height(); ++y) {
        const uchar *row = indexed.constScanLine(y);
        for (int x = 0; x < indexed.width(); ++x) {
            const int symbol = row[x];
            if (prefix < 0) {
                prefix = symbol;
                continue;
            }
            const int pair = (prefix << 8) | symbol;
            auto match = dict.constFind(pair);
            if (match != dict.cend()) {
                prefix = *match;
                continue;
            }
            stream.emitCode(prefix, width);
            // The decoder creates the previous dictionary entry when it reads this code.
            if (next == (1 << width) && width < 12)
                ++width;
            if (next < 4096)
                dict.insert(pair, next++);
            else {
                stream.emitCode(clear, width);
                dict.clear();
                next = end + 1;
                width = bits + 1;
            }
            prefix = symbol;
        }
    }
    if (prefix >= 0) {
        stream.emitCode(prefix, width);
        if (next == (1 << width) && width < 12)
            ++width;
    }
    stream.emitCode(end, width);
    stream.finish();
}
} // namespace

bool readGifTiming(const QByteArray &data, GifTiming &timing) {
    if (data.size() < 13 || !(data.startsWith("GIF89a") || data.startsWith("GIF87a")))
        return false;
    GifTiming found;
    auto at = [&](qsizetype p) { return static_cast<unsigned char>(data[p]); };
    auto palette = [&](int flags) { return (flags & 128) ? 3 * (2 << (flags & 7)) : 0; };
    qsizetype pos = 13 + palette(at(10));
    quint16 delay = 0;
    while (pos < data.size()) {
        const int type = at(pos++);
        if (type == 0x3b) {
            if (found.delays.isEmpty())
                return false;
            timing = std::move(found);
            return true;
        }
        if (type == 0x2c) {
            if (data.size() - pos < 9)
                return false;
            pos += 9 + palette(at(pos + 8));
            if (pos >= data.size())
                return false;
            ++pos;
            found.delays.append(delay);
            delay = 0;
        } else if (type == 0x21) {
            if (pos >= data.size())
                return false;
            int label = at(pos++);
            if (label == 0xf9 && data.size() - pos >= 6 && at(pos) == 4)
                delay = quint16(at(pos + 2) | (at(pos + 3) << 8));
            if (label == 0xff && data.size() - pos >= 16 && at(pos) == 11 &&
                (data.mid(pos + 1, 11) == "NETSCAPE2.0" || data.mid(pos + 1, 11) == "ANIMEXTS1.0") &&
                at(pos + 12) == 3 && at(pos + 13) == 1)
                found.repeats = at(pos + 14) | (at(pos + 15) << 8);
            if (label == 0x01)
                delay = 0;
        } else
            return false;
        while (true) {
            if (pos >= data.size())
                return false;
            const int length = at(pos++);
            if (length == 0)
                break;
            if (data.size() - pos < length)
                return false;
            pos += length;
        }
    }
    return false;
}

GifEncoder::GifEncoder(QSize canvas, int repeats) : size(canvas) {
    if (size.isEmpty() || size.width() > 65535 || size.height() > 65535)
        return;
    bytes.append("GIF89a", 6);
    word(bytes, size.width());
    word(bytes, size.height());
    // Use a two-color global table and an optimized local palette for each full frame.
    bytes.append('\xf0');
    bytes.append(2, char(0));
    bytes.append(3, char(0));
    bytes.append(3, '\xff');
    if (repeats >= 0) {
        bytes.append("\x21\xff\x0b"
                     "NETSCAPE2.0"
                     "\x03\x01",
                     16);
        word(bytes, repeats);
        bytes.append(char(0));
    }
}
void GifEncoder::useFrozenBackgroundPalette(const QImage &base) {
    // Reserve colors for moving subjects, and quantize static pixels identically in every frame.
    fixedPalette = indexedFrame(base, 192).colorTable();
    for (int r : {0, 85, 170, 255})
        for (int g : {0, 85, 170, 255})
            for (int b : {0, 85, 170, 255}) {
                const auto color = qRgb(r, g, b);
                if (!fixedPalette.contains(color))
                    fixedPalette.append(color);
            }
}
bool GifEncoder::appendFrame(const QImage &image, quint16 delay) {
    if (bytes.isEmpty() || image.size() != size)
        return false;
    const auto indexed = fixedPalette.isEmpty() ? indexedFrame(image)
                                                : image.convertToFormat(QImage::Format_Indexed8,
                                                                        fixedPalette, Qt::ThresholdDither);
    if (indexed.isNull() || indexed.colorCount() < 1)
        return false;
    const auto table = indexed.colorTable();
    int bits = 1;
    while ((qsizetype(1) << bits) < table.size())
        ++bits;
    bytes.append("\x21\xf9\x04\x04", 4);
    word(bytes, delay);
    bytes.append(2, char(0));
    bytes.append(char(0x2c));
    bytes.append(4, char(0));
    word(bytes, size.width());
    word(bytes, size.height());
    bytes.append(char(0x80 | (bits - 1)));
    for (int i = 0; i < (1 << bits); ++i) {
        const QRgb rgb = table.value(i, qRgb(0, 0, 0));
        bytes.append(char(qRed(rgb)));
        bytes.append(char(qGreen(rgb)));
        bytes.append(char(qBlue(rgb)));
    }
    encodePixels(bytes, indexed, std::max(2, bits));
    return true;
}
QByteArray GifEncoder::finish() {
    if (!bytes.isEmpty())
        bytes.append(char(0x3b));
    return std::move(bytes);
}
