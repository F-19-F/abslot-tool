# AB slot tools for MTK

This tool is able to change the boot_ctl struct in misc partition,and makes you phone automatically change slot when your phone cannot boot to current slot when you have done something dangerous.






### background
If you do some dangerous operations to your mediatek phone, you may brick your phone,for normal user,fastboot is the last line of defense.If your operations damage the lk(little kernel),where is the fastboot located in ,you have no way except enter edl,however,modern vendors like XiaoMi have limited the permission to flash partition via edl,you may use some tools like MTKClient to bypass this limitation, but it doesn't work on newly published phone.On other words, you have to send your phone to you vendor to fix this.

Recovery may help you slove this normal problems,however,for newly published phone , recovery relies on the boot partition , where the GKI kernel is located. If you flash custom kernel,you may not be able to enter recovery!

The solution maybe utilize the AB partition mechanism,for mediatek devices , their bootroom get which preloader to load via query_attr of flash , load preloader a or b,then preloader will load misc partition , and change the boot_part info in the flash according to the boot_ctrl struct in misc partition if needed, and preloader will reduce tries_remaining if this part cannot boot successfully (successful_boot) ,  when tries_remaining == 0 ,preloader will change slot to another slot.Howerer,when successful_boot is set to 1(after you successfully boot to android,HAL will set this flag to 1),the preloader will not 
automatically change slot even if current slot cannot boot.

This tool protects your phone by setting successful_boot of slot to 0 , then makes preloader to check  successful_boot and automatically change slot to another.

