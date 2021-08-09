#pragma once

#include <memory>
#include <libraw/libraw.h>
#include <QWidget>

class Histogram : public QWidget
{
    std::shared_ptr<LibRaw> libRaw_;
    std::vector<unsigned> red_, green_, blue_;
    float blackLevel_=0;
    unsigned blackLevelBin_=0;
    unsigned whiteLevelBin_=0;
    unsigned countMax_=1;
    bool logarithmic_=true;
public:
    Histogram(QWidget* parent=nullptr);
    void compute(std::shared_ptr<LibRaw> const& libRaw, const float blackLevel);
    void setLogY(bool enable);
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    void compute();
};
