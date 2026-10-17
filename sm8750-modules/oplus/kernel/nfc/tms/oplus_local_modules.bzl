load("//build/kernel/oplus:oplus_modules_define.bzl",
     "define_oplus_ddk_module",
     "oplus_ddk_get_kernel_version",
     "oplus_ddk_get_target",
     "oplus_ddk_get_variant")

def define_oplus_local_modules():
    target = oplus_ddk_get_target()
    variant = oplus_ddk_get_variant()

    # TMS NFC/ESE device driver -> tms_device_modules.ko
    define_oplus_ddk_module(
        name = "tms_device_modules",
        srcs = native.glob([
            "thn31/**/*.c",
            "thn31/**/*.h",
        ]),
        includes = [
            "thn31",
            "thn31/nfc",
            "thn31/ese",
            "thn31/debuger",
            "kernel/oneplus/sm8750/include",
        ],
        local_defines = [
            "CONFIG_TMS_NFC_DEVICE",
            "CONFIG_TMS_ESE_DEVICE",
            "TMS_DEBUGER_LOGGER",
            "TMS_DEBUGER_CHECKER",
        ],
    )

    # oplus_nfc -> oplus_nfc.ko
    define_oplus_ddk_module(
        name = "oplus_nfc",
        srcs = native.glob([
            "oplus_nfc/*.c",
            "oplus_nfc/*.h",
        ]),
        includes = [
            "oplus_nfc",
            "kernel/oneplus/sm8750/include",
        ],
    )
