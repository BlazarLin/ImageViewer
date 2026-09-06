#pragma once

#include "core/processing/ImageProcessor.h"

#include <QImage>
#include <QMainWindow>

#include <memory>

class QAction;
class QDockWidget;
class QLabel;
class QTimer;
template <typename T>
class QFutureWatcher;

namespace core::navigation {
class DirectoryModel;
}

namespace ui {
class ImageView;
class PreprocessPanel;
class ThumbnailBar;
class TitleBar;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openFile(const QString& path);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;

private slots:
    void onOpen();
    void onPrevious();
    void onNext();
    void onAbout();
    void onZoomChanged(double factor);
    void onPreprocessParametersChanged(
        const core::processing::ProcessingParameters& parameters);
    void startPreprocess();
    void onPreprocessFinished();
    void toggleMaximized();
    void selectLanguage(const QString& localeName);

private:
    void setupUi();
    void setupActions();
    void setupMenusAndToolbar();
    void setupStatusBar();
    void updateImageInformation();
    void updateNavigationActions();
    void showOriginalImage();
    QString formatTitleText() const;
    QString pixelFormatText(const QImage& image) const;
    QString formatFileSize(qint64 nBytes) const;

    ui::ImageView* view_ = nullptr;
    ui::ThumbnailBar* thumbnailBar_ = nullptr;
    ui::TitleBar* titleBar_ = nullptr;
    ui::PreprocessPanel* preprocessPanel_ = nullptr;
    QDockWidget* preprocessDock_ = nullptr;

    std::unique_ptr<core::navigation::DirectoryModel> directoryModel_;
    QFutureWatcher<core::processing::ProcessingResult>* processingWatcher_ = nullptr;
    QTimer* processingTimer_ = nullptr;
    core::processing::ProcessingParameters processingParameters_;
    QImage originalImage_;
    quint64 nProcessingGeneration_ = 0;
    quint64 nRunningGeneration_ = 0;
    bool bProcessingPending_ = false;

    QAction* actOpen_ = nullptr;
    QAction* actPrevious_ = nullptr;
    QAction* actNext_ = nullptr;
    QAction* actFit_ = nullptr;
    QAction* actFitWidth_ = nullptr;
    QAction* actFitHeight_ = nullptr;
    QAction* actActualSize_ = nullptr;
    QAction* actTogglePreprocess_ = nullptr;
    QAction* actAbout_ = nullptr;
    QAction* actExit_ = nullptr;

    QLabel* statusFile_ = nullptr;
    QLabel* statusSize_ = nullptr;
    QLabel* statusZoom_ = nullptr;
    QLabel* statusProcessing_ = nullptr;

    QString currentPath_;
};
