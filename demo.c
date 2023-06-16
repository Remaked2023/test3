#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linnux/ioport.h>
#include <linux/io.h>
#include <linux/gpio.h>
/* 少了对应芯片类型的头文件 */


#define GPIO_ACTIVE_HIGH 1		//定义高电平为1，按键松开
#define GPIO_ACTIVE_LOW  0		//定义低电平为0，按键摁下


// 定义一个字符设备gpio_key
static struct cdev gpio_key_cdev;

// 定义设备号
static dev_t key_dev_num = 0;


//引脚资源结构体数组key_gpios
static struct gpio key_gpios[2] = {
	// 引脚地址   引脚输入模式-初始电平状态为低电平    自定义引脚名字
	{ PIO + 2, GPIO_ACTIVE_HIGH,						"KEY1"	},	//gpio 的地址起始地址宏 暂定PIO
	{ PIO + 7, GPIO_ACTIVE_HIGH,						"KEY2"	},	//gpio7
};

//文件打开函数key_open()
static int key_open (struct inode *inode, struct file *file)
{
	printk("<4>""key_open\n");
	
	return 0;
}

//文件关闭函数key_close()
int key_close(struct inode *inode, struct file *file)
{
	printk("<4>""key_close\n");
	
	return 0;
}

//文件读取函数，返回值为读取字节
//读取时接收到的数据是一个二维数组
ssize_t key_read(struct file *file, char __user *buf, size_t len, loff_t *offset);
{
	char kbuf[2] = {gpio_get_value(PIO + 2), gpio_get_value(PIO + 7);
	
	int rt = 0;
	
	if(buf == NULL)
		return -EINVAL;	//参数非法，返回退出
	
	if(len > 2)		// 最多读出2个字节
		return -EINVAL;
	
	//将内核空间的kbuf拷贝给用户空间buf数据
	//返回没有被读取成功的字节数
	rt = copy_to_user(buf, kbuf, len);
	
	// 更新len，为最大读取字节数 - 没有成功读取字节数
	len = len - rt;
	
	return len;
}

// 设置fops结构体和对应链接函数名(猜测是作为函数指针传入，还没注意)
static struct file_operations key_fops = {
	.owner		= THIS_MODULE;
	.open		= key_open;
	.write		= led_write;
	.release	= led_close;
}

// 入口函数
static int __init key_init(void)
{
	int rt = 0;
	
	
	//动态申请设备号
	rt = alloc_chrdev_region(&gpio_key_cdev, 0, 1, "gpio_keys_agn");
	if (rt < 0) {
		printk("register_chrdev_region error\n");
		
		goto err_alloc_chrdev_region;
	}
	
	//打印主设备号
	printk("major = %d\n", MAJOR(key_dev_num));

	//打印次设备号
	printk("minor = %d\n", MINOR(key_dev_num));
	
	//初始化设备(绑定文件操作结构体)
	cdev_init(&gpio_key_cdev, &key_fops);
	
	//加入内核
	rt = cdev_add(&gpio_key_cdev, key_dev_num, 1);
	
	if(rt < 0) {
		printk("cdev_add error\n");
		
		goto err_dev_add;
	}
	
	//取巧，释放内核占用的引脚资源
	gpio_free_array(key_gpios, ARRAY_SIZE(key_gpios));
	
	//重新申请引脚资源
	rt = gpio_request_array(key_gpios, ARRAY_SIZE(key_gpios));
	
	if(rt < 0) {
		goto err_gpio_request_array;
	}
	
	printk("gpio_key_agn init\n");
	
	return 0;

err_gpio_request_array:
	//删除字符设备
	cdev_del(&gpio_key_cdev);

err_dev_add:
	//注销设备号
	unregister_chrdev_region(key_dev_num, 1);
	
err_alloc_chrdev_region:	
	//这里没有申请成功，没有东西需要撤销
	//返回错误码
	return rt;
}

static void __exit key_exit(void)
{
	//释放引脚资源
	gpio_free_array(key_gpios, ARRAY_SIZE(key_gpios);

	//删除设备
	cdev_del(&gpio_key_cdev, 1);

	//撤销设备号
	unregister_chrdev_region(key_dev_num, 1);
	
	printk("gpio_key_agn exit");
}

//驱动入口函数设定
module_init(key_init);

//驱动出口函数设定
module_exit(key_exit);

MODULE_AUTHOR("Agntech Linhongyi");		// 作者
MODULE_DESCRIPTION("agn,gpio_key");		// 描述抄了设备树的兼容性
MODULE_LICENSE("GPL");		//GPL 认证，要不要v2不知道