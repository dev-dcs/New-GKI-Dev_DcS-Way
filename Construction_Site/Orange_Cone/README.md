# Orange_Cone

Stock boot sources per Android generation. Drop the device's stock init_boot.img
(name it exactly init_boot.img) into the matching gen folder:

    Orange_Cone/A17/init_boot.img    Android 17 (Pixel, incl. mustang)
    Orange_Cone/A16/init_boot.img    Android 16

build-magisk-family.yml reads Construction_Site/Orange_Cone/<gen>/init_boot.img;
if it's missing it falls back to the boot_image_url input (factory/OTA zip).

@Dev_DcS
