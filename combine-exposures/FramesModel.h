#ifndef INCLUDE_ONCE_D3BEC169_3B69_4528_8991_CA5A37FBAC73
#define INCLUDE_ONCE_D3BEC169_3B69_4528_8991_CA5A37FBAC73

#include <QStandardItemModel>

class FramesModel : public QStandardItemModel
{
public:
    struct Column
    {
        enum
        {
            FileName,
            ShotTime,
            ShutterTime,
            ISO,
            Aperture,
            Exposure,

            TotalCount
        } value;
        operator int() const { return value; }
    };
    enum { COLUMN_COUNT=Column::TotalCount };
    enum Role
    {
        ShotTimeRole=Qt::UserRole,
    };
public:
    FramesModel(QObject* parent=nullptr);
};

#endif
