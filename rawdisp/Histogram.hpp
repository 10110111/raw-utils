#pragma once

#include <memory>
#include <libraw/libraw.h>
#include <QWidget>
#include <QFutureWatcher>

class Histogram : public QWidget
{
    std::shared_ptr<LibRaw> libRaw_;
    std::vector<unsigned> red_, green_, blue_;
    float blackLevel_=0;
    unsigned blackLevelBin_=0;
    unsigned whiteLevelBin_=0;
    unsigned countMax_=1;
    bool logarithmic_=true;
    struct Update
    {
        std::vector<unsigned> red, green, blue;
        unsigned blackLevelBin=0;
        unsigned whiteLevelBin=0;
        unsigned countMax=1;
    };
    std::atomic<unsigned> lastUpdateIndex_{0};
    QFutureWatcher<std::shared_ptr<Update>> updateWatcher_;
public:
    Histogram(QWidget* parent=nullptr);
    void compute(std::shared_ptr<LibRaw> const& libRaw, const float blackLevel);
    void setLogY(bool enable);
    bool logY() const { return logarithmic_; }
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    void compute();
    void onComputed();
};
