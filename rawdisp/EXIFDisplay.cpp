#include "EXIFDisplay.hpp"
#include <cmath>
#include <exiv2/exiv2.hpp>
#include <QDebug>
#include <QLabel>
#include <QFileInfo>
#include <QGridLayout>
#include <QFontMetrics>

namespace
{

QString formatDefault(Exiv2::Exifdatum const& datum)
{
    return QString::fromStdString(datum.toString());
}

QString formatExposureTime(Exiv2::Exifdatum const& datum)
{
    if(datum.typeId() != Exiv2::unsignedRational)
        return formatDefault(datum);

    const auto [num,denom] = datum.toRational();
    if(denom==0)
        return formatDefault(datum);
    const auto frac = double(num)/denom;
    if(frac > 60)
    {
        const int min = std::floor(frac/60);
        const int sec = std::lround(frac-60*min);
        return QString("%1m%2s").arg(min).arg(sec);
    }
    if(denom==1)
        return QString("%1 s").arg(num);
    if(num==1)
        return QString("1/%1 s").arg(denom);
    if(frac < 10)
        return QString("%1 s").arg(frac, 0, 'g', 3);
    return QString("%1 s").arg(frac, 0, 'g', 4);
}

QString formatExposureBias(Exiv2::Exifdatum const& datum)
{
    if(datum.typeId() != Exiv2::signedRational)
        return formatDefault(datum);
    auto [num,denom] = datum.toRational();
    if(denom==0)
        return formatDefault(datum);

    const char sign = double(num)/denom<0 ? '-' : '+';
    num = std::abs(num);
    denom = std::abs(denom);
    if(num==0 || denom==1)
        return QString("%1%2").arg(sign).arg(num);
    const auto whole = num/denom;
    const auto smallNum = num-whole*denom;
    if(smallNum==1 && denom==3)
        return QString(u8"%1%2\u2153").arg(sign).arg(whole);
    if(smallNum==2 && denom==3)
        return QString(u8"%1%2\u2154").arg(sign).arg(whole);
    if(smallNum==1 && denom==2)
        return QString(u8"%1%2\u00bd").arg(sign).arg(whole);
    return QString(u8"%1%2 %3/%4").arg(sign).arg(whole).arg(smallNum).arg(denom);
}

QString formatAperture(Exiv2::Exifdatum const& datum)
{
    if(datum.typeId() != Exiv2::unsignedRational)
        return formatDefault(datum);
    auto [num,denom] = datum.toRational();
    if(denom==0)
        return formatDefault(datum);

    const auto fNum = double(num)/denom;

    return QString("f/%1").arg(fNum, 0, 'g', 2);
}

struct Entry
{
    QString name;
    std::string key;
    QString (*format)(Exiv2::Exifdatum const& datum) = formatDefault;
    QLabel* caption=nullptr;
    QLabel* value=nullptr;
};

std::vector<Entry> entriesToShow
{
    {"Date", "Exif.Photo.DateTimeOriginal"},
    {"Camera", "Exif.Image.Model"},
    {"Lens model", "Exif.Photo.LensModel"},
    {"ISO", "Exif.Photo.ISOSpeedRatings"},
    {"Aperture", "Exif.Photo.FNumber", &formatAperture},
    {"Exposure time", "Exif.Photo.ExposureTime", &formatExposureTime},
    {"Expo bias", "Exif.Photo.ExposureBiasValue", &formatExposureBias},
};

}

EXIFDisplay::EXIFDisplay(QWidget* parent)
    : QDockWidget(tr("EXIF info"), parent)
    , layout_(new QGridLayout)
{
    const auto holder = new QWidget;
    setWidget(holder);
    holder->setLayout(layout_);
    layout_->setHorizontalSpacing(QFontMetrics(font()).horizontalAdvance(' '));
}

void EXIFDisplay::clear()
{
    if(errorLabel_)
    {
        errorLabel_->deleteLater();
        errorLabel_ = nullptr;
    }
    for(auto& entry : entriesToShow)
    {
        if(entry.caption)
        {
            entry.caption->deleteLater();
            entry.caption = nullptr;
        }

        if(entry.value)
        {
            entry.value->deleteLater();
            entry.value = nullptr;
        }
    }
}

void EXIFDisplay::loadFile(QString const& filename)
try
{
    clear();

    if(QFileInfo(filename).isDir())
        return;

    const auto image = Exiv2::ImageFactory::open(filename.toStdString());
    if(!image.get())
    {
        qDebug().nospace() << "EXIFDisplay::loadFile(): failed to open file";
        return;
    }
    image->readMetadata();
    const auto& exif = image->exifData();

#if 0
    for(const auto& d : exif)
        qDebug().nospace() << "Key: " << d.key().c_str();
#endif

    int row=0;
    for(auto& entry : entriesToShow)
    {
        qDebug().nospace() << "Looking for key \"" << entry.key.c_str() << "\"...";
        if(!entry.value)
        {
            entry.caption = new QLabel(entry.name+":");
            entry.caption->setAlignment(Qt::AlignRight);
            entry.value = new QLabel;
            entry.value->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);
            layout_->addWidget(entry.caption, row, 0);
            layout_->addWidget(entry.value  , row, 1);
            layout_->setRowStretch(row, 0);
            layout_->setColumnStretch(1, 1);
            ++row;
        }
        const auto it=exif.findKey(Exiv2::ExifKey(entry.key));
        if(it==exif.end())
        {
            qDebug().nospace() << "Key \"" << entry.key.c_str() << "\" not found in EXIF data";
            entry.value->setText("");
            continue;
        }
        entry.value->setText(entry.format(*it));
    }
    layout_->setRowStretch(row, 1);
}
catch(Exiv2::Error& e)
{
    clear();
    errorLabel_ = new QLabel(tr("exiv2 error: %1").arg(e.what()), 0, 0);
    layout_->addWidget(errorLabel_, 0,0);
}
