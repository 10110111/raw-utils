#include "EXIFDisplay.hpp"
#include <exiv2/exiv2.hpp>
#include <QDebug>
#include <QLabel>
#include <QFileInfo>
#include <QGridLayout>
#include <QFontMetrics>

namespace
{

struct Entry
{
    QString name;
    std::string key;
    QLabel* caption=nullptr;
    QLabel* value=nullptr;
};

std::vector<Entry> entriesToShow
{
    {"Camera", "Exif.Image.Model"},
    {"Lens model", "Exif.Photo.LensModel"},
    {"Exposure time", "Exif.Photo.ExposureTime"},
    {"Date", "Exif.Photo.DateTimeOriginal"},
    {"ISO", "Exif.Photo.ISOSpeedRatings"},
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
        entry.value->setText(QString::fromStdString(it->toString()));
    }
    layout_->setRowStretch(row, 1);
}
catch(Exiv2::Error& e)
{
    clear();
    errorLabel_ = new QLabel(tr("exiv2 error: %1").arg(e.what()), 0, 0);
    layout_->addWidget(errorLabel_, 0,0);
}
