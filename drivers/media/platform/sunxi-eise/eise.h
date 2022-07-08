#ifndef ISE_H
#define ISE_H

/* This head file include all files need for basical
** ise hardware operation, such as read and write.
*/
#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/rmap.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/mm.h>
#include <asm/siginfo.h>
#include <asm/signal.h>
#include <linux/clk/sunxi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "eise_api.h"
#include <linux/regulator/consumer.h>

#define DEVICE_NAME "sunxi_eise"

/* system address */
#define PLL_ISE_CTRL_REG                (0x00D0)
#define EISE_CLK_REG                     (0x06D0)
#define MBUS_CLK_GATING_REG             (0x0804)
#define EISE_BGR_REG                     (0x06DC)

/* ise register */
#define EISE_CTRL_REG                    (0x00)
#define EISE_IN_SIZE                     (0x28)
#define EISE_OUT_SIZE                    (0x38)
#define EISE_ICFG_REG                    (0x04)
#define EISE_OCFG_REG                    (0x08)
#define EISE_INTERRUPT_EN                (0x0c)
#define EISE_TIME_OUT_NUM                (0x3c)

#define EISE_INTERRUPT_STATUS            (0x10)
#define EISE_ERROR_FLAG                  (0x14)
#define EISE_RESET_REG                   (0x88)

#define eise_err(x, arg...) printk(KERN_ERR"[EISE_ERR]"x, ##arg)
#define eise_warn(x, arg...) printk(KERN_WARNING"[EISE_WARN]"x, ##arg)
#define eise_print(x, arg...) printk(KERN_INFO"[EISE]"x, ##arg)
#define eise_debug(x, arg...) printk(KERN_DEBUG"[EISE_DEBUG]"x, ##arg)

#endif

MODULE_LICENSE("Dual BSD/GPL");