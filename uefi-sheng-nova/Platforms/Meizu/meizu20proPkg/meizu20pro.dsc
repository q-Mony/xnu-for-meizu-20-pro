##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = meizu20pro
  PLATFORM_GUID                  = 7C1F2E44-5A3B-4C9D-8E21-9B6F0D4A1C37
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/meizu20proPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = meizu20proPkg/meizu20pro.fdf
!ifndef USE_CUSTOM_DISPLAY_DRIVER
  USE_CUSTOM_DISPLAY_DRIVER      = 0
!endif

  #
  # 0 = Mu drivers only. The previous stage's display keeps scanning out, so
  #     the boot manager is visible without any vendor hardware driver.
  # 1 = also load the stock vendor hardware drivers (SMEM, clocks, RPMh, UFS,
  #     USB, charger, ...).
  #
  # Defaults only: a -D on the build command line overrides them, which is how
  # the board builds both variants from one DSC.
  #
!ifndef MEIZU_VENDOR_HW
  MEIZU_VENDOR_HW                = 0
!endif

  #
  # 0 = SM8550-AB
  # 1 = SM8550-AC
  #
  SOC_TYPE                       = 0

!include KailuaPkg/KailuaPkg.dsc.inc

!if ($(USE_CUSTOM_DISPLAY_DRIVER) == 1) && ($(MEIZU_VENDOR_HW) == 0)
  !error The stock DisplayDxe chain needs MEIZU_VENDOR_HW = 1
!endif

[PcdsFixedAtBuild]
  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xA760D000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Meizu"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"meizu20pro"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"meizu20pro"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"meizu20pro"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"meizu20pro"

  #
  # Simple Frame Buffer
  #
  # The panel is qcom,mdss_dsi_meizu_amoled_samsung_cmd (1080x2400, from the
  # phone's bootargs and its own device tree). ABL's scan-out stride is
  # assumed equal to the width; unverified until the first boot.
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2400
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferFontScale|2
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferDelay|30000

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"
!if $(MEIZU_VENDOR_HW) == 0
  gQcomPkgTokenSpaceGuid.PcdBuildXblHobs|FALSE
!endif

[LibraryClasses]
  #
  # Memory Libraries
  #
  MemoryMapLib|meizu20proPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # QCOM Libraries
  #
  ConfigurationMapLib|meizu20proPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

[Components]
  #
  # ACPI Tables
  #
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
  meizu20proPkg/Drivers/MeizuDisplayDepexDxe/MeizuDisplayDepexDxe.inf
  meizu20proPkg/Drivers/MeizuGopLogDxe/MeizuGopLogDxe.inf
  meizu20proPkg/Drivers/MeizuBootMemoryDxe/MeizuBootMemoryDxe.inf
  meizu20proPkg/Drivers/MeizuKeyInputDxe/MeizuKeyInputDxe.inf
  DarwinBootPkg/Driver/DarwinBoot.inf
  meizu20proPkg/Tools/BootAndroid/BootAndroid.inf

[LibraryClasses]
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf
