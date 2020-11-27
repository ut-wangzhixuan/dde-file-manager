/*
 * Copyright (C) 2019 ~ 2019 Deepin Technology Co., Ltd.
 *               2019 ~ 2019 Chris Xiong
 *
 * Author:     Chris Xiong<chirs241097@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dfmrootcontroller.h"
#include "dfmevent.h"
#include "dfmapplication.h"
#include "models/dfmrootfileinfo.h"
#include "private/dabstractfilewatcher_p.h"
#include "utils/singleton.h"
#include "app/define.h"
#include "app/filesignalmanager.h"
#include "shutil/fileutils.h"
#include <dgiofile.h>
#include <dgiofileinfo.h>
#include <dgiomount.h>
#include <dgiovolume.h>
#include <dgiovolumemanager.h>
#include <ddiskmanager.h>
#include <dblockdevice.h>
#include <ddiskdevice.h>

#include "dialogs/dialogmanager.h"
#include "deviceinfo/udisklistener.h"

#include <gvfs/networkmanager.h>

#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

class DFMRootFileWatcherPrivate : public DAbstractFileWatcherPrivate
{
public:
    explicit DFMRootFileWatcherPrivate(DFMRootFileWatcher *qq)
        : DAbstractFileWatcherPrivate(qq) {}

    bool start() override;
    bool stop() override;

protected:
    void initBlockDevConnections(QSharedPointer<DBlockDevice> blk, const QString &devs);

private:
    QSharedPointer<DGioVolumeManager> vfsmgr;
    QSharedPointer<DDiskManager> udisksmgr;
    QList<QMetaObject::Connection> connections;
    QList<QSharedPointer<DBlockDevice>> blkdevs;
    QStringList connectionsurl;

    Q_DECLARE_PUBLIC(DFMRootFileWatcher)
};

DFMRootController::DFMRootController(QObject *parent) : DAbstractFileController(parent)
{

}

bool DFMRootController::renameFile(const QSharedPointer<DFMRenameEvent> &event) const
{
    DAbstractFileInfoPointer fi(new DFMRootFileInfo(event->fromUrl()));
    if (!fi->canRename()) {
        return false;
    }

    QString udiskspath = fi->extraProperties()["udisksblk"].toString();
    QScopedPointer<DBlockDevice> blk(DDiskManager::createBlockDevice(udiskspath));
    Q_ASSERT(blk && blk->path().length() > 0);

    blk->setLabel(event->toUrl().path(), {});
    if (blk->lastError().type() != QDBusError::NoError) {
        qDebug() << blk->lastError() << blk->lastError().name();
    }

    return blk->lastError().type() == QDBusError::NoError;
}

const QList<DAbstractFileInfoPointer> DFMRootController::getChildren(const QSharedPointer<DFMGetChildrensEvent> &event) const
{
    QList<DAbstractFileInfoPointer> ret;

    if (event->url().scheme() != DFMROOT_SCHEME || event->url().path() != "/") {
        return ret;
    }

    static const QList<QString> udir = {"desktop", "videos", "music", "pictures", "documents", "downloads"};
    for (auto d : udir) {
        DAbstractFileInfoPointer fp(new DFMRootFileInfo(DUrl(DFMROOT_ROOT + d + "." SUFFIX_USRDIR)));
        if (fp->exists()) {
            ret.push_back(fp);
        }
    }

    DDiskManager dummy;
    QStringList blkds = dummy.blockDevices({});
    for (auto blks : blkds) {
        QScopedPointer<DBlockDevice> blk(DDiskManager::createBlockDevice(blks));
        QScopedPointer<DDiskDevice> drv(DDiskManager::createDiskDevice(blk->drive()));
        if (DFMGlobal::isWayLand() && blks.contains(QRegularExpression("/sd[a-c][1-9]*$"))) {
            qDebug()  << " blDev->drive()"  << blks << blk->drive();
            continue;
        }

        if (!blk->hasFileSystem() && !drv->mediaCompatibility().join(" ").contains("optical") && !blk->isEncrypted()) {
            if (!drv->removable()) // 满足外围条件的本地磁盘，直接遵循以前的处理直接 continue
                continue;

            if (FileUtils::deviceShouldBeIgnore(blk->device())) // 对于可移动设备，根据设备描述符选择过滤（sp3 需求，异常设备需要显示，以提供格式化入口）
                continue;
        }

        //检查挂载目录下是否存在diskinfo文件
        QByteArrayList mps = blk->mountPoints();
        if (!mps.empty()) {
            QString mpPath(mps.front());
            if (mpPath.lastIndexOf("/") != (mpPath.length() - 1))
                mpPath += "/";
            QDir kidDir(mpPath + "UOSICON");
            if (kidDir.exists()) {
                QString jsonPath = kidDir.absolutePath();
                loadDiskInfo(jsonPath);
            }
        }
        if ((blk->hintIgnore() && !blk->isEncrypted()) || blk->cryptoBackingDevice().length() > 1) {
            continue;
        }
        using namespace DFM_NAMESPACE;
        if (DFMApplication::genericAttribute(DFMApplication::GA_HiddenSystemPartition).toBool() && blk->hintSystem()) {
            continue;
        }

        DAbstractFileInfoPointer fp(new DFMRootFileInfo(DUrl(DFMROOT_ROOT + QString(blk->device()).mid(QString("/dev/").length()) + "." SUFFIX_UDISKS)));
        ret.push_back(fp);
    }

    for (auto gvfsvol : DGioVolumeManager::getVolumes()) {
        if (gvfsvol->volumeMonitorName().contains(QRegularExpression("(MTP|GPhoto2|Afc)$")) && !gvfsvol->getMount()) {
            gvfsvol->mount();
        }
    }
    if (event->canconst()) {
        return ret;
    }
    //寻找所有的移动设备（移动硬盘，手机，U盘等）
    QStringList urllist;
    for (auto gvfsmp : DGioVolumeManager::getMounts()) {
        if (gvfsmp->getVolume() && gvfsmp->getVolume()->volumeMonitorName().endsWith("UDisks2")) {
            continue;
        }
        if (gvfsmp->mountClass() == "GUnixMount") {
            continue;
        }
        if (DUrl(gvfsmp->getRootFile()->uri()).scheme() == BURN_SCHEME) {
            continue;
        }
        DUrl url;
        url.setScheme(DFMROOT_SCHEME);
        url.setPath("/" + QUrl::toPercentEncoding(gvfsmp->getRootFile()->path()) + "." SUFFIX_GVFSMP);

        if (urllist.contains(QString("/" + QUrl::toPercentEncoding(gvfsmp->getRootFile()->path()) + "." SUFFIX_GVFSMP))) {
            continue;
        }
        DAbstractFileInfoPointer fp(new DFMRootFileInfo(url));
        if (fp->exists()) {
            urllist << QString("/" + QUrl::toPercentEncoding(gvfsmp->getRootFile()->path()) + "." SUFFIX_GVFSMP);
            ret.push_back(fp);
        }
    }
    qDebug() << "获取mountfile  exists  jieshu" << QThread::currentThreadId();
    return ret;
}

const DAbstractFileInfoPointer DFMRootController::createFileInfo(const QSharedPointer<DFMCreateFileInfoEvent> &event) const
{
    return DAbstractFileInfoPointer(new DFMRootFileInfo(event->url()));
}

DAbstractFileWatcher *DFMRootController::createFileWatcher(const QSharedPointer<DFMCreateFileWatcherEvent> &event) const
{
    return new DFMRootFileWatcher(event->url());
}

void DFMRootController::loadDiskInfo(const QString &jsonPath) const
{
    //不存在该目录
    if (jsonPath.isEmpty()) {
        return;
    }

    //读取本地json文件
    QFile file(jsonPath + "/diskinfo.json");
    if (!file.open(QIODevice::ReadWrite)) {
        return;
    }

    //解析json文件
    QJsonParseError jsonParserError;
    QJsonDocument jsonDocument = QJsonDocument::fromJson(file.readAll(), &jsonParserError);
    if (jsonDocument.isNull() || jsonParserError.error != QJsonParseError::NoError) {
        return;
    }

    if (jsonDocument.isObject()) {
        QJsonObject jsonObject = jsonDocument.object();
        if (jsonObject.contains("DISKINFO") && jsonObject.value("DISKINFO").isArray()) {
            QJsonArray jsonArray = jsonObject.value("DISKINFO").toArray();
            for (int i = 0; i < jsonArray.size(); i++) {
                if (jsonArray[i].isObject()) {
                    QJsonObject jsonObjectInfo = jsonArray[i].toObject();
                    DiskInfoStr str;
                    if (jsonObjectInfo.contains("uuid"))
                        str.uuid = jsonObjectInfo.value("uuid").toString();
                    if (jsonObjectInfo.contains("drive"))
                        str.driver = jsonObjectInfo.value("drive").toString();
                    if (jsonObjectInfo.contains("label"))
                        str.label = jsonObjectInfo.value("label").toString();

                    DFMRootFileInfo::DiskInfoMap[str.uuid] = str;
                }
            }
        }
    }
}

DFMRootFileWatcher::DFMRootFileWatcher(const DUrl &url, QObject *parent):
    DAbstractFileWatcher(*new DFMRootFileWatcherPrivate(this), url, parent)
{

}

void DFMRootFileWatcherPrivate::initBlockDevConnections(QSharedPointer<DBlockDevice> blk, const QString &devs)
{
    Q_Q(DFMRootFileWatcher);
    DFMRootFileWatcher *wpar = qobject_cast<DFMRootFileWatcher *>(q);
    blkdevs.push_back(blk);
    blk->setWatchChanges(true);
    QString urlstr = DFMROOT_ROOT + devs.mid(QString("/org/freedesktop/UDisks2/block_devices/").length()) + "." SUFFIX_UDISKS;
    DUrl url(urlstr);

    if (blk->isEncrypted()) {
        QSharedPointer<DBlockDevice> ctblk(DDiskManager::createBlockDevice(blk->cleartextDevice()));
        blkdevs.push_back(ctblk);
        ctblk->setWatchChanges(true);
        foreach (const QString &str, connectionsurl) {
            if (str == urlstr + QString("_en")) {
                return;
            }
        }
        connections.push_back(QObject::connect(blk.data(), &DBlockDevice::cleartextDeviceChanged, [wpar, url](const QString &) {
            Q_EMIT wpar->fileAttributeChanged(url);
        }));
        connections.push_back(QObject::connect(ctblk.data(), &DBlockDevice::idLabelChanged, [wpar, url](const QString &) {
            Q_EMIT wpar->fileAttributeChanged(url);
        }));
        connections.push_back(QObject::connect(ctblk.data(), &DBlockDevice::mountPointsChanged, [wpar, url](const QByteArrayList &) {
            Q_EMIT wpar->fileAttributeChanged(url);
        }));
        connectionsurl << urlstr + QString("_en");
    } else {
        foreach (const QString &str, connectionsurl) {
            if (str == urlstr) {
                return;
            }
        }
        connections.push_back(QObject::connect(blk.data(), &DBlockDevice::idLabelChanged, [wpar, url](const QString &) {
            Q_EMIT wpar->fileAttributeChanged(url);
        }));
        connections.push_back(QObject::connect(blk.data(), &DBlockDevice::mountPointsChanged, [wpar, url](const QByteArrayList &) {
            Q_EMIT wpar->fileAttributeChanged(url);
        }));

        connections.push_back(QObject::connect(blk.data(), &DBlockDevice::sizeChanged, [wpar, url](qulonglong) {
            Q_EMIT wpar->fileAttributeChanged(url);
        }));
        connections.push_back(QObject::connect(blk.data(), &DBlockDevice::idTypeChanged, [wpar, url](QString) {
            Q_EMIT wpar->fileAttributeChanged(url);
        }));
        connections.push_back(QObject::connect(blk.data(), &DBlockDevice::cleartextDeviceChanged, [wpar, url](const QString &) {
            Q_EMIT wpar->fileAttributeChanged(url);
        }));
        connectionsurl << urlstr;
    }
}

bool DFMRootFileWatcherPrivate::start()
{
    Q_Q(DFMRootFileWatcher);

    if (q->fileUrl().path() != "/" || started) {
        return false;
    }

    if (!vfsmgr) {
        vfsmgr.reset(new DGioVolumeManager);
    }
    if (!udisksmgr) {
        udisksmgr.reset(new DDiskManager);
    }

    udisksmgr->setWatchChanges(true);

    DFMRootFileWatcher *wpar = qobject_cast<DFMRootFileWatcher *>(q);

    connections.push_back(QObject::connect(vfsmgr.data(), &DGioVolumeManager::mountAdded, [wpar](QExplicitlySharedDataPointer<DGioMount> mnt) {
        if (mnt->getVolume() && mnt->getVolume()->volumeMonitorName().endsWith("UDisks2")) {
            return;
        }
        if (DUrl(mnt->getRootFile()->uri()).scheme() == BURN_SCHEME) {
            return;
        }
        if (mnt->mountClass() == "GUnixMount") {
            return;
        }
        if (mnt->getRootFile()->path().length() == 0) {
            return;
        }
        DUrl url;
        url.setScheme(DFMROOT_SCHEME);
        url.setPath("/" + QUrl::toPercentEncoding(mnt->getRootFile()->path()) + "." SUFFIX_GVFSMP);
        Q_EMIT wpar->subfileCreated(url);
    }));
    connections.push_back(QObject::connect(vfsmgr.data(), &DGioVolumeManager::mountRemoved, [wpar](QExplicitlySharedDataPointer<DGioMount> mnt) {
        if (mnt->getVolume() && mnt->getVolume()->volumeMonitorName().endsWith("UDisks2")) {
            return;
        }

        DUrl url;
        url.setScheme(DFMROOT_SCHEME);
        QString path = mnt->getRootFile()->path();
        // 此处 Gio Wrapper 或许有 bug， 有时可以获取 uri，但无法获取 path, 因此有了以下略丑的代码
        // 目的是将已知的 uri 拼装成 path，相关 bug：46021
        if (path.isNull() || path.isEmpty()) {
            QStringList qq = mnt->getRootFile()->uri().replace("/", "").split(":");
            if (qq.size() >= 3) {
                path = QString("/run/user/") + QString::number(getuid()) +
                       QString("/gvfs/") + qq.at(0) + QString(":host=" + qq.at(1) + QString(",port=") + qq.at(2));
            } else if (qq.size() == 2) {
                //修复bug55778,在mips合arm上这里切分出来就是2个
                if (qq.at(0).startsWith("smb")) {
                    QStringList smblist = mnt->getRootFile()->uri().replace(":/", "").split("/");
                    path = smblist.count() >= 3 ? QString("/run/user/")+ QString::number(getuid()) + QString("/gvfs/") +
                                        smblist.at(0) + QString("-share:server=" + smblist.at(1) + QString(",share=") + smblist.at(2)) : QString();
                }
                else {
                    path = QString("/run/user/")+ QString::number(getuid()) +
                                                                QString("/gvfs/") + qq.at(0) + QString(":host=" + qq.at(1));
                }
            }
        }
        qDebug() << path;
        url.setPath("/" + QUrl::toPercentEncoding(path) + "." SUFFIX_GVFSMP);
        Q_EMIT wpar->fileDeleted(url);
        QString uri = mnt->getRootFile()->uri();
        qDebug() << uri << "mount removed";
        if (uri.contains("smb-share://") || uri.contains("smb://") || uri.contains("ftp://") || uri.contains("sftp://")) {
            // remove NetworkNodes cache, so next time cd uri will fetchNetworks
            QString smbUri = uri;
            if (smbUri.endsWith("/")) {
                smbUri = smbUri.left(smbUri.length() - 1);
            }
            DUrl smbUrl(smbUri);
            NetworkManager::NetworkNodes.remove(smbUrl);
            smbUrl.setPath("");
            NetworkManager::NetworkNodes.remove(smbUrl);

            mnt->unmount(); // yes, we need do it again...otherwise we will goto an removed path like /run/user/1000/gvfs/smb-sharexxxx
        }
    }));
    connections.push_back(QObject::connect(vfsmgr.data(), &DGioVolumeManager::volumeAdded, [](QExplicitlySharedDataPointer<DGioVolume> vol) {
        if (vol->volumeMonitorName().contains(QRegularExpression("(MTP|GPhoto2|Afc)$"))) {
            vol->mount();
        }
    }));
    connections.push_back(QObject::connect(udisksmgr.data(), &DDiskManager::blockDeviceAdded, [wpar, this](const QString & blks) {
        QSharedPointer<DBlockDevice> blk(DDiskManager::createBlockDevice(blks));
        QScopedPointer<DDiskDevice> drv(DDiskManager::createDiskDevice(blk->drive()));

        if (!blk->hasFileSystem() && !drv->mediaCompatibility().join(" ").contains("optical") && !blk->isEncrypted()) {
            if (!drv->removable()) // 对于本地磁盘，直接按照以前的方式，满足外围条件直接 return
                return;

            if (FileUtils::deviceShouldBeIgnore(blk->device())) // 设备需要被滤去，比如 /dev/sda 下还包含 /dev/sda1 时，需要滤去 sda
                return;

//            dialogManager->showFormatDialog(blk->device()); // 暂时取消监听设备接入时的格式化提示，只在用户进入不可读取的磁盘时提示格式化
        }

        if ((blk->hintIgnore() && !blk->isEncrypted()) || blk->cryptoBackingDevice().length() > 1) {
            return;
        }
        using namespace DFM_NAMESPACE;
        if (DFMApplication::genericAttribute(DFMApplication::GA_HiddenSystemPartition).toBool() && blk->hintSystem()) {
            return;
        }

        initBlockDevConnections(blk, blks);

        Q_EMIT wpar->subfileCreated(DUrl(DFMROOT_ROOT + blks.mid(QString("/org/freedesktop/UDisks2/block_devices/").length()) + "." SUFFIX_UDISKS));
    }));
    connections.push_back(QObject::connect(udisksmgr.data(), &DDiskManager::blockDeviceRemoved, [wpar](const QString & blks) {
        Q_EMIT wpar->fileDeleted(DUrl(DFMROOT_ROOT + blks.mid(QString("/org/freedesktop/UDisks2/block_devices/").length()) + "." SUFFIX_UDISKS));
    }));

    for (auto devs : udisksmgr->blockDevices({})) {
        QSharedPointer<DBlockDevice> blk(DDiskManager::createBlockDevice(devs));
        QScopedPointer<DDiskDevice> drv(DDiskManager::createDiskDevice(blk->drive()));

        auto mountPoints = blk->mountPoints();
        if (!drv->removable() && !mountPoints.isEmpty()) { // feature: hide the specified dir of unremovable devices
            QString mountPoint = mountPoints[0];
            if (!mountPoint.endsWith("/"))
                mountPoint += "/";
            // no permission to create files under '/', cannot create .hidden file, so just hardcode here.
            deviceListener->appendHiddenDirs(mountPoint + "root");
            deviceListener->appendHiddenDirs(mountPoint + "lost+found");
        }

        if (!blk->hasFileSystem() && !drv->mediaCompatibility().join(" ").contains("optical") && !blk->isEncrypted()) {
            continue;
        }
        if ((blk->hintIgnore() && !blk->isEncrypted()) || blk->cryptoBackingDevice().length() > 1) {
            continue;
        }

        initBlockDevConnections(blk, devs);
    }

    started = true;
    return true;
}

bool DFMRootFileWatcherPrivate::stop()
{
    if (!started) {
        return false;
    }

    udisksmgr->setWatchChanges(false);

    for (auto &conn : connections) {
        QObject::disconnect(conn);
    }
    connections.clear();
    connectionsurl.clear();
    blkdevs.clear();

    vfsmgr.clear();
    udisksmgr.clear();

    started = false;

    return true;
}
