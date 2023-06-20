#include <linux/init.h>		// __init __exit
#include <linux/kernel.h>	// printk
#include <linux/module.h>	// module_init module_exit
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>   // 混杂设备
#include <linux/platform_device.h>  // 平台设备驱动模型
#include <linux/of.h>		// 设备树相关头文件
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/string.h>
/* 少了对应芯片类型的头文件 */

#if 0             //设备树  
gpio_keys_agn {         
        compatible = "agn,gpio_key";        // 兼容性
		key-gpios = <&pio 2 GPIO_ACTIVE_LOW //引脚资源
			&pio 7 GPIO_ACTIVE_LOW>;
    };   
#endif

/* 定义按键值 */
#define KEY_VAL         0xF0              // 按键摁下
#define INV_KEY_VAL     0x00              // 按键松开

/* key设备结构体 */
struct key_dev {
    int key_num = 2;                   /* gpio引脚占用数 */
    int key_gpios[key_num];            /* key使用的GPIO编号，总计2个 */
    atomic_t key_vals[key_num];        /* 按键值，对应两个GPIO引脚 */
} keydev;


static int key_open (struct inode *inode, struct file *filp)
{
    printk("key_open\n");

    return 0;
}


/* 关闭函数没有资源释放 */
static int key_close (struct inode *inode, struct file *filp)
{
    printk("key_close\n");
    
    return 0;
}

/* 处理获取的引脚电平数据，处理后送数据到用户空间 */
/* file:打开文件路径
 * buf: 用户用于接收端口高低电平
 * len:接收数组长度(未使用)
 * off:偏移量(未使用)
 */
static ssize_t key_read (struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    /* 获取两引脚高低电平信息 */
    u8 key_val = 0;        // 最高0~7，设置8位
    int num = keydev->key_num;

    // 检测所有按键状态，
    for(int key_no = 0; key_no < num; key_no++) {
        if (gpio_get_value(keydev->key_gpios[key_no]) == 0) /* 按键按下 */
            atomic_set(&keydev->key_vals[key_no], KEY_VAL);
        else                                     /* 按键没有按下 */
            atomic_set(&keydev->key_vals[key_no], INV_KEY_VAL);
    }

    /* 保存按键值 */
    for(int key_no = num - 1; key_no >= 0; key_no--) {
        if (keydev->key_vals[key_no] == KEY_VAL) 
            key_val |= 0x01;        // 用1表示按键按下
        
        if (key_no != 0)
            key_val = key_val<<1;   // 从右到左依次表示key1~keyn按键状态
    }

    /* #include <linux/uaccess.h>
     * unsigned long copy_to_user(void __user *to,
     *    const void *from, unsigned long n);
     */
    copy_to_user(buf, &key_val, sizeof(key_val));  // 送出数据

    return num; // 返回读取有效数据位数
}

// 文件操作结构体，绑定open、close、read函数
static const struct file_operations key_fops = {
    .open = key_open,
    .release = key_close,
    .read = key_read,
    .module = THIS.MODULE,
};

// misc结构体，用于后续平台申请设备号
static struct miscdevice key_misc = {
    .minor = MISC_DYNAMIC_MINOR,        // 动态分配次设备号
    .name = "gpio-key",                 // 设置设备名
    .fops = &key_fops,                  // 绑定fops
};


// 平台入口函数
static int key_probe(struct platform_device *pdev)
{
    int rt;
    struct device_node *pdevnode = pdev->dev.of_node;

    // 注册混杂设备
    rt = misc_register(&key_misc);

    if(rt < 0){
        printk("misc_register fail\n");

        return rt;
    }

    // 从设备树中查找"key-gpios"，得到对应gpio口、引脚号、使能，共两组
    for (int num = 0; num < keydev->key_num; num++) {
        rt = of_get_named_gpio(pdevnode, "key-gpios", keydev->key_gpios[num], num);

        if(rt < 0 ){
            printk(KERN_ERR "of_get_named_gpio key-gpios fail\n");

            goto err_of_get_named_gpio;
        }

        // 提示引脚信息，包括引脚地址和引脚有效信号
        printk(KERN_INFO "key%d %s\n", num, keydev->key_gpios[num]);
        
        // 释放gpio引脚
        gpio_free(keydev->key_gpios[num]);
        
        char key_name[6];
        sprintf(key_name, "key%d", num);

        // 重新申请gpio引脚资源
        rt = gpio_request(keydev_num[num], key_name);
        if (rt < 0){
            printk(KERN_ERR "gpio_request fail\n");

            goto err_gpio_request;
        }
    }

    return 0;

// 错误处理
err_gpio_request:
err_of_get_named_gpio:
    misc_deregister(&key_misc); // 注销设备

    return rt;
}

// 平台卸载函数
static int key_remove(struct platform_device *pdev)
{
    for (int num = 0; num < keydev->key_num; num++)
        gpio_free(keydev->key_gpios[num]);     
    
    misc_deregister(&key_misc); // 注销设备

    return 0;
}

// 定义设备匹配信息结构
static const struct of_device_id of_key_match[] = {
    {
        .compatible = "agn,gpio_key",
    },  //compatible 兼容属性名，需要与设备树节点属性一致
    {},
};

static struct platform_driver key_driver = {
    .probe = key_probe,                 // 平台初始化函数
    .remove = __devexit_p(key_remove),  // 平台驱动的卸载函数
    .driver = {
        .owner  = THIS_MODULE,
        .name   = "gpio-key",           // 命名平台驱动，不能为空，否则会在安装驱动时出错
        .of_match_table = of_key_match, // 设备树设备匹配
    },
};

// 模块入口函数
static int __init gpio_init(void)
{
    int rt = platform_driver_register(&key_driver); // 绑定平台驱动结构体

    if (rt <0){
        printk(KERN_INFO "platform_driver_register err\n");

        return rt;
    }

    printk(KERN_INFO "gpio_init\n");

    return rt;
}

// 模块出口函数
static void __exit gpio_exit(void)
{
    platform_driver_unregister(&key_driver);

    printk("gpio_exit\n");
}

module_init(gpio_init);

module_exit(gpio_exit);

MODULE_AUTHOR("Linhongyi");
MODULE_DESCRIPTION("gpio-key driver");
MODULE_LICENSE("GPL");
