#include "FramesModel.h"

FramesModel::FramesModel(QObject* parent)
    : QStandardItemModel(0,COLUMN_COUNT,parent)
{
    setHorizontalHeaderLabels(QStringList{
                              "File name",
                              "Shot time",
                              "Shutter time",
                              "ISO",
                              "Aperture",
                              "Exposure",
                              });
}
