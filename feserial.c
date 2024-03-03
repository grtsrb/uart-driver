#include <asm-generic/errno-base.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/miscdevice.h>
#include <linux/resource.h>
#include <linux/pm_runtime.h>
#include <asm/io.h>
#include <linux/fs.h>

// Driver structure
struct uart_dev {
    void __iomem *regs;
    struct miscdevice miscdev;
    // TO DO




};

static unsigned reg_read(struct uart_dev *dev, int offset)
{
    return readl(dev->regs + offset);
}

static void reg_write(struct uart_dev *dev, int value, int offset)
{
    writel(value, dev->regs + offset);
}


//TODO feserial_write
static ssize_t feserial_write(struct file *file, const char __user *buf, size_t sz, loff_t *ppos)
{
    struct uart_dev *dev;
    dev = container_of(file->private_data, struct uart_dev, miscdev);
    int i;
    for (i = 0; i < sz; i++)
    {
        if (copy_from_user(buf + i, )) return -EFAULT;

    }


    return -EINVAL;
}

//TODO feserial_read
static ssize_t feserial_read(struct file *file, char __user *buf, size_t sz, loff_t *ppos)
{
    return -EINVAL;
}

// ioctl function

static long feserial_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
    return -EINVAL;
}
static const struct file_operations feserial_fops = {
    .owner = THIS_MODULE,
    .write = feserial_write,
    .read = feserial_read,
};


static int feserial_probe(struct platform_device *pdev)
{
	pr_info("Called feserial_probe\n");
    struct uart_dev *dev;
    struct resource *res;
    int ret;

    // Allocate memory
    dev = devm_kzalloc(&pdev->dev, sizeof(struct uart_dev), GFP_KERNEL);
    if (!dev) {
        dev_err(&pdev->dev, "Can not allocate memory!\n");
        return -ENOMEM;
    };

    // Get base address
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res) {
        dev_err(&pdev->dev, "Can not get base address!\n");
    };

    // Remap resources 
    dev->regs = devm_ioremap_resource(&pdev->dev, res);
    if (!dev->regs) {
        dev_err(&pdev->dev, "Can not remap registers!\n");
    };

    // Set power management
    pm_runtime_enable(&pdev->dev);
    pm_runtime_get_sync(&pdev->dev);

    // Misdevice init
    dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    dev->miscdev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "feserial-%x", res->start);
    dev->miscdev.fops = &feserial_fops;
    
    platform_set_drvdata(pdev, dev);

    ret = misc_register(&dev->miscdev);
    if (ret < 0) {
        // Throw error
        dev_err(&pdev->dev, "Can not register misc device!\n");
        // Disable Power Management!
        pm_runtime_disable(&pdev->dev);
        return ret;
    }

	return 0;
}

static int feserial_remove(struct platform_device *pdev)
{
	pr_info("Called feserial_remove\n");
    struct uart_dev *dev;

    dev = platform_get_drvdata(pdev);

    // Unregister Misc device!
    misc_deregister(&dev->miscdev);
    // Disable Power Management!
    pm_runtime_disable(&pdev->dev);
    return 0;
}

static struct of_device_id feserial_dt_match[] = {
        { .compatible = "rtrk,serial" },
        { },
};

static struct platform_driver feserial_driver = {
        .driver = {
                .name = "feserial",
                .owner = THIS_MODULE,
                .of_match_table = of_match_ptr(feserial_dt_match),
        },
        .probe = feserial_probe,
        .remove = feserial_remove,
};

module_platform_driver(feserial_driver);
MODULE_LICENSE("GPL");
