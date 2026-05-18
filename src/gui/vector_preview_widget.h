#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QTableWidget;

class VectorPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit VectorPreviewWidget(QWidget* parent = nullptr);

    void loadVector(const QString& path);
    void clear();

private:
    void buildUi();
    void loadOutline();
    void loadFeatureInfo();
    void loadAttributeTable();

    QString filePath_;
    QLabel* outlineLabel_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QTableWidget* attrTable_ = nullptr;

    static constexpr int kOutlineSize = 480;
    static constexpr int kMaxPreviewRows = 100;
};
