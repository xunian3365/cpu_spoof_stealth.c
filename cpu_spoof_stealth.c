#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kprobes.h>
#include <linux/version.h>
#include <linux/string.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>
#include <linux/kobject.h>
#include <asm/cputype.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stealth_Spoof");
MODULE_DESCRIPTION("CPU Spoof + Full Self-Hiding (lsmod invisible)");
// 重要：请将下面的 vermagic 替换为你手机 uname -r 的完整输出
MODULE_INFO(vermagic, "5.10.252-dirty SMP preempt mod_unload aarch64");

// ==================== 伪装硬件参数 Google Tensor G2 ====================
#define FAKE_MIDR 0x4e000004UL

static const char FAKE_CPUINFO[] =
"Processor\t: AArch64 Processor rev 0 (aarch64)\n"
"Features\t: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp\n"
"CPU implementer\t: 0x4e\n"
"CPU architecture: 8\n"
"CPU variant\t: 0x2\n"
"CPU part\t: 0x004\n"
"CPU revision\t: 0\n"
"\n"
"processor\t: 0\nBogoMIPS\t: 48.00\nCPU implementer\t: 0x4e\nCPU architecture: 8\nCPU variant\t: 0x0\nCPU part\t: 0x003\nCPU revision\t: 0\n"
"processor\t: 1\nBogoMIPS\t: 48.00\nCPU implementer\t: 0x4e\nCPU architecture: 8\nCPU variant\t: 0x0\nCPU part\t: 0x003\nCPU revision\t: 0\n"
"processor\t: 2\nBogoMIPS\t: 48.00\nCPU implementer\t: 0x4e\nCPU architecture: 8\nCPU variant\t: 0x0\nCPU part\t: 0x003\nCPU revision\t: 0\n"
"processor\t: 3\nBogoMIPS\t: 48.00\nCPU implementer\t: 0x4e\nCPU architecture: 8\nCPU variant\t: 0x0\nCPU part\t: 0x003\nCPU revision\t: 0\n"
"processor\t: 4\nBogoMIPS\t: 48.00\nCPU implementer\t: 0x4e\nCPU architecture: 8\nCPU variant\t: 0x1\nCPU part\t: 0x004\nCPU revision\t: 0\n"
"processor\t: 5\nBogoMIPS\t: 48.00\nCPU implementer\t: 0x4e\nCPU architecture: 8\nCPU variant\t: 0x1\nCPU part\t: 0x004\nCPU revision\t: 0\n"
"processor\t: 6\nBogoMIPS\t: 48.00\nCPU implementer\t: 0x4e\nCPU architecture: 8\nCPU variant\t: 0x2\nCPU part\t: 0x004\nCPU revision\t: 0\n"
"processor\t: 7\nBogoMIPS\t: 48.00\nCPU implementer\t: 0x4e\nCPU architecture: 8\nCPU variant\t: 0x2\nCPU part\t: 0x004\nCPU revision\t: 0\n"
"Hardware\t: Google Tensor G2 (GS201)\n";

static DEFINE_SPINLOCK(spoof_lock);
static bool midr_hooked = false;
static bool cpuinfo_hooked = false;

/* ========== 1. MIDR 底层拦截 ========== */
static int midr_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    if (regs)
        regs->regs[0] = FAKE_MIDR;
    return 0;
}
static struct kretprobe midr_probe = {
    .handler = midr_ret,
    .maxactive = 64,
};

/* ========== 2. cpuinfo 输出篡改 ========== */
static int cpuinfo_entry(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    if (!regs) return 0;
    ri->data = (void *)regs->regs[0];
    return 0;
}
static int cpuinfo_ret(struct kretprobe_instance *ri, struct pt_regs *regs)
{
    struct seq_file *m = (struct seq_file *)ri->data;
    unsigned long flags;
    if (!m) return 0;

    spin_lock_irqsave(&spoof_lock, flags);
    m->count = 0;
    seq_puts(m, FAKE_CPUINFO);
    spin_unlock_irqrestore(&spoof_lock, flags);
    return 0;
}
static struct kretprobe cpuinfo_probe = {
    .handler = cpuinfo_ret,
    .entry_handler = cpuinfo_entry,
    .data_size = sizeof(struct seq_file *),
    .maxactive = 64,
};

/* ========== 3. 符号查找工具 ========== */
struct sym_ctx {
    const char *name;
    unsigned long addr;
};
static int __find_cb(void *priv, const char *sym, unsigned long addr)
{
    struct sym_ctx *ctx = priv;
    if (!strcmp(sym, ctx->name)) {
        ctx->addr = addr;
        return 1;
    }
    return 0;
}
static unsigned long find_sym(const char *name)
{
    struct sym_ctx ctx = { .name = name, .addr = 0 };
    kallsyms_on_each_symbol(__find_cb, &ctx);
    return ctx.addr;
}

/* ========== 4. 模块自隐藏（核心隐身） ========== */
static void hide_self(void)
{
    list_del_init(&THIS_MODULE->list);
    kobject_del(&THIS_MODULE->mkobj.kobj);
    pr_info("[Stealth] Module self-hidden.\n");
}

/* ========== 5. 卸载后门（/proc/spoof_unload） ========== */
static struct proc_dir_entry *unload_entry;

static ssize_t unload_write(struct file *file, const char __user *buf, size_t len, loff_t *pos)
{
    char data[4];
    if (copy_from_user(data, buf, min(len, sizeof(data)-1))) return -EFAULT;
    if (data[0] == '1') {
        pr_info("[Stealth] Unload triggered.\n");
        synchronize_kprobes();
        if (cpuinfo_hooked) unregister_kretprobe(&cpuinfo_probe);
        if (midr_hooked) unregister_kretprobe(&midr_probe);
        list_add(&THIS_MODULE->list, &__this_module.list);
        proc_remove(unload_entry);
        module_put_and_exit(THIS_MODULE, 0);
    }
    return len;
}
static const struct proc_ops unload_ops = {
    .proc_write = unload_write,
};

/* ========== 6. 加载与卸载 ========== */
static int __init stealth_init(void)
{
    int rc;
    unsigned long sym;

    // 挂载 MIDR 钩子
    sym = find_sym("read_cpuid_midr");
    if (sym) {
        midr_probe.kp.symbol_name = "read_cpuid_midr";
        rc = register_kretprobe(&midr_probe);
        if (rc == 0) { midr_hooked = true; pr_info("[Stealth] MIDR hooked\n"); }
    }

    // 挂载 cpuinfo 钩子
    sym = find_sym("cpuinfo_show");
    if (!sym) { pr_err("[Stealth] cpuinfo_show missing\n"); goto fail; }
    cpuinfo_probe.kp.symbol_name = "cpuinfo_show";
    rc = register_kretprobe(&cpuinfo_probe);
    if (rc != 0) { pr_err("[Stealth] cpuinfo hook failed\n"); goto fail; }
    cpuinfo_hooked = true;
    pr_info("[Stealth] cpuinfo_show hooked\n");

    // 创建卸载后门
    unload_entry = proc_create("spoof_unload", 0222, NULL, &unload_ops);

    // 执行隐身（lsmod 消失）
    hide_self();

    pr_info("[Stealth] Module loaded. To unload: echo 1 > /proc/spoof_unload\n");
    return 0;

fail:
    synchronize_kprobes();
    if (midr_hooked) unregister_kretprobe(&midr_probe);
    return -ENOENT;
}

static void __exit stealth_exit(void)
{
    // 实际由 proc 后门触发
}

module_init(stealth_init);
module_exit(stealth_exit);