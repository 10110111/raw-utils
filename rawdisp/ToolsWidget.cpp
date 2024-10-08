#include "ToolsWidget.hpp"
#include "Manipulator.hpp"

static Manipulator* addManipulator(QVBoxLayout*const layout, ToolsWidget*const tools,
                                   QString const& label, const double min, const double max, const double defaultValue,
                                   const int decimalPlaces, QString const& unit="")
{
    const auto manipulator=new Manipulator(label, min, max, defaultValue, decimalPlaces);
    layout->addWidget(manipulator);
    tools->connect(manipulator, &Manipulator::valueChanged, tools, &ToolsWidget::settingChanged);
    if(!unit.isEmpty())
        manipulator->setUnit(unit);
    return manipulator;
}

ToolsWidget::ToolsWidget(QWidget* parent)
    : QDockWidget(tr("Tools"),parent)
{
    const auto mainWidget=new QWidget(this);
    const auto layout=new QVBoxLayout;
    mainWidget->setLayout(layout);
    setWidget(mainWidget);

    exposureCompensation_ = addManipulator(layout, this, tr(u8"\u0394e&xposure"), -5, 5, 0, 2);
    rotationAngle_ = addManipulator(layout, this, tr("&Rotation"), -180, 180, 0, 2, u8"\u00b0");

    previewCheckBox_ = new QCheckBox(tr("Show embedded preview"));
    previewCheckBox_->setEnabled(false);
    connect(previewCheckBox_, &QCheckBox::stateChanged, this, &ToolsWidget::settingChanged);
    layout->addWidget(previewCheckBox_);

    clippedHighlightsMarking_ = new QCheckBox(tr("Mark clipped highlights"));
    connect(clippedHighlightsMarking_, &QCheckBox::stateChanged, this, &ToolsWidget::settingChanged);
    layout->addWidget(clippedHighlightsMarking_);

    mustReducePepperNoise_ = new QCheckBox(tr("Reduce pepper noise"));
    connect(mustReducePepperNoise_, &QCheckBox::stateChanged, this, &ToolsWidget::demosaicSettingChanged);
    layout->addWidget(mustReducePepperNoise_);

    mustTransformToSRGB_ = new QCheckBox(tr("Transform from camera to sRGB"));
    mustTransformToSRGB_->setChecked(true);
    connect(mustTransformToSRGB_, &QCheckBox::stateChanged, this, &ToolsWidget::demosaicSettingChanged);
    layout->addWidget(mustTransformToSRGB_);

    layout->addStretch();
}
