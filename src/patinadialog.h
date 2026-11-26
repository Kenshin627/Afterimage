#pragma once
#include "workshop.h"
#include <QBuffer>
#include <QDialog>
#include <QFutureWatcher>
#include <QMovie>
#include <QTimer>
class Canvas;
class QCheckBox;
class QComboBox;
class QLabel;
class QMimeData;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QVBoxLayout;

class PatinaDialog : public QDialog {
    Q_OBJECT
  public:
    PatinaDialog(PatinaInput original, QImage studio, PatinaSettings options, WorkshopSettings workshop,
                 QWidget *parent);
    ~PatinaDialog() override;
    const PatinaSettings &settings() const { return config; }
    const WorkshopSettings &workshopSettings() const { return workshop; }
    const PatinaInput &appliedImage() const { return applied; }
    void setStudioEffect(const QImage &image);

  protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

  private:
    enum class Output { Preview, Animation };
    struct Progress {
        std::atomic_int done{0}, total{0};
    };
    PatinaInput original, studio, other, input, applied;
    PatinaSettings config;
    WorkshopSettings workshop;
    WorkshopEffect effect = WorkshopEffect::Patina;
    PatinaResult result;
    Canvas *canvas = nullptr;
    QTabWidget *tabs = nullptr;
    QStackedWidget *effectPages = nullptr;
    QComboBox *sourceMenu = nullptr;
    QPushButton *exportBtn = nullptr, *applyBtn = nullptr, *animateBtn = nullptr, *cancelBtn = nullptr,
                *againBtn = nullptr, *openBtn = nullptr, *reroll = nullptr;
    QCheckBox *popBox = nullptr;
    QWidget *namesGroup = nullptr, *settingsPanel = nullptr;
    QSpinBox *years = nullptr, *grid = nullptr, *quality = nullptr, *edge = nullptr;
    QLabel *feedback = nullptr, *fileInfo = nullptr;
    QProgressBar *bar = nullptr;
    QList<std::function<void()>> refreshers;
    QTimer debounce, poll;
    QFutureWatcher<PatinaResult> watcher;
    CancelFlag cancel;
    std::shared_ptr<Progress> progress;
    QBuffer gifBuffer, originalBuffer;
    QMovie movie, originalMovie;
    quint64 revision = 0, activeRevision = 0;
    bool running = false, pending = false, syncing = false;
    Output queued = Output::Preview;
    QString saveAfter;
    QWidget *controls();
    QWidget *basicPanel();
    QWidget *advancedPanel();
    void sync();
    void edited();
    void request(Output render, QString path = {});
    void start();
    void finish();
    void stop();
    void refreshProgress();
    void showResult();
    void clearPlayer();
    void selectSource(int index);
    void openFile();
    void paste();
    bool useMime(const QMimeData *mime);
    void setOther(PatinaInput doc);
    void exportFile();
    void save(const QString &path);
    void useResult(bool sendToEditor);
    QCheckBox *check(QString title, bool PatinaSettings::*field, QVBoxLayout *layout);
    QSpinBox *integer(QString title, int PatinaSettings::*field, int lo, int hi, QVBoxLayout *layout,
                      bool slider = false);
    void decimal(QString title, double PatinaSettings::*field, double lo, double hi, double step,
                 QVBoxLayout *layout);
};
