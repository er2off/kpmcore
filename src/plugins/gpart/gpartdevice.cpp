/*
    SPDX-FileCopyrightText: 2023 Er2 <er2@dismail.de>

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "plugins/gpart/gpartdevice.h"
#include "plugins/gpart/gpartpartitiontable.h"

#include "core/partitiontable.h"

#include "util/externalcommand.h"
#include "util/report.h"

#include <libgeom.h>

GpartDevice::GpartDevice(const Device& d) :
    CoreBackendDevice(d.deviceNode()),
    m_device(&d)
{
}

GpartDevice::~GpartDevice()
{
}

bool GpartDevice::open()
{
    return true;
}

bool GpartDevice::openExclusive()
{
    setExclusive(true);

    return true;
}

bool GpartDevice::close()
{
    if (isExclusive())
        setExclusive(false);

    CoreBackendPartitionTable* ptable = new GpartPartitionTable(m_device);
    ptable->commit();
    delete ptable;

    return true;
}

std::unique_ptr<CoreBackendPartitionTable> GpartDevice::openPartitionTable()
{
    return std::make_unique<GpartPartitionTable>(m_device);
}

bool GpartDevice::createPartitionTable(Report& report, const PartitionTable& ptable)
{
    QString tableType;
    if (ptable.type() == PartitionTable::msdos || ptable.type() == PartitionTable::msdos_sectorbased)
        tableType = QStringLiteral("mbr");
    else
        tableType = ptable.typeName();

    ExternalCommand destroyCommand(report, QStringLiteral("gpart"), {
        QStringLiteral("destroy"),
        QStringLiteral("-F"),
        m_device->deviceNode()
    } );
    ExternalCommand createCommand(report, QStringLiteral("gpart"), {
        QStringLiteral("create"),
        QStringLiteral("-s"),
        tableType,
        m_device->deviceNode()
    } );
    return destroyCommand.run(-1) && createCommand.run(-1) && createCommand.exitCode() == 0;
}
