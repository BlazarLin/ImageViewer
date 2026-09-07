#pragma once

#include "core/processing/ImageProcessor.h"
#include "core/loader/ImageLoader.h"
#include "core/analysis/ImageAnalysis.h"
#include "core/cache/ImageCache.h"

#include <QImage>
#include <QMainWindow>

#include <memory>
#include <QVector>

struct PreloadResult {
    QString path;
    QImage image;
};

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
class AnalysisPanel;
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
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, long* result) override;

private slots:
    void onOpen();
    void startImageLoad();
    void onImageLoadFinished();
    void onRefresh();
    void onSaveResult();
    void onSavePreset();
    void onLoadPreset();
    void onPrevious();
    void onNext();
    void onAbout();
    void onZoomChanged(double factor);
    void onPreprocessParametersChanged(
        const core::processing::ProcessingParameters& parameters);
    void startPreprocess();
    void onPreprocessFinished();
    void startRoiAnalysis(const QRect& region);
    void onRoiAnalysisFinished();
    void toggleComparison(bool bEnabled);
    void scheduleNeighborPreload();
    void onNeighborPreloadFinished();
    void toggleMaximized();
    void selectLanguage(const QString& localeName);
    void setDebugConsoleVisible(bool bVisible);

private:
    void applyLoadedImage(const QString& path, const QImage& image);
    void clearAnalysis();
    void updateResultActions();
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

    QFutureWatcher<core::loader::LoadResult>* loadWatcher_ = nullptr;
    QString requestedPath_;
    QString runningLoadPath_;
    quint64 nLoadGeneration_ = 0;
    quint64 nRunningLoadGeneration_ = 0;
    bool bLoading_ = false;
    bool bLoadTaskActive_ = false;

    ui::ImageView* view_ = nullptr;
    ui::ThumbnailBar* thumbnailBar_ = nullptr;
    ui::TitleBar* titleBar_ = nullptr;
    ui::PreprocessPanel* preprocessPanel_ = nullptr;
    ui::AnalysisPanel* analysisPanel_ = nullptr;
    QDockWidget* preprocessDock_ = nullptr;
    QDockWidget* analysisDock_ = nullptr;

    std::unique_ptr<core::navigation::DirectoryModel> directoryModel_;
    QFutureWatcher<core::processing::ProcessingResult>* processingWatcher_ = nullptr;
    QFutureWatcher<core::analysis::AnalysisResult>* analysisWatcher_ = nullptr;
    QFutureWatcher<QVector<PreloadResult>>* preloadWatcher_ = nullptr;
    QTimer* processingTimer_ = nullptr;
    core::processing::ProcessingParameters processingParameters_;
    QImage originalImage_;
    QImage processedImage_;
    QImage displayedImage_;
    quint64 nProcessingGeneration_ = 0;
    quint64 nRunningGeneration_ = 0;
    bool bProcessingPending_ = false;
    quint64 nAnalysisGeneration_ = 0;
    quint64 nRunningAnalysisGeneration_ = 0;
    QRect pendingAnalysisRegion_;
    bool bAnalysisPending_ = false;
    core::cache::ImageCache imageCache_;
    bool bPreloadPending_ = false;

    QAction* actOpen_ = nullptr;
    QAction* actRefresh_ = nullptr;
    QAction* actSaveResult_ = nullptr;
    QAction* actSavePreset_ = nullptr;
    QAction* actLoadPreset_ = nullptr;
    QAction* actPrevious_ = nullptr;
    QAction* actNext_ = nullptr;
    QAction* actFit_ = nullptr;
    QAction* actFitWidth_ = nullptr;
    QAction* actFitHeight_ = nullptr;
    QAction* actActualSize_ = nullptr;
    QAction* actTogglePreprocess_ = nullptr;
    QAction* actToggleAnalysis_ = nullptr;
    QAction* actCompare_ = nullptr;
    QAction* actShowConsole_ = nullptr;
    QAction* actAbout_ = nullptr;
    QAction* actExit_ = nullptr;

    QLabel* statusFile_ = nullptr;
    QLabel* statusSize_ = nullptr;
    QLabel* statusZoom_ = nullptr;
    QLabel* statusProcessing_ = nullptr;

    QString currentPath_;
};
