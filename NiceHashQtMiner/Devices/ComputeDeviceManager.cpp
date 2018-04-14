#define __ComputeDeviceManager_cpp__
//#define _udev_ false
#define _hwinfo_ true
#if _hwinfo_
#	include <hd.h>
#endif
#include "Devices/ComputeDeviceManager.h"
#include "Utils/Helpers.h"
#include "Interfaces/IMessageNotifier.h"
#include "International.h"
#include "Configs/ConfigManager.h"
#include "Configs/Data/GeneralConfig.h"
#if WITH_NVIDIA
#	include "Devices/CUDA_Unsupported.h"
#endif
#include "Utils/Links.h"
#include "Devices/ComputeDevice/ComputeDevice.h"
#include "PInvoke/CPUID.h"
#include "Globals.h"
#include "Devices/CPUUtils.h"
#include "Devices/ComputeDevice/CPUComputeDevice.h"
#if WITH_NVIDIA
//#	include <nvml.h>
//#	include "3rdParty/NVAPI.h" // @!!! neexistuje Linux verza
//#	include "3rdParty/NVML.h"
//#	include "NVCtrl/NVCtrlLib.h"
//#	include "NVCtrl/nv_control.h"
#	include "Devices/CudaDevice.h"
#	include "Devices/ComputeDevice/CudaComputeDevice.h"
#endif
#include "Qt/LException.h"
#include "Devices/OpenCLDevice.h"
#include "Forms/DriverVersionConfirmationDialog.h"
#if WITH_AMD
#	include "3rdParty/ADL.h"
#	include "Devices/AmdGpuDevice.h"
#	include "Devices/ComputeDevice/AmdComputeDevice.h"
#endif
#include <QProcess>
#include <QException>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QJsonArray>
#include <QJsonObject>
#include <sys/sysinfo.h>
#include <sys/resource.h>
#include <boost/integer_traits.hpp>

#if _udev_
#include <libudev.h>
#endif

#if WITH_NVIDIA
//using namespace NVIDIA::NVAPI;
//using namespace NVIDIA::NVML;
#endif
using namespace NiceHashQtMiner; // cpuid
#if WITH_AMD
using namespace ATI::ADL;
#endif


const QString ComputeDeviceManager::Query::Tag="ComputeDeviceManager.Query";

#if WITH_NVIDIA
ComputeDeviceManager::Query::NvidiaSmiDriver::NvidiaSmiDriver(int left, int right)
{
	LeftPart=left;
	_rightPart=right;
}

bool ComputeDeviceManager::Query::NvidiaSmiDriver::IsLesserVersionThan(NvidiaSmiDriver* b)
{
	if (LeftPart<b->LeftPart) {
		return true;
		}
	return LeftPart==b->LeftPart && GetRightVal(_rightPart)<GetRightVal(b->_rightPart);
}

QString ComputeDeviceManager::Query::NvidiaSmiDriver::ToString()
{
	return QString::number(LeftPart)+"."+QString::number(_rightPart);
}

int ComputeDeviceManager::Query::NvidiaSmiDriver::GetRightVal(int val)
{
	if (val>=10) {
		return val;
		}
	return val*10;
}

ComputeDeviceManager::Query::NvidiaSmiDriver* ComputeDeviceManager::Query::NvidiaRecomendedDriver=new ComputeDeviceManager::Query::NvidiaSmiDriver(372, 54);
ComputeDeviceManager::Query::NvidiaSmiDriver* ComputeDeviceManager::Query::NvidiaMinDetectionDriver=new ComputeDeviceManager::Query::NvidiaSmiDriver(362, 61);
ComputeDeviceManager::Query::NvidiaSmiDriver* ComputeDeviceManager::Query::_currentNvidiaSmiDriver=new ComputeDeviceManager::Query::NvidiaSmiDriver(-1, -1);
ComputeDeviceManager::Query::NvidiaSmiDriver* ComputeDeviceManager::Query::InvalidSmiDriver=new ComputeDeviceManager::Query::NvidiaSmiDriver(-1, -1);
#endif
int ComputeDeviceManager::Query::_cpuCount=0;
int ComputeDeviceManager::Query::_gpuCount=0;
#if WITH_NVIDIA
ComputeDeviceManager::Query::NvidiaSmiDriver* ComputeDeviceManager::Query::GetNvidiaSmiDriver()
{
	if (WindowsDisplayAdapters.HasNvidiaVideoController()) {
		try {
			QProcess P;
			P.setProgram("nvidia-smi");
			if (!P.waitForStarted()) {
				return InvalidSmiDriver;
				}
			if (!P.waitForFinished()) {
				return InvalidSmiDriver;
				}
			QString stdOut=QString(P.readAll());
			const QString findString="Driver Version: ";
			foreach (QString line, stdOut.split('\n')) {
				if (line.contains(findString)) {
					int start=line.indexOf(findString);
					QString driverVer=line.mid(start, start+7);
					driverVer=driverVer.replace(findString, "").mid(0, 7).trimmed();
//					double drVerDouble=driverVer.toDouble();
//					int dot=driverVer.indexOf('.');
					int leftPart=driverVer.mid(0, 3).toInt();
					int rightPart=driverVer.mid(4, 2).toInt();
					return new NvidiaSmiDriver(leftPart, rightPart);
					}
				}
			}
		catch (QException ex) {
			Helpers::ConsolePrint(Tag, QString("GetNvidiaSMIDriver Exception: ")+ex.what());
			return InvalidSmiDriver;
			}
		}
	return InvalidSmiDriver;
}
#endif
void ComputeDeviceManager::Query::ShowMessageAndStep(QString infoMsg)
{
	if (MessageNotifier()!=nullptr) {
		MessageNotifier()->SetMessageAndIncrementStep(infoMsg);
		}
}
#if WITH_NVIDIA
bool ComputeDeviceManager::Query::CheckVideoControllersCountMismath()
{
	QList<CudaDevice*> currentCudaDevices;
	if (!Nvidia.IsSkipNvidia()) {
		Nvidia.QueryCudaDevices(&currentCudaDevices);
		}
	int gpusOld=_cudaDevices->count();
	int gpusNew=currentCudaDevices.count();

	Helpers::ConsolePrint("ComputeDeviceManager.CheckCount", QString("CUDA GPUs count: Old: %1 / New: %2").arg(gpusOld).arg(gpusNew));
	return gpusNew<gpusOld;
}
#endif
IMessageNotifier* ComputeDeviceManager::Query::MessageNotifier_=nullptr;

void ComputeDeviceManager::Query::QueryDevices(IMessageNotifier* messageNotifier)
{
	MessageNotifier_=messageNotifier;
	// #0 get video controllers, used for cross checking
	WindowsDisplayAdapters::QueryVideoControllers();
	// Order important CPU Query must be first
	// #1 CPU
	Cpu::QueryCpus();
#if WITH_NVIDIA
	// #2 CUDA
	if (Nvidia.IsSkipNvidia()) {
		Helpers::ConsolePrint(Tag, "Skipping NVIDIA device detection, settings are set to disabled");
		}
	else {
		ShowMessageAndStep(International::GetText("Compute_Device_Query_Manager_CUDA_Query"));
		Nvidia::QueryCudaDevices();
		}
#else
	Helpers::ConsolePrint(Tag, "Skipping NVIDIA device detection, support not compiled in");
#endif
#if WITH_AMD
	// OpenCL and AMD
	if (ConfigManager.generalConfig->DeviceDetection->DisableDetectionAMD) {
		Helpers::ConsolePrint(Tag, "Skipping AMD device detection, settings set to disabled");
		ShowMessageAndStep(International::GetText("Compute_Device_Query_Manager_AMD_Query_Skip"));
		}
	else {
		// #3 OpenCL
		ShowMessageAndStep(International::GetText("Compute_Device_Query_Manager_OpenCL_Query"));
		OpenCL::QueryOpenCLDevices();
		// #4 AMD query AMD from OpenCL devices, get serial and add devices
		Amd::QueryAmd();
		}
#else
	Helpers::ConsolePrint(Tag, "Skipping AMD device detection, support not compiled in");
#endif
//ComputeDevice* cd0=Avaliable.AllAvailableDevices->first();
	// #5 uncheck CPU if GPUs present, call it after we Query all devices
	Group::UncheckedCpu();
//ComputeDevice* cd1=Avaliable.AllAvailableDevices->first();

	// @todo update this to report undetected hardware
	// #6 check NVIDIA, AMD devices count
	int nvidiaCount=0;
	{
		int amdCount=0;
		foreach (VideoControllerData* vidCtrl, *AvailableVideoControllers) {
#if WITH_NVIDIA
			if (vidCtrl->Name.toLower().contains("nvidia") && CudaUnsupported::IsSupported(vidCtrl->Name)) {
				nvidiaCount+=1;
				}
			else if (vidCtrl->Name.toLower().contains("nvidia")) {
				Helpers::ConsolePrint(Tag, "Device not supported NVIDIA/CUDA device not supported "+vidCtrl->Name);
				}
#endif
#if WITH_AMD
			amdCount+=vidCtrl->Name.toLower().contains("amd")? 1 : 0;
#endif
			}
#if WITH_NVIDIA
		Helpers::ConsolePrint(Tag, nvidiaCount==_cudaDevices->count()? "Cuda NVIDIA/CUDA device count GOOD" : "Cuda NVIDIA/CUDA device count BAD!!!");
#endif
#if WITH_AMD
		Helpers::ConsolePrint(Tag, AmdDevices->count()? "AMD GPU device count GOOD" : "AMD GPU device count BAD!!!");
#endif
	}
#if WITH_NVIDIA
	// allerts
	_currentNvidiaSmiDriver=GetNvidiaSmiDriver();
	// if we have nvidia cards but no CUDA devices tell the user to upgrade driver
	bool isNvidiaErrorShown=false; // to prevent showing twice
	bool showWarning=ConfigManager.generalConfig->ShowDriverVersionWarning && WindowsDisplayAdapters.HasNvidiaVideoController();
	if (showWarning && _cudaDevices->count()!=nvidiaCount && _currentNvidiaSmiDriver->IsLesserVersionThan(NvidiaMinDetectionDriver)) {
		isNvidiaErrorShown=true;
		QString minDriver=NvidiaMinDetectionDriver->ToString();
		QString recomendDrvier=NvidiaRecomendedDriver->ToString();
		QMessageBox::critical(nullptr, International::GetText("Compute_Device_Query_Manager_NVIDIA_RecomendedDriver_Title"), International::GetText("Compute_Device_Query_Manager_NVIDIA_Driver_Detection").arg(minDriver).arg(recomendDrvier), QMessageBox::Ok);
		}
	// recomended driver
	if (showWarning && _currentNvidiaSmiDriver->IsLesserVersionThan(NvidiaRecomendedDriver) && !isNvidiaErrorShown && _currentNvidiaSmiDriver->LeftPart>-1) {
		QString recomendDrvier=NvidiaRecomendedDriver->ToString();
		QString nvdriverString= _currentNvidiaSmiDriver->LeftPart>-1
			? International::GetText("Compute_Device_Query_Manager_NVIDIA_Driver_Recomended_PART").arg(_currentNvidiaSmiDriver->ToString())
			: "";
		QMessageBox::warning(nullptr, International::GetText("Compute_Device_Query_Manager_NVIDIA_RecomendedDriver_Title"), International::GetText("Compute_Device_Query_Manager_NVIDIA_Driver_Recomended").arg(recomendDrvier).arg(nvdriverString).arg(recomendDrvier), QMessageBox::Ok);
		}
#endif

	// no devices found
	if (Avaliable.AllAvailableDevices->count()<=0) {
		QMessageBox::StandardButton result=QMessageBox::warning(nullptr, International::GetText("Compute_Device_Query_Manager_No_Devices_Title"), International::GetText("Compute_Device_Query_Manager_No_Devices"), QMessageBox::Ok|QMessageBox::Cancel);
		if (result==QMessageBox::Ok) {
			QDesktopServices::openUrl(QUrl(Links::NHQTM_NoDevHelp));
			}
		}

#if WITH_AMD
	// create AMD bus ordering for Claymore
	QList<ComputeDevice*>* amdDevices=new QList<ComputeDevice*>;
	foreach (ComputeDevice* a, *Avaliable.AllAvailableDevices) {
		if (a->DeviceType==Enums::DeviceType::AMD) {
			amdDevices->append(a);
			}
		}
	std::sort(amdDevices->begin(), amdDevices->end(), [](const ComputeDevice* a, const ComputeDevice* b) { return a->BusID()<b->BusID(); });
	for (int i=0; i<amdDevices->count(); i++) {
		amdDevices->at(i)->IDByBus=i;
		}
#endif
#if WITH_NVIDIA
	// create NV bus ordering for Claymore
	QList<ComputeDevice*>* nvDevices=new QList<ComputeDevice*>;
	foreach (ComputeDevice* a, *Avaliable.AllAvailableDevices) {
		if (a->DeviceType==Enums::DeviceType::NVIDIA) {
			nvDevices->append(a);
			}
		}
	std::sort(nvDevices->begin(), nvDevices->end(), [](const ComputeDevice* a, const ComputeDevice* b) { return a->BusID()<b->BusID(); }); //@todo smer ?
	for (int i=0; i<nvDevices->count(); i++) {
		nvDevices->at(i)->IDByBus=i;
		}
#endif

	// get GPUs RAM sum
	// bytes
	Avaliable.NvidiaRamSum=0;
	Avaliable.AmdRamSum=0;
	foreach (ComputeDevice* dev, *Avaliable.AllAvailableDevices) {
		if (dev->DeviceType==Enums::DeviceType::NVIDIA) {
#if WITH_NVIDIA
			Avaliable.NvidiaRamSum+=dev->GpuRam;
#endif
			}
		else if (dev->DeviceType==Enums::DeviceType::AMD) {
#if WITH_AMD
			Avaliable.AmdRamSum+=dev->GpuRam;
#endif
			}
		}
	// Make gpu ram needed not larger than 4GB per GPU
	double totalGpuRam=std::min((Avaliable.NvidiaRamSum+Avaliable.AmdRamSum)*0.6/1024, (double)Avaliable.AvailGpUs()*4*1024*1024);
	double totalSysRam=SystemSpecs.FreePhysicalMemory+SystemSpecs.FreeVirtualMemory;
	// check
	if (ConfigManager.generalConfig->ShowDriverVersionWarning && totalSysRam<totalGpuRam) {
		Helpers::ConsolePrint(Tag, "virtual memory size BAD");
		QMessageBox::warning(nullptr, International::GetText("Warning_with_Exclamation"), International::GetText("VirtualMemorySize_BAD"), QMessageBox::Ok);
		}
	else {
		Helpers::ConsolePrint(Tag, "virtual memory size GOOD");
		}

	// #x remove reference
	MessageNotifier_=nullptr;
}

QList<ComputeDeviceManager::Query::VideoControllerData*>* ComputeDeviceManager::Query::AvailableVideoControllers=new QList<VideoControllerData*>;
/*
QString ComputeDeviceManager::Query::WindowsDisplayAdapters::SafeGetProperty(QString key)
{
	return "key is null";
}
*/
void ComputeDeviceManager::Query::WindowsDisplayAdapters::QueryVideoControllers()
{
	QueryVideoControllers(AvailableVideoControllers, true);
}

void ComputeDeviceManager::Query::WindowsDisplayAdapters::QueryVideoControllers(QList<ComputeDeviceManager::Query::VideoControllerData*>* avaliableVideoControllers, bool warningsEnabled)
{
	QStringList stringBuilder({""});
	stringBuilder << "QueryVideoControllers: ";
#if _hwinfo_
// hwinfo/hd
	hd_data_t* hd_data;
	hd_t* hd;
//	unsigned display_idx;
	hd_data=(hd_data_t*)calloc(1, sizeof(*hd_data));
	hd=hd_list(hd_data, hw_display, 1, NULL);
	bool allVideoControllersOK=true;
	for (; hd; hd=hd->next) {
//		hd_dump_entry(hd_data, hd, stdout);
		VideoControllerData* vidController=new VideoControllerData;

		vidController->Name=QString(hd->model);
		vidController->PNPDeviceID=QString(hd->unique_id);
		vidController->Status= hd->status.invalid? "invalid" : "ok";
		vidController->SysFS=QString(hd->sysfs_id);
//		vidController->UDev=QString(hd->unix_dev_name);
		vidController->SysFS_BusID=QString(hd->sysfs_bus_id);
		vidController->modalias=QString(hd->modalias);

		stringBuilder << "\thwinfo hd_list(hw_display) detected:";
		stringBuilder << QString("\t\tName %1").arg(vidController->Name);
		stringBuilder << QString("\t\tDescription %1").arg(vidController->Description);
		stringBuilder << QString("\t\tPNPDeviceID %1").arg(vidController->PNPDeviceID);
		stringBuilder << QString("\t\tStatus %1").arg(vidController->Status);
		stringBuilder << QString("\t\tInfSection %1").arg(vidController->InfSection);
		stringBuilder << QString("\t\tAdapterRAM %1").arg(vidController->AdapterRAM);
		stringBuilder << QString("\t\tSysFS %1").arg(vidController->SysFS);
//		stringBuilder << QString("\t\tUDev %1").arg(vidController->UDev);
		stringBuilder << QString("\t\tSysFS_BusID %1").arg(vidController->SysFS_BusID);
		stringBuilder << QString("\t\tmodalias %1").arg(vidController->modalias);
//hd.detail.pci.data.bus
//hd.detail.pci.data.sysfs_id

		if (allVideoControllersOK && vidController->Status.toLower()!="ok") {
			allVideoControllersOK=false;
			}
		AvailableVideoControllers->append(vidController);
		}
//	display_idx=hd_display_adapter(hd_data);
//hd_dump_entry(hd_data, hd_get_device_by_idx(hd_data, display_idx), stdout);
	hd_free_hd_list(hd);
	hd_free_hd_data(hd_data);
	free(hd_data);
printf("hwinfo found cnt: %d\n", AvailableVideoControllers->count());
#endif
#if _udev_
//libudev
	struct udev *udev;
//	struct udev_enumerate *enumerate;
//	struct udev_list_entry *devices, *dev_list_entry;
	struct udev_device *dev;
	
	// Create the udev object
	udev=udev_new();
	if (!udev) {
		printf("Can't create udev\n");
		exit(1);
		}

	struct udev_list_entry *ple, *ale, *tle;
	const char *n;
//enumerate=udev_enumerate_new(udev);
//udev_enumerate_add_match_subsystem(enumerate, "pci");
//udev_enumerate_scan_devices(enumerate);
//devices=udev_enumerate_get_list_entry(enumerate);
	foreach (VideoControllerData* vcd, *AvailableVideoControllers) {
printf("from %s\n", vcd->SysFS.toStdString().c_str());
//		dev=udev_device_new_from_syspath(udev, vcd->SysFS.toStdString().c_str());
dev=udev_device_new_from_subsystem_sysname(udev, "pci", vcd->SysFS_BusID.toStdString().c_str());
		printf("Device Node Path: %s\n", udev_device_get_devnode(dev)); //NULL
		printf("Device devnum: %lu\n", udev_device_get_devnum(dev)); //0
		printf("Device devpath: %s\n", udev_device_get_devpath(dev)); //!
		printf("Device devtype: %s\n", udev_device_get_devtype(dev)); //NULL
		printf("Device isinit: %d\n", udev_device_get_is_initialized(dev)); //1
		printf("Device sysname: %s\n", udev_device_get_sysname(dev)); // 0000:01:00.0
		printf("Device sysnum: %s\n", udev_device_get_sysnum(dev)); //0
		printf("Device syspath: %s\n", udev_device_get_syspath(dev)); // /sys/devices/pci0000:00/0000:00:01.0/0000:01:00.0
		printf("Device driver: %s\n", udev_device_get_driver(dev)); //amdgpu
		printf("Device subsystem: %s\n", udev_device_get_subsystem(dev)); //pci
		printf("Device devnode: %s\n", udev_device_get_devnode(dev)); //NULL
		struct udev_list_entry *pl=udev_device_get_properties_list_entry(dev);
		struct udev_list_entry *al=udev_device_get_sysattr_list_entry(dev);
		struct udev_list_entry *tl=udev_device_get_tags_list_entry(dev);
		udev_list_entry_foreach (ple, pl) {
			n=udev_list_entry_get_name(ple);
			printf("p %s: %s\n", n, udev_device_get_property_value(dev, n));
			}
		udev_list_entry_foreach (ale, al) {
			n=udev_list_entry_get_name(ale);
			printf("a %s: %s\n", n, udev_device_get_sysattr_value(dev, n));
			}
		udev_list_entry_foreach (tle, tl) {
			printf("tlename: %s\n", udev_list_entry_get_name(tle));
			}
		}
#if false
	// Create a list of the devices in the 'hidraw' subsystem
	enumerate=udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "pci");
	udev_enumerate_scan_devices(enumerate);
	devices=udev_enumerate_get_list_entry(enumerate);
	/* For each item enumerated, print out its information. udev_list_entry_foreach is a macro which expands to a loop.
	 * The loop will be executed for each member in devices, setting dev_list_entry to a list entry which contains
	 * the device's path in /sys.
	 */
	udev_list_entry_foreach(dev_list_entry, devices) {
		const char *path;

		// Get the filename of the /sys entry for the device and create a udev_device object (dev) representing it
		path=udev_list_entry_get_name(dev_list_entry);
//printf((QString("path=udev_list_entry_get_name: ")+path+"\n").toStdString().c_str());
		dev=udev_device_new_from_syspath(udev, path);
if (AvailableVideoControllers->first()->SysFS!=QString(udev_device_get_devpath(dev))) {
	udev_device_unref(dev);
	continue;
	}
		const char *n;
		// usb_device_get_devnode() returns the path to the device node itself in /dev
		printf("Device Node Path: %s\n", udev_device_get_devnode(dev));
		printf("Device devnum: %lu\n", udev_device_get_devnum(dev));
		printf("Device devpath: %s\n", udev_device_get_devpath(dev)); //!
		printf("Device devtype: %s\n", udev_device_get_devtype(dev));
		printf("Device isinit: %d\n", udev_device_get_is_initialized(dev));
		printf("Device subsystem: %s\n", udev_device_get_subsystem(dev));
		printf("Device sysname: %s\n", udev_device_get_sysname(dev));
		printf("Device sysnum: %s\n", udev_device_get_sysnum(dev));
		printf("Device syspath: %s\n", udev_device_get_syspath(dev));
		struct udev_list_entry *pl=udev_device_get_properties_list_entry(dev);
		struct udev_list_entry *al=udev_device_get_sysattr_list_entry(dev);
		struct udev_list_entry *tl=udev_device_get_tags_list_entry(dev);
		udev_list_entry_foreach (ple, pl) {
			n=udev_list_entry_get_name(ple);
			printf("p %s: %s\n", n, udev_device_get_property_value(dev, n));
			}
		udev_list_entry_foreach (ale, al) {
			n=udev_list_entry_get_name(ale);
			printf("a %s: %s\n", n, udev_device_get_sysattr_value(dev, n));
			}
		udev_list_entry_foreach (tle, tl) {
			printf("tlename: %s\n", udev_list_entry_get_name(tle));
			}
#if 0
		/* The device pointed to by dev contains information about the hidraw device. In order to get information
		 * about the USB device, get the parent device with the subsystem/devtype pair of "usb"/"usb_device".
		 * This will be several levels up the tree, but the function will find it.
		 */
		dev=udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
		if (!dev) {
			printf("Unable to find parent usb device.");
			exit(1);
			}
#endif
		/* From here, we can call get_sysattr_value() for each file in the device's /sys entry. The strings passed
		 * into these functions (idProduct, idVendor, serial, etc.) correspond directly to the files in the directory
		 * which represents the USB device. Note that USB strings are Unicode, UCS2 encoded, but the strings returned
		 * from udev_device_get_sysattr_value() are UTF-8 encoded.
		 */
/*		printf("  VID/PID: %s %s\n",
		        udev_device_get_sysattr_value(dev,"idVendor"),
		        udev_device_get_sysattr_value(dev, "idProduct"));
		printf("  %s\n  %s\n",
		        udev_device_get_sysattr_value(dev,"manufacturer"),
		        udev_device_get_sysattr_value(dev,"product"));
		printf("  serial: %s\n",
		         udev_device_get_sysattr_value(dev, "serial"));*/
		udev_device_unref(dev);
		}
	/* Free the enumerator object */
	udev_enumerate_unref(enumerate);
#endif
	udev_unref(udev);
#endif
	Helpers::ConsolePrint(Tag, stringBuilder.join('\n'));
	if (warningsEnabled) {
		if (ConfigManager.generalConfig->ShowDriverVersionWarning && !allVideoControllersOK) {
			QString msg=International::GetText("QueryVideoControllers_NOT_ALL_OK_Msg");
			foreach (VideoControllerData* vc, *AvailableVideoControllers) {
				if (vc->Status.toLower()!="ok") {
					msg+="\n"+International::GetText("QueryVideoControllers_NOT_ALL_OK_Msg_Append").arg(vc->Name).arg(vc->Status).arg(vc->PNPDeviceID);
					}
				}
			QMessageBox::warning(nullptr, International::GetText("QueryVideoControllers_NOT_ALL_OK_Title"), msg, QMessageBox::Ok);
			}
		}
}

bool ComputeDeviceManager::Query::WindowsDisplayAdapters::HasNvidiaVideoController()
{
	foreach (VideoControllerData* vctrl, *AvailableVideoControllers)
	{
		if (vctrl->Name.toLower().contains("nvidia")) {
			return true;
			}
		}
	return false;
}

void ComputeDeviceManager::Query::Cpu::QueryCpus()
{
	Helpers::ConsolePrint(Tag, "QueryCpus START");
	// get all CPUs
	Avaliable.CpusCount=CPUID::GetPhysicalProcessorCount();
	Avaliable.IsHyperThreadingEnabled=CPUID::IsHyperThreadingEnabled();

	Helpers::ConsolePrint(Tag, Avaliable.IsHyperThreadingEnabled? "HyperThreadingEnabled = TRUE" : "HyperThreadingEnabled = FALSE");

	// get all cores (including virtual - HT can benefit mining)
	int threadsPerCpu=CPUID::GetVirtualCoresCount()/Avaliable.CpusCount;

	if (!Helpers::Is64BitOperatingSystem()) {
		QMessageBox::warning(nullptr, International::GetText("Warning_with_Exclamation"), International::GetText("Form_Main_msgbox_CPUMining64bitMsg"), QMessageBox::Ok);
		Avaliable.CpusCount=0;
		}

	if (threadsPerCpu*Avaliable.CpusCount>64) {
		QMessageBox::warning(nullptr, International::GetText("Warning_with_Exclamation"), International::GetText("Form_Main_msgbox_CPUMining64CoresMsg"), QMessageBox::Ok);
		Avaliable.CpusCount=0;
		}

	// TODO important move this to settings
	int threadsPerCpuMask=threadsPerCpu;
	Globals::ThreadsPerCpu=threadsPerCpu;

	if (CPUUtils::IsCpuMiningCapable()) {
		if (Avaliable.CpusCount==1) {
			Avaliable.AllAvailableDevices->append(new CpuComputeDevice(0, "CPU0", CPUID::GetCPUName().trimmed(), threadsPerCpu, 0, ++_cpuCount));
			}
		else if (Avaliable.CpusCount>1) {
			for (int i=0; i<Avaliable.CpusCount; i++) {
				Avaliable.AllAvailableDevices->append(new CpuComputeDevice(i, "CPU"+QString::number(i), CPUID::GetCPUName().trimmed(), threadsPerCpu, CPUID::CreateAffinityMask(i, threadsPerCpuMask), ++_cpuCount));
				}
			}
		}
	Helpers::ConsolePrint(Tag, "QueryCpus END");
}
#if WITH_NVIDIA
QList<CudaDevice*>* ComputeDeviceManager::Query::_cudaDevices=new QList<CudaDevice*>;

QString ComputeDeviceManager::Query::Nvidia::_queryCudaDevicesString="";
/*
void ComputeDeviceManager::Query::Nvidia::QueryCudaDevicesOutputErrorDataReceived(QString str)
{
	_queryCudaDevicesString+=str;
}
*/
bool ComputeDeviceManager::Query::Nvidia::IsSkipNvidia()
{
	return ConfigManager.generalConfig->DeviceDetection->DisableDetectionNVIDIA;
}

void ComputeDeviceManager::Query::Nvidia::QueryCudaDevices()
{
	Helpers::ConsolePrint(Tag, "QueryCudaDevices START");
	QueryCudaDevices(_cudaDevices);

	if (_cudaDevices!=nullptr && _cudaDevices->count()) {
		Avaliable.HasNvidia=true;
		QStringList stringBuilder({""});
		stringBuilder << "CudaDevicesDetection:";

		// Enumerate NVAPI handles and map to busid
//		QMap<int, NvPhysicalGpuHandle> idHandles;
		QMap<int, nvmlDevice_t*> idHandles;
#if 0
		if (NVAPI.IsAvailable) {
			NvPhysicalGpuHandle[]* handles=new NvPhysicalGpuHandle[NVAPI.MAX_PHYSICAL_GPUS];
			if (NVAPI.NvAPI_EnumPhysicalGPUs==nullptr) {
				Helpers::ConsolePrint("NVAPI", "NvAPI_EnumPhysicalGPUs unavailable");
				}
			else {
				status=NVAPI.NvAPI_EnumPhysicalGPUs(handles);
				if (status!=NvStatus.OK) {
					Helpers::ConsolePrint("NVAPI", "Enum physical GPUs failed with status: %1".arg(status));
					}
				else {
					foreach (NvPhysicalGpuHandle handle, handles) {
						idStatus=NVAPI.NvAPI_GPU_GetBusID(handle);
						if (idStatus!=NvStatus.OK) {
							Helpers::ConsolePrint("NVAPI", QString("Bus ID get failed with status: ").arg(idStatus));
							}
						else {
							Helpers::ConsolePrint("NVAPI", QString("Found handle for busid ").arg(id));
							idHandles[id]=handle;
							}
						}
					}
				}
			}
#endif
		foreach (CudaDevice* cudaDev, *_cudaDevices) {
			// check sm versions
			bool isUnderSM21;
			{
				bool isUnderSM2Major=cudaDev->SM_major<2;
				bool isUnderSM2Minor=cudaDev->SM_minor<1;
				isUnderSM21=isUnderSM2Major && isUnderSM2Minor;
			}
			bool skip=isUnderSM21;
			QString skipOrAdd= skip? "SKIPED" : "ADDED";
			const QString isDisabledGroupStr=""; // TODO remove
			QString etherumCapableStr=cudaDev->IsEtherumCapable()? "YES" : "NO";
			stringBuilder << "\t"+skipOrAdd+" device"+isDisabledGroupStr+":";
			stringBuilder << "\t\tID: "+QString::number(cudaDev->DeviceID);
			stringBuilder << "\t\tBusID: "+QString::number(cudaDev->pciBusID);
			stringBuilder << "\t\tNAME: "+cudaDev->GetName();
			stringBuilder << "\t\tVENDOR: "+cudaDev->VendorName;
			stringBuilder << "\t\tUUID: "+cudaDev->UUID;
			stringBuilder << "\t\tSM: "+cudaDev->SMVersionString;
			stringBuilder << "\t\tMEMORY: "+QString::number(cudaDev->DeviceGlobalMemory);
			stringBuilder << "\t\tETHEREUM: "+etherumCapableStr;

			if (!skip) {
				Enums::DeviceGroupType group;
				switch (cudaDev->SM_major) {
					case 2:
						group=Enums::DeviceGroupType::NVIDIA_2_1;
						break;
					case 3:
						group=Enums::DeviceGroupType::NVIDIA_3_x;
						break;
					case 5:
						group=Enums::DeviceGroupType::NVIDIA_5_x;
						break;
					case 6:
						group=Enums::DeviceGroupType::NVIDIA_6_x;
						break;
					default:
						group=Enums::DeviceGroupType::NVIDIA_6_x;
						break;
					}
//				NvPhysicalGpuHandle handle=idHandles.value(cudaDev->pciBusID);
				nvmlDevice_t* handle=idHandles.value(cudaDev->pciBusID);
				Avaliable.AllAvailableDevices->append(new CudaComputeDevice(cudaDev, group, ++_gpuCount, handle));
				}
			}
		Helpers::ConsolePrint(Tag, stringBuilder.join('\n'));
		}
	Helpers::ConsolePrint(Tag, "QueryCudaDevices END");
}

void ComputeDeviceManager::Query::Nvidia::QueryCudaDevices(QList<CudaDevice*>* cudaDevices)
{
	_queryCudaDevicesString="";

	QProcess* cudaDevicesDetection=new QProcess;
	cudaDevicesDetection->setProgram("./CudaDeviceDetection");
//	QMetaObject::connect(cudaDevicesDetection, SIGNAL(readyReadStandardError()), &ComputeDeviceManager::Query::Nvidia::QueryCudaDevicesOutputErrorDataReceived());
//	QMetaObject::connect(cudaDevicesDetection, SIGNAL(readyReadStandardOutput()), &ComputeDeviceManager::Query::Nvidia::QueryCudaDevicesOutputErrorDataReceived());

	const int waitTime=30*1000; // 30s
	try {
		cudaDevicesDetection->start();
		if (!cudaDevicesDetection->waitForStarted()) {
			Helpers::ConsolePrint(Tag, "CudaDevicesDetection process could not start");
			}
		else {
			if (cudaDevicesDetection->waitForFinished(waitTime)) {
				_queryCudaDevicesString=QString(cudaDevicesDetection->readAll());
				cudaDevicesDetection->close();
				}
			}
		}
	catch (QException ex) {
		// TODO
		Helpers::ConsolePrint(Tag, QString("CudaDevicesDetection threw Exception: ")+ex.what());
		}
//	finally {
		if (_queryCudaDevicesString!="") {
			try {
				QJsonParseError er;
				QJsonDocument jd=QJsonDocument::fromJson(_queryCudaDevicesString.toLatin1(), &er);
				if (er.error) {
					throw LException(er.errorString().toLatin1());
					}
				if (jd.isArray() && jd.array().count()) {
					CudaDevice* cudaDevice;
					foreach (QJsonValue av, jd.array()) {
						QJsonObject avo=av.toObject();
						cudaDevice=new CudaDevice;
						cudaDevice->DeviceID=avo.value("DeviceID").toInt();
						cudaDevice->pciBusID=avo.value("pciBusID").toInt();
						cudaDevice->VendorID=avo.value("VendorID").toInt();
						cudaDevice->VendorName=avo.value("VendorName").toString();
						cudaDevice->DeviceName=avo.value("DeviceName").toString();
						cudaDevice->SMVersionString=avo.value("SMVersionString").toString();
						cudaDevice->SM_major=avo.value("SM_major").toInt();
						cudaDevice->SM_minor=avo.value("SM_minor").toInt();
						cudaDevice->UUID=avo.value("UUID").toString();
						cudaDevice->DeviceGlobalMemory=avo.value("DeviceGlobalMemory").toDouble();
						cudaDevice->pciDeviceId=avo.value("pciDeviceId").toInt();
						cudaDevice->pciSubSystemId=avo.value("pciSubSystemId").toInt();
						cudaDevice->SMX=avo.value("SMX").toInt();
						cudaDevices->append(cudaDevice);
						}
					}
				}
			catch (...) { }

			if (_cudaDevices==nullptr || !_cudaDevices->count()) {
				Helpers::ConsolePrint(Tag, QString("CudaDevicesDetection found no devices. CudaDevicesDetection returned: ")+_queryCudaDevicesString);
				}
			}
//		}
}
#endif
QList<ComputeDeviceManager::Query::OpenCLJsonData_t>* ComputeDeviceManager::Query::_openCLJsonData=new QList<ComputeDeviceManager::Query::OpenCLJsonData_t>;
bool ComputeDeviceManager::Query::_isOpenCLQuerySuccess=false;

QString ComputeDeviceManager::Query::OpenCL::_queryOpenCLDevicesString="";

void ComputeDeviceManager::Query::OpenCL::QueryOpenCLDevicesOutputErrorDataReceived() // @todo
{
//	_queryOpenCLDevicesString+=readAll();
}

void ComputeDeviceManager::Query::OpenCL::QueryOpenCLDevices()
{
	Helpers::ConsolePrint(Tag, "QueryOpenCLDevices START");
	QProcess* OpenCLDevicesDetection=new QProcess;
	OpenCLDevicesDetection->setProgram("./AMDOpenCLDeviceDetection");
//	OpenCLDevicesDetection->setWorkingDirectory(qApp->applicationDirPath());

	const int waitTime=30*1000;
	try {
		OpenCLDevicesDetection->start();
		if (!OpenCLDevicesDetection->waitForStarted(waitTime)) {
			Helpers::ConsolePrint(Tag, "AMDOpenCLDeviceDetection process could not start: "+OpenCLDevicesDetection->errorString());
			}
		else {
			OpenCLDevicesDetection->waitForFinished(waitTime);
			_queryOpenCLDevicesString=QString(OpenCLDevicesDetection->readAll());
			}
		//finally{}
		if (_queryOpenCLDevicesString!="" && _queryOpenCLDevicesString.startsWith('[')) {
			QJsonArray arr=QJsonDocument::fromJson(_queryOpenCLDevicesString.toLatin1()).array();
			foreach (QJsonValue v, arr) {
				QJsonObject od=v.toObject();
				OpenCLJsonData_t d;
				d.PlatformName=od.value("PlatformName").toString();
				d.PlatformNum=od.value("PlatformNum").toInt();
				QList<OpenCLDevice*>* devs=new QList<OpenCLDevice*>;
				foreach (QJsonValue jDev, od.value("Devices").toArray()) {
					QJsonObject jDevO=jDev.toObject();
					OpenCLDevice* lDev=new OpenCLDevice;

					lDev->DeviceID=jDevO.value("DeviceID").toInt();
					lDev->_CL_DEVICE_NAME=jDevO.value("_CL_DEVICE_NAME").toString();
					lDev->_CL_DEVICE_TYPE=jDevO.value("_CL_DEVICE_TYPE").toString();
					lDev->_CL_DEVICE_GLOBAL_MEM_SIZE=jDevO.value("_CL_DEVICE_GLOBAL_MEM_SIZE").toDouble();
					lDev->_CL_DEVICE_VENDOR=jDevO.value("_CL_DEVICE_VENDOR").toString();
					lDev->_CL_DEVICE_VERSION=jDevO.value("_CL_DEVICE_VERSION").toString();
					lDev->_CL_DRIVER_VERSION=jDevO.value("_CL_DRIVER_VERSION").toString();
					lDev->AMD_BUS_ID=jDevO.value("AMD_BUS_ID").toInt();
					devs->append(lDev);
					}
				d.Devices=devs;
				_openCLJsonData->append(d);
				}
			}
		else {
			Helpers::ConsolePrint(Tag, "AMDOpenCLDeviceDetection found no devices. AMDOpenCLDeviceDetection returned: "+_queryOpenCLDevicesString);
			}
		}
	catch (QException ex) {
		Helpers::ConsolePrint(Tag, "AMDOpenCLDeviceDetection threw Exception: "+QString(ex.what()));
		}
	if (!_openCLJsonData->count()) {
		Helpers::ConsolePrint(Tag, "AMDOpenCLDeviceDetection found no devices. AMDOpenCLDeviceDetection returned: "+_queryOpenCLDevicesString);
		}
	else {
		_isOpenCLQuerySuccess=true;
		QStringList stringBuilder({""});
		stringBuilder << "AMDOpenCLDeviceDetection found devices success:";
		foreach (OpenCLJsonData_t oclElem, *_openCLJsonData) {
			stringBuilder << "\tFound devices for platform: "+oclElem.PlatformName;
			foreach (OpenCLDevice* oclDev, *oclElem.Devices) {
				stringBuilder << "\t\tDevice:";
				stringBuilder << "\t\t\tDevice ID "+QString::number(oclDev->DeviceID);
				stringBuilder << "\t\t\tDevice NAME "+oclDev->_CL_DEVICE_NAME;
				stringBuilder << "\t\t\tDevice TYPE "+oclDev->_CL_DEVICE_TYPE;
				}
			}
		Helpers::ConsolePrint(Tag, stringBuilder.join('\n'));
		}
	Helpers::ConsolePrint(Tag, "QueryOpenCLDevices END");
}

QList<OpenCLDevice*>* ComputeDeviceManager::Query::AmdDevices=new QList<OpenCLDevice*>;
#if WITH_AMD
void ComputeDeviceManager::Query::Amd::QueryAmd()
{
	const int amdVendorID=1002;
	Helpers::ConsolePrint(Tag, "QueryAMD START");

	QMap<QString, bool>* deviceDriverOld=new QMap<QString, bool>;
	QMap<QString, bool>* deviceDriverNoNeoscryptLyra2RE=new QMap<QString, bool>;
	bool showWarningDialog=false;

	foreach (VideoControllerData* vidContrllr, *AvailableVideoControllers) {
		Helpers::ConsolePrint(Tag, "Checking AMD device (driver): "+vidContrllr->Name+" ("+vidContrllr->DriverVersion+")");

		deviceDriverOld->insert(vidContrllr->Name, false);
		deviceDriverNoNeoscryptLyra2RE->insert(vidContrllr->Name, false);
		QVersionNumber sgminerNoNeoscryptLyra2RE=QVersionNumber::fromString("21.19.164.1");
		if ((vidContrllr->Name.contains("AMD") || vidContrllr->Name.contains("Radeon")) && showWarningDialog==false) {
			QVersionNumber amdDriverVersion=QVersionNumber::fromString(vidContrllr->DriverVersion);

			if (!ConfigManager.generalConfig->ForceSkipAMDNeoscryptLyraCheck) {
				bool greaterOrEqual=QVersionNumber::compare(amdDriverVersion, sgminerNoNeoscryptLyra2RE)>=0;
				if (greaterOrEqual) {
					deviceDriverNoNeoscryptLyra2RE->insert(vidContrllr->Name, true);
					Helpers::ConsolePrint(Tag, "Driver version seems to be "+sgminerNoNeoscryptLyra2RE.toString()+" or higher. NeoScrypt and Lyra2REv2 will be removed from list");
					}
				}

			if (amdDriverVersion.majorVersion()<15) {
				showWarningDialog=true;
				deviceDriverOld->insert(vidContrllr->Name, true);
				Helpers::ConsolePrint(Tag, "WARNING!!! Old AMD GPU driver detected! All optimized versions disabled, mining speed will not be optimal. Consider upgrading AMD GPU driver. Recommended AMD GPU driver version is 15.7.1.");
				}
			}
		}
	if (ConfigManager.generalConfig->ShowDriverVersionWarning && showWarningDialog) {
		QDialog* warningDialog=new DriverVersionConfirmationDialog;
		warningDialog->exec();
		delete warningDialog;
		}

	// get platform version
	ShowMessageAndStep(International::GetText("Compute_Device_Query_Manager_AMD_Query"));
	QList<OpenCLDevice*>* amdOclDevices=new QList<OpenCLDevice*>;
	if (_isOpenCLQuerySuccess) {
		bool amdPlatformNumFound=false;
		foreach (OpenCLJsonData_t oclEl, *_openCLJsonData) {
			if (!oclEl.PlatformName.contains("AMD") && !oclEl.PlatformName.contains("amd")) {
				continue;
				}
			amdPlatformNumFound=true;
			QString amdOpenCLPlatformStringKey=oclEl.PlatformName;
			Avaliable.AmdOpenCLPlatformNum=oclEl.PlatformNum;
			amdOclDevices=oclEl.Devices;
			Helpers::ConsolePrint(Tag, "AMD platform found: Key: "+amdOpenCLPlatformStringKey+", Num: "+QString::number(Avaliable.AmdOpenCLPlatformNum));
			break;
			}
		if (amdPlatformNumFound) {
			{ // get only AMD gpus
				foreach (OpenCLDevice* oclDev, *amdOclDevices) {
					if (oclDev->_CL_DEVICE_TYPE.contains("GPU")) {
						AmdDevices->append(oclDev);
						}
					}
			}

			if (!AmdDevices->count()) {
				Helpers::ConsolePrint(Tag, "AMD GPUs count is 0");
				}
			else {
				Helpers::ConsolePrint(Tag, QString("AMD GPUs count: %1").arg(AmdDevices->count()));
#if ADL_FOUND
				Helpers::ConsolePrint(Tag, "AMD Getting device name and serial from ADL");
				// ADL
				bool isAdlInit=true;
				// ADL does not get devices in order map devices by bus number
				// bus id, <name, uuid>
				QMap<int, std::tuple<QString, QString, QString, int>*>* busIDsInfo=new QMap<int, std::tuple<QString, QString, QString, int>*>;
				QStringList* amdDeviceName=new QStringList;
				QStringList* amdDeviceUuid=new QStringList;
				try {
					if (!ADL::Init()) {
						throw LException("ADL::Init error");
						}
					int adlRet=-1;
					int numberOfAdapters=0;
					adlRet=ADL::ADL_Main_Control_Create(ADL_Main_Memory_Alloc, 1);
					if (adlRet==ADL::ADL_SUCCESS) {
						ADL::ADL_Adapter_NumberOfAdapters_Get(&numberOfAdapters);
						Helpers::ConsolePrint(Tag, "Number Of Adapters: "+QString::number(numberOfAdapters));

						if (numberOfAdapters>0) {
							// Get OS adpater info from ADL
//							ADLAdapterInfoArray* osAdapterInfoData=new ADLAdapterInfoArray;

							int size=sizeof (AdapterInfo)*numberOfAdapters;
							LPAdapterInfo adapterBuffer=(LPAdapterInfo)malloc(size);
							memset(adapterBuffer, '\0', size);

							adlRet=ADL::ADL_Adapter_AdapterInfo_Get(adapterBuffer, size);
							if (adlRet==ADL::ADL_SUCCESS) {
								int isActive=0;

								for (int i=0; i<numberOfAdapters; i++) {
									// Check if the adapter is active
									AdapterInfo adapterInfo=adapterBuffer[i];
									adlRet=ADL::ADL_Adapter_Active_Get(adapterInfo.iAdapterIndex, &isActive);

									if (adlRet==ADL::ADL_SUCCESS) {
										int vendorID=adapterInfo.iVendorID;
										QString devName=adapterInfo.strAdapterName;
										if (vendorID==amdVendorID
											||devName.toLower().contains("amd")
											||devName.toLower().contains("radeon")
											||devName.toLower().contains("firepro")
											) {
											QString pnpStr=""; // adapterInfo.strPNPString;
											QString infSection="";
											foreach (VideoControllerData* vCtrl, *AvailableVideoControllers) {
												if (vCtrl->PNPDeviceID==pnpStr) {
													infSection=vCtrl->InfSection;
													}
												}
											int backSlashLast=pnpStr.lastIndexOf('\\');
											QString serial=pnpStr.mid(backSlashLast, pnpStr.length()-backSlashLast);
											int end0=serial.indexOf('&');
											int end1=serial.indexOf('&', end0+1);
											// get serial
											serial=serial.mid(end0+1, (end1-end0)-1);

											QString udid=adapterInfo.strUDID;
											int pciVenIDStrSize=21; // PCI_VEN_XXXX&DEV_XXXX
											QString uuid=udid.left(pciVenIDStrSize)+"_"+serial;
											int budId=adapterInfo.iBusNumber;
											int index=adapterInfo.iAdapterIndex;
											if (!amdDeviceUuid->contains(uuid)) {
//												try {
													Helpers::ConsolePrint(Tag, "ADL device added BusNumber:"+QString::number(budId)+"  NAME:"+devName+"  UUID"+uuid);
//													}
//												catch (...) {
//													}
												amdDeviceUuid->append(uuid);
												amdDeviceName->append(devName);
												if (!busIDsInfo->contains(budId)) {
													busIDsInfo->insert(budId, new std::tuple<QString, QString, QString, int>(devName, uuid, infSection, index));
													}
												}
											}
										}
									}
								}
							else {
								Helpers::ConsolePrint(Tag, "ADL_AdapterInfo_Get() returned error code "+QString::number(adlRet));
								isAdlInit=false;
								}
							// Release the memory for the AdapterInfo structure
							if (adapterBuffer!=nullptr) {
								free(adapterBuffer);
								}
							}
						if (numberOfAdapters<=0) {
							ADL::ADL_Main_Control_Destroy(); // Close ADL if it found no AMD devices
							}
						}
					else {
						// TODO
						Helpers::ConsolePrint(Tag, "ADL_Main_Control_Create() returned error code "+QString::number(adlRet));
						Helpers::ConsolePrint(Tag, "Check if ADL is properly installed!");
						isAdlInit=false;
						}
					}
				catch (LException ex) {
					Helpers::ConsolePrint(Tag, QString("AMD ADL exception: ")+ex.what());
					isAdlInit=false;
					}

				bool isBusIDOk=true;
				{ // check if buss ids are unique and different from -1
					QSet<int> busIDs;
					// Override AMD bus IDs
					QStringList overrides=ConfigManager.generalConfig->OverrideAMDBusIds.split(',');
					for (int i=0; i<AmdDevices->count(); i++) {
						OpenCLDevice* amdOclDev=AmdDevices->at(i);
						bool ok;
						if (overrides.count()>i) {
							int overrideBus=overrides[i].toInt(&ok);
							if (ok && overrideBus>=0) {
								amdOclDev->AMD_BUS_ID=overrideBus;
								}
							}
						if (amdOclDev->AMD_BUS_ID<0 || !busIDsInfo->contains(amdOclDev->AMD_BUS_ID)) {
							isBusIDOk=false;
							break;
							}
						busIDs.insert(amdOclDev->AMD_BUS_ID);
						}
					// check if unique
					isBusIDOk= isBusIDOk && busIDs.count()==AmdDevices->count();
				}
				// print BUS id status
				Helpers::ConsolePrint(Tag, isBusIDOk? "AMD Bus IDs are unique and valid. OK" : "AMD Bus IDs IS INVALID. Using fallback AMD detection mode");

				// AMD device creation (in NHM context)
				if (isAdlInit && isBusIDOk) {
					Helpers::ConsolePrint(Tag, "Using AMD device creation DEFAULT Reliable mappings");
					Helpers::ConsolePrint(Tag, AmdDevices->count()==amdDeviceUuid->count()? "AMD OpenCL and ADL AMD query COUNTS GOOD/SAME" : "AMD OpenCL and ADL AMD query COUNTS DIFFERENT/BAD");
					QStringList stringBuilder({""});
					stringBuilder << "QueryAMD [DEFAULT query] devices: ";
					foreach (OpenCLDevice* dev, *AmdDevices) {
						Avaliable.HasAmd=true;

						int busID=dev->AMD_BUS_ID;
						if (busID!=-1 && busIDsInfo->contains(busID)) {
							QString deviceName=std::get<0>(*busIDsInfo->value(busID));
							AmdGpuDevice* newAmdDev=new AmdGpuDevice(dev, deviceDriverOld->value(deviceName), std::get<2>(*busIDsInfo->value(busID)), deviceDriverNoNeoscryptLyra2RE->value(deviceName));
							newAmdDev->DeviceName=deviceName;
							newAmdDev->UUID=std::get<1>(*busIDsInfo->value(busID));
							newAmdDev->AdapterIndex=std::get<3>(*busIDsInfo->value(busID));
							bool isDisabledGroup=ConfigManager.generalConfig->DeviceDetection->DisableDetectionAMD;

							Avaliable.AllAvailableDevices->append(new AmdComputeDevice(newAmdDev, ++_gpuCount, false));
//							try {
								stringBuilder << QString("\t%1 device%2:").arg(isDisabledGroup? "SKIPED" : "ADDED").arg(isDisabledGroup? " (AMD group disabled)" : "");
								stringBuilder << "\t\tID: "+QString::number(newAmdDev->DeviceID());
								stringBuilder << "\t\tNAME: "+newAmdDev->DeviceName;
								stringBuilder << "\t\tCODE_NAME: "+newAmdDev->Codename();
								stringBuilder << "\t\tUUID: "+newAmdDev->UUID;
								stringBuilder << "\t\tMEMORY: "+QString::number(newAmdDev->DeviceGlobalMemory());
								stringBuilder << QString("\t\tETHEREUM: ")+(newAmdDev->IsEtherumCapable()? "YES" : "NO");
//								}
//							catch (...) {}
							}
						else {
							stringBuilder+="\tDevice not added, Bus No. "+QString::number(busID)+" not found:\n";
							}
						}
					Helpers::ConsolePrint(Tag, stringBuilder.join('\n'));
					}
				else {
#endif
					Helpers::ConsolePrint(Tag, "Using AMD device creation FALLBACK UnReliable mappings");
					QStringList stringBuilder({""});
					stringBuilder << "QueryAMD [FALLBACK query] devices: ";

					// get video AMD controllers and sort them by RAM
					// (find a way to get PCI BUS Numbers from PNPDeviceID)
					QList<VideoControllerData*>* amdVideoControllers=new QList<VideoControllerData*>;
					foreach (VideoControllerData* vcd, *AvailableVideoControllers) {
						if (vcd->Name.toLower().contains("amd")
							|| vcd->Name.toLower().contains("radeon")
							|| vcd->Name.toLower().contains("firepro")
							) {
							amdVideoControllers->append(vcd);
							}
						}
					// sort by ram not ideal
					std::sort(amdVideoControllers->begin(), amdVideoControllers->end(), [](const VideoControllerData* a, const VideoControllerData* b) { return a->AdapterRAM<b->AdapterRAM; });
					std::sort(AmdDevices->begin(), AmdDevices->end(), [](const OpenCLDevice* a, const OpenCLDevice* b) { return a->_CL_DEVICE_GLOBAL_MEM_SIZE<b->_CL_DEVICE_GLOBAL_MEM_SIZE; });
					int minCount=std::min(amdVideoControllers->count(), AmdDevices->count());

					for (int i=0; i<minCount; ++i) {
						Avaliable.HasAmd=true;

						QString deviceName=amdVideoControllers->at(i)->Name;
						AmdGpuDevice* newAmdDev=new AmdGpuDevice(AmdDevices->at(i), deviceDriverOld->value(deviceName), amdVideoControllers->at(i)->InfSection, deviceDriverNoNeoscryptLyra2RE->value(deviceName));
						newAmdDev->DeviceName=deviceName;
						newAmdDev->UUID="UNUSED";
						bool isDisabledGroup=ConfigManager.generalConfig->DeviceDetection->DisableDetectionAMD;

						Avaliable.AllAvailableDevices->append(new AmdComputeDevice(newAmdDev, ++_gpuCount, true));
//						try {
							stringBuilder << QString("\t%1 device%2:").arg(isDisabledGroup? "SKIPED" : "ADDED").arg(isDisabledGroup? " (AMD group disabled)" : "");
							stringBuilder << "\t\tID: "+QString::number(newAmdDev->DeviceID());
							stringBuilder << "\t\tNAME: "+newAmdDev->DeviceName;
							stringBuilder << "\t\tCODE_NAME: "+newAmdDev->Codename();
							stringBuilder << "\t\tUUID: "+newAmdDev->UUID;
							stringBuilder << "\t\tMEMORY: "+QString::number(newAmdDev->DeviceGlobalMemory());
							stringBuilder << QString("\t\tETHEREUM: ")+(newAmdDev->IsEtherumCapable()? "YES" : "NO");
//							}
//						catch (...) {}
						}
					Helpers::ConsolePrint(Tag, stringBuilder.join('\n'));
#if ADL_FOUND
					}
#endif
				}
			}
		}
	Helpers::ConsolePrint(Tag, "QueryAMD END");
}
#endif
uint64_t ComputeDeviceManager::SystemSpecs::FreePhysicalMemory=0;
uint64_t ComputeDeviceManager::SystemSpecs::FreeSpaceInPagingFiles=0;
uint64_t ComputeDeviceManager::SystemSpecs::FreeVirtualMemory=0;
uint32_t ComputeDeviceManager::SystemSpecs::LargeSystemCache=0;
uint32_t ComputeDeviceManager::SystemSpecs::MaxNumberOfProcesses=0;
uint64_t ComputeDeviceManager::SystemSpecs::MaxProcessMemorySize=0;

uint32_t ComputeDeviceManager::SystemSpecs::NumberOfLicensedUsers=0;
uint32_t ComputeDeviceManager::SystemSpecs::NumberOfProcesses=0;
uint32_t ComputeDeviceManager::SystemSpecs::NumberOfUsers=0;
uint32_t ComputeDeviceManager::SystemSpecs::OperatingSystemSKU=0;

uint64_t ComputeDeviceManager::SystemSpecs::SizeStoredInPagingFiles=0;

uint32_t ComputeDeviceManager::SystemSpecs::SuiteMask=0;

uint64_t ComputeDeviceManager::SystemSpecs::TotalSwapSpaceSize=0;
uint64_t ComputeDeviceManager::SystemSpecs::TotalVirtualMemorySize=0;
uint64_t ComputeDeviceManager::SystemSpecs::TotalVisibleMemorySize=0;

void ComputeDeviceManager::SystemSpecs::QueryAndLog() // @todo
{
	struct sysinfo sysInfo;
	if (!sysinfo(&sysInfo)) {
		uint32_t uB=sysInfo.mem_unit;
		FreePhysicalMemory=sysInfo.freeram*uB;
//		FreeSpaceInPagingFiles=sysInfo.freeswap*uB;
//		FreeVirtualMemory=sysInfo.freehigh*uB;
//		LargeSystemCache
		MaxNumberOfProcesses=boost::integer_traits<pid_t>::const_max;
//		MaxProcessMemorySize
//		NumberOfLicensedUsers
		NumberOfProcesses=sysInfo.procs;
//		NumberOfUsers
//		OperatingSystemSKU
//		SizeStoredInPagingFiles
//		SuiteMask
		TotalSwapSpaceSize=sysInfo.totalswap*uB;
		TotalVirtualMemorySize=(sysInfo.totalram+sysInfo.totalswap)*uB;
//		TotalVisibleMemorySize=sysInfo.totalram*uB;
		}
	struct rlimit rLimit;
	if (!getrlimit(RLIMIT_AS, &rLimit)) {
		MaxProcessMemorySize=rLimit.rlim_max;
		}
	Helpers::ConsolePrint("SystemSpecs", "FreePhysicalMemory = "+QString::number(FreePhysicalMemory));
//	Helpers::ConsolePrint("SystemSpecs", "FreeSpaceInPagingFiles = "+QString::number(FreeSpaceInPagingFiles));
//	Helpers::ConsolePrint("SystemSpecs", "FreeVirtualMemory = "+QString::number(FreeVirtualMemory));
//	Helpers::ConsolePrint("SystemSpecs", "LargeSystemCache = "+QString::number(LargeSystemCache));
	Helpers::ConsolePrint("SystemSpecs", "MaxNumberOfProcesses = "+QString::number(MaxNumberOfProcesses));
	Helpers::ConsolePrint("SystemSpecs", "MaxProcessMemorySize = "+QString::number(MaxProcessMemorySize));
//	Helpers::ConsolePrint("SystemSpecs", "NumberOfLicensedUsers = "+QString::number(NumberOfLicensedUsers));
	Helpers::ConsolePrint("SystemSpecs", "NumberOfProcesses = "+QString::number(NumberOfProcesses));
//	Helpers::ConsolePrint("SystemSpecs", "NumberOfUsers = "+QString::number(NumberOfUsers));
//	Helpers::ConsolePrint("SystemSpecs", "OperatingSystemSKU = "+QString::number(OperatingSystemSKU));
//	Helpers::ConsolePrint("SystemSpecs", "SizeStoredInPagingFiles = "+QString::number(SizeStoredInPagingFiles));
//	Helpers::ConsolePrint("SystemSpecs", "SuiteMask = "+QString::number(SuiteMask));
	Helpers::ConsolePrint("SystemSpecs", "TotalSwapSpaceSize = "+QString::number(TotalSwapSpaceSize));
	Helpers::ConsolePrint("SystemSpecs", "TotalVirtualMemorySize = "+QString::number(TotalVirtualMemorySize));
//	Helpers::ConsolePrint("SystemSpecs", "TotalVisibleMemorySize = "+QString::number(TotalVisibleMemorySize));
}

//Available
bool ComputeDeviceManager::Available::HasNvidia=false;
bool ComputeDeviceManager::Available::HasAmd=false;
bool ComputeDeviceManager::Available::HasCpu=false;
int ComputeDeviceManager::Available::CpusCount=0;

int ComputeDeviceManager::Available::AvailCpus()
{
	int cnt=0;
	foreach (ComputeDevice* d, *AllAvailableDevices) {
		if (d->DeviceType==Enums::DeviceType::CPU) {
			cnt++;
			}
		}
	return cnt;
}

int ComputeDeviceManager::Available::AvailNVGpus()
{
	int cnt=0;
	foreach (ComputeDevice* d, *AllAvailableDevices) {
		if (d->DeviceType==Enums::DeviceType::NVIDIA) {
			cnt++;
			}
		}
	return cnt;
}

int ComputeDeviceManager::Available::AvailAmdGpus()
{
	int cnt=0;
	foreach (ComputeDevice* d, *AllAvailableDevices) {
		if (d->DeviceType==Enums::DeviceType::AMD) {
			cnt++;
			}
		}
	return cnt;
}

//int ComputeDeviceManager::Available::GPUsCount=0;
int ComputeDeviceManager::Available::AmdOpenCLPlatformNum=-1;
bool ComputeDeviceManager::Available::IsHyperThreadingEnabled=false;

ulong ComputeDeviceManager::Available::NvidiaRamSum=0;
ulong ComputeDeviceManager::Available::AmdRamSum=0;

QList<ComputeDevice*>* ComputeDeviceManager::Available::AllAvailableDevices=new QList<ComputeDevice*>;

ComputeDevice* ComputeDeviceManager::Available::GetDeviceWithUuid(QString uuid)
{
	foreach (ComputeDevice* dev, *ComputeDeviceManager::Avaliable.AllAvailableDevices) {
		if (uuid==dev->Uuid()) {
			return dev;
			}
		}
	return nullptr;
}

QList<ComputeDevice*>* ComputeDeviceManager::Available::GetSameDevicesTypeAsDeviceWithUuid(QString uuid)
{
	QList<ComputeDevice*>* sameTypes=new QList<ComputeDevice*>;
	ComputeDevice* compareDev=GetDeviceWithUuid(uuid);
	foreach (ComputeDevice* dev, *AllAvailableDevices) {
		if (uuid!=dev->Uuid() && compareDev->DeviceType==dev->DeviceType) {
			sameTypes->append(GetDeviceWithUuid(dev->Uuid()));
			}
		}
	return sameTypes;
}

ComputeDevice* ComputeDeviceManager::Available::GetCurrentlySelectedComputeDevice(int index, bool unique)
{
	return AllAvailableDevices->value(index);
}

int ComputeDeviceManager::Available::GetCountForType(Enums::DeviceType type)
{
	int count=0;
	foreach (ComputeDevice* device, *AllAvailableDevices) {
		if (device->DeviceType==type) {
			++count;
			}
		}
	return count;
}


void ComputeDeviceManager::Group::DisableCpuGroup()
{
	foreach (ComputeDevice* device, *Available::AllAvailableDevices) {
		if (device->DeviceType==Enums::DeviceType::CPU) {
			device->Enabled=false;
			}
		}
}

bool ComputeDeviceManager::Group::ContainsAmdGpus()
{
	foreach (ComputeDevice* device, *Available::AllAvailableDevices) {
		if (device->DeviceType==Enums::DeviceType::AMD) {
			return true;
			}
		}
	return false;
}

bool ComputeDeviceManager::Group::ContainsGpus()
{
	foreach (ComputeDevice* device, *Available::AllAvailableDevices) {
		if (device->DeviceType==Enums::DeviceType::NVIDIA || device->DeviceType==Enums::DeviceType::AMD) {
			return true;
			}
		}
	return false;
}

void ComputeDeviceManager::Group::UncheckedCpu()
{
	// Auto uncheck CPU if any GPU is found
	if (ContainsGpus()) {
		DisableCpuGroup();
		}
}
