# test3

[1.题目](#jump)

[2.v3分析](#jump1)

[3.gpio_key_agn -v4 当前版本](#jump2)

[4.改进](#jump3)

## <span id="jump">1. 题目</span>

**根据dts设备树和原理图编写按键驱动程序，实现按键驱动功能。**

gpio_keys_agn {        

        **compatible** = "agn,gpio_key";        // 兼容性

        **key-gpios** = <&pio 2 GPIO_ACTIVE_LOW //引脚资源

            &pio 7 GPIO_ACTIVE_LOW>;

    };

![Image](https://github.com/Remaked2023/image-blog/blob/main/image1-test3.png?raw=true)

## <span id ="jump1">2. v3分析</span>

- `compatible`兼容性属性可以用于定位设备树对应节点`gpio_keys_agn`中的信息，将`of_device_id`结构体中`.compatible`成员设置为`"agn,gpio_key"`与设备树兼容性属性保持一致，然后在`platform_driver`结构体中`.driver`成员的`.of_match_table`成员进行匹配绑定，再在模块入口函数中用`platform_driver_register(&key_driver)`进行结构体绑定，获取对应设备树设备和对应设备节点。

- 模块初始化调用`platform_driver_register(&key_driver)`后会进入`key_driver`结构体绑定的平台入口函数`key_probe()`，在其中需要完成注册设备和资源申请。

- 注册设备用到的是`miscdevice`结构体绑定，即混杂设备注册方式，绑定文件操作结构体变量key_fops地址，绑定对应`open()`、`read()`、`close()`函数在驱动中的对应函数入口，同时申请设备号

- `key-gpios`能对应引脚资源，即`pio口2号引脚`和`pio口7号引脚`，他们的使能信号都是低电平触发。在platform平台驱动模型入口函数中使用`of_get_named_gpio()`获取`"key-gpios"`标识，分别获取对应顺序编号0,1两个引脚资源地址，然后再进行资源申请操作。

- 在平台卸载函数中需要释放申请的引脚资源和对混杂设备进行注销，对应`gpio_free()`和`misc_deregister()`

- 在整体上，把按键事件变换为摁下和松开，对应为`KEY_VAL`(0xF0)和`INV_KEY_VAL`(0x00)的01事件，定义对应的宏用于读取判断；同时定义key设备结构体key_dev，保存gpio引脚占用数(即按键数)、存储对应gpio引脚的资源地址集，以及用于保存读取按键状态值的集合

- `key_read()`函数中设置一个u8变量`key_val`用于保存处理后的按键状态集，逻辑上检测所有按键状态，同时将`key_dev`结构体变量`keydev`的成员`key_vals`对应元素写`KEY_VAL`或`INVKEY_VAL`，再将按键状态以01形式按按键名从高到低顺序从右到左进移位进入`key_val`中，然后送出`key_val`到用户输入的`buf`中，并返回读取有效数据位数
  
  - 在用户存储数组`buf`中，应有如下数据位格式:
    
    ...keyn,keyn-1,...,key1    对应0为松开，1为摁下，最高8个按键状态

## <span id="jump2">3. gpio_keys_agn -v4  当前版本</span>

```c
/*
 * static int __init gpio_init(void);   驱动入口函数
 * 注册平台驱动(1.绑定平台入口出口函数、2.绑定设备树节点)
 * 
 * static int key_probe(struct platform_device *pdev);  平台入口函数
 * 1.注册混杂设备(注册次设备号，绑定文件操作集)
 * 2.获取设备树节点的引脚地址gpio_2和gpio_7并申请引脚资源用于ioctl等文件操作函数
 * 3.初始化等待队列头
 * 4.初始化gpio2和gpio7的按键中断，共享模式、下降沿触发(按键按下)
 * 
 * irqreturn_t keys_irq_handler(int irq, void *dev);    中断服务函数
 * 1.用(中断号不知道)判断哪个按键按下，逻辑上只同时允许1个按键按下，中断可被打断
 * 2.设置全局变量值keydev.keyvals获取当前按下键位
 * 3.设置等待队列条件为真，唤醒等待队列
 * 
 * static long key_ioctl (struct file *filp,
 *           unsigned int cmd, unsigned long args);     文件操作，目前只设置读
 * 1.响应等待队列，阻塞直到等待条件为真(中断处理完成)
 * 2.送按下键位给应用层
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
/* 少了对应芯片类型的头文件 */

#if 0             //设备树  
gpio_keys_agn {         
        compatible = "agn,gpio_key";        // 兼容性
		key-gpios = <&pio 2 GPIO_ACTIVE_LOW //引脚资源
			&pio 7 GPIO_ACTIVE_LOW>;
    };   
#endif

/* 定义按键值 */
#define KEY_VAL         1              // 按键摁下
#define INV_KEY_VAL     0              // 按键松开

#define GPIO_KEY_READ   _IO('K', 0);    //ioctl的cmd定义

unsigned int key2 = 2;      //按键2的中断服务函数参数(标识)
unsigned int key7 = 7;      //按键7的中断服务函数参数(标识)

static int key_press_flag = 0;
static wait_queue_head_t gpio_key_wq;

/* key设备结构体 */
struct key_dev {
    int key_num = 2;                   /* gpio引脚占用数 */
    int key_gpios[key_num] = {0};      /* key使用的GPIO引脚，总计2个 */
    int key_vals = 0;        /* 按键值，对应两个GPIO引脚 */
} keydev;

/* 中断服务函数，获取按键状态，下降沿触发，按下时写按键状态到keydev
 * irq中断号，dev是传入参数指针
 * 中断正常返回1，失败返回0
 */
irqreturn_t keys_irq_handler(int irq, void *dev)
{
    //获取参数内容
    unsigned int key = *(unsigned int *) dev;   //强转
    printk("key_irq_handler\n");

    //判断按键
    /*
        if (irq == gpio_to_irq(keydev.key_gpios[0]))
        因为不确定gpio_to_irq()会不会造成阻塞，
        暂时用传参key作为不准确的判断条件
    */
    if (key == 2) keydev.key_vals |= 1<<0;  //...01
    if (key == 7) keydev.key_vals |= 1<<1;  //...10

    //队列等待条件为真，代表按键按下
    key_press_flag = 1;
    //唤醒队列
    wake_up(&gpio_key_wq);

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

/* 处理获取的引脚电平数据，处理后送数据到用户空间 */
/* filp:打开文件路径
 * cmd:指令集，目前只包含读
 * len:接收数组长度(未使用)
 * off:偏移量(未使用)
 */
static long key_ioctl (struct file *filp, unsigned int cmd, unsigned long args)
{
    int rt = 0;
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
    rt = copy_to_user((void *)args, &keydev.key_vals);
    keydev.key_vals = 0;

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
        
        char key_name[6];       //keyn\0
        sprintf(key_name, "key%d", num);

        // 重新申请gpio引脚资源
        rt = gpio_request(keydev->key_gpios[num], key_name);
        if (rt < 0){
            printk(KERN_ERR "gpio_request fail\n");

            goto err_gpio_request;
        }
    }

    //初始化队列头，用于中断后传输按下键位，唤醒不需要在入口函数进行
    init_waitqueue_head(gpio_key_wq);

    //申请中断1，设置gpio2中断方式
    rt = request_irq(gpio_to_irq(key_gpios[0], keys_irq_handler, 
                    IRQF_SHARED | IRQF_TRIGGER_FALLING, "gpio_2", &key2));
    if (rt) {
        printk("request_irq error\n");
        goto err_request_irq;
    }
    //申请中断2，设置gpio7中断方式
    rt = request_irq(gpio_to_irq(key_gpios[1], keys_irq_handler, 
                    IRQF_SHARED | IRQF_TRIGGER_FALLING, "gpio_7", &key7));
    if (rt) {
        printk("request_irq error\n");
        goto err_request_irq;
    }    

    printk("key_probe init");
    return 0;

// 错误处理
err_request_irq:
    free_irq(gpio_to_irq(key_gpios[0]), &key2);
    free_irq(gpio_to_irq(key_gpios[1]), &key7);
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
```

## <span id="jump3">4. 改进</span>

1. 首先是就实时性改进，需要把不停检测按键状态读取的方式改成按键按下时才读取。用中断的方式，当对应gpio口产生一次下降沿即按键按下时，触发中断对按键值进行变动，但是因为要避免阻塞，在中断服务函数不能使用信号量处理函数，于是遇到了数据传输问题。

2. 解决方式是用等待队列的方式让用户层的读操作等待按键中断发生，然后在按键中断结束后再读取按键值，然后重新清空按键。缺点是在用户层会造成线程阻塞，优点是不再需要死循环，读取成功后就能直接进行其他操作。

3. 平台驱动函数里增加了队列头的初始化，并额外申请了gpio口的中断。

4. 读取函数改为了调用ioctl的方式，更新了file_operations操作集中read和ioctl的绑定。

5. 文件读取函数中响应等待队列，在按键中断发生后对按键值读取，然后再复位队列标志和传出按键键位值
