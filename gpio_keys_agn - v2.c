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

/* 少了对应芯片类型的头文件 */

#if 0             //设备树  
gpio_keys_agn {         
        compatible = "agn,gpio_key";        // 兼容性
		key-gpios = <&pio 2 GPIO_ACTIVE_LOW //引脚资源
			&pio 7 GPIO_ACTIVE_LOW>;
    };   
#endif

static u32          gpio_info[6];    // 存储gpio引脚信息的u32数组
static u32          gpio_num[2];
static const char  *gpio1_name = "key1";    // 设备树中没有对引脚进行命名，手动写key1
static const char  *gpio2_name = "key2";    // 同



static int key_open (struct inode *inode, struct file *file)
{
    gpio_direction_input(gpio_num[0]);
    gpio_direction_input(gpio_num[1]);  // 分别设置&pio 2和&pio 7引脚为输入模式
    printk("key_open\n");

    return 0;
}

static int key_close (struct inode *inode, struct file *file)
{
    printk("key_close\n");
    
    return 0;
}

/* 处理获取的引脚电平数据，处理后送数据到用户空间 */
static ssize_t key_read (struct file *file, const char __user *buf, size_t len, loff_t *off)
{
    int rt;

    /* 获取两引脚高低电平信息 */
    char kbuf[2] = {gpio_get_value(gpio_num[0]), gpio_get_value(gpio_num[1])};      

    if (len > sizeof (kbuf))
        return -EINVAL;
    
    rt = copy_to_user(kbuf, buf, len);  // 送出数据

    len = len - rt

    return len;
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

    // 从设备树中查找"key-gpios"，得到对应的6个u32值，对应gpio口、引脚号、使能，共两组
    rt = of_property_read_u32_array(pdevnode, "key-gpios", gpio_info, 6);

    if(rt < 0 ){
        printk(KERN_ERR "of_property_read_u32_array key-gpios fail\n");

        goto err_of_property_read_u32_array;
    }

    // 把读取到的GPIO口信息和引脚号组合，不知道对不对
    gpio_num[0] = gpio_info[0] + gpio_info[1];
    gpio_num[1] = gpio_info[3] + gpio_info[4];

    // 提示引脚信息，包括引脚地址和引脚有效信号
    printk(KERN_INFO "gpio_num1 %d, gpio_ensign %d\n", gpio_num[0], gpio_info[2]);
    printk(KERN_INFO "gpio_num2 %d, gpio_ensign %d\n", gpio_num[1], gpio_info[5]);

    // 释放gpio引脚
    gpio_free(gpio_num[0]);
    gpio_free(gpio_num[1]);

    // 重新申请gpio引脚资源
    rt = gpio_request(gpio_num[0], gpio1_name);
    if (rt < 0){
        printk(KERN_ERR "gpio_request fail\n");

        goto err_gpio_request;
    }

    rt = gpio_request(gpio_num[1], gpio2_name);
    if (rt < 0){
        printk(KERN_ERR "gpio_request fail\n");

        goto err_gpio_request;
    }    

    return 0;

// 错误处理
err_gpio_request:
err_of_property_read_u32_array:
    misc_deregister(&key_misc); // 注销设备

    return rt;
}


// 平台卸载函数
static int key_remove(struct platform_device *pdev)
{
    gpio_free(gpio_num[0]);     // 释放1号引脚&pio 2
    gpio_free(gpio_num[1]);     // 释放2号引脚&pio 7
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
        .of_match_table = of_key_match, //设备树设备匹配
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