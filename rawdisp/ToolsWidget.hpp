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
    double rotationAngle() const { return rotationAngle_->value(); }
    bool previewMode() const { return previewCheckBox_->isChecked(); }
    bool clippedHighlightsMarkingEnabled() const { return clippedHighlightsMarking_->isChecked(); }
    bool mustTransformToSRGB() const { return mustTransformToSRGB_->isChecked(); }
    bool mustReducePepperNoise() const { return mustReducePepperNoise_->isChecked(); }

    void enablePreview() { previewCheckBox_->setEnabled(true); }
    void disablePreview() { previewCheckBox_->setEnabled(false); }

signals:
    void settingChanged();
    void demosaicSettingChanged();

private:
    Manipulator* exposureCompensation_=nullptr;
    Manipulator* rotationAngle_=nullptr;
    QCheckBox* previewCheckBox_=nullptr;
    QCheckBox* clippedHighlightsMarking_=nullptr;
    QCheckBox* mustTransformToSRGB_=nullptr;
    QCheckBox* mustReducePepperNoise_=nullptr;
};
