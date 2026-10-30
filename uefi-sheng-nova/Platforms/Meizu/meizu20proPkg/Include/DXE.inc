  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

!if $(MEIZU_VENDOR_HW) == 1
  INF Binaries/meizu20pro/QcomPkg/Drivers/EnvDxe/EnvDxe.inf
!endif

  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf

!if $(MEIZU_VENDOR_HW) == 1
  INF Binaries/meizu20pro/QcomPkg/Drivers/ShmBridgeDxe/ShmBridgeDxeLA.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/TzDxe/ScmDxeCompat.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/TzDxe/TzDxeLA.inf
!endif

  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf

!if $(MEIZU_VENDOR_HW) == 1
  INF Binaries/meizu20pro/QcomPkg/Drivers/ChipInfoDxe/ChipInfoDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/IPCCDxe/IPCCDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/GLinkDxe/GLinkDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/PlatformInfoDxe/PlatformInfoDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/DALSYSDxe/DALSYSDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/HALIOMMUDxe/HALIOMMUDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/HWIODxe/HWIODxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/I2CDxe/I2CDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/TLMMDxe/TLMMDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/PmicDxe/PmicDxeLa.inf
!endif

  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF meizu20proPkg/Drivers/MeizuBootMemoryDxe/MeizuBootMemoryDxe.inf
  # The XNU loader is part of the firmware: a driver loaded from a volume is
  # refused by the image verification of this build.
  INF DarwinBootPkg/Driver/DarwinBoot.inf
  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  INF FatPkg/EnhancedFatDxe/Fat.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf

!if $(MEIZU_VENDOR_HW) == 1
  INF Binaries/meizu20pro/QcomPkg/Drivers/UFSDxe/UFSDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/CmdDbDxe/CmdDbDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/PwrUtilsDxe/PwrUtilsDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/RpmhDxe/RpmhDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/NpaDxe/NpaDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/ULogDxe/ULogDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/VcsDxe/VcsDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/ClockDxe/ClockDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/SPMIDxe/SPMIDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/ICBDxe/ICBDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/SmemDxe/SmemDxe.inf
!endif
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

!if $(MEIZU_VENDOR_HW) == 1
  INF Binaries/meizu20pro/QcomPkg/Drivers/RscDxe/RscDxe.inf
!endif

  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF meizu20proPkg/Drivers/MeizuKeyInputDxe/MeizuKeyInputDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf

!if $(MEIZU_VENDOR_HW) == 1
  INF Binaries/meizu20pro/QcomPkg/Drivers/GpiDxe/GpiDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/SPIDxe/SPIDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/SdccDxe/SdccDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/FeatureEnablerDxe/FeatureEnablerDxe.inf

  # USE_CUSTOM_DISPLAY_DRIVER = 1: this phone's own DisplayDxe, unchanged,
  # with the panel XMLs from its uefi partition (RAW.inc) and empty protocols
  # for the two DEPEX GUIDs nothing in the image installs. Otherwise
  # SimpleFbDxe publishes a GOP on the frame buffer the previous stage left
  # scanning out.
!endif
!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
  INF meizu20proPkg/Drivers/MeizuGopLogDxe/MeizuGopLogDxe.inf
  INF meizu20proPkg/Drivers/MeizuDisplayDepexDxe/MeizuDisplayDepexDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/DisplayDxe/DisplayDxe.inf
!else
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf
!endif

!if $(MEIZU_VENDOR_HW) == 1
  INF Binaries/meizu20pro/QcomPkg/Drivers/ButtonsDxe/ButtonsDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/ChargerExDxe/ChargerExDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/PmicGlinkDxe/PmicGlinkDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/QcomChargerDxe/QcomChargerDxeLA.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/UsbPwrCtrlDxe/UsbPwrCtrlDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/TsensDxe/TsensDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/LimitsDxe/LimitsDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/DDRInfoDxe/DDRInfoDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/XhciPciEmulationDxe/XhciPciEmulationDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/XhciDxe/XhciDxe.inf
!endif

  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf

!if $(MEIZU_VENDOR_HW) == 1
  INF Binaries/meizu20pro/QcomPkg/Drivers/UsbMsdDxe/UsbMsdDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/UsbDeviceDxe/UsbDeviceDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/UsbConfigDxe/UsbConfigDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/UsbInitDxe/UsbInitDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/UCDxe/UCDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/RNGDxe/RngDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/PILDxe/PILDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/PILProxyDxe/PILProxyDxe.inf
  INF Binaries/meizu20pro/QcomPkg/Drivers/SPSSDxe/SPSSDxe.inf
!endif
