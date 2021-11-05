#pragma once

#include <QCheckBox>
#include <QDockWidget>
#include "Manipulator.hpp"

class ToolsWidget : public QDockWidget
{
    Q_OBJECT

public:
    ToolsWidget(QWidget* parent=nullptr);

    double exposureCompensation() const { return exposureCompensation_->value(); }
    bool previewMode() const { return previewCheckBox_->isChecked(); }
    bool clippedHighlightsMarkingEnabled() const { return clippedHighlightsMarking_->isChecked(); }

    void enablePreview() { previewCheckBox_->setEnabled(true); }
    void disablePreview() { previewCheckBox_->setEnabled(false); }

signals:
    void settingChanged();

private:
    Manipulator* exposureCompensation_=nullptr;
    QCheckBox* previewCheckBox_=nullptr;
    QCheckBox* clippedHighlightsMarking_=nullptr;
};
