#pragma once

#include <QDockWidget>

class QLabel;
class QGridLayout;
class QSpacerItem;
class EXIFDisplay : public QDockWidget
{
    Q_OBJECT

public:
    EXIFDisplay(QWidget* parent=nullptr);
    void loadFile(QString const& filename);

private:
    void clear();

private:
    QGridLayout* layout_;
    QLabel* errorLabel_ = nullptr;
};
