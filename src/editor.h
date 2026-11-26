#pragma once
#include "community.h"
#include "workshop.h"
#include <QFutureWatcher>
#include <QMainWindow>
#include <QTimer>
#include <QPixmap>
class Canvas;
class Inspector;
class SampleTile;
class QButtonGroup;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QMenu;
struct EditState {
    QJsonObject base, style;
    bool operator==(const EditState &other) const {
        return base == other.base && style == other.style;
    }
};
class Editor : public QMainWindow {
    Q_OBJECT
  public:
    explicit Editor(QString storageDirectory = {});
    ~Editor() override;
    bool openPath(const QString &path);
    bool ready() const {
        return rendered == revision && !frame.processed.isNull();
    }
    const Frame &currentFrame() const {
        return frame;
    }

  signals:
    void frameReady(const QImage &image);

  protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

  private:
    QWidget *makeHeader();
    QWidget *makeLibrary();
    QWidget *makeStage();
    QWidget *makeFooter();
    void makeMenus();
    void libraryMenu(QWidget *anchor);
    void rebuildLibrary();
    void syncSelection();
    void showLibrary(int index);
    void applyPreset(const QJsonObject &preset);
    void updateControls();
    void change(const QString &key, const QJsonValue &value);
    void remember(const EditState &old);
    void undo(bool forward);
    void setSource(QImage image, QString title, int localIndex = -1);
    void setSource(PatinaInput input, int localIndex = -1);
    void openPatina();
    void requestRender();
    void startRender();
    void finishRender();
    void openImage();
    void paste();
    void exportImage();
    void copyImage();
    void savePersonal();
    void importPreset();
    void exportPreset();
    void removePersonal();
    bool updatePersonal(QJsonObject preset, bool remove);
    void chooseWatermark();
    void refreshPublic();
    void gallery();
    void submit(bool capture);
    void message(const QString &text);
    void syncPublic(bool interactive);
    void warn(const QString &title, const QString &text);
    QVector<QJsonObject> factory, personal, publicStyles;
    QHash<QByteArray, QPixmap> previews;
    PatinaInput document;
    PatinaSettings patinaOptions;
    WorkshopSettings workshopOptions;
    QImage thumbSource;
    QVector<QImage> samples;
    QStringList sampleNames, galleryNames;
    QString dataDir;
    EditState current, gestureStart;
    QVector<EditState> past, future;
    bool gesturing = false;
    int libraryIndex = 0, sourceEpoch = 0;
    bool refreshing = false, checkedPublic = false, uploading = false;
    quint64 revision = 0, rendered = 0, runningRevision = 0;
    bool rendering = false;
    Frame frame;
    CancelFlag canceled;
    QTimer debounce, noticeTimer;
    QFutureWatcher<Frame> worker;
    Community community;
    Canvas *canvas = nullptr;
    Inspector *inspector = nullptr;
    QWidget *left = nullptr;
    QMenu *mainMenu = nullptr;
    QButtonGroup *libraryTabs = nullptr, *viewTabs = nullptr;
    QLabel *libraryCount = nullptr, *emptyLibrary = nullptr, *docLabel = nullptr, *sourceSize = nullptr,
           *outputSize = nullptr, *currentLabel = nullptr, *status = nullptr;
    QLineEdit *search = nullptr;
    QListWidget *presetList = nullptr;
    QPushButton *undoButton = nullptr, *redoButton = nullptr, *exportButton = nullptr,
                *zoomButton = nullptr, *moreButton = nullptr;
    QList<SampleTile *> tiles;
};
