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
#include <linux/interrupt.h>
// Offset values defined in -> bus.h and serial.h
#include <linux/amba/bus.h>
#include <linux/amba/serial.h>
#include <linux/wait.h>
#include <linux/spinlock.h>

#define BUFFER_SIZE 128

// Driver structure
struct uart_dev {
    void __iomem *regs;
    struct miscdevice miscdev;
    // TO DO
    int char_counter;
    int irq;
    wait_queue_head_t wait_queue;

    // buffer
    int buffer_index;
    char buffer[BUFFER_SIZE];

    spinlock_t lock;
};

enum feserial_state{SERIAL_RESET_COUNTER = 0, SERIAL_GET_COUNTER = 1};

static unsigned reg_read(struct uart_dev *dev, int offset)
{
    return readl(dev->regs + offset);
}

static void reg_write(struct uart_dev *dev, int value, int offset)
{
    writel(value, dev->regs + offset);
}

static void feserial_write_one_char(struct uart_dev *dev, char c){
    while ((reg_read(dev, UART01x_FR) & UART011_FR_TXFE) != 0) cpu_relax();

    reg_write(dev, c, UART01x_DR);

    dev->char_counter++;
}

//TODO feserial_write
static ssize_t feserial_write(struct file *file, const char __user *buf, size_t sz, loff_t *ppos)
{
    struct uart_dev *dev;
    unsigned char user_data[sz];
    int i;
    dev = container_of(file->private_data, struct uart_dev, miscdev);

    // Copy buffer from user_space to user_data

    if (copy_from_user(user_data, buf, sz)) return -EFAULT; 
    // Write data, check if it is \n
    for (i = 0; i < sz; i++)
    {
        feserial_write_one_char(dev, user_data[i]);
        if (user_data[i] == '\n') feserial_write_one_char(dev, '\r');

    }
    return -EINVAL;
}

//TODO feserial_read
static ssize_t feserial_read(struct file *file, char __user *buf, size_t sz, loff_t *ppos)
{
    struct uart_dev *dev;
    dev = container_of(file->private_data, struct uart_dev, miscdev);
    
    // Wait until buffer starts to fill
    wait_event_interruptible(dev->wait_queue, dev->buffer_index > 0);

    spin_lock(&dev->lock);
    if (dev->buffer_index == 0){
        return 0;
    }
    if (copy_to_user(buf, dev->buffer, dev->buffer_index)) {
        return -EFAULT;
    }

    memset(dev->buffer, 0, BUFFER_SIZE);
    dev->buffer_index = 0;
    spin_unlock(&dev->lock);

    return dev->buffer_index;
}


// ioctl function

static long feserial_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){
    struct uart_dev *dev;
    dev = container_of(filp->private_data, struct uart_dev, miscdev);
    void __user *argp = (void __user*) arg;

    switch (cmd) {
        case SERIAL_RESET_COUNTER:
            dev->char_counter = 0;
        break;
        case SERIAL_GET_COUNTER:
            if(copy_to_user(argp, &dev->char_counter, sizeof(int))) return -EFAULT;
        break;

        default:
            return -EFAULT;
    
    }

    return 0;
}

static irqreturn_t feserial_irq_handler(int irq, void *dev_id) {

    struct uart_dev *dev = dev_id;
    
    char recieved_char = reg_read(dev, UART01x_DR);
   spin_lock(&dev->lock); 
    if (dev->buffer_index < BUFFER_SIZE) {
        dev->buffer[dev->buffer_index] = recieved_char;
        dev->buffer_index++;
    } else {
        pr_info("Buffer overflow!\n");
    }
    spin_unlock(&dev->lock);
    wake_up_interruptible(&dev->wait_queue);
    
    return IRQ_HANDLED;
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
        return -EBUSY;
    };
    
    // Print base address
    dev_info(&pdev->dev, "Base address: %X", res->start);
    // Remap resources 
    dev->regs = devm_ioremap_resource(&pdev->dev, res);
    if (!dev->regs) {
        dev_err(&pdev->dev, "Can not remap registers!\n");
    };

    dev->char_counter = 0;
    dev->buffer_index = 0;

    // Initialize spinlock
    spin_lock_init(&dev->lock);

    // Initialize wait_queue
    init_waitqueue_head(&dev->wait_queue);

    // Set power management
    pm_runtime_enable(&pdev->dev);
    pm_runtime_get_sync(&pdev->dev);
   
    // Enable interrupts
    reg_write(dev, UART011_RXIM, UART011_CR);
    
    // Get and register interrupts
    
    dev->irq = platform_get_irq(pdev, 0);
    
    if (dev->irq < 0) {
        dev_err(&pdev->dev, "Can not register IRQ handler!\n");
        return -EINVAL;
    }

    ret = devm_request_irq(&pdev->dev, dev->irq, feserial_irq_handler, 0, dev_name(&pdev->dev), dev);
    
    if (ret < 0) {
        dev_err(&pdev->dev, "Can not register IRQ handler!\n");
        return ret;
    }

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
