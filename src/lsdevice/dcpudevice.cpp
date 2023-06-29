// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "dcpudevice.h"
#include "dlsdevice.h"

#include "scan.h"
#include "hw.h"

#include <QString>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QRegularExpression>

#include <sys/time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <errno.h>
#include <stdio.h>
#include <sys/sysinfo.h>


DDEVICE_BEGIN_NAMESPACE


#define PATH_TPL_CPU_CACHE "/sys/devices/system/cpu/%d/cache"
#define PATH_TPL_CPU_CACHE_ID "/sys/devices/system/cpu/%d/cache/index%d/id"
#define PATH_TPL_CPU_CACHE_TYPE "/sys/devices/system/cpu/%d/cache/index%d/type"
#define PATH_TPL_CPU_CACHE_SIZE "/sys/devices/system/cpu/%d/cache/index%d/size"
#define PATH_TPL_CPU_CACHE_LEVEL "/sys/devices/system/cpu/%d/cache/index%d/level"
#define PATH_TPL_CPU_CACHE_SHR_CPU_LIST "/sys/devices/system/cpu/%d/cache/index%d/shared_cpu_list"

#define PATH_TPL_CPU_FREQ_MAX "/sys/devices/system/cpu/cpu%1/cpufreq/cpuinfo_max_freq"
#define PATH_TPL_CPU_FREQ_MIN "/sys/devices/system/cpu/cpu%1/cpufreq/cpuinfo_min_freq"
#define PATH_TPL_CPU_FREQ_CUR "/sys/devices/system/cpu/cpu%1/cpufreq/scaling_cur_freq"

#define PATH_TPL_CPU_TOPO_CORE_ID "/sys/devices/system/cpu/%d/topology/core_id"
#define PATH_TPL_CPU_TOPO_CORE_SIBLINGS "/sys/devices/system/cpu/%d/topology/core_siblings_list"
#define PATH_TPL_CPU_TOPO_PHY_PKG_ID "/sys/devices/system/cpu/%d/topology/physical_package_id"
#define PATH_TPL_CPU_TOPO_THR_SIBLINGS "/sys/devices/system/cpu/%d/topology/thread_siblings_list"

#define PATH_CPU_TEMP_INPUT  "/sys/class/hwmon/hwmon1/temp1_input"

#define PROC_CPU_STAT_PATH "/proc/stat"
#define PROC_CPU_INFO_PATH "/proc/cpuinfo"

struct cpuInfoLst {
    int  processorID {0};
    int   physicalID  {0};
    int   coreID  {0};
    QString   minFreq  {""};
    QString   maxFreq  {""};
    QString   cache_All {""};
    QString   cache_L1 {""};
    QString   cache_L2 {""};
    QString   cache_L3 {""};
    QString   flags  {""};
    QString   stepping  {""};
    QString   family  {""};
    QString   bogoMIPS  {""};
    QString   currentFreq  {""};
    DCpuDevice::DCpuStat   stat ;
    DCpuDevice::DCpuUsage   usage ;
};

struct cpuBaseInfo{
    int       physicalId{0};
    QString   vendor  {""};
    QString   model  {""};
    int   coreCount  {0};
    int   threadCount{0};
    QString   architecture  {""};
    QString   temperature  {""};
    DCpuDevice::DCpuStat   stat ;
    DCpuDevice::DCpuUsage   usage ;
};

class DCpuDevicePrivate
{
public:
    explicit DCpuDevicePrivate(DCpuDevice *parent)
        : q_ptr(parent)
        , m_hwNode("computer", hw::sys_tem)
    {

//        m_listDeviceInfos.clear();
        m_cpuBaseInfos.clear();
        scan_system(m_hwNode);
//        addDeviceInfo(m_hwNode, m_listDeviceInfos);
        addDeviceInfo(m_infos,m_cpuBaseInfos);
    }

    void addDeviceInfo(hwNode &node, QList< DlsDevice::DDeviceInfo >  &infoLst);
    void addDeviceInfo(QList< cpuInfoLst >  &infoLst, QList < cpuBaseInfo > & baseInfoLst);


    Q_DECLARE_PUBLIC(DCpuDevice)
private:
    DCpuDevice *q_ptr = nullptr;

    hwNode            m_hwNode ;
    QList<cpuInfoLst> m_infos;
    QList <cpuBaseInfo> m_cpuBaseInfos;
//    QList< DlsDevice::DDeviceInfo > m_listDeviceInfos;
};

void DCpuDevicePrivate::addDeviceInfo(hwNode &node, QList< DlsDevice::DDeviceInfo> &infoLst)
{
    DlsDevice::DDeviceInfo entry;
    entry.deviceInfoLstMap.clear();
    entry.devClass = DlsDevice::DtkCpu;

    entry.deviceBaseAttrisLst.append("Vendor");
    entry.deviceInfoLstMap.insert("Vendor", QString::fromStdString(node.getVendor()));
    entry.vendorName = QString::fromStdString(node.getVendor());

    entry.deviceBaseAttrisLst.append("Name");
    entry.deviceInfoLstMap.insert("Name", QString::fromStdString(node.getProduct()));
    entry.productName = QString::fromStdString(node.getProduct());

//--------------------------------ADD Children---------------------
    if (hw::processor == node.getClass())
        infoLst.append(entry);
    for (int i = 0; i < node.countChildren(); i++) {
        addDeviceInfo(*node.getChild(i), infoLst);
    }
}

static int readCpuInfo(QMap<QString, QMap<QString, QString>> &cpuInfos)
{
    QFile file(PROC_CPU_INFO_PATH);
    if (!file.exists()) {
        qWarning() << PROC_CPU_INFO_PATH << "not exist.";
        return -1;
    }

    if (!file.open(QFile::ReadOnly)) {
        qWarning() << PROC_CPU_INFO_PATH << "open failed." << file.errorString();
        return -1;
    }

    QString datas = file.readAll();
    file.close();

    int physicalId = 0;
    QStringList processors = datas.split("\n\n", D_SPLIT_BEHAVIOR);
    for (int i = 0; i < processors.count(); ++i) {
        QStringList lines = processors[i].split("\n", D_SPLIT_BEHAVIOR);
        QMap<QString, QString> procInfo;
        for (const QString &line : lines) {
            const QStringList &kv = line.split(':', D_SPLIT_BEHAVIOR);
            procInfo.insert(kv.value(0).trimmed(), kv.value(1).trimmed());
            if (kv.value(0).compare("physical id") && kv.value(1).toInt() != physicalId) {
                physicalId = kv.value(1).toInt();
            }
        }

        if (!procInfo.value("processor").isEmpty())
            cpuInfos.insert(procInfo.value("processor"), procInfo);
    }

    return physicalId + 1;
}

static void readCpuStat(QVector<QPair<DCpuDevice::DCpuStat, DCpuDevice::DCpuUsage>> &cpuinfo)
{
    QFile file(PROC_CPU_STAT_PATH);
    if (!file.exists()) {
        qWarning() << PROC_CPU_STAT_PATH << "not exist.";
        return;
    }

    if (!file.open(QFile::ReadOnly)) {
        qWarning() << PROC_CPU_STAT_PATH << "open failed." << file.errorString();
        return;
    }

    QRegularExpression reg = QRegularExpression("cpu\\d*");
    do {
         QString line = file.readLine();

         //  cpu    7048360   4246    3733400     801045435      846386      0       929664      0       0        0
         //       |  user  |  nice  |   sys    |    idle     |  iowait  | hardqirq | softirq | steal | guest | guest_nice |
         QStringList cpuStatus = line.split(" ", D_SPLIT_BEHAVIOR);
         if (cpuStatus.size() < 11) {
             qWarning() << PROC_CPU_STAT_PATH << "parse failed";
             break;
         }
         if (!reg.match(cpuStatus.at(0)).hasMatch()) {
             break;
         }

         DCpuDevice::DCpuStat stat;
         quint64 *stats[] = {
             &stat.user,
             &stat.nice,
             &stat.sys,
             &stat.idle,
             &stat.iowait,
             &stat.hardirq,
             &stat.softirq,
             &stat.steal,
             &stat.guest,
             &stat.guestNice
         };
         DCpuDevice::DCpuUsage usage;

         int idx = 1;
         for (quint64 *st : stats) {
             *st = cpuStatus.at(idx++).toULongLong();
             usage.total += *st;
         }
         usage.idle = stat.idle + stat.iowait;

         cpuinfo.push_back({stat, usage});
    } while (!file.atEnd());
}

static void readCpuFreq(int processorID, QString *maxFreq = nullptr, QString *minFreq = nullptr)
{
    auto read_freq = [](const QString &filename){
        QString freqInfo;
        QFile freqfile(filename);
        if (freqfile.exists() && freqfile.open(QFile::ReadOnly)) {
             freqInfo = freqfile.readLine();
            int fre = freqInfo.toInt() / 1000;
            freqInfo = QString::number(fre) + "Mhz";
        }
        return freqInfo;
    };

    if (maxFreq)
        *maxFreq = read_freq(QString(PATH_TPL_CPU_FREQ_MAX).arg(processorID));

    if (minFreq)
        *minFreq = read_freq(QString(PATH_TPL_CPU_FREQ_MIN).arg(processorID));
}

static int threadCounts()
{
    QDir dir("/proc");
    QFileInfoList infoList = dir.entryInfoList();
    int threads = 0;
    for (QFileInfo info : infoList) {
        if (info.isDir() && info.fileName().toInt() > 0) {
            QDir taskDir("/proc/" + info.fileName() + "/task");
            threads += taskDir.entryInfoList().count();
        }
    }

    return threads;
}

static inline QString cpuArch()
{
    QString arch;
    struct utsname os {};
    auto rc = uname(&os);
    if (!rc) {
        arch = os.machine;
    }

    return arch;
}

static QString cpuTemperature()
{
    // Linux下获取cpu温度 cat /sys/class/hwmon/hwmon1/temp1_input
    // https://www.kernel.org/doc/Documentation/hwmon/sysfs-interface
    // cat /sys/class/hwmon/hwmon1/temp1_label      Package id 0

    QString temperature;
    QFile tempfile(PATH_CPU_TEMP_INPUT);

    if (tempfile.exists() && tempfile.open(QFile::ReadOnly)) {
        QString tempinfo = tempfile.readLine();
        tempfile.close();
        temperature = QString("%1").arg(tempinfo.toInt() / 1000).append("°C");
    }

    return temperature;
}

void DCpuDevicePrivate::addDeviceInfo(QList<cpuInfoLst> &infoLst, QList<cpuBaseInfo> &baseInfoLst)
{
    QFile file(PROC_CPU_INFO_PATH);

    QVector<QPair<DCpuDevice::DCpuStat, DCpuDevice::DCpuUsage>> cpuStatsAndUsage;

    readCpuStat(cpuStatsAndUsage);

    QMap<QString, QMap<QString, QString>> cpuInfos;
    readCpuInfo(cpuInfos);

    int prePhysicalID = -1;
    for (auto cpuInfo : cpuInfos) {
        struct cpuInfoLst info;
        info.processorID = cpuInfo.value("processor").toInt();
        info.physicalID = cpuInfo.value("physical id").toInt();
        info.coreID = cpuInfo.value("core id").toInt();
        info.cache_All = cpuInfo.value("cache size");
        info.flags = cpuInfo.value("flags");
        info.stepping = cpuInfo.value("stepping");
        info.family = cpuInfo.value("cpu family");
        info.bogoMIPS = cpuInfo.value("bogomips");

        info.currentFreq = cpuInfo.value("cpu MHz");
        readCpuFreq(info.processorID, &info.maxFreq, &info.minFreq);
        if (cpuStatsAndUsage.size() > info.processorID + 1) {
            // 多个 cpu 时
            info.stat = cpuStatsAndUsage.value(info.processorID + 1).first;
            info.usage = cpuStatsAndUsage.value(info.processorID + 1).second;
        }

        if (prePhysicalID != info.physicalID) {
            prePhysicalID = info.physicalID;
            struct cpuBaseInfo baseInfo;
            baseInfo.vendor = cpuInfo.value("vendor_id");
            baseInfo.model = cpuInfo.value("model name");
            baseInfo.coreCount = cpuInfo.value("cpu cores").toInt();
            baseInfo.physicalId = info.physicalID;

            Q_UNUSED(threadCounts());
            baseInfo.threadCount = int(cpuInfos.count()); // threadCounts(); ??
            baseInfo.architecture = cpuArch();
            baseInfo.temperature = cpuTemperature();
            if (cpuStatsAndUsage.size() > 0) {
                // 多个 cpu 时
                baseInfo.stat = cpuStatsAndUsage.value(0).first;
                baseInfo.usage = cpuStatsAndUsage.value(0).second;
            }
            baseInfoLst.append(baseInfo);
        }
        infoLst.append(info);
    }
}

DCpuDevice::DCpuDevice(QObject *parent)
    : QObject(parent)
    , d_ptr(new DCpuDevicePrivate(this))
{
}

DCpuDevice::~DCpuDevice()
{
}

int DCpuDevice::physicalCount()
{
    Q_D(DCpuDevice);
    return int(d->m_cpuBaseInfos.count());
}

int DCpuDevice::coreCount(int physicalID)
{
    Q_D(DCpuDevice);
    if(physicalID < physicalCount())
        return  d->m_cpuBaseInfos[physicalID].coreCount;
    return -1;
}

int DCpuDevice::threadCount(int physicalID, int /*coreID*/)
{  
    Q_D(DCpuDevice); 
    if(physicalID < physicalCount())
        return  d->m_cpuBaseInfos[physicalID].threadCount;
    return -1;
}

int DCpuDevice::physicalID(int processorID)
{
    Q_D(DCpuDevice);
    if(processorID < d->m_infos.count())
        return  d->m_infos[processorID].physicalID;
    return -1;
}

int DCpuDevice::coreID(int processorID)
{
    Q_D(DCpuDevice);
    if(processorID < d->m_infos.count())
        return  d->m_infos[processorID].coreID;
    return -1;
}

QString DCpuDevice::architecture(int physicalID)
{
    Q_D(DCpuDevice);
    if(physicalID < physicalCount())
        return  d->m_cpuBaseInfos[physicalID].architecture;
    return QString();
}

QString DCpuDevice::vendor(int physicalID)
{
    Q_D(DCpuDevice);
    if(physicalID <physicalCount())
        return  d->m_cpuBaseInfos[physicalID].vendor;
    return QString();
}

QString DCpuDevice::model(int physicalID)
{
    Q_D(DCpuDevice);
    if(physicalID <physicalCount())
        return  d->m_cpuBaseInfos[physicalID].model;
    return QString();
}

QString DCpuDevice::minFreq(int processorID)
{
    Q_D(DCpuDevice);
    if(processorID < d->m_infos.count())
        return  d->m_infos[processorID].minFreq;
    return QString();
}

QString DCpuDevice::maxFreq(int processorID)
{
    Q_D(DCpuDevice);
    if(processorID < d->m_infos.count())
        return  d->m_infos[processorID].maxFreq;
    return QString();
}

QString DCpuDevice::cache(int processorID, QString /*type*/)
{
    Q_D(DCpuDevice);
    if(processorID < d->m_infos.count())
        return  d->m_infos[processorID].cache_All;
    return QString();
}

QString DCpuDevice::flags(int processorID)
{
    Q_D(DCpuDevice);
    if(processorID < d->m_infos.count())
        return  d->m_infos[processorID].flags;
    return QString();
}

QString DCpuDevice::stepping(int processorID)
{
    Q_D(DCpuDevice);
    if(processorID < d->m_infos.count())
        return  d->m_infos[processorID].stepping;
    return QString();
}

QString DCpuDevice::family(int processorID)
{
    Q_D(DCpuDevice);
    if(processorID < d->m_infos.count())
        return  d->m_infos[processorID].family;
    return QString();
}

QString DCpuDevice::bogoMIPS(int processorID)
{
    Q_D(DCpuDevice);
    if(processorID < d->m_infos.count())
        return  d->m_infos[processorID].bogoMIPS; 
    return QString();
}

QString DCpuDevice::temperature(int physicalID)
{
    Q_D(DCpuDevice);
    if(physicalID <physicalCount())
        return  d->m_cpuBaseInfos[physicalID].temperature;
    return QString();
}

QString DCpuDevice::currentFreq(int processorID)
{
    Q_D(DCpuDevice);
    if(processorID < d->m_infos.count())
        return  d->m_infos[processorID].currentFreq;
    return QString();
}

DCpuDevice::DCpuStat DCpuDevice::stat()
{
    Q_D(DCpuDevice);
    return  d->m_cpuBaseInfos[0].stat; 
}

DCpuDevice::DCpuUsage DCpuDevice::usage()
{
     Q_D(DCpuDevice);
    return  d->m_cpuBaseInfos[0].usage;
}

DCpuDevice::DCpuStat DCpuDevice::stat(int processorID)
{
        Q_D(DCpuDevice);
    if(processorID < d->m_infos.count())
        return  d->m_infos[processorID].stat; 
    return DCpuStat();
}

DCpuDevice::DCpuUsage DCpuDevice::usage(int processorID)
{
    Q_D(DCpuDevice);
    if(processorID < d->m_infos.count())
    return  d->m_infos[processorID].usage;

    return DCpuUsage();
}

DDEVICE_END_NAMESPACE
