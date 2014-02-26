#include <libudev.h>
#include <list>

class Devices {
    public:
       Devices();
       ~Devices();

       bool isBigDiskConnected() const;
       int getUdevFd() const;
       void manageChanges();
       bool isCopyAvailable() const;

    protected:
       enum mountStatus {
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

       bool _mount(udev_device*, bool readOnly, mountStatus currentStatus = undefined) const;
       bool _umount(udev_device*, mountStatus currentStatus = undefined) const;
       bool _umount(const char*) const;
       mountStatus _getStatus(udev_device*) const;
       void _checkSizes();
       void _onAdded(udev_device*);
       void _onRemoved(udev_device*);
};
