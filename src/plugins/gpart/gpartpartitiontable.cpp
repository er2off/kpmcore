/*
    SPDX-FileCopyrightText: 2023 Er2 <er2@dismail.de>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "plugins/gpart/gpartpartitiontable.h"
#include "plugins/gpart/gpartbackend.h"

#include "core/partition.h"
#include "core/device.h"

#include "fs/filesystem.h"

#include "util/report.h"
#include "util/externalcommand.h"

#include <QRegularExpression>

#include <KLocalizedString>

// TODO: Use libgeom instead?
#include <libgeom.h>

GpartPartitionTable::GpartPartitionTable(const Device *d) :
    CoreBackendPartitionTable(),
    m_device(d)
{
}

GpartPartitionTable::~GpartPartitionTable()
{
}

bool GpartPartitionTable::open()
{
    return true;
}


bool GpartPartitionTable::commit(quint32 timeout)
{
    Q_UNUSED(timeout)
    // Nothing to do?

    return true;
}

static struct {
    FileSystem::Type type;
    QLatin1String partitionType;
} typemap[] = {
    { FileSystem::Type::Btrfs, QLatin1String("linux-data") },
    { FileSystem::Type::Ext2, QLatin1String("linux-data") },
    { FileSystem::Type::Ext3, QLatin1String("linux-data") },
    { FileSystem::Type::Ext4, QLatin1String("linux-data") },
    { FileSystem::Type::LinuxSwap, QLatin1String("linux-swap") },
    { FileSystem::Type::FreeBSDSwap, QLatin1String("freebsd-swap") },
    { FileSystem::Type::Fat12, QLatin1String("fat16") },
    { FileSystem::Type::Fat16, QLatin1String("fat16") },
    { FileSystem::Type::Fat32, QLatin1String("fat32") },
    { FileSystem::Type::Nilfs2, QLatin1String("linux-data") },
    { FileSystem::Type::Ntfs, QLatin1String("ms-basic-data") },
    { FileSystem::Type::Exfat, QLatin1String("ms-basic-data") },
    { FileSystem::Type::ReiserFS, QLatin1String("linux-data") },
    { FileSystem::Type::Reiser4, QLatin1String("linux-data") },
    { FileSystem::Type::Xfs, QLatin1String("linux-data") },
    { FileSystem::Type::Jfs, QLatin1String("linux-data") },
    { FileSystem::Type::Hfs, QLatin1String("apple-hfs") },
    { FileSystem::Type::HfsPlus, QLatin1String("apple-hfs") },
    { FileSystem::Type::Apfs, QLatin1String("apple-apfs") },
    { FileSystem::Type::Ufs, QLatin1String("freebsd-ufs") },
    { FileSystem::Type::Udf, QLatin1String("ms-basic-data") },
    { FileSystem::Type::Zfs, QLatin1String("freebsd-zfs") },
    { FileSystem::Type::Unformatted, QLatin1String("!0") } // FIXME: Doesn't work
};
static QLatin1String getPartitionType(FileSystem::Type t)
{
    for (quint32 i = 0; i < sizeof(typemap) / sizeof(typemap[0]); i++)
        if (typemap[i].type == t)
            return typemap[i].partitionType;

    return QLatin1String();
}

QString GpartPartitionTable::createPartition(Report& report, const Partition& partition)
{
    if ( !(partition.roles().has(PartitionRole::Extended) || partition.roles().has(PartitionRole::Logical) || partition.roles().has(PartitionRole::Primary) ) ) {
        report.line() << xi18nc("@info:progress", "Unknown partition role for new partition <filename>%1</filename> (roles: %2)", partition.deviceNode(), partition.roles().toString());
        return QString();
    }

    QLatin1String type;
    if (partition.roles().has(PartitionRole::Extended))
        type = QLatin1String("ebr");
    else
        type = getPartitionType(partition.fileSystem().type());

    ExternalCommand createCommand(report, QStringLiteral("gpart"), {
        QStringLiteral("add"),
        QStringLiteral("-b"),
        QString::number(partition.firstSector()),
        QStringLiteral("-s"),
        QString::number(partition.length()),
        QStringLiteral("-t"),
        type,
        partition.devicePath()
    } );
    if (createCommand.start(-1) && createCommand.exitCode() == 0) {
        QRegularExpression re(QStringLiteral("(.+) added"));
        QRegularExpressionMatch rem = re.match(createCommand.output());
        if (rem.hasMatch()) {
            return QStringLiteral("/dev/") + rem.captured(1);
        }
    }

    report.line() << xi18nc("@info:progress", "Failed to add partition <filename>%1</filename> to device <filename>%2</filename>.", partition.deviceNode(), m_device->deviceNode());

    return QString();
}

bool GpartPartitionTable::deletePartition(Report& report, const Partition& partition)
{
    ExternalCommand deleteCommand(report, QStringLiteral("gpart"), {
        QStringLiteral("delete"),
        QStringLiteral("-i"),
        QString::number(partition.number()),
        partition.devicePath()
    } );
    if (deleteCommand.start(-1) && deleteCommand.exitCode() == 0)
        return true;

    report.line() << xi18nc("@info:progress", "Could not delete partition <filename>%1</filename>.", partition.devicePath());
    return false;
}

bool GpartPartitionTable::updateGeometry(Report& report, const Partition& partition, qint64 sectorStart, qint64 sectorEnd)
{
    // update geometry is partition recreation
    QLatin1String type;
    if (partition.roles().has(PartitionRole::Extended))
        type = QLatin1String("ebr");
    else
        type = getPartitionType(partition.fileSystem().type());

    ExternalCommand gpartCommand(report, QStringLiteral("gpart"), {
        QStringLiteral("delete"),
        QStringLiteral("-i"),
        QString::number(partition.number()),
        partition.devicePath()
    } );
    ExternalCommand createCommand(report, QStringLiteral("gpart"), {
        QStringLiteral("add"),
        QStringLiteral("-b"),
        QString::number(sectorStart),
        QStringLiteral("-s"),
        QString::number(sectorEnd - sectorStart + 1),
        QStringLiteral("-i"),
        QString::number(partition.number()),
        QStringLiteral("-t"),
        type,
        partition.devicePath()
    } );

    return gpartCommand.run(-1) && gpartCommand.exitCode() == 0
        && createCommand.run(-1) && createCommand.exitCode() == 0;
}

bool GpartPartitionTable::clobberFileSystem(Report& report, const Partition& partition)
{
    Q_UNUSED(report)
    Q_UNUSED(partition)

    return true;
}

bool GpartPartitionTable::resizeFileSystem(Report& report, const Partition& partition, qint64 newLength)
{
    // Nobody doesn't call it?
    return false;

    ExternalCommand gpartCommand(report, QStringLiteral("gpart"), {
        QStringLiteral("resize"),
        QStringLiteral("-i"),
        QString::number(partition.number()),
        QStringLiteral("-s"),
        QString::number(newLength),
        partition.devicePath()
    } );
    return gpartCommand.run(-1) && gpartCommand.exitCode() == 0;
}

FileSystem::Type GpartPartitionTable::detectFileSystemBySector(Report& report, const Device& device, qint64 sector)
{
    Q_UNUSED(report)
    Q_UNUSED(device)
    Q_UNUSED(sector)

    FileSystem::Type type = FileSystem::Type::Unknown;
    return type;
}

bool GpartPartitionTable::setPartitionSystemType(Report& report, const Partition& partition)
{
    QString partitionType = partition.type();
    if (partitionType.isEmpty())
        partitionType = getPartitionType(partition.fileSystem().type());
    else
        partitionType.prepend(QStringLiteral("!"));
    if (partitionType.isEmpty())
        return true;

    ExternalCommand gpartCommand(report, QStringLiteral("gpart"), {
        QStringLiteral("modify"),
        QStringLiteral("-i"),
        QString::number(partition.number()),
        QStringLiteral("-t"),
        partitionType,
        partition.devicePath()
    } );
    return gpartCommand.run(-1) && gpartCommand.exitCode() == 0;
}

bool GpartPartitionTable::setPartitionLabel(Report& report, const Partition& partition, const QString& label)
{
    Q_UNUSED(report)
    Q_UNUSED(partition)
    Q_UNUSED(label)

    if (label.isEmpty())
        return true;
    ExternalCommand gpartCommand(report, QStringLiteral("gpart"), {
        QStringLiteral("modify"),
        QStringLiteral("-i"),
        QString::number(partition.number()),
        QStringLiteral("-l"),
        label,
        partition.devicePath()
    } );
    return gpartCommand.run(-1) && gpartCommand.exitCode() == 0;
}

QString GpartPartitionTable::getPartitionUUID(Report& report, const Partition& partition)
{
    Q_UNUSED(report)
    Q_UNUSED(partition)

    return QString();
}

bool GpartPartitionTable::setPartitionUUID(Report& report, const Partition& partition, const QString& uuid)
{
    Q_UNUSED(report)
    Q_UNUSED(partition)
    Q_UNUSED(uuid)

    return true;
}

bool GpartPartitionTable::setPartitionAttributes(Report& report, const Partition& partition, quint64 attrs)
{
    Q_UNUSED(report)
    Q_UNUSED(partition)
    Q_UNUSED(attrs)

    return true;
}

bool GpartPartitionTable::setFlag(Report& report, const Partition& partition, PartitionTable::Flag partitionManagerFlag, bool state)
{
    Q_UNUSED(report)
    Q_UNUSED(partition)
    Q_UNUSED(partitionManagerFlag)
    Q_UNUSED(state)

    return true;
}
