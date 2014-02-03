#include "devices.hpp"

#include <cstring>
#include <string>
#include <errno.h>
#include <mntent.h>
#include <fstab.h>
#include <sys/mount.h>
#include <sys/statvfs.h>


#include "config.h"
#include "log.hpp"

Devices::Devices() {
    _bigDiskConnected = false;
    _udev = udev_new();
    _monitor = udev_monitor_new_from_netlink(_udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(_monitor, "block", NULL);
    udev_monitor_enable_receiving(_monitor);
    _udevFd = udev_monitor_get_fd(_monitor);

    udev_enumerate *lister = udev_enumerate_new(_udev);
    udev_enumerate_add_match_subsystem(lister, "block");
    udev_enumerate_scan_devices(lister);
    udev_list_entry *devlist = udev_enumerate_get_list_entry(lister);
    udev_list_entry *devIt;
    udev_list_entry_foreach(devIt, devlist) {
        const char *path = udev_list_entry_get_name(devIt);
	udev_device *device = udev_device_new_from_syspath(_udev, path);
        _onAdded(device);
        udev_device_unref(device);
    }
    udev_enumerate_unref(lister);
    _checkSizes();
}


Devices::~Devices() {
    udev_monitor_unref(_monitor);
    udev_unref(_udev);
    for(char *name : _copyables) {
        free(name);
    }
}

void Devices::manageChanges() {
    udev_device *device = udev_monitor_receive_device(_monitor);
    if(!device) {
        log(LOG_ERR, "No Device from receive_device().");
        return;
    }
    const char* action = udev_device_get_action(device);
    if(strcmp(action, "add") == 0) {
        _onAdded(device);
    }
    else if(strcmp(action, "remove") == 0) {
        _onRemoved(device);
    }
    else {
        _onChanged(device);
    }
    udev_device_unref(device);
}

bool Devices::isBigDiskConnected() const {
    return _bigDiskConnected;
}

int Devices::getUdevFd() const {
    return _udevFd;
}

bool Devices::isCopyAvailable() const {
    return _bigDiskConnected && !_copyables.empty();
}

void Devices::_onAdded(udev_device *device) {
    const char *devtype = udev_device_get_devtype(device);
    if(strcmp(devtype, "partition") != 0) {
        return;
    }
/*            const char* idFsType = udev_device_get_property_value(device, "ID_FS_TYPE");
            const char* idFsLabel = udev_device_get_property_value(device, "ID_FS_LABEL");
*/
    Devices::mountStatus status = _getStatus(device);
    const char* sysname = udev_device_get_sysname(device);
    switch(status) {
        case Devices::ignored: log(LOG_INFO, "ignored: %s", sysname); break;
        case Devices::system: log(LOG_INFO, "system: %s", sysname); break;
        case Devices::umounted: log(LOG_INFO, "umounted: %s", sysname); break;
        case Devices::ro: log(LOG_INFO, "ro: %s", sysname); break;
        case Devices::rw: log(LOG_INFO, "rw: %s", sysname); break;
    }
    bool mountFailure = false;
    if((status == Devices::umounted) || (status == Devices::rw)){
        mountFailure = !_mount(device, true, status);
    }
    if((!mountFailure) && (status != Devices::ignored) && (status != Devices::system)) {
        const char* idFsLabelEnc = udev_device_get_property_value(device, "ID_FS_LABEL_ENC");
        if(strcmp(idFsLabelEnc, BIG_DISK_NAME) == 0){
            _bigDiskConnected = true;
        }
        else{
            _copyables.push_back(strdup(idFsLabelEnc));
        }
    }
}

void Devices::_onRemoved(udev_device *device) {
    const char *devtype = udev_device_get_devtype(device);
    if(strcmp(devtype, "partition") != 0) {
        return;
    }
    _umount(device);
    const char* idFsLabelEnc = udev_device_get_property_value(device, "ID_FS_LABEL_ENC");
    if(strcmp(idFsLabelEnc, BIG_DISK_NAME) == 0){
        _bigDiskConnected = false;
    }
    else{
        _copyables.remove_if([&](const char* name) -> bool {
            return (strcmp(idFsLabelEnc, name) == 0);
        });
    }
}


void Devices::_onChanged(udev_device *device) {

}

void Devices::_checkSizes() {
    if(!isCopyAvailable()) {
        return;
    }
    struct statvfs info;
    if(statvfs("/media/" BIG_DISK_NAME, &info)) {
        log(LOG_ERR, "unable to get space available on %s: %s", "/media/" BIG_DISK_NAME, strerror(errno));
        return;
    }
    double bigDiskSpace = (double)info.f_bfree * (double)info.f_bsize;

    _copyables.remove_if([&](const char* name) -> bool {
        std::string path = "/media/";
        path += name;
        if(statvfs(path.c_str(), &info)) {
            log(LOG_ERR, "unable to get used space on %s: %s", path.c_str(), strerror(errno));
            return true;
        }
        double available = (double)info.f_bfree * (double)info.f_bsize;
        double total = (double)info.f_blocks * (double)info.f_bsize;
        double used = total - available;
        if(bigDiskSpace > used) {
            return false;
        }
        else {
            log(LOG_INFO, "not enought space for %s", path.c_str());
            return true;
        }
    });
}

Devices::mountStatus Devices::_getStatus(udev_device *device) const { 
    const char* idFsLabelEnc = udev_device_get_property_value(device, "ID_FS_LABEL_ENC");
    if(idFsLabelEnc == NULL) {
        return Devices::system;
    }

    //check the ignore list
    const char* toIgnore[] = IGNORED_PARTITIONS;
    unsigned int toIgnoreLength = sizeof(toIgnore)/sizeof(const char*);

    const char* uuid = udev_device_get_property_value(device, "ID_FS_UUID");
    const char* sysname = udev_device_get_sysname(device);
    std::string devname = "/dev/";
    devname += sysname;

    for(unsigned int i = 0; i < toIgnoreLength; i++) {
        if((strcmp(toIgnore[i], idFsLabelEnc) == 0) || (strcmp(toIgnore[i], sysname) == 0) ||
            (strcmp(toIgnore[i], uuid) == 0) || (devname == toIgnore[i])) {
                return Devices::ignored;
        }
    }

    //Check in fstab for system partitions
    fstab *tab = getfsspec(devname.c_str());
    if(tab){
        return Devices::system;
    }
    tab = getfsspec(uuid);
    if(tab){
        return Devices::system;
    }

    //Check if/how it is mounted
    FILE *mtab = setmntent(_PATH_MOUNTED, "r");
    mntent *partInfo;
    while ((partInfo = getmntent(mtab))) {
         if((devname == partInfo->mnt_fsname) || (strcmp(uuid, partInfo->mnt_fsname) == 0)) {
            bool readOnly = hasmntopt(partInfo, MNTOPT_RO) != NULL;
            endmntent(mtab);
            return readOnly ? Devices::ro : Devices::rw;
         }
    }
    endmntent(mtab);
    return Devices::umounted;
}

bool Devices::_mount(udev_device *device, bool readOnly, Devices::mountStatus status) const {
    if(status == Devices::undefined) {
        status = _getStatus(device);
    }
    if((status == Devices::ignored) || (status == Devices::system)) {
        return false;
    }
    if((status == Devices::rw) && (!readOnly)) {
        return false;
    }
    if((status == Devices::ro) && (readOnly)) {
        return false;
    }

    std::string path = "/media/";
    path += udev_device_get_property_value(device, "ID_FS_LABEL_ENC");
    int result = mkdir(path.c_str(), S_IRWXU|S_IRWXG|S_IRWXO);
    if((result != 0) && (errno != EEXIST)){
        log(LOG_ERR, "Unable to create folder %s: %s", path.c_str(), strerror(errno));
        return false;
    }

    const char* fstype = udev_device_get_property_value(device, "ID_FS_TYPE");
    unsigned long int options;
    std::string data;
    if(strcmp(fstype, "vfat") == 0) {
        options = MS_NOATIME;
        data = "uid=" DOS_PART_OWNER;
    }
    else if(strcmp(fstype, "ntfs") == 0) {
        options = MS_NOATIME;
        data = "uid=" DOS_PART_OWNER ",locale=en_US.UTF8";
    }
    else if(strcmp(fstype, "ext2") == 0) {
        options = MS_NOATIME;
    }
    else if(strcmp(fstype, "ext3") == 0) {
        options = MS_NOATIME;
    }
    else if(strcmp(fstype, "ext4") == 0) {
        options = MS_NOATIME;
    }
    else {
        log(LOG_ERR, "unmanaged fs type %s", fstype);
        return false;
    }
    if(status != Devices::umounted){
        options |= MS_REMOUNT;
    }
    if(readOnly) {
        options |= MS_RDONLY;
    }

    std::string devname = "/dev/";
    devname += udev_device_get_sysname(device);
    if(mount(devname.c_str(), path.c_str(), fstype, options, data.c_str()) != 0){
        log(LOG_ERR, "mount %s failed: %s", devname.c_str(), strerror(errno));
        return false;
    }
    return true;
}

bool Devices::_umount(udev_device *device, Devices::mountStatus status) const {
    if(status == Devices::undefined) {
        status = _getStatus(device);
    }
    if((status == Devices::ignored) || (status == Devices::system)) {
        return false;
    }
    if(status == Devices::umounted) {
        return false;
    }

    std::string path = "/media/";
    path += udev_device_get_property_value(device, "ID_FS_LABEL_ENC");
    if(umount2(path.c_str(), MNT_DETACH) != 0){
        std::string devname = "/dev/";
        devname += udev_device_get_sysname(device);
        log(LOG_ERR, "umount %s (%s) failed: %s", path.c_str(), devname.c_str(), strerror(errno));
        return false;
    }

    int result = rmdir(path.c_str());
    if((result != 0) && (errno != ENOENT)){
        log(LOG_ERR, "Unable to delete folder %s: %s", path.c_str(), strerror(errno));
        return false;
    }
    return true;
}
