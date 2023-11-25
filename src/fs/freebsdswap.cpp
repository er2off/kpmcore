/*
    SPDX-FileCopyrightText: 2023 Er2 <er2@dismail.de>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "fs/freebsdswap.h"

#include "util/externalcommand.h"

#include <KLocalizedString>

#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

namespace FS
{
FileSystem::CommandSupportType freebsdswap::m_Create = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType freebsdswap::m_Grow = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType freebsdswap::m_Shrink = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType freebsdswap::m_Move = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType freebsdswap::m_Copy = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType freebsdswap::m_GetUsed = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType freebsdswap::m_GetLabel = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType freebsdswap::m_SetLabel = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType freebsdswap::m_GetUUID = FileSystem::cmdSupportNone;
FileSystem::CommandSupportType freebsdswap::m_UpdateUUID = FileSystem::cmdSupportNone;

freebsdswap::freebsdswap(qint64 firstsector, qint64 lastsector, qint64 sectorsused, const QString& label, const QVariantMap& features) :
    FileSystem(firstsector, lastsector, sectorsused, label, features, FileSystem::Type::FreeBSDSwap)
{
}

void freebsdswap::init()
{
    m_Shrink = m_Grow = m_Create = findExternal(QStringLiteral("swapctl")) ? cmdSupportFileSystem : cmdSupportNone;
    m_SetLabel = cmdSupportNone;
    m_UpdateUUID = cmdSupportNone;
    m_GetLabel = cmdSupportCore;
    m_GetUsed = cmdSupportFileSystem;
    m_Copy = cmdSupportNone;
    m_Move = cmdSupportCore;
    m_GetUUID = cmdSupportNone;
}

bool freebsdswap::supportToolFound() const
{
    return
        m_GetUsed != cmdSupportNone &&
        m_GetLabel != cmdSupportNone &&
        m_SetLabel != cmdSupportNone &&
        m_Create != cmdSupportNone &&
//         m_Check != cmdSupportNone &&
//         m_UpdateUUID != cmdSupportNone &&
        m_Grow != cmdSupportNone &&
        m_Shrink != cmdSupportNone &&
//         m_Copy != cmdSupportNone &&
        m_Move != cmdSupportNone; // &&
//         m_Backup != cmdSupportNone &&
//         m_GetUUID != cmdSupportNone;
}

int freebsdswap::maxLabelLength() const
{
    return 15;
}

bool freebsdswap::create(Report& report, const QString& deviceNode)
{
    // freebsd swap doesn't need to prepare
    Q_UNUSED(report)
    Q_UNUSED(deviceNode)

    return true;
//    ExternalCommand cmd(report, QStringLiteral("mkswap"), { deviceNode });
//    return cmd.run(-1) && cmd.exitCode() == 0;
}

bool freebsdswap::resize(Report& report, const QString& deviceNode, qint64 length) const
{
    Q_UNUSED(report)
    Q_UNUSED(deviceNode)
    Q_UNUSED(length)
    //ExternalCommand cmd(report, QStringLiteral("truncate"), {} );

    return true; //cmd.run(-1) && cmd.exitCode() == 0;
}

QString freebsdswap::mountTitle() const
{
    return xi18nc("@title:menu", "Activate swap");
}

QString freebsdswap::unmountTitle() const
{
    return xi18nc("@title:menu", "Deactivate swap");
}

bool freebsdswap::canMount(const QString& deviceNode, const QString& mountPoint) const {
    Q_UNUSED(deviceNode)
    // freebsd swap doesn't need mount point to activate
    return mountPoint != QStringLiteral("/");
}

bool freebsdswap::mount(Report& report, const QString& deviceNode, const QString& mountPoint)
{
    Q_UNUSED(mountPoint)
    ExternalCommand cmd(report, QStringLiteral("swapon"), { deviceNode });
    return cmd.run(-1) && cmd.exitCode() == 0;
}

bool freebsdswap::unmount(Report& report, const QString& deviceNode)
{
    ExternalCommand cmd(report, QStringLiteral("swapoff"), { deviceNode });
    return cmd.run(-1) && cmd.exitCode() == 0;
}

qint64 freebsdswap::readUsedCapacity(const QString& deviceNode) const
{
    QFileInfo kernelPath(deviceNode);
    ExternalCommand cmd(QStringLiteral("swapctl"), { QStringLiteral("-l") });

    if (cmd.run(-1) && cmd.exitCode() == 0) {
        QByteArray data = cmd.rawOutput();

        QTextStream in(&data);
        while (!in.atEnd()) {
            QStringList line = in.readLine().split(QRegularExpression(QStringLiteral("\\s+")));
            if (line[0] == kernelPath.canonicalFilePath()) {
                return line[1].toLongLong() * 1024;
            }
        }
    }
    return -1;
}
}
