#include <asm-generic/errno-base.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/miscdevice.h>
#include <linux/resource.h>
#include <linux/pm_runtime.h>
#include <asm/io.h>
#include <linux/clk.h>
// Offset values defined in -> bus.h and serial.h
#include <linux/amba/bus.h>
#include <linux/amba/serial.h>


#define BAUDRATE 115200

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




static int feserial_probe(struct platform_device *pdev)
{
	pr_info("Called feserial_probe\n");
    struct uart_dev *dev;
    struct resource *res;
    struct clk *uart_clk;
    unsigned long baud_divisor;
    unsigned long uartfreq;
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
        return -ENOMEM;
    };

    // Set power management
    pm_runtime_enable(&pdev->dev);
    pm_runtime_get_sync(&pdev->dev);

    // Get frequency from device tree
    uart_clk = devm_clk_get(&pdev->dev, NULL);

    if (!uart_clk) {
        dev_err(&pdev->dev, "Could not get uart0 clock.\n");
        return -EBUSY;
    }

    ret = clk_prepare_enable(uart_clk);
    if (ret) {
        dev_err(&pdev->dev, "Unable to enable uart0 clock!\n");
        return ret;
    }
    // Get frequency
    uartfreq = clk_get_rate(uart_clk);
    pr_info("Freq: %lx\n", uartfreq); 
    // Check if bit is set, if set turn it off.
    if (reg_read(dev, UART011_CR) & UART01x_CR_UARTEN) {
        reg_write(dev, reg_read(dev, UART011_CR) & (~UART01x_CR_UARTEN), UART011_CR);
    }
    // Calculate baud_divisor

    baud_divisor = uartfreq / (16 * BAUDRATE);
    pr_info("Freq: %lx\n", baud_divisor);
    // Write integer value
    reg_write(dev, (baud_divisor >> 6), UART011_IBRD);

    // Write fraction value
    reg_write(dev, (baud_divisor & 0x3F), UART011_FBRD);

    // Enable UART011
    reg_write(dev, reg_read(dev, UART011_CR) | UART01x_CR_UARTEN, UART011_CR);

    // Enable RX
    reg_write(dev, reg_read(dev, UART011_CR) | UART011_CR_RXE, UART011_CR);

    // Enable TX
    reg_write(dev, reg_read(dev, UART011_CR) | UART011_CR_TXE, UART011_CR);
	return 0;
}

static int feserial_remove(struct platform_device *pdev)
{
	pr_info("Called feserial_remove\n");
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
