#include "EXIFDisplay.hpp"
#include <exiv2/exiv2.hpp>
#include <QLabel>
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
    {"Shutter speed", "Exif.Photo.ShutterSpeedValue"},
    {"Date", "Exif.Photo.DateTimeOriginal"},
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
    for(auto& entry : entriesToShow)
    {
        entry.caption->deleteLater();
        entry.caption = nullptr;

        entry.value->deleteLater();
        entry.value = nullptr;
    }
}

void EXIFDisplay::loadFile(QString const& filename)
{
    const auto image = Exiv2::ImageFactory::open(filename.toStdString());
    if(!image.get())
    {
        clear();
        return;
    }
    image->readMetadata();
    const auto& exif = image->exifData();
    int row=0;
    for(auto& entry : entriesToShow)
    {
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
            entry.value->setText("");
            continue;
        }
        entry.value->setText(QString::fromStdString(it->toString()));
    }
    layout_->setRowStretch(row, 1);
}
