#ifndef _DEVICES_HPP
#define _DEVICES_HPP

#include <libudev.h>
#include <list>

//TODO: perf: use async forblong operation: mounting! , udev scan? , fstab and mtab scan ?
//TODO: good error managment
class Devices {
    public:
       Devices();
       ~Devices();

       bool isBigDiskConnected() const;
       int getUdevFd() const;
       void manageChanges();
       bool isCopyAvailable() const;

    protected:
       enum MountStatus {
           umounted,
           ro,
           rw,
           system,
           ignored,
           undefined
       };

       udev *_udev;
       udev_monitor *_monitor;
       int _udevFd;
       bool _bigDiskConnected;
       std::list<char*> _copyables;

       bool _mount(udev_device*, bool readOnly, MountStatus currentStatus = undefined) const;
       bool _umount(udev_device*, MountStatus currentStatus = undefined) const;
       bool _umount(const char*) const;
       MountStatus _getStatus(udev_device*) const;
       void _checkSizes();
       void _onAdded(udev_device*);
       void _onRemoved(udev_device*);
};

#endif // _DEVICES_HPP
