// Board raw files must come from the Meizu 20 Pro firmware, not QRD8550.
  FILE FREEFORM = A1E235DE-E825-4591-9623-C43175811826 {
    SECTION UI = "SecParti.cfg"
    SECTION RAW = Binaries/meizu20pro/RawFiles/SecParti.cfg
  }

!if $(USE_CUSTOM_DISPLAY_DRIVER) == 1
  # This phone's own panel XMLs, original FFS GUIDs and UI names, taken from
  # its uefi partition. DisplayDxe reads msm_drm.dsi_display0 from the device
  # tree bootargs; this phone passes qcom,mdss_dsi_meizu_amoled_samsung_cmd,
  # which maps to Panel_meizu_amoled_samsung_dsc_cmd.xml. The XML describes the
  # 1440x3200 panel but its active timing is the 1080x2400 mode the phone boots
  # in. The other two XMLs stay packaged because DisplayDxe's table lists them.
  FILE FREEFORM = 0913F2A8-09D2-4234-8BAB-5A933A89D1B0 {
    SECTION UI = "Panel_meizu_amoled_samsung_dsc_cmd.xml"
    SECTION RAW = Binaries/meizu20pro/RawFiles/Panel_meizu_amoled_samsung_dsc_cmd.xml
  }
  FILE FREEFORM = 9ED70EAF-A657-4863-815F-C122757FEE5C {
    SECTION UI = "Panel_meizu_amoled_boe_dsc_cmd.xml"
    SECTION RAW = Binaries/meizu20pro/RawFiles/Panel_meizu_amoled_boe_dsc_cmd.xml
  }
  FILE FREEFORM = F764C9FB-F605-4080-B971-70DC90BA754A {
    SECTION UI = "Panel_meizu_amoled_3k_dsc_cmd.xml"
    SECTION RAW = Binaries/meizu20pro/RawFiles/Panel_meizu_amoled_3k_dsc_cmd.xml
  }
  FILE FREEFORM = 03E9256F-E621-48D8-8576-7E3B4FCF2D41 {
    SECTION UI = "PanelSimConfig.csv"
    SECTION RAW = Binaries/meizu20pro/RawFiles/PanelSimConfig.csv
  }
!endif
