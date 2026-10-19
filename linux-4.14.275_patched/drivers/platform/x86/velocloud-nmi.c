#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <misc/velocloud.h>

#include <asm/io.h>
#include <asm/nmi.h>
#include <linux/mc146818rtc.h>

#define GNMI_MM_REG_OFFSET	0x80

spinlock_t dg_vc_bh_lock = __SPIN_LOCK_UNLOCKED(dg_vc_bh_lock);

static volatile pid_t vc_registered_pid = -1;

static void __iomem *ilb_mm = NULL;

extern struct kobject *vc_platform_kobj;

extern struct dmi_system_id *vc_dmi;

static void do_vc_dg_task(unsigned long ignored);

static int nmi_vc_major = -1;

DECLARE_TASKLET(vc_dg_bh, do_vc_dg_task,0);

static ssize_t show_dg_pwr_loss(struct device *dev, struct device_attribute *attr, char *buf){
	return 0;
}

static DEVICE_ATTR(dg_pwr_loss, S_IRUGO, show_dg_pwr_loss, NULL);

static ssize_t show_dg_pid(struct device *dev, struct device_attribute *attr, char *buf){
	u32 pid;
        spin_lock_bh(&dg_vc_bh_lock);
        pid = vc_registered_pid;
        spin_unlock_bh(&dg_vc_bh_lock);

	return sprintf(buf, "%d\n", pid);
}

static DEVICE_ATTR(nmi_pid, S_IRUSR , show_dg_pid, NULL);

static ssize_t show_TCO_WDT_timeout(struct device *dev, struct device_attribute *attr, char *buf){
	return 0;
}

static DEVICE_ATTR(tco_wdt_timeout, S_IRUGO, show_TCO_WDT_timeout, NULL);

static ssize_t show_reset_factory_defaults(struct device *dev, struct device_attribute *attr, char *buf){
        return sprintf(buf, "%c\n", CMOS_READ(0x10));
}

static ssize_t set_reset_factory_defaults(struct device *dev, struct device_attribute *attr, const char *buf, size_t size){
        s32 err;
        unsigned long val;

        err = kstrtoul(buf, 10, &val);
        if (err){
                return err;
	}

	// XXX Only allow CMOS clear for now.
	if (val == 0) {
		spin_lock_irq(&rtc_lock);
		CMOS_WRITE(0, 0x10);
		spin_unlock_irq(&rtc_lock);
	} else {
		return -EINVAL;
	}

        return size;
}


static DEVICE_ATTR(reset_factory_default, S_IRUGO, show_reset_factory_defaults, set_reset_factory_defaults);

static void do_vc_dg_task(unsigned long ignored){
	struct siginfo info;
	struct task_struct *task;
	
	rcu_read_lock();

	spin_lock_bh(&dg_vc_bh_lock);

	/* If our registered client is not there, no harm, no foul. */
	if (vc_registered_pid < 0) {
		spin_unlock_bh(&dg_vc_bh_lock);
		rcu_read_unlock();
		return;
	}

	task = pid_task(find_vpid(vc_registered_pid), PIDTYPE_PID);
	spin_unlock_bh(&dg_vc_bh_lock);
	
	memset(&info, 0, sizeof(struct siginfo));

	info.si_code = SI_KERNEL;
	info.si_signo = SIGPWR;

	if (!task) {
		rcu_read_unlock();
		return;
	}
	send_sig_info(SIGPWR, &info, task);
	
	rcu_read_unlock();

	return;
}


static int nmi_dg_notify_userspace(void){
	int sysfs_ret;
	if (CMOS_READ(0x12)) {
		printk(KERN_INFO "!!! Power loss occurred during last\nsystem runtime. !!!\n");

		if (vc_platform_kobj){
			sysfs_ret = sysfs_create_file(vc_platform_kobj, &dev_attr_dg_pwr_loss.attr);
		}

		spin_lock_irq(&rtc_lock);
		CMOS_WRITE(0, 0x12);
		spin_unlock_irq(&rtc_lock);
	}
	return 0;
}

static int nmi_handler(unsigned int ulReason, struct pt_regs *regs){
	u32 ilb_reg;

	if (likely((ilb_mm))) {
		ilb_reg = ioread32(ilb_mm + GNMI_MM_REG_OFFSET);

		/* If the NMI source comes from the NMI pin (GPIO_0) then
		 * write CMOS and handle the nmi interrupt.  Otherwise, 
		 * simply pass off to another handler (if registered). 
		 */
		if ((ilb_reg & 1)) {
			CMOS_WRITE(1, 0x12);

			CMOS_READ(0x12);

			tasklet_schedule(&vc_dg_bh);

			return NMI_HANDLED;
		}
	}
	return NMI_DONE;
}

static int nmi_vc_open(struct inode *inode, struct file *file){
	spin_lock_bh(&dg_vc_bh_lock);

	if (vc_registered_pid >= 0) {

		spin_unlock_bh(&dg_vc_bh_lock);

		return -EACCES;
	}
	
	vc_registered_pid = pid_nr(get_task_pid(current, PIDTYPE_PID));

	spin_unlock_bh(&dg_vc_bh_lock);

        return 0;
}

static int nmi_vc_close(struct inode *inode, struct file *file){
	spin_lock_bh(&dg_vc_bh_lock);

	if (vc_registered_pid == pid_nr(get_task_pid(current, PIDTYPE_PID)))
		vc_registered_pid = -1;
	
	spin_unlock_bh(&dg_vc_bh_lock);

        return 0;
}

static const struct file_operations nmi_vc_fops = {
        .owner          = THIS_MODULE,
        .open           = nmi_vc_open,
        .release        = nmi_vc_close,
};

static const struct file_operations edge510_fops = {
	.owner		= THIS_MODULE,
	.open		= NULL,
	.release	= NULL,
};

static int __init ipi_init(void){
	int rc;
	struct dmi_system_id *dmi;
	unsigned long id = VC_NONE;
	struct pci_dev *dev = NULL;
	u32 pci_ilb_bar, ilb_reg, pci_ilb_len;
	int sysfs_ret;
	// determine velocloud board;

	dmi = vc_dmi;
	if (dmi) {
		pr_info("velocloud-nmi %s\n", dmi->ident);
		id = (unsigned long)dmi->driver_data;
	} else {
		return -EINVAL;
	}

	if (id == VC_EDGE510) {

		if (vc_platform_kobj) {

			if (CMOS_READ(0x10)) {

				spin_lock_irq(&rtc_lock);
				CMOS_WRITE(0, 0x10);
				spin_unlock_irq(&rtc_lock);

				sysfs_ret = sysfs_create_file(vc_platform_kobj, &dev_attr_reset_factory_default.attr);
			}
			if ((nmi_vc_major = register_chrdev (0, "nmi-vc", &edge510_fops)) < 0)
				printk(KERN_WARNING "nmi_vc: Failed to register character device.");

			return 0;
		}
		return -EINVAL;
	} 

	else if (id < VC_EDGE520 || id > VC_EDGE540B){
		return -EINVAL;
	}
	rc = register_nmi_handler(NMI_UNKNOWN, nmi_handler, 0, "vc-dg");
	if (rc){
		return -ENODEV;
	}
	dev = pci_get_device(PCI_VENDOR_ID_INTEL, 0x1f38, NULL);

	if (dev) {
		/* Read the BAR (ILB) */
		pci_read_config_dword(dev, 0x50, &pci_ilb_bar);

		pci_write_config_dword(dev, 0x50, 0xffffffff);

		pci_read_config_dword(dev, 0x50, &pci_ilb_len);

		pci_ilb_len = ~(pci_ilb_len & 0xfffffff0) + 1;

		/* Write original value back + MemEn. */	
		pci_write_config_dword(dev, 0x50, pci_ilb_bar | 1 << 1);

		ilb_mm = ioremap(pci_ilb_bar & 0xfffffff0, pci_ilb_len);

		ilb_reg = ioread32(ilb_mm + GNMI_MM_REG_OFFSET);

		/* NMI asserted on rising edge and enable NMI messages. */
		if ((ilb_reg & 0xf) != 0x6){
			iowrite32(ilb_reg | 0x6, ilb_mm + GNMI_MM_REG_OFFSET);
		}
	}
		
	nmi_dg_notify_userspace();

	if (vc_platform_kobj){
		sysfs_ret = sysfs_create_file(vc_platform_kobj, &dev_attr_nmi_pid.attr);
	}

	if (CMOS_READ(0x11)) {
		spin_lock_irq(&rtc_lock);
		CMOS_WRITE(0, 0x11);
		spin_unlock_irq(&rtc_lock);

		sysfs_ret = sysfs_create_file(vc_platform_kobj, &dev_attr_tco_wdt_timeout.attr);
	}

	if (CMOS_READ(0x10)) {
		spin_lock_irq(&rtc_lock);
		CMOS_WRITE(0, 0x10);
		spin_unlock_irq(&rtc_lock);

		sysfs_ret = sysfs_create_file(vc_platform_kobj, &dev_attr_reset_factory_default.attr);
	}

	if ((nmi_vc_major = register_chrdev (0, "nmi-vc", &nmi_vc_fops)) < 0){
                printk(KERN_WARNING "nmi_vc: Failed to register character device.");
	}

	return 0;
}

static void __exit ipi_exit(void){
	struct dmi_system_id *dmi;
	unsigned long id = VC_NONE;

	// determine velocloud board;
	dmi = vc_dmi;

	if (dmi){
		id = (unsigned long)dmi->driver_data;
	}
	if (id == VC_EDGE510) {

		if (vc_platform_kobj) {
			sysfs_remove_file(vc_platform_kobj, &dev_attr_reset_factory_default.attr);
			if (nmi_vc_major >= 0){
                		unregister_chrdev(nmi_vc_major, "nmi-vc");
			}
		}
		return;
	} 
	else if (id < VC_EDGE520 || id > VC_EDGE540B){
		return;
	}
	unregister_nmi_handler(NMI_UNKNOWN, "vc-dg"); 

	iounmap(ilb_mm);

	if (nmi_vc_major >= 0){
                unregister_chrdev(nmi_vc_major, "nmi-vc");
	}
	if (vc_platform_kobj) {
                sysfs_remove_file(vc_platform_kobj, &dev_attr_nmi_pid.attr);
		sysfs_remove_file(vc_platform_kobj, &dev_attr_dg_pwr_loss.attr);
		sysfs_remove_file(vc_platform_kobj, &dev_attr_tco_wdt_timeout.attr);
		sysfs_remove_file(vc_platform_kobj, &dev_attr_reset_factory_default.attr);
	}

	nmi_vc_major = -1;
}

module_init(ipi_init);
module_exit(ipi_exit);

MODULE_AUTHOR("Jordan Rhody");
MODULE_LICENSE("GPL");

