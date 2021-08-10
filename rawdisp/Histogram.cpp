#include "Histogram.hpp"
#include <QDebug>
#include <QPainter>
#include <QResizeEvent>
#include <QtConcurrent>
#include "timing.hpp"

Histogram::Histogram(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NoSystemBackground);
    connect(&updateWatcher_, &QFutureWatcher<Update>::finished, this, &Histogram::onComputed);
}

void Histogram::compute(std::shared_ptr<LibRaw> const& libRaw, const float blackLevel)
{
    libRaw_=libRaw;
    blackLevel_=blackLevel;
    compute();
}

void Histogram::compute()
{
    if(!libRaw_) return;

    red_.clear();
    green_.clear();
    blue_.clear();
    update();
    const auto future = QtConcurrent::run([libRaw=libRaw_,blackLevel=blackLevel_,histWidth=width()]
    {
        const auto numBins = histWidth - 2; // two columns reserved for black & white levels
        auto out=std::make_shared<Update>();
        out->red.resize(numBins);
        out->green.resize(numBins);
        out->blue.resize(numBins);
        const bool haveFP = libRaw->have_fpdata();
        const auto marginLeft = libRaw->imgdata.sizes.left_margin;
        const auto marginTop  = libRaw->imgdata.sizes.top_margin;
        const auto xMax = marginLeft + libRaw->imgdata.sizes.width;
        const auto yMax = marginTop  + libRaw->imgdata.sizes.height;
        const auto stride = libRaw->imgdata.sizes.raw_width;
        const auto col00 = libRaw->imgdata.idata.cdesc[libRaw->COLOR(0,0)];
        const auto col01 = libRaw->imgdata.idata.cdesc[libRaw->COLOR(0,1)];
        const auto col10 = libRaw->imgdata.idata.cdesc[libRaw->COLOR(1,0)];
        const auto col11 = libRaw->imgdata.idata.cdesc[libRaw->COLOR(1,1)];
        auto& topLeft     = col00=='R' ? out->red : col00=='G' ? out->green : out->blue;
        auto& topRight    = col01=='R' ? out->red : col01=='G' ? out->green : out->blue;
        auto& bottomLeft  = col10=='R' ? out->red : col10=='G' ? out->green : out->blue;
        auto& bottomRight = col11=='R' ? out->red : col11=='G' ? out->green : out->blue;
        const auto whiteLevel = libRaw->imgdata.rawdata.color.maximum;
        const auto t0 = currentTime();
        if(haveFP && libRaw->imgdata.rawdata.float_image)
        {
            const auto* data = libRaw->imgdata.rawdata.float_image;
            const auto binNum = [=](const float v) -> unsigned
                                {
                                    const auto b = std::lround(double(v)/(1.1*whiteLevel)*(numBins-1));
                                    return b>=numBins ? numBins-1 : b;
                                };
            out->blackLevelBin=binNum(blackLevel);
            out->whiteLevelBin=binNum(whiteLevel);
            for(int y=marginTop; y<yMax-1; y+=2)
            {
                for(int x=marginLeft; x<xMax-1; x+=2)
                {
                    const auto tl = data[(y+0)*stride+x+0];
                    const auto tr = data[(y+0)*stride+x+1];
                    const auto bl = data[(y+1)*stride+x+0];
                    const auto br = data[(y+1)*stride+x+1];
                    ++topLeft[binNum(tl)];
                    ++topRight[binNum(tr)];
                    ++bottomLeft[binNum(bl)];
                    ++bottomRight[binNum(br)];
                }
            }
        }
        else if(!haveFP && libRaw->imgdata.rawdata.raw_image)
        {
            const auto* data = libRaw->imgdata.rawdata.raw_image;
            const auto binNum = [=](const unsigned v) -> unsigned
                                {
                                    const auto b = std::lround(double(v)/(1.1*whiteLevel)*(numBins-1));
                                    return b>=numBins ? numBins-1 : b;
                                };
            out->blackLevelBin=binNum(blackLevel);
            out->whiteLevelBin=binNum(whiteLevel);
            for(int y=marginTop; y<yMax-1; y+=2)
            {
                for(int x=marginLeft; x<xMax-1; x+=2)
                {
                    const auto tl = data[(y+0)*stride+x+0];
                    const auto tr = data[(y+0)*stride+x+1];
                    const auto bl = data[(y+1)*stride+x+0];
                    const auto br = data[(y+1)*stride+x+1];
                    ++topLeft[binNum(tl)];
                    ++topRight[binNum(tr)];
                    ++bottomLeft[binNum(bl)];
                    ++bottomRight[binNum(br)];
                }
            }
        }
        if(out->red.size())
        {
            const auto redMax   = *std::max_element(out->red.begin(), out->red.end());
            const auto greenMax = *std::max_element(out->green.begin(), out->green.end());
            const auto blueMax  = *std::max_element(out->blue.begin(), out->blue.end());
            out->countMax = std::max({redMax,(greenMax+1)/2,blueMax});
            const auto t1 = currentTime();
            qDebug().nospace() << "Histogram with " << numBins << " bins computed in " << double(t1-t0) << " seconds";
        }
        return out;
    });
    updateWatcher_.setFuture(future);
}

void Histogram::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::gray);

    if(red_.empty() || green_.empty() || blue_.empty())
    {
        p.drawText(rect(), Qt::AlignCenter|Qt::AlignHCenter, tr("(no data)"));
        return;
    }

    const int height = this->height();
    const int bottom = height-1;
    unsigned shift=0;
    for(unsigned x=0; x<red_.size()+2; ++x)
    {
        if(x==blackLevelBin_ || x==whiteLevelBin_)
        {
            ++shift;
            ++x;
        }
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setPen(Qt::black);
        p.drawLine(QPoint(x,bottom), QPoint(x,0));

        p.setCompositionMode(QPainter::CompositionMode_Plus);
        constexpr double MAX = 0.9;
        const double logOffset = 1./countMax_;
        int tallestLineTop=bottom+1;
        if(const double count = red_[x-shift])
        {
            p.setPen(Qt::red);
            auto redY = count/countMax_;
            if(logarithmic_)
                redY = std::log10(redY+logOffset)/-std::log10(logOffset) + 1;
            const auto midRed = std::round(bottom - MAX*height*redY);
            if(midRed<tallestLineTop)
                tallestLineTop=midRed;
            p.drawLine(QPoint(x,bottom), QPoint(x,midRed));
        }

        if(const double count = green_[x-shift])
        {
            p.setPen(Qt::green);
            
            auto greenY = count/countMax_/2;
            if(logarithmic_)
                greenY = std::log10(greenY+logOffset)/-std::log10(logOffset) + 1;
            const auto midGreen = std::round(bottom - MAX*height*greenY);
            if(midGreen<tallestLineTop)
                tallestLineTop=midGreen;
            p.drawLine(QPoint(x,bottom), QPoint(x,midGreen));
        }

        if(const double count = blue_[x-shift])
        {
            p.setPen(Qt::blue);
            auto blueY = count/countMax_;
            if(logarithmic_)
                blueY = std::log10(blueY+logOffset)/-std::log10(logOffset) + 1;
            const auto midBlue = std::round(bottom - MAX*height*blueY);
            if(midBlue<tallestLineTop)
                tallestLineTop=midBlue;
            p.drawLine(QPoint(x,bottom), QPoint(x,midBlue));
        }

        p.setPen(Qt::gray);
        p.drawLine(QPoint(x,tallestLineTop-1), QPoint(x,0));
    }
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setPen(QPen(Qt::black,1,Qt::DashLine));
    p.drawLine(QPoint(blackLevelBin_,bottom), QPoint(blackLevelBin_,0));
    p.setPen(QPen(Qt::white,1,Qt::DashLine));
    p.drawLine(QPoint(whiteLevelBin_,bottom), QPoint(whiteLevelBin_,0));
}

void Histogram::resizeEvent(QResizeEvent*const event)
{
    if(event->oldSize().width()!=width())
        compute();
}

void Histogram::setLogY(const bool enable)
{
    logarithmic_=enable;
    update();
}

void Histogram::onComputed()
{
    const auto u = updateWatcher_.future().result();
    red_   = std::move(u->red);
    green_ = std::move(u->green);
    blue_  = std::move(u->blue);
    blackLevelBin_ = u->blackLevelBin;
    whiteLevelBin_ = u->whiteLevelBin;
    countMax_ = u->countMax;
    update();
}
