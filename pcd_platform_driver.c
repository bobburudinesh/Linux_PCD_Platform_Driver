#include<linux/module.h>
#include<linux/fs.h>
#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/kdev_t.h>
#include<linux/uaccess.h>
#include<linux/platform_device.h>
#include<linux/slab.h>
#include<linux/mod_devicetable.h>
#include "platform.h"


#undef pr_fmt
#define pr_fmt(fmt)  "%s :" fmt,__func__
#define MAX_DEVICES	10

loff_t pcd_lseek(struct file *filp, loff_t offset, int whence);
ssize_t pcd_read(struct file *filp, char __user *buff, size_t count, loff_t *f_pos);
ssize_t pcd_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos);
int pcd_open(struct inode *inode, struct file *filp);
int pcd_release(struct inode *inode, struct file *filp);
int check_permission(int dev_perm, int acc_mode); 
int pcd_platform_driver_probe(struct platform_device *pdev);
void pcd_platform_driver_remove(struct platform_device *pdev);



enum pcdev_names
{
        PCDEVA1X,
        PCDEVB1X,
        PCDEVC1X,
        PCDEVD1X
};

struct device_config
{
        int config_item1;
        int config_item2;
};

struct device_config pcdev_config[] =
{
        [PCDEVA1X] = {.config_item1 = 60, .config_item2 = 21},
        [PCDEVB1X] = {.config_item1 = 50, .config_item2 = 22},
        [PCDEVC1X] = {.config_item1 = 40, .config_item2 = 23},
        [PCDEVD1X] = {.config_item1 = 30, .config_item2 = 24}
};

struct platform_device_id pcdevs_ids[] =
{
        [0] = {.name = "pcdev-A1x", .driver_data = PCDEVA1X},
        [1] = {.name = "pcdev-B1x", .driver_data = PCDEVB1X},
        [2] = {.name = "pcdev-C1x", .driver_data = PCDEVC1X},
        [3] = {.name = "pcdev-D1x", .driver_data = PCDEVD1X}

};


/*Device private data structure*/
struct pcdev_private_data
{
	struct pcdev_platform_data pdata;/*platform data of device*/
	char *buffer;/*pointer to private buffer of device*/
	dev_t dev_num; /*holds device number*/
	struct cdev cdev;
};


/*Driver private data structure*/
struct pcdrv_private_data
{
	int total_devices;
	dev_t device_num_base;
	struct class *class_pcd;
	struct device *device_pcd;
};

struct pcdrv_private_data pcdrv_data;

int check_permission(int dev_perm, int acc_mode) {
	if(dev_perm == RDWR) {
	return 0;
	}
	/*Ensures Read only access*/
	if( (dev_perm == RDONLY) && ( (acc_mode & FMODE_READ) && !(acc_mode & FMODE_WRITE))) {
	return 0;
	}
	/*Ensures Write only Access*/
        if( (dev_perm == WRONLY) && ( (acc_mode & FMODE_WRITE) && !(acc_mode & FMODE_READ))) {
        return 0;
        }	
	
	return -EPERM;
}

loff_t pcd_lseek(struct file *filp, loff_t offset, int whence){
	/*Get private data of char device inorder to get max size of character device*/
	struct pcdev_private_data *pcdev_data = (struct pcdev_private_data*)filp->private_data;
        int max_size = pcdev_data->pdata.size;
	loff_t temp;
	pr_info("lseek requested\n");
	pr_info("Current value of the file position = %lld\n",filp->f_pos);

	switch(whence){
		case SEEK_SET:
			if(offset>max_size || offset<0){
				return -EINVAL;
			}
			filp->f_pos = offset;
			break;
		case SEEK_CUR:
			temp = filp->f_pos + offset;
			if((temp > max_size) || (temp < 0)){
				return -EINVAL;
			}
			filp->f_pos = offset;
			break;

		case SEEK_END:
			temp = filp->f_pos + offset;
			if((temp > max_size) || (temp < 0)) {
				return -EINVAL;
			}
			filp->f_pos = offset;
			break;
		default:
			return -EINVAL;	
	};
	pr_info("New value of the file position = %lld\n",filp->f_pos);
	return 0;
}
ssize_t pcd_read(struct file *filp, char __user *buff, size_t count, loff_t *f_pos){
	
	/*Get private data of char device inorder to get max size of character device*/
	struct pcdev_private_data *pcdev_data = (struct pcdev_private_data*)filp->private_data;
	int max_size = pcdev_data->pdata.size;
	pr_info("read requested for %zu bytes\n", count);
	pr_info("Current file position = %lld\n", *f_pos);
	/*Check if count has to be adjusted*/
	if((*f_pos + count) > max_size) {
		count = max_size - *f_pos;
	}
	/*Read from kernel space buffer to user space buffer*/
	if(!copy_to_user(buff,pcdev_data->buffer+(*f_pos),count)){
		return -EFAULT;
	}
	/*Update the current file position*/
	*f_pos += count;
	pr_info("Number of bytes successfully read = %zu\n", count);
	pr_info("Updated file position = %lld\n", *f_pos);
	/*return number of bytes sucessfully read*/
	return count;
}
ssize_t pcd_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos){
	/*Get private data of char device inorder to get max size of character device*/
	struct pcdev_private_data *pcdev_data = (struct pcdev_private_data*)filp->private_data;
	int max_size = pcdev_data->pdata.size;
	pr_info("write requested for %zu bytes\n", count);
	pr_info("Current file position = %lld\n", *f_pos);
	/*Check if count has to be adjusted*/
	if((*f_pos + count)>max_size) {
		count = max_size - *f_pos;
	}
	/*Write from user space buffer to kernel space buffer*/
	if(!copy_from_user(pcdev_data->buffer+(*f_pos), buff, count)){
		return -EFAULT;
	}
	/*Update the current file position*/
	*f_pos += count;
	pr_info("Nuber of bytes successfully written = %zu\n", count);
	pr_info("Updated file position = %lld\n", *f_pos);
	/*return number of bytes successfully written*/
	return count;
}
int pcd_open(struct inode *inode, struct file *filp){
	int ret;
	struct pcdev_private_data *pcdev_data;
	int minor_num;
	minor_num = MINOR(inode->i_rdev);
	pr_info("Minor Number = %d\n", minor_num);
	/*Get devices private data structure*/
	pcdev_data = container_of(inode->i_cdev, struct pcdev_private_data, cdev);
	/*to pass device private data to other methods of the driver, as filp is passed as argument to other functions*/
	filp->private_data = pcdev_data;
	/*Check Permissions*/
	ret = check_permission(pcdev_data->pdata.perm, filp->f_mode);
	(!ret) ? pr_info("open was successful\n") : pr_info("open was unsuccessful\n");
	return ret;
}
int pcd_release(struct inode *inode, struct file *filp){
	pr_info("release was successful\n");
	return 0;
}


//File operations of the driver
struct file_operations pcd_fops = 
{
	.open = pcd_open,
	.write = pcd_write,
	.read = pcd_read,
	.llseek = pcd_lseek,
	.release = pcd_release,
	.owner = THIS_MODULE
};

/*Remove function gets called when device is removed from the system*/
void pcd_platform_driver_remove(struct platform_device *pdev)
{
	struct pcdev_private_data	*dev_data = dev_get_drvdata(&pdev->dev);
	/*1. Remove a device that was created with device_create()*/
	device_destroy(pcdrv_data.class_pcd, dev_data->dev_num);
	/*2. Remove a cdev entry from the system*/
	cdev_del(&dev_data->cdev);
	pcdrv_data.total_devices--;
	pr_info("platform device is removed");
}
/*Probe function gets called when matched platform device is found*/
int pcd_platform_driver_probe(struct platform_device *pdev)
{
	int ret;
	struct pcdev_private_data	*dev_data;

	struct pcdev_platform_data	*pdata;

	pr_info("A device is detected\n");
	/*1. Get the platform data*/
	pdata = pdev->dev.platform_data;
	if(!pdata) {
		pr_info("No platform data available\n");
		return -EINVAL;
	}
	/*2. Dynamically allocate memory for the device private data*/
	dev_data = devm_kzalloc(&pdev->dev, sizeof(struct pcdev_platform_data), GFP_KERNEL);
	if(!dev_data) {
		pr_info("Cannot allocate memory");
		return -ENOMEM;
	}
	/*save the device private data pointer in platform device structure*/
	dev_set_drvdata(&pdev->dev, dev_data);
	dev_data->pdata.size = pdata->size;
	dev_data->pdata.perm = pdata->perm;
	dev_data->pdata.serial_number = pdata->serial_number;


	pr_info("Device serial number = %s\n", dev_data->pdata.serial_number);
	pr_info("Device size = %d\n", dev_data->pdata.size);
	pr_info("Device permission = %d\n", dev_data->pdata.perm);
	pr_info("Config item 1 = %d\n", pcdev_config[pdev->id_entry->driver_data].config_item1);
	pr_info("Config item 2 = %d\n", pcdev_config[pdev->id_entry->driver_data].config_item2);
	/*3. Dynamically allocate memory for the device buffer using size information from the platform data*/
	dev_data->buffer = devm_kzalloc(&pdev->dev, dev_data->pdata.size, GFP_KERNEL);
	if(!dev_data->buffer) {
		pr_info("Cannot allocate memory\n");
		return -ENOMEM;
	}
	/*4. Get the device number*/
	dev_data->dev_num = pcdrv_data.device_num_base + pdev->id;
	/*5. Do Cdev init and Cdev add*/
	cdev_init(&dev_data->cdev, &pcd_fops);

	dev_data->cdev.owner = THIS_MODULE;
	ret = cdev_add(&dev_data->cdev, dev_data->dev_num,1);
	if(ret < 0){
		pr_err("Cdev add failed\n");
		return ret;
	}
	/*6. Create device file for the detected platform device*/
	pcdrv_data.device_pcd = device_create(pcdrv_data.class_pcd, NULL, dev_data->dev_num,NULL,"pcdev-%d",pdev->id );
	if(IS_ERR(pcdrv_data.device_pcd)){
		pr_err("Device crate failed\n");
		ret = PTR_ERR(pcdrv_data.device_pcd);
		return ret;
	}
	pcdrv_data.total_devices++;
	pr_info("The probe was successful\n");
	return 0;
	
}


/*struct platform Driver*/
struct platform_driver pcd_platform_driver = 
{
	.probe = pcd_platform_driver_probe,
	.remove = pcd_platform_driver_remove,
	.id_table = pcdevs_ids,
	.driver = {
		.name = "pseudo-char-device"
	}

};

static int __init pcd_driver_init(void)
{	
	int ret;
	/*1. Dynamically allocate a device number for MAX_DEVICES*/
	ret = alloc_chrdev_region(&pcdrv_data.device_num_base, 0, MAX_DEVICES, "pcdevs");
	if(ret < 0){
		pr_err("Alloc chrdev failed\n");
		return ret;
	}

	/*2. Create device class under /sys/class*/
	pcdrv_data.class_pcd = class_create("pcd_class");
	if(IS_ERR(pcdrv_data.class_pcd)){
		pr_err("Class creation failed\n");
		ret = PTR_ERR(pcdrv_data.class_pcd);
		unregister_chrdev_region(pcdrv_data.device_num_base,MAX_DEVICES);
		return ret;
	}

	/*3. Register a platform driver*/
	platform_driver_register(&pcd_platform_driver);
	pr_info("pcd platform driver loaded\n");
	return 0;
}

static void __exit pcd_driver_cleanup(void)
{
	/*Unregister the platform driver*/
	platform_driver_unregister(&pcd_platform_driver);
	/*Class Destroy*/
	class_destroy(pcdrv_data.class_pcd);
	/*Unregister device numbers for MAX_DEVICES*/
	unregister_chrdev_region(pcdrv_data.device_num_base, MAX_DEVICES);
	pr_info("pcd platform driver unloaded\n");
}

module_init(pcd_driver_init);
module_exit(pcd_driver_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dinesh Bobburu");
MODULE_DESCRIPTION("A Psuedo character platform driver which handles n platform devices");
