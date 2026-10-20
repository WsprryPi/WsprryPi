#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/completion.h>
#include <linux/dmaengine.h>
#include <linux/dma-direct.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#define PROFILE_LENGTH 66792U
#define TICKS_DMA0_CTRL 0x0
#define TICKS_DMA0_CYCLES 0x4
#define DMA_TICK0_EN 0x0
#define DMA_TICK0_CTRL 0x4
#define DMA_TICK_REQ BIT(0)
#define DMA_TICK_SINGLE BIT(1)
#define DMA_TICK_FINISH_CLEAR BIT(0)

static uint cycles = 511;
module_param(cycles, uint, 0444);
static uint dwell = 19;
module_param(dwell, uint, 0444);
static uint tone = 2;
module_param(tone, uint, 0444);
static uint cancel_ms;
module_param(cancel_ms, uint, 0444);
static bool fail_after_dma;
module_param(fail_after_dma, bool, 0444);
static uint initial_wait_ms = 2000;
module_param(initial_wait_ms, uint, 0444);

struct tone_profile {
	u32 lower_word;
	u32 upper_word;
	u32 lower_count;
};

static const struct tone_profile profiles[] = {
	{232445, 232446, 66312},
	{232444, 232445, 1134},
	{232444, 232445, 2747},
	{232444, 232445, 4360},
};

struct dma_probe {
	struct clk *clk;
	struct dma_chan *chan;
	void __iomem *ticks;
	void __iomem *dma_tick;
	u32 *words;
	dma_addr_t words_dma;
	struct completion done;
	struct delayed_work cancel_work;
	unsigned long original_rate;
	u64 start_ns;
	u64 finish_ns;
	dma_cookie_t cookie;
	u32 writes_at_request;
	u32 writes_at_pause;
	int cancel_result;
	bool exclusive;
	bool submitted;
	bool cancel_requested;
	bool cancelled;
	bool timed_out;
};

struct captured_regmap_read {
	u32 *value;
};

static struct task_struct *capture_task;
static u32 captured_values[4];
static unsigned int captured_count;

static int regmap_read_entry(struct kretprobe_instance *instance,
	struct pt_regs *regs)
{
	struct captured_regmap_read *data =
		(struct captured_regmap_read *)instance->data;

	if (current != READ_ONCE(capture_task))
		return 1;
	data->value = (u32 *)regs_get_kernel_argument(regs, 2);
	return 0;
}

static int regmap_read_return(struct kretprobe_instance *instance,
	struct pt_regs *regs)
{
	struct captured_regmap_read *data =
		(struct captured_regmap_read *)instance->data;
	unsigned int index;

	if (current != READ_ONCE(capture_task) || regs_return_value(regs))
		return 0;
	index = captured_count;
	if (index < ARRAY_SIZE(captured_values)) {
		captured_values[index] = READ_ONCE(*data->value);
		captured_count = index + 1;
	}
	return 0;
}

static int provider_raw_readback(struct dma_probe *probe, u32 *div_int,
	u32 *div_frac, unsigned long *rate)
{
	static struct kretprobe read_probe = {
		.kp.symbol_name = "regmap_read",
		.entry_handler = regmap_read_entry,
		.handler = regmap_read_return,
		.data_size = sizeof(struct captured_regmap_read),
		.maxactive = 8,
	};
	struct kprobe recalc_probe = {
		.symbol_name = "rp1_clock_recalc_rate",
	};
	unsigned long (*recalc)(struct clk_hw *, unsigned long);
	struct clk *parent;
	struct clk_hw *hw;
	unsigned long parent_rate;
	int ret;

	ret = register_kprobe(&recalc_probe);
	if (ret)
		return ret;
	recalc = (void *)recalc_probe.addr;
	unregister_kprobe(&recalc_probe);

	ret = register_kretprobe(&read_probe);
	if (ret)
		return ret;
	hw = __clk_get_hw(probe->clk);
	parent = clk_get_parent(probe->clk);
	if (!hw || !parent) {
		ret = -ENODEV;
		goto out;
	}
	parent_rate = clk_get_rate(parent);
	captured_count = 0;
	WRITE_ONCE(capture_task, current);
	barrier();
	*rate = recalc(hw, parent_rate);
	barrier();
	WRITE_ONCE(capture_task, NULL);
	if (captured_count != 2) {
		ret = -EIO;
		goto out;
	}
	*div_int = captured_values[0];
	*div_frac = captured_values[1];
	out:
	unregister_kretprobe(&read_probe);
	return ret;
}

static void transfer_done(void *arg)
{
	struct dma_probe *probe = arg;

	probe->finish_ns = ktime_get_ns();
	if (READ_ONCE(probe->cancel_requested)) {
		probe->writes_at_pause = PROFILE_LENGTH;
		probe->cancelled = true;
	}
	complete(&probe->done);
}

static void stop_engine(struct dma_probe *probe)
{
	if (probe->dma_tick)
		writel(0, probe->dma_tick + DMA_TICK0_EN);
	if (probe->ticks)
		writel(0, probe->ticks + TICKS_DMA0_CTRL);
	if (probe->chan && probe->submitted) {
		dmaengine_terminate_sync(probe->chan);
		probe->submitted = false;
	}
}

static void cancel_transfer(struct work_struct *work)
{
	struct dma_probe *probe = container_of(to_delayed_work(work),
		struct dma_probe, cancel_work);
	struct dma_tx_state state;
	enum dma_status status;

	status = dmaengine_tx_status(probe->chan, probe->cookie, &state);
	if (status == DMA_IN_PROGRESS && state.residue <=
		PROFILE_LENGTH * sizeof(*probe->words))
		probe->writes_at_request = PROFILE_LENGTH -
			state.residue / sizeof(*probe->words);
	if (!probe->writes_at_request) {
		u64 elapsed_ns = ktime_get_ns() - probe->start_ns;
		u64 estimated = div64_u64(elapsed_ns * 50000000ULL,
			511ULL * NSEC_PER_SEC);
		probe->writes_at_request = min_t(u64, estimated, PROFILE_LENGTH);
	}
	WRITE_ONCE(probe->cancel_requested, true);
	pr_info("rp1_gpclk_dma_probe: cancellation requested after approximately %u writes; draining the finite descriptor\n",
		probe->writes_at_request);
}

static void release_resources(struct dma_probe *probe)
{
	int ret;

	stop_engine(probe);
	cancel_delayed_work_sync(&probe->cancel_work);
	if (probe->clk && probe->exclusive) {
		ret = clk_set_rate(probe->clk, probe->original_rate);
		pr_info("rp1_gpclk_dma_probe: restore requested=%lu observed=%lu result=%d\n",
			probe->original_rate, clk_get_rate(probe->clk), ret);
		clk_rate_exclusive_put(probe->clk);
		probe->exclusive = false;
	}
	if (probe->words)
		dma_free_coherent(probe->chan->device->dev,
			PROFILE_LENGTH * sizeof(*probe->words),
			probe->words, probe->words_dma);
	probe->words = NULL;
	if (probe->chan)
		dma_release_channel(probe->chan);
	probe->chan = NULL;
}

static int rp1_dma_probe(struct platform_device *pdev)
{
	struct dma_slave_config config = {};
	struct dma_async_tx_descriptor *tx;
	const struct tone_profile *profile;
	dma_cookie_t cookie;
	u64 divider_address;
	u64 accumulator = 0;
	u64 elapsed_ns;
	u32 expected_fraction;
	u32 provider_div_int;
	u32 provider_div_frac;
	u32 stable_div_int;
	u32 stable_div_frac;
	unsigned long provider_rate;
	unsigned long stable_rate;
	unsigned long timeout;
	unsigned int i;
	int ret;
	struct dma_probe *probe;

	if (cycles == 0 || cycles > 511 || dwell > 31 ||
		tone >= ARRAY_SIZE(profiles) || cancel_ms > 1000 ||
		initial_wait_ms == 0 || initial_wait_ms > 5000)
		return -EINVAL;

	probe = devm_kzalloc(&pdev->dev, sizeof(*probe), GFP_KERNEL);
	if (!probe)
		return -ENOMEM;
	platform_set_drvdata(pdev, probe);
	init_completion(&probe->done);
	INIT_DELAYED_WORK(&probe->cancel_work, cancel_transfer);

	probe->clk = devm_clk_get(&pdev->dev, "gpclk");
	if (IS_ERR(probe->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(probe->clk), "could not acquire clk_gp0\n");
	ret = clk_rate_exclusive_get(probe->clk);
	if (ret)
		return ret;
	probe->exclusive = true;
	probe->original_rate = clk_get_rate(probe->clk);
	if (probe->original_rate != 50000000) {
		ret = -EINVAL;
		goto fail;
	}

	probe->ticks = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(probe->ticks)) {
		ret = PTR_ERR(probe->ticks);
		probe->ticks = NULL;
		goto fail;
	}
	probe->dma_tick = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(probe->dma_tick)) {
		ret = PTR_ERR(probe->dma_tick);
		probe->dma_tick = NULL;
		goto fail;
	}
	probe->chan = dma_request_chan(&pdev->dev, "tick0");
	if (IS_ERR(probe->chan)) {
		ret = PTR_ERR(probe->chan);
		probe->chan = NULL;
		goto fail;
	}
	ret = of_property_read_u64(pdev->dev.of_node, "divider-dma-address",
		&divider_address);
	if (ret)
		goto fail;
	dev_info(&pdev->dev, "divider CPU address=%#llx DMA address=%#llx\n",
		divider_address,
		(unsigned long long)phys_to_dma(probe->chan->device->dev,
			(phys_addr_t)divider_address));
	ret = clk_set_rate(probe->clk, 14097098);
	if (ret)
		goto fail;

	probe->words = dma_alloc_coherent(probe->chan->device->dev,
		PROFILE_LENGTH * sizeof(*probe->words), &probe->words_dma, GFP_KERNEL);
	if (!probe->words) {
		ret = -ENOMEM;
		goto fail;
	}
	profile = &profiles[tone];
	for (i = 0; i < PROFILE_LENGTH; ++i) {
		accumulator += profile->lower_count;
		if (accumulator >= PROFILE_LENGTH) {
			probe->words[i] = (profile->lower_word & 0xffff) << 16;
			accumulator -= PROFILE_LENGTH;
		} else {
			probe->words[i] = (profile->upper_word & 0xffff) << 16;
		}
	}

	config.direction = DMA_MEM_TO_DEV;
	config.dst_addr = divider_address;
	config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	config.dst_maxburst = 1;
	ret = dmaengine_slave_config(probe->chan, &config);
	if (ret)
		goto fail;
	tx = dmaengine_prep_slave_single(probe->chan, probe->words_dma,
		PROFILE_LENGTH * sizeof(*probe->words), DMA_MEM_TO_DEV,
		DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!tx) {
		ret = -EIO;
		goto fail;
	}
	tx->callback = transfer_done;
	tx->callback_param = probe;
	cookie = dmaengine_submit(tx);
	ret = dma_submit_error(cookie);
	if (ret)
		goto fail;
	probe->cookie = cookie;
	probe->submitted = true;

	/* GPCLK0 is deliberately neither prepared nor enabled. */
	writel(cycles, probe->ticks + TICKS_DMA0_CYCLES);
	writel(DMA_TICK_FINISH_CLEAR | (dwell << 4),
		probe->dma_tick + DMA_TICK0_CTRL);
	dma_async_issue_pending(probe->chan);
	probe->start_ns = ktime_get_ns();
	writel(DMA_TICK_REQ | DMA_TICK_SINGLE, probe->dma_tick + DMA_TICK0_EN);
	writel(1, probe->ticks + TICKS_DMA0_CTRL);
	if (cancel_ms)
		schedule_delayed_work(&probe->cancel_work,
			msecs_to_jiffies(cancel_ms));
	timeout = wait_for_completion_timeout(&probe->done,
		msecs_to_jiffies(initial_wait_ms));
	if (!timeout) {
		u64 elapsed_ns = ktime_get_ns() - probe->start_ns;
		u64 estimated = div64_u64(elapsed_ns * 50000000ULL,
			511ULL * NSEC_PER_SEC);
		probe->writes_at_request = min_t(u64, estimated, PROFILE_LENGTH);
		probe->timed_out = true;
		WRITE_ONCE(probe->cancel_requested, true);
		pr_info("rp1_gpclk_dma_probe: initial wait timed out after %u ms; draining finite descriptor\n",
			initial_wait_ms);
		timeout = wait_for_completion_timeout(&probe->done,
			msecs_to_jiffies(1000));
	}
	stop_engine(probe);
	cancel_delayed_work_sync(&probe->cancel_work);
	if (!timeout) {
		ret = -ETIMEDOUT;
		goto fail;
	}
	elapsed_ns = probe->finish_ns - probe->start_ns;
	expected_fraction = probe->words[PROFILE_LENGTH - 1];
	if (probe->cancelled) {
		msleep(50);
		ret = provider_raw_readback(probe, &stable_div_int,
			&stable_div_frac, &stable_rate);
		if (ret || stable_div_int != 3 ||
			stable_div_frac != expected_fraction) {
			dev_err(&pdev->dev,
				"post-cancellation stability failed: ret=%d observed=%u:%u:%lu expected=3:%u\n",
				ret, stable_div_int, stable_div_frac, stable_rate,
				expected_fraction);
			ret = ret ?: -EIO;
			goto fail;
		}
		pr_info("rp1_gpclk_dma_probe: cancelled tone=%u elapsed_ns=%llu timed_out=%u result=%d request_writes=%u paused_writes=%u additional_writes=%u stable_int=%u stable_frac=%u stable_rate=%lu\n",
			tone, elapsed_ns, probe->timed_out, probe->cancel_result,
			probe->writes_at_request, probe->writes_at_pause,
			probe->writes_at_pause - probe->writes_at_request,
			stable_div_int, stable_div_frac, stable_rate);
		release_resources(probe);
		return 0;
	}
	if (fail_after_dma) {
		dev_err(&pdev->dev, "injected failure after DMA completion\n");
		ret = -EIO;
		goto fail;
	}
	/* Read the register back through the same DMA-owned path. */
	reinit_completion(&probe->done);
	config.direction = DMA_DEV_TO_MEM;
	config.src_addr = divider_address;
	config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	config.src_maxburst = 1;
	ret = dmaengine_slave_config(probe->chan, &config);
	if (ret)
		goto fail;
	tx = dmaengine_prep_slave_single(probe->chan, probe->words_dma,
		sizeof(*probe->words), DMA_DEV_TO_MEM,
		DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!tx) {
		ret = -EIO;
		goto fail;
	}
	tx->callback = transfer_done;
	tx->callback_param = probe;
	cookie = dmaengine_submit(tx);
	ret = dma_submit_error(cookie);
	if (ret)
		goto fail;
	probe->submitted = true;
	dma_async_issue_pending(probe->chan);
	writel(DMA_TICK_REQ | DMA_TICK_SINGLE, probe->dma_tick + DMA_TICK0_EN);
	writel(1, probe->ticks + TICKS_DMA0_CTRL);
	timeout = wait_for_completion_timeout(&probe->done, msecs_to_jiffies(100));
	stop_engine(probe);
	if (!timeout) {
		ret = -ETIMEDOUT;
		goto fail;
	}
	if (probe->words[0] != expected_fraction) {
		dev_err(&pdev->dev, "fractional-divider readback mismatch: expected=%u observed=%u\n",
			expected_fraction, probe->words[0]);
		ret = -EIO;
		goto fail;
	}
	ret = provider_raw_readback(probe, &provider_div_int, &provider_div_frac,
		&provider_rate);
	if (ret || provider_div_int != 3 || provider_div_frac != expected_fraction) {
		dev_err(&pdev->dev,
			"provider readback failed: ret=%d int=%u frac=%u expected_frac=%u rate=%lu\n",
			ret, provider_div_int, provider_div_frac, expected_fraction,
			provider_rate);
		ret = ret ?: -EIO;
		goto fail;
	}
	pr_info("rp1_gpclk_dma_probe: complete tone=%u words=%u cycles=%u dwell=%u elapsed_ns=%llu final_fraction=%u dma_readback=%u provider_int=%u provider_frac=%u provider_rate=%lu\n",
		tone, PROFILE_LENGTH, cycles, dwell,
		elapsed_ns, expected_fraction, probe->words[0],
		provider_div_int, provider_div_frac, provider_rate);
	release_resources(probe);
	return 0;

fail:
	dev_err(&pdev->dev, "clock-disabled DMA probe failed: %d\n", ret);
	release_resources(probe);
	return ret;
}

static void rp1_dma_remove(struct platform_device *pdev)
{
	struct dma_probe *probe = platform_get_drvdata(pdev);

	release_resources(probe);
}

static const struct of_device_id rp1_dma_of_match[] = {
	{ .compatible = "wsprrypi,rp1-gpclk-dma-clock-only" },
	{}
};
MODULE_DEVICE_TABLE(of, rp1_dma_of_match);

static struct platform_driver rp1_dma_driver = {
	.probe = rp1_dma_probe,
	.remove = rp1_dma_remove,
	.driver = {
		.name = "rp1_gpclk_dma_probe",
		.of_match_table = rp1_dma_of_match,
	},
};
module_platform_driver(rp1_dma_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Clock-disabled RP1 GPCLK tick-paced DMA probe");
