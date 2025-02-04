/*
    SPDX-FileCopyrightText: 2008-2010 Volker Lanz <vl@fidra.de>
    SPDX-FileCopyrightText: 2013-2018 Andrius Štikonas <andrius@stikonas.eu>
    SPDX-FileCopyrightText: 2019 Yuri Chornoivan <yurchor@ukr.net>
    SPDX-FileCopyrightText: 2020 Arnaud Ferraris <arnaud.ferraris@collabora.com>
    SPDX-FileCopyrightText: 2020 Gaël PORTAY <gael.portay@collabora.com>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "fs/ntfs.h"

#include "util/externalcommand.h"
#include "util/capacity.h"
#include "util/report.h"
#include "util/globallog.h"

#include <KLocalizedString>

#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QFile>

#include <algorithm>
#include <ctime>

namespace FS
{
FileSystem::CommandSupportType ntfs::m_GetUsed = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType ntfs::m_GetLabel = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType ntfs::m_Create = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType ntfs::m_Grow = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType ntfs::m_Shrink = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType ntfs::m_Move = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType ntfs::m_Check = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType ntfs::m_Copy = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType ntfs::m_Backup = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType ntfs::m_SetLabel = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType ntfs::m_UpdateUUID = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType ntfs::m_GetUUID = FileSystem::cmdSupportNone;

bool ntfs::m_UseMkNTFS = false;

ntfs::ntfs(qint64 firstsector, qint64 lastsector, qint64 sectorsused, const QString& label, const QVariantMap& features) :
    FileSystem(firstsector, lastsector, sectorsused, label, features, FileSystem::Type::Ntfs)
{
}

void ntfs::init()
{
    m_Shrink = m_Grow = m_Check = findExternal(QStringLiteral("ntfsresize")) ? cmdSupportFileSystem : cmdSupportNone;
    m_GetUsed = findExternal(QStringLiteral("ntfsinfo")) ? cmdSupportFileSystem : cmdSupportNone;
    m_GetLabel = cmdSupportCore;
    m_SetLabel = findExternal(QStringLiteral("ntfslabel")) ? cmdSupportFileSystem : cmdSupportNone;
    // mkfs.ntfs is not always available so also try to find mkntfs
    m_UseMkNTFS = false;
    if (findExternal(QStringLiteral("mkntfs"))) {
        m_UseMkNTFS = true;
        m_Create = cmdSupportFileSystem;
    }
    else if (findExternal(QStringLiteral("mkfs.ntfs")))
        m_Create = cmdSupportFileSystem;
    else
        m_Create = cmdSupportNone;
    m_Copy = findExternal(QStringLiteral("ntfsclone")) ? cmdSupportFileSystem : cmdSupportNone;
    m_Backup = cmdSupportCore;
    m_UpdateUUID = cmdSupportCore;
    m_Move = (m_Check != cmdSupportNone) ? cmdSupportCore : cmdSupportNone;
    m_GetUUID = cmdSupportCore;
}

bool ntfs::supportToolFound() const
{
    return
        m_GetUsed != cmdSupportNone &&
        m_GetLabel != cmdSupportNone &&
        m_SetLabel != cmdSupportNone &&
        m_Create != cmdSupportNone &&
        m_Check != cmdSupportNone &&
        m_UpdateUUID != cmdSupportNone &&
        m_Grow != cmdSupportNone &&
        m_Shrink != cmdSupportNone &&
        m_Copy != cmdSupportNone &&
        m_Move != cmdSupportNone &&
        m_Backup != cmdSupportNone &&
        m_GetUUID != cmdSupportNone;
}

FileSystem::SupportTool ntfs::supportToolName() const
{
    return SupportTool(QStringLiteral("ntfs-3g"), QUrl(QStringLiteral("https://www.tuxera.com/community/open-source-ntfs-3g/")));
}

qint64 ntfs::minCapacity() const
{
    return 2 * Capacity::unitFactor(Capacity::Unit::Byte, Capacity::Unit::MiB);
}

qint64 ntfs::maxCapacity() const
{
    return 256 * Capacity::unitFactor(Capacity::Unit::Byte, Capacity::Unit::TiB);
}

int ntfs::maxLabelLength() const
{
    return 128;
}

qint64 ntfs::readUsedCapacity(const QString& deviceNode) const
{
    ExternalCommand cmd(QStringLiteral("ntfsinfo"), { QStringLiteral("--mft"), QStringLiteral("--force"), deviceNode });

    if (cmd.run(-1) && cmd.exitCode() == 0) {
        QRegularExpression re(QStringLiteral("Cluster Size: (\\d+)"));
        QRegularExpressionMatch reClusterSize = re.match(cmd.output());
        qint64 clusterSize = reClusterSize.hasMatch() ? reClusterSize.captured(1).toLongLong() : -1;

        QRegularExpression re2(QStringLiteral("Free Clusters: (\\d+)"));
        QRegularExpressionMatch reFreeClusters = re2.match(cmd.output());
        qint64 freeClusters = reFreeClusters.hasMatch() ? reFreeClusters.captured(1).toLongLong() : -1;

        QRegularExpression re3(QStringLiteral("Volume Size in Clusters: (\\d+)"));
        QRegularExpressionMatch reVolumeSize = re3.match(cmd.output());
        qint64 volumeSize = reVolumeSize.hasMatch() ? reVolumeSize.captured(1).toLongLong() : -1;

        qint64 usedBytes = -1;
        qDebug () << volumeSize << freeClusters << clusterSize;
        if (clusterSize > -1 && freeClusters > -1 && volumeSize > -1) {
            usedBytes = (volumeSize - freeClusters) * clusterSize;
        }
        return usedBytes;
    }

    return -1;
}

bool ntfs::writeLabel(Report& report, const QString& deviceNode, const QString& newLabel)
{
    ExternalCommand writeCmd(report, QStringLiteral("ntfslabel"), { QStringLiteral("--force"), deviceNode, newLabel }, QProcess::SeparateChannels);

    if (!writeCmd.run(-1))
        return false;

    return true;
}

bool ntfs::check(Report& report, const QString& deviceNode) const
{
    ExternalCommand cmd(report, QStringLiteral("ntfsresize"), { QStringLiteral("--no-progress-bar"), QStringLiteral("--info"), QStringLiteral("--force"), QStringLiteral("--verbose"), deviceNode });
    return cmd.run(-1) && cmd.exitCode() == 0;
}

bool ntfs::create(Report& report, const QString& deviceNode)
{
    ExternalCommand cmd(report, m_UseMkNTFS ? QStringLiteral("mkntfs") : QStringLiteral("mkfs.ntfs"), { QStringLiteral("--quick"), QStringLiteral("--verbose"), deviceNode });
    return cmd.run(-1) && cmd.exitCode() == 0;
}

bool ntfs::copy(Report& report, const QString& targetDeviceNode, const QString& sourceDeviceNode) const
{
    ExternalCommand cmd(report, QStringLiteral("ntfsclone"), { QStringLiteral("--force"), QStringLiteral("--overwrite"), targetDeviceNode, sourceDeviceNode });

    return cmd.run(-1) && cmd.exitCode() == 0;
}

bool ntfs::resize(Report& report, const QString& deviceNode, qint64 length) const
{
    QStringList args = { QStringLiteral("--no-progress-bar"), QStringLiteral("--force"), deviceNode, QStringLiteral("--size"), QString::number(length) };

    QStringList dryRunArgs = args;
    dryRunArgs << QStringLiteral("--no-action");
    ExternalCommand cmdDryRun(QStringLiteral("ntfsresize"), dryRunArgs);

    if (cmdDryRun.run(-1) && cmdDryRun.exitCode() == 0) {
        ExternalCommand cmd(report, QStringLiteral("ntfsresize"), args);
        return cmd.run(-1) && cmd.exitCode() == 0;
    }

    return false;
}

bool ntfs::updateUUID(Report& report, const QString& deviceNode) const
{
    Q_UNUSED(report)
    ExternalCommand cmd(QStringLiteral("ntfslabel"), { QStringLiteral("--new-serial"), deviceNode });

    return cmd.run(-1) && cmd.exitCode() == 0;
}

bool ntfs::updateBootSector(Report& report, const QString& deviceNode) const
{
    report.line() << xi18nc("@info:progress", "Updating boot sector for NTFS file system on partition <filename>%1</filename>.", deviceNode);

    qint64 n = firstSector();
    char* s = reinterpret_cast<char*>(&n);

#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    std::swap(s[0], s[3]);
    std::swap(s[1], s[2]);
#endif

    ExternalCommand cmd;
    if (!cmd.writeData(report, QByteArray(s, sizeof(s)), deviceNode, 28)) {
        Log() << xi18nc("@info:progress", "Could not write new start sector to partition <filename>%1</filename> when trying to update the NTFS boot sector.", deviceNode);
        return false;
    }

    // Also update backup NTFS boot sector located at the end of the partition
    // NOTE: this should fail if filesystem does not span the whole partition
    qint64 pos = (lastSector() - firstSector()) * sectorSize() + 28;
    if (!cmd.writeData(report, QByteArray(s, sizeof(s)), deviceNode, pos)) {
        Log() << xi18nc("@info:progress", "Could not write new start sector to partition <filename>%1</filename> when trying to update the NTFS boot sector.", deviceNode);
        return false;
    }

    Log() << xi18nc("@info:progress", "Updated NTFS boot sector for partition <filename>%1</filename> successfully.", deviceNode);

    return true;
}
}
