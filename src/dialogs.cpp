#include "dialogs.h"
#include "widgets.h"
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QRegularExpression>
#include <QVBoxLayout>

QString chooseSample(QWidget *parent, Community &community, const QStringList &names) {
    QDialog dialog(parent);
    dialog.setWindowTitle("原站样片库");
    dialog.resize(720, 530);
    auto *layout = vertical(&dialog, 18, 12);
    auto *head = new QWidget;
    auto *hr = horizontal(head, 0, 8);
    hr->addWidget(label("原站样片库", "heading"));
    hr->addWidget(label(QString::number(names.size()), "count"));
    hr->addStretch();
    auto *search = new QLineEdit;
    search->setPlaceholderText("按文件名搜索样片");
    hr->addWidget(search);
    layout->addWidget(head);
    auto *list = new QListWidget;
    list->setViewMode(QListView::IconMode);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setIconSize({136, 79});
    list->setGridSize({155, 110});
    list->setWordWrap(false);
    list->setTextElideMode(Qt::ElideMiddle);
    layout->addWidget(list, 1);
    auto *foot = new QWidget;
    auto *fr = horizontal(foot, 0, 8);
    auto *prev = button("上一页");
    auto *next = button("下一页");
    auto *info = label({}, "mono");
    fr->addWidget(prev);
    fr->addWidget(info);
    fr->addWidget(next);
    fr->addStretch();
    auto *apply = button("载入样片", "image", "primary");
    fr->addWidget(apply);
    layout->addWidget(foot);
    layout->addWidget(label("首次载入需要联网，已打开的样片保存在本机缓存。", "subtle"));
    int page = 0, generation = 0;
    QString selected;
    auto fill = [&] {
        ++generation;
        list->setProperty("generation", generation);
        list->clear();
        apply->setEnabled(false);
        QStringList filtered = names.filter(search->text().trimmed(), Qt::CaseInsensitive);
        constexpr int pageSize = 12;
        const int pages = qMax(1, (int(filtered.size()) + pageSize - 1) / pageSize);
        page = qBound(0, page, pages - 1);
        info->setText(QString("%1 / %2").arg(page + 1).arg(pages));
        prev->setEnabled(page > 0);
        next->setEnabled(page + 1 < pages);
        for (int i = page * pageSize; i < qMin((page + 1) * pageSize, int(filtered.size())); ++i) {
            const QString filename = filtered[i];
            auto *item = new QListWidgetItem(glyph("image"), QFileInfo(filename).completeBaseName(), list);
            item->setData(Qt::UserRole, filename);
            item->setToolTip(filename);
            item->setTextAlignment(Qt::AlignHCenter);
            QPointer<QListWidget> live = list;
            const int rev = generation, row = list->row(item);
            community.sample(filename, true, [live, rev, row](QByteArray raw, QString error) {
                if (!live || live->property("generation").toInt() != rev || !live->item(row))
                    return;
                QPixmap pix;
                if (error.isEmpty() && pix.loadFromData(raw))
                    live->item(row)->setIcon(pix);
                else
                    live->item(row)->setToolTip(live->item(row)->text() + "  ·  可尝试载入原图");
            });
        }
        if (!list->count())
            info->setText("0 张");
    };
    QObject::connect(list, &QListWidget::currentRowChanged, &dialog,
                     [&](int row) { apply->setEnabled(row >= 0); });
    QObject::connect(prev, &QPushButton::clicked, &dialog, [&] {
        --page;
        fill();
    });
    QObject::connect(next, &QPushButton::clicked, &dialog, [&] {
        ++page;
        fill();
    });
    QObject::connect(search, &QLineEdit::textChanged, &dialog, [&] {
        page = 0;
        fill();
    });
    const auto use = [&] {
        if (auto *item = list->currentItem()) {
            selected = item->data(Qt::UserRole).toString();
            dialog.accept();
        }
    };
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, [&](QListWidgetItem *) { use(); });
    QObject::connect(apply, &QPushButton::clicked, &dialog, use);
    fill();
    dialog.exec();
    return selected;
}

Submission confirmSubmission(QWidget *parent, bool capture, const QString &name, const QImage &preview,
                             bool customLogo) {
    QDialog dialog(parent);
    dialog.setWindowTitle(capture ? "投稿样片" : "分享公共预设");
    dialog.resize(395, 390);
    auto *layout = vertical(&dialog, 20, 10);
    layout->addWidget(label(capture ? "向原站投稿样片" : "分享公共预设", "heading"));
    auto *picture = new QLabel;
    picture->setAlignment(Qt::AlignCenter);
    picture->setPixmap(
        QPixmap::fromImage(preview.scaled(340, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    layout->addWidget(picture);
    auto *note = label(capture ? "提交后，所选图像和署名将发送至 magiconch.com，供原作者审核选用。"
                               : "提交后，预设参数、画面预览和署名将发送至 magiconch.com，供原作者审核。",
                       "subtle");
    note->setWordWrap(true);
    layout->addWidget(note);
    if (customLogo && !capture) {
        auto *l = label("原站的水印参数使用默认图标。自定义图标可通过预设文件分享。", "subtle");
        l->setWordWrap(true);
        layout->addWidget(l);
    }
    auto *title = new QLineEdit(name);
    title->setMaxLength(4);
    if (!capture) {
        layout->addWidget(label("预设名称 · 四个中日文字"));
        layout->addWidget(title);
    } else {
        title->setParent(&dialog);
        title->hide();
    }
    auto *author = new QLineEdit;
    author->setMaxLength(80);
    author->setPlaceholderText("作者署名（选填）");
    layout->addWidget(author);
    auto *buttons = new QDialogButtonBox;
    auto *submit = buttons->addButton("提交审核", QDialogButtonBox::AcceptRole);
    submit->setObjectName("primary");
    buttons->addButton("取消", QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    const QRegularExpression valid(QStringLiteral("^[\\p{Han}\\p{Hiragana}\\p{Katakana}々ー]{4}$"));
    auto enable = [&] { submit->setEnabled(capture || valid.match(title->text().trimmed()).hasMatch()); };
    QObject::connect(title, &QLineEdit::textChanged, &dialog, enable);
    enable();
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    bool accepted = dialog.exec() == QDialog::Accepted;
    return {title->text().trimmed(), author->text().trimmed(), accepted};
}

void showHelp(QWidget *parent) {
    QDialog d(parent);
    d.setWindowTitle("余像 · 使用说明");
    auto *layout = vertical(&d, 22, 12);
    layout->addWidget(label("余像  AFTERIMAGE", "heading"));
    auto *intro =
        label("在左侧选择预设，在右侧展开参数组。点击绿色数值可精确输入，双击滑块可恢复该参数的默认值。");
    intro->setWordWrap(true);
    layout->addWidget(intro);
      auto *patinaHelp = label("顶部「效果工坊」提供 13 种效果。可叠加处理、绘制局部动图蒙版、导出透明双图和字符文本，也可把照片做成循环反馈动画。");
      patinaHelp->setWordWrap(true);
      layout->addWidget(patinaHelp);
    auto *grid = new QGridLayout;
    int row = 0;
    const auto key = [&](QString a, QString b) {
        grid->addWidget(label(a, "mono"), row, 0);
        grid->addWidget(label(b), row++, 1);
    };
    key("Ctrl + O / V", "打开文件 / 从剪贴板粘贴");
    key("Ctrl + S", "导出当前效果图");
    key("Ctrl + Z / Y", "撤销 / 重做");
    key("1 / 2 / 3", "原图 / 效果 / 可拖动分屏");
    key("F / 滚轮", "适应画布 / 缩放");
    key("双击画布", "切换适应与 100%");
    layout->addLayout(grid);
    auto *about =
          label("「蒸気機」和「电子包浆」原作由卜卜口制作。桌面版使用 C++ / Qt。", "subtle");
    about->setWordWrap(true);
    layout->addWidget(about);
    auto *rowBox = new QWidget;
    auto *bar = horizontal(rowBox, 0, 6);
    auto *web = button("访问原作");
    auto *repo = button("原作仓库");
    auto *ok = button("关闭", {}, "primary");
    bar->addWidget(web);
    bar->addWidget(repo);
    bar->addStretch();
    bar->addWidget(ok);
    layout->addWidget(rowBox);
    QObject::connect(web, &QPushButton::clicked, &d,
                     [] { QDesktopServices::openUrl(QUrl("https://magiconch.com/vaporwave/")); });
    QObject::connect(repo, &QPushButton::clicked, &d,
                     [] { QDesktopServices::openUrl(QUrl("https://github.com/itorr/vaporwave")); });
    QObject::connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    d.resize(440, 330);
    d.exec();
}
