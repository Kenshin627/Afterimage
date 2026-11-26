#include "editor.h"
#include "dialogs.h"
#include "style.h"
#include "widgets.h"
#include <QApplication>
#include <QBuffer>
#include <QButtonGroup>
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMessageBox>
#include <QMimeData>
#include <QMimeDatabase>
#include <QSettings>
#include <QStandardPaths>

namespace {
const QString imageFilter = "图像 (*.jpg *.jpeg *.png *.webp *.bmp *.gif *.tif *.tiff);;所有文件 (*)";
QString lastDir(const QString &key) {
    return QSettings()
        .value(key, QStandardPaths::writableLocation(QStandardPaths::PicturesLocation))
        .toString();
}
void keepDir(const QString &key, const QString &file) {
    QSettings().setValue(key, QFileInfo(file).absolutePath());
}
QString safeName(QString s) {
    for (QChar c : QStringLiteral("\\/:*?\"<>|"))
        s.replace(c, '_');
    return s.trimmed().left(80);
}
} // namespace

void Editor::openImage() {
    const QString file = QFileDialog::getOpenFileName(this, "打开图像", lastDir("importDir"), imageFilter);
    if (!file.isEmpty() && openPath(file))
        keepDir("importDir", file);
}
bool Editor::openPath(const QString &path) {
    QString error;
    auto input = openPatinaInput(path, &error);
    if (input.image.isNull()) {
        warn("无法打开图像", QFileInfo(path).fileName() + "\n" + error);
        return false;
    }
    setSource(std::move(input));
    return true;
}
void Editor::paste() {
    const QMimeData *m = QApplication::clipboard()->mimeData();
    if (!m) {
        message("剪贴板为空");
        return;
    }
    for (const auto &url : m->urls())
        if (url.isLocalFile() && openPath(url.toLocalFile()))
            return;
    if (m->hasFormat("image/gif")) {
        QString error;
        auto input = gifPatinaInput(m->data("image/gif"), "剪贴板 GIF", &error);
        if (!input.image.isNull()) {
            setSource(std::move(input));
            return;
        }
    }
    if (m->hasImage()) {
        auto img = QApplication::clipboard()->image();
        if (!img.isNull()) {
            setSource(img, "剪贴板图像");
            return;
        }
    }
    message("剪贴板中没有可用图像");
}
void Editor::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasImage() || e->mimeData()->hasUrls() || e->mimeData()->hasFormat("image/gif"))
        e->acceptProposedAction();
}
void Editor::dropEvent(QDropEvent *e) {
    for (const auto &url : e->mimeData()->urls())
        if (url.isLocalFile()) {
            if (openPath(url.toLocalFile()))
                e->acceptProposedAction();
            return;
        }
    if (e->mimeData()->hasFormat("image/gif")) {
        QString error;
        auto input = gifPatinaInput(e->mimeData()->data("image/gif"), "拖入的 GIF", &error);
        if (!input.image.isNull()) {
            setSource(std::move(input));
            e->acceptProposedAction();
            return;
        }
    }
    if (e->mimeData()->hasImage()) {
        auto image = qvariant_cast<QImage>(e->mimeData()->imageData());
        if (!image.isNull()) {
            setSource(image, "拖入的图像");
            e->acceptProposedAction();
        }
    }
}
void Editor::copyImage() {
    if (!ready()) {
        message("画面处理完成后即可复制");
        return;
    }
    QApplication::clipboard()->setImage(frame.processed);
    message("效果图已复制到剪贴板");
}
void Editor::exportImage() {
    if (!ready()) {
        message("画面处理完成后即可导出");
        return;
    }
    const Frame snapshot = frame;
    const QString stem = safeName(QFileInfo(document.name).completeBaseName()) + " - " +
                         safeName(current.style["name"].toString());
    QFileDialog dialog(this, "导出效果图", lastDir("exportDir"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters({"PNG 图像 (*.png)", "JPEG 图像 (*.jpg *.jpeg)", "WebP 图像 (*.webp)"});
    dialog.setDefaultSuffix("png");
    dialog.selectFile(stem);
    connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog](const QString &f) {
        dialog.setDefaultSuffix(f.startsWith("JPEG") ? "jpg" : f.startsWith("WebP") ? "webp" : "png");
    });
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    QString path = dialog.selectedFiles().first();
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext.isEmpty())
        path += "." + dialog.defaultSuffix();
    else if (!QStringList{"png", "jpg", "jpeg", "webp"}.contains(ext)) {
        warn("无法导出", "请选择 PNG、JPEG 或 WebP 扩展名。");
        return;
    }
    QString error;
    if (!saveFrame(snapshot, path, &error)) {
        warn("导出失败", error);
        return;
    }
    keepDir("exportDir", path);
    message("已导出  " + QFileInfo(path).fileName());
}
void Editor::chooseWatermark() {
    const QString path =
        QFileDialog::getOpenFileName(this, "选择自定义水印", lastDir("importDir"), imageFilter);
    if (path.isEmpty())
        return;
    QString error;
    QImage mark = readImage(path, &error);
    if (mark.isNull()) {
        warn("无法读取水印", error);
        return;
    }
    QByteArray png;
    QBuffer b(&png);
    b.open(QIODevice::WriteOnly);
    if (!mark.save(&b, "PNG") || png.size() > 18 * 1024 * 1024) {
        warn("水印过大", "请选择编码为 PNG 后小于 18 MB 的水印图像。");
        return;
    }
    change("logoPng", QString::fromLatin1(png.toBase64()));
    message("已应用自定义水印");
}
bool Editor::updatePersonal(QJsonObject preset, bool remove) {
    const QString path = dataDir + "/personal.json";
    QLockFile lock(path + ".lock");
    if (!lock.tryLock(1200)) {
        warn("保存失败", "个人预设库正在被另一个窗口使用，请稍后重试。");
        return false;
    }
    QFile file(path);
    QJsonArray array;
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            warn("读取个人预设失败", file.errorString());
            return false;
        }
        QJsonParseError pe;
        const auto doc = QJsonDocument::fromJson(file.readAll(), &pe);
        file.close();
        if (pe.error != QJsonParseError::NoError || !doc.isArray()) {
            warn("预设库格式异常", path);
            return false;
        }
        array = doc.array();
    }
    if (remove) {
        bool found = false;
        for (qsizetype i = 0; i < array.size(); ++i)
            if (completeStyle(array[i].toObject()) == preset) {
                array.removeAt(i);
                found = true;
                break;
            }
        if (!found) {
            personal = readCollection(path);
            rebuildLibrary();
            message("该预设已移除");
            return false;
        }
    } else {
        const QString base = preset["name"].toString();
        QSet<QString> used;
        for (auto entry : array)
            used.insert(entry.toObject()["name"].toString());
        QString name = base;
        for (int n = 2; used.contains(name); ++n)
            name = base.left(54) + QString(" (%1)").arg(n);
        preset["name"] = name;
        array.append(preset);
    }
    QString err;
    if (!writeAtomically(path, QJsonDocument(array).toJson(QJsonDocument::Compact), &err)) {
        warn("保存个人预设失败", err);
        return false;
    }
    personal = readCollection(path);
    if (!remove) {
        libraryIndex = 1;
        libraryTabs->button(1)->setChecked(true);
        search->clear();
        applyPreset(preset);
    }
    rebuildLibrary();
    return true;
}
void Editor::savePersonal() {
    bool ok = false;
    QString name = QInputDialog::getText(this, "保存个人预设", "预设名称", QLineEdit::Normal,
                                         current.style["name"].toString(), &ok)
                       .simplified()
                       .left(60);
    if (!ok || name.isEmpty())
        return;
    QJsonObject preset = current.style;
    preset["name"] = name;
    if (updatePersonal(preset, false))
        message("已保存至个人预设库");
}
void Editor::removePersonal() {
    auto *item = presetList->currentItem();
    if (libraryIndex != 1 || !item)
        return;
    const auto preset = item->data(Qt::UserRole).toJsonObject();
    if (QMessageBox::question(this, "删除个人预设", "删除「" + preset["name"].toString() + "」？",
                              QMessageBox::Yes | QMessageBox::Cancel,
                              QMessageBox::Cancel) == QMessageBox::Yes)
        if (updatePersonal(preset, true))
            message("个人预设已删除");
}
void Editor::exportPreset() {
    const QString path = QFileDialog::getSaveFileName(
        this, "导出预设文件",
        QDir(lastDir("presetDir")).filePath(safeName(current.style["name"].toString()) + ".json"),
        "预设文件 (*.json)");
    if (path.isEmpty())
        return;
    QJsonObject doc{{"format", "afterimage-preset"}, {"version", 1}, {"style", current.style}};
    QString error;
    if (!writeAtomically(path, QJsonDocument(doc).toJson(), &error)) {
        warn("导出预设失败", error);
        return;
    }
    keepDir("presetDir", path);
    message("预设文件已导出");
}
void Editor::importPreset() {
    const QString path =
        QFileDialog::getOpenFileName(this, "导入预设文件", lastDir("presetDir"), "预设文件 (*.json)");
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        warn("读取失败", file.errorString());
        return;
    }
    if (file.size() > 28 * 1024 * 1024) {
        warn("导入失败", "预设文件过大。");
        return;
    }
    QJsonObject doc = QJsonDocument::fromJson(file.readAll()).object();
    if (doc.contains("format") && (doc["format"] != "afterimage-preset" || doc["version"].toInt() != 1)) {
        warn("导入失败", "此预设文件版本不受支持。");
        return;
    }
    const auto raw = doc.contains("style") ? doc["style"].toObject() : doc;
    if (!isPreset(raw)) {
        warn("导入失败", "文件需要包含预设名称与有效的图像参数。");
        return;
    }
    if (raw.contains("logoPng") &&
        QImage::fromData(QByteArray::fromBase64(raw["logoPng"].toString().toLatin1()), "PNG").isNull()) {
        warn("导入失败", "预设中的自定义水印无法读取。");
        return;
    }
    if (updatePersonal(completeStyle(raw), false)) {
        keepDir("presetDir", path);
        message("预设已导入并应用");
    }
}
void Editor::refreshPublic() {
    libraryIndex = 2;
    libraryTabs->button(2)->setChecked(true);
    rebuildLibrary();
    checkedPublic = true;
    syncPublic(true);
}
void Editor::syncPublic(bool interactive) {
    if (refreshing)
        return;
    refreshing = true;
    message("正在获取公共预设…");
    community.presets([this, interactive](QByteArray raw, QString error) {
        refreshing = false;
        const auto doc = QJsonDocument::fromJson(raw);
        QVector<QJsonObject> updated;
        if (error.isEmpty() && doc.isArray())
            for (auto v : doc.array())
                if (!v.toObject()["name"].toString().trimmed().isEmpty())
                    updated << completeStyle(v.toObject());
        if (!error.isEmpty() || !doc.isArray() || (!doc.array().isEmpty() && updated.isEmpty())) {
            if (interactive)
                warn("公共预设刷新失败", error.isEmpty() ? "原站返回了无法识别的预设数据。" : error);
            message("公共预设暂时离线，已保留本机列表");
            return;
        }
        publicStyles = updated;
        QString cacheError;
        bool saved = writeAtomically(dataDir + "/community.json", raw, &cacheError);
        if (libraryIndex == 2)
            rebuildLibrary();
        message(saved ? QString("已同步 %1 个公共预设").arg(updated.size())
                      : "已更新本次列表，缓存写入失败  " + cacheError);
    });
}
void Editor::gallery() {
    const QString name = chooseSample(this, community, galleryNames);
    if (name.isEmpty())
        return;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/samples";
    QDir().mkpath(dir);
    const QString path = dir + "/" + name;
    if (QFileInfo::exists(path)) {
        QImage cached = readImage(path);
        if (!cached.isNull()) {
            setSource(cached, name);
            return;
        }
    }
    const int epoch = ++sourceEpoch;
    message("正在载入  " + name);
    community.sample(name, false, [this, epoch, path, name](QByteArray bytes, QString err) {
        QImage img = QImage::fromData(bytes);
        if (!img.isNull() && err.isEmpty())
            writeAtomically(path, bytes);
        if (epoch != sourceEpoch)
            return;
        if (img.isNull() || !err.isEmpty()) {
            warn("样片载入失败", err.isEmpty() ? "无法读取原站的图像。" : err);
            return;
        }
        setSource(std::move(img), name);
    });
}
void Editor::submit(bool capture) {
    if (uploading) {
        message("上一份投稿正在发送");
        return;
    }
    QByteArray bytes, mime;
    QString filename;
    QImage preview;
    if (capture) {
        const QString path = QFileDialog::getOpenFileName(this, "选择要授权原站使用的样片",
                                                          lastDir("importDir"), imageFilter);
        if (path.isEmpty())
            return;
        QString err;
        preview = readImage(path, &err);
        if (preview.isNull()) {
            warn("无法读取样片", err);
            return;
        }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            warn("无法读取样片", file.errorString());
            return;
        }
        bytes = file.readAll();
        filename = QFileInfo(path).fileName();
        mime = QMimeDatabase().mimeTypeForData(bytes).name().toLatin1();
    } else {
        if (!ready()) {
            message("请在画面处理完成后分享");
            return;
        }
        bytes = frame.jpeg;
        mime = "image/jpeg";
        filename = "preview.jpg";
        preview = frame.processed;
    }
    QJsonObject preset = shareableStyle(current.style);
    const auto entry = confirmSubmission(this, capture, preset["name"].toString(), preview,
                                         current.style.contains("logoPng"));
    if (!entry.accepted)
        return;
    preset["name"] = entry.name;
    uploading = true;
    message("正在向原站提交…");
    community.submit(capture, bytes, mime, filename, preset, entry.author, [this](QString error) {
        uploading = false;
        if (!error.isEmpty()) {
            warn("提交未完成", error);
            return;
        }
        QMessageBox::information(this, "已提交", "原站已收件。内容经原站审核后可公开显示。");
    });
}
