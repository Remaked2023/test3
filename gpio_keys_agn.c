/*
 * static int __init gpio_init(void);   驱动入口函数
 * 注册平台驱动(1.绑定平台入口出口函数、2.绑定设备树节点)
 * 
 * static int key_probe(struct platform_device *pdev);  平台入口函数
 * 1.注册混杂设备(注册次设备号，绑定文件操作集)
 * 2.获取设备树节点的端口描述符，通过描述符获取中断号申请中断
 * 3.初始化等待队列头
 * 4.初始化gpio2和gpio7的按键中断，共享模式、双边沿触发
 * 
 * irqreturn_t keys_irq_handler(int irq, void *dev);    中断服务函数
 * 1.设置key_tasklet的data成员值为中断号
 * 2.调度key_tasklet
 * 
 * void key_tasklet_handler(unsigned long data)；   小任务，中断下半部
 * 1.判断中断号是否合法
 * 2.处理键值数据保存到key_val
 * 3.队列标志位置1，唤醒队列
 * 
 * static long key_ioctl (struct file *filp,
 *           unsigned int cmd, unsigned long args);     文件操作，目前只设置读
 * 1.响应等待队列，阻塞直到等待条件为真(中断处理完成)
 * 2.送按键状态给应用层
 * 
 * static int key_remove(struct platform_device *pdev);    平台卸载函数
 * static void __exit gpio_exit(void);          驱动卸载函数
 */
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
#include <linux/gpio/consumer.h>    // gpiod
#include <linux/miscdevice.h>   // 混杂设备
#include <linux/platform_device.h>  // 平台设备驱动模型
#include <linux/of.h>		// 设备树相关头文件
#include <linux/of_gpio.h>
#include <linux/of_address.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/string.h>
#include <linux/interrupts.h>   //中断常用的函数接口
#include <mach/devices.h>       //中断号
#include <linux/sched.h>        //等待队列头
#include <linux/delay.h>        //延时函数，消抖用
/* 少了对应芯片类型的头文件 */

#if 0             //设备树  
gpio_keys_agn {         
        compatible = "agn,gpio_key";        // 兼容性
		key-gpios = <&pio 2 GPIO_ACTIVE_LOW //引脚资源
			&pio 7 GPIO_ACTIVE_LOW>;
    };   
#endif

/* ioctl的cmd定义 */
#define GPIO_KEY_READ   _IO('K', 0);    

/* 键值
 * 0x01表示key7按下、key2松开；  0x10表示key2按下、key7松开；
 * 0x11表示两个键都按下；        0x00表示都松开
 */
static int key_val = 0;

/* gpio2按键的描述符 */
struct gpio_desc gpio_key2;
/* gpio7按键的描述符 */
struct gpio_desc gpio_key7;

/* 等待队列标志 */
static int key_press_flag = 0;
/* 等待队列头 */
static wait_queue_head_t gpio_key_wq;

/* tasklet处理函数
 * data是传入参数，设定为传入中断号
 * 如传入中断号有效(>=0)，则进行延时消抖和按键状态处理，并在处理后唤醒队列
 */
void key_tasklet_handler(unsigned long data)
{
    int irq = (int)data;

    if (irq >=0) {
        //10ms消抖，不会造成阻塞，但会延长处理时间
        mdelay(10);
        /* gpiod_get_value()属于原子操作，不会造成阻塞，
        但在tasklet中使用全局变量key_val需要注意 */
        key_val = gpiod_get_value(gpio_key2) << 1 | gpiod_get_value(gpio_key7);

        //队列等待条件为真，代表按键状态发生改变
        key_press_flag = 1;
        //唤醒队列
        wake_up(&gpio_key_wq);
    }
}

//分配初始化tasklet,名字、处理函数、data初始值
DECLARE_TASKLET(key_tasklet, key_tasklet_handler, -1);

/* 中断处理函数，双边沿触发，传入中断号给key_tasklet，并调度该小任务
 * irq中断号，dev是传入参数指针
 * 中断正常返回1，失败返回0
 */
irqreturn_t keys_irq_handler(int irq, void *dev)
{
    //设定tasklet传参为中断号
    key_tasklet.data = (unsigned long)irq;

    //登记调度tasklet，将按键操作处理交由中断下半部进行
    tasklet_schedule(&key_tasklet);

    printk("key_irq_handler\n");
    return IRQ_HANDLED;
}


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

/* 处理gpio按键状态数据，处理后送数据到用户空间 */
/* filp:打开文件路径
 * cmd:指令集，目前只包含读
 * len:接收数组长度(未使用)
 * off:偏移量(未使用)
 */
static long key_ioctl (struct file *filp, unsigned int cmd, unsigned long args)
{
    int rt = 0;
    //先让的队列休眠，防止在读取前队列处于唤醒状态
    key_press_flag = 0;

    switch (cmd){
    case GPIO_KEY_READ：
        //访问等待队列，判断key_press_flag条件是否为真
        //可中断睡眠
        wait_event_interruptible(gpio_key_wq, key_press_flag);
        key_press_flag = 0;
        break;
    default:
        printk("key ENOIOCTLCMD\n");
        return -ENOIOCTLCMD;
    }

    /* #include <linux/uaccess.h>
     * unsigned long copy_to_user(void __user *to,
     *    const void *from, unsigned long n);
     */   
    rt = copy_to_user((void *)args, &key_val);


    if (rt != 0)
        return -EFAULT;
    
    return 0;
}

// 文件操作结构体，绑定open、close、read函数
static const struct file_operations key_fops = {
    .open = key_open,
    .release = key_close,
    .ioctl = key_ioctl,
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

    /* gpiod是用gpio描述符的方式规定gpio端口使用，不用再对端口进行资源申请和释放操作，通过描述符获取中断号和对应引脚 */
    // 获取gpio描述符，存储在描述符数组中
    gpio_key2 = gpiod_get_index(pdevnode, "key-gpios", 0, GPIO_ACTIVE_LOW);
    if (IS_ERR(gpio_key2)) {
        printk("gpiod_get error\n");
        goto err_gpio_get;
    }

    gpio_key7 = gpiod_get_index(pdevnode, "key-gpios", 1, GPIO_ACTIVE_LOW);
    if (IS_ERR(gpio_key7)) {
        printk("gpiod_get error\n");
        goto err_gpio_get;
    }

    // 获取gpio口的中断号
    int gpio_key_irq[] = {gpiod_to_irq(gpio_key2), gpiod_to_irq(gpio_key7)};

    /* 申请中断1，设置gpio2中断方式 */
    if (gpio_key_irq[0]) {
        rt = request_irq(gpio_key_irq[0], keys_irq_handler, 
                        IRQF_SHARED | IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING, "gpio_2", NULL));
        if (rt) {
            printk("request_irq error\n");
            goto err_request_irq;
        }
    }

    /* 申请中断2，设置gpio7中断方式 */
    if (gpio_key_irq[1]) {
        rt = request_irq(gpio_key_irq[1], keys_irq_handler, 
                        IRQF_SHARED | IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING, "gpio_7", NULL));
        if (rt) {
            printk("request_irq error\n");
            goto err_request_irq;
        }   
    }

    //初始化队列头，用于中断后传输按下键位，唤醒不需要在入口函数进行
    init_waitqueue_head(gpio_key_wq); 

    printk("key_probe init");
    return 0;

// 错误处理
err_request_irq:
    free_irq(gpio_key_irq[0], NULL);
    free_irq(gpio_key_irq[1], NULL);
err_gpiod_get:
    gpiod_put(gpio_key2);
    gpiod_put(gpio_key7);
err_of_get_named_gpio:
    misc_deregister(&key_misc); // 注销设备

    return rt;
}

// 平台卸载函数
static int key_remove(struct platform_device *pdev)
{
    gpiod_put(gpio_key2);
    gpiod_put(gpio_key7);    
    
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