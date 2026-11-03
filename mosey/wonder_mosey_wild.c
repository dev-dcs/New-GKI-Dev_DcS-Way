// SPDX-License-Identifier: GPL-2.0
/*
 * wonder_mosey_wild - fake "wonder" wiphy for Mosey (Quick Share / AirDrop)
 *
 * mosey_server wants a phy called "wonder" it can hang a monitor
 * interface off (wonder0), tune to ch149 and poke with Broadcom-style
 * vendor commands (OUI 0x001A11, subcmds 1..5). Pixel 9/10 get that from
 * bcmdhd's wondertap, everything else gets this.
 *
 * Interface follows thelok1s/mosey-extended's README. This is a plain
 * cfg80211 driver on purpose: phones ship cfg80211 as a vendor module but
 * most never load mac80211, so depending on it would rule out half the
 * devices out there.
 *
 * No radio behind it. Frames sent on wonder0 are dropped, nothing is ever
 * received. It only exists so mosey_server's bring-up completes.
 *
 * Pixel 10 series (blazer) has native wondertap in the BCM4398 (bcmdhd), so
 * there this driver is a no-op fallback - it loads only on devices that have
 * no real wonder phy. The 5 GHz table leads with ch149/5745, the channel
 * Quick Share / mosey uses, so an emulated phy advertises it first.
 *
 * @Dev_DcS
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <net/cfg80211.h>
#include <net/netlink.h>

#define WONDER_OUI		0x001A11
#define WONDER_MAX_IFACES	4

enum wonder_subcmd {
	WONDER_SET_FREQ		= 1,
	WONDER_SET_FILTER	= 2,
	WONDER_SET_FIXED_TX_RATE = 3,
	WONDER_SET_REG		= 4,
	WONDER_GET_IF_MAC_ADDR	= 5,
};

/*
 * 5.12 moved interface add/del under the wiphy mutex and gave drivers
 * cfg80211_{,un}register_netdevice() for it.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
#define WONDER_WIPHY_MTX 1
#endif

static struct wiphy *wonder_wiphy;

/* ---- module params ---------------------------------------------------- */

static DEFINE_SPINLOCK(wonder_mac_lock);
static u8 wonder_mac[ETH_ALEN] = { 0x6a, 0xb0, 0x5d, 0xc7, 0x27, 0x3d };

static void wonder_get_mac(u8 *out)
{
	spin_lock_bh(&wonder_mac_lock);
	ether_addr_copy(out, wonder_mac);
	spin_unlock_bh(&wonder_mac_lock);
}

static int wonder_mac_set(const char *val, const struct kernel_param *kp)
{
	char buf[24];	/* "xx:xx:xx:xx:xx:xx" plus whatever echo tacks on */
	u8 mac[ETH_ALEN];

	if (strscpy(buf, val, sizeof(buf)) < 0)
		return -EINVAL;
	if (!mac_pton(strim(buf), mac))
		return -EINVAL;
	/* unicast + locally administered, same rule mosey-extended documents */
	if (!is_valid_ether_addr(mac) || !(mac[0] & 0x02))
		return -EINVAL;

	spin_lock_bh(&wonder_mac_lock);
	ether_addr_copy(wonder_mac, mac);
	spin_unlock_bh(&wonder_mac_lock);
	return 0;
}

static int wonder_mac_get(char *buf, const struct kernel_param *kp)
{
	u8 mac[ETH_ALEN];

	wonder_get_mac(mac);
	return sysfs_emit(buf, "%pM\n", mac);
}

static const struct kernel_param_ops wonder_mac_ops = {
	.set = wonder_mac_set,
	.get = wonder_mac_get,
};
module_param_cb(mac_addr, &wonder_mac_ops, NULL, 0644);
MODULE_PARM_DESC(mac_addr,
	"MAC handed back for vendor subcmd 5 (locally administered, default 6a:b0:5d:c7:27:3d)");

/*
 * Nobody outside Google knows how mosey_server unpacks the subcmd 5 reply.
 * 0 = bare 6 bytes in VENDOR_DATA, 1 = wrapped in nl attribute 1.
 * Writable so it can be flipped on a live device without rebuilding.
 */
static int mac_reply_fmt;
module_param(mac_reply_fmt, int, 0644);
MODULE_PARM_DESC(mac_reply_fmt, "Subcmd 5 reply: 0 = raw bytes (default), 1 = nla type 1");

/* ---- monitor netdev --------------------------------------------------- */

static netdev_tx_t wonder_xmit(struct sk_buff *skb, struct net_device *dev)
{
	dev->stats.tx_dropped++;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static const struct net_device_ops wonder_netdev_ops = {
	.ndo_start_xmit = wonder_xmit,
};

static void wonder_netdev_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->type = ARPHRD_IEEE80211_RADIOTAP;
	dev->header_ops = NULL;
	dev->hard_header_len = 0;
	dev->netdev_ops = &wonder_netdev_ops;
	dev->needs_free_netdev = true;
	dev->priv_flags |= IFF_NO_QUEUE;
	dev->flags &= ~IFF_MULTICAST;
}

static struct wireless_dev *wonder_add_iface(struct wiphy *wiphy, const char *name,
					     unsigned char name_assign_type,
					     enum nl80211_iftype type,
					     struct vif_params *params)
{
	struct wireless_dev *wdev;
	struct net_device *dev;
	u8 mac[ETH_ALEN];
	int count = 0;
	int err;

	if (type != NL80211_IFTYPE_MONITOR)
		return ERR_PTR(-EOPNOTSUPP);

	list_for_each_entry(wdev, &wiphy->wdev_list, list)
		count++;
	if (count >= WONDER_MAX_IFACES)
		return ERR_PTR(-EBUSY);

	dev = alloc_netdev(sizeof(*wdev), name, name_assign_type, wonder_netdev_setup);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	wonder_get_mac(mac);
	eth_hw_addr_set(dev, mac);

	wdev = netdev_priv(dev);
	wdev->wiphy = wiphy;
	wdev->iftype = type;
	wdev->netdev = dev;
	dev->ieee80211_ptr = wdev;
	SET_NETDEV_DEV(dev, wiphy_dev(wiphy));

#ifdef WONDER_WIPHY_MTX
	err = cfg80211_register_netdevice(dev);
#else
	err = register_netdevice(dev);
#endif
	if (err) {
		free_netdev(dev);
		return ERR_PTR(err);
	}

	netif_carrier_on(dev);
	pr_info("%s up on %s\n", dev->name, wiphy_name(wiphy));
	return wdev;
}

/* caller holds RTNL (and the wiphy mutex on 5.12+) */
static void wonder_drop_iface(struct wireless_dev *wdev)
{
	if (!wdev->netdev)
		return;
#ifdef WONDER_WIPHY_MTX
	cfg80211_unregister_netdevice(wdev->netdev);
#else
	unregister_netdevice(wdev->netdev);
#endif
}

static int wonder_del_iface(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	wonder_drop_iface(wdev);
	return 0;
}

static int wonder_change_iface(struct wiphy *wiphy, struct net_device *dev,
			       enum nl80211_iftype type, struct vif_params *params)
{
	if (type != NL80211_IFTYPE_MONITOR)
		return -EOPNOTSUPP;
	/* cfg80211 WARNs if the driver says ok but iftype didn't follow */
	dev->ieee80211_ptr->iftype = type;
	return 0;
}

/* 6.13 started passing the netdev in (android16-6.12 still has the old form) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
static int wonder_set_monitor_channel(struct wiphy *wiphy, struct net_device *dev,
				      struct cfg80211_chan_def *chandef)
#else
static int wonder_set_monitor_channel(struct wiphy *wiphy,
				      struct cfg80211_chan_def *chandef)
#endif
{
	pr_debug("monitor channel %u MHz\n", chandef->chan->center_freq);
	return 0;
}

static const struct cfg80211_ops wonder_cfg_ops = {
	.add_virtual_intf	= wonder_add_iface,
	.del_virtual_intf	= wonder_del_iface,
	.change_virtual_intf	= wonder_change_iface,
	.set_monitor_channel	= wonder_set_monitor_channel,
};

/* ---- vendor commands -------------------------------------------------- */

static int wonder_vendor_ack(struct wiphy *wiphy, struct wireless_dev *wdev,
			     const void *data, int len)
{
	/* subcmds 1-4 only configure hardware that isn't there */
	return 0;
}

static int wonder_vendor_get_mac(struct wiphy *wiphy, struct wireless_dev *wdev,
				 const void *data, int len)
{
	struct sk_buff *skb;
	u8 mac[ETH_ALEN];

	wonder_get_mac(mac);

	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, nla_total_size(ETH_ALEN));
	if (!skb)
		return -ENOMEM;

	if (READ_ONCE(mac_reply_fmt) == 1) {
		if (nla_put(skb, 1, ETH_ALEN, mac)) {
			kfree_skb(skb);
			return -EMSGSIZE;
		}
	} else {
		skb_put_data(skb, mac, ETH_ALEN);
	}

	return cfg80211_vendor_cmd_reply(skb);
}

#define WONDER_VCMD(_sub, _fn) {				\
	.info	= { .vendor_id = WONDER_OUI, .subcmd = (_sub) },\
	.doit	= (_fn),					\
	.policy	= VENDOR_CMD_RAW_DATA,				\
}

/*
 * No NEED_WDEV flag: whether mosey_server targets the phy or wonder0
 * isn't documented, and without the flag nl80211 takes either.
 */
static const struct wiphy_vendor_command wonder_vendor_cmds[] = {
	WONDER_VCMD(WONDER_SET_FREQ, wonder_vendor_ack),
	WONDER_VCMD(WONDER_SET_FILTER, wonder_vendor_ack),
	WONDER_VCMD(WONDER_SET_FIXED_TX_RATE, wonder_vendor_ack),
	WONDER_VCMD(WONDER_SET_REG, wonder_vendor_ack),
	WONDER_VCMD(WONDER_GET_IF_MAC_ADDR, wonder_vendor_get_mac),
};

/* ---- bands ------------------------------------------------------------ */

#define CHAN2G(_ch, _freq) { .band = NL80211_BAND_2GHZ, .center_freq = (_freq), \
			     .hw_value = (_ch), .max_power = 20 }
#define CHAN5G(_ch, _freq) { .band = NL80211_BAND_5GHZ, .center_freq = (_freq), \
			     .hw_value = (_ch), .max_power = 20 }

static struct ieee80211_channel wonder_chan_2g[] = {
	CHAN2G(1, 2412), CHAN2G(6, 2437), CHAN2G(11, 2462),
};

static struct ieee80211_channel wonder_chan_5g[] = {
	/* ch149/5745 first: the channel Quick Share / mosey (Pixel "blazer") uses */
	CHAN5G(149, 5745), CHAN5G(153, 5765), CHAN5G(157, 5785), CHAN5G(161, 5805),
	CHAN5G(36, 5180), CHAN5G(44, 5220),
};

static struct ieee80211_rate wonder_rates_2g[] = {
	{ .bitrate = 10 }, { .bitrate = 20 }, { .bitrate = 55 }, { .bitrate = 110 },
	{ .bitrate = 60 }, { .bitrate = 120 }, { .bitrate = 240 }, { .bitrate = 540 },
};

/* 5 GHz has no CCK, so it gets the OFDM tail of the same table */
static struct ieee80211_supported_band wonder_band_2g = {
	.band		= NL80211_BAND_2GHZ,
	.channels	= wonder_chan_2g,
	.n_channels	= ARRAY_SIZE(wonder_chan_2g),
	.bitrates	= wonder_rates_2g,
	.n_bitrates	= ARRAY_SIZE(wonder_rates_2g),
};

static struct ieee80211_supported_band wonder_band_5g = {
	.band		= NL80211_BAND_5GHZ,
	.channels	= wonder_chan_5g,
	.n_channels	= ARRAY_SIZE(wonder_chan_5g),
	.bitrates	= wonder_rates_2g + 4,
	.n_bitrates	= ARRAY_SIZE(wonder_rates_2g) - 4,
};

/* ---- init / exit ------------------------------------------------------ */

static int __init wonder_init(void)
{
	struct wiphy *wiphy;
	u8 mac[ETH_ALEN];
	int err;

	wiphy = wiphy_new_nm(&wonder_cfg_ops, 0, "wonder");
	if (!wiphy)
		return -ENOMEM;

	wonder_get_mac(mac);
	ether_addr_copy(wiphy->perm_addr, mac);

	wiphy->interface_modes = BIT(NL80211_IFTYPE_MONITOR);
	wiphy->bands[NL80211_BAND_2GHZ] = &wonder_band_2g;
	wiphy->bands[NL80211_BAND_5GHZ] = &wonder_band_5g;
	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;
	wiphy->vendor_commands = wonder_vendor_cmds;
	wiphy->n_vendor_commands = ARRAY_SIZE(wonder_vendor_cmds);
	/* mosey may ask for an "active" monitor; nothing here cares either way */
	wiphy->features |= NL80211_FEATURE_ACTIVE_MONITOR;

	err = wiphy_register(wiphy);
	if (err) {
		pr_err("wiphy_register: %d (is the phy name 'wonder' taken?)\n", err);
		wiphy_free(wiphy);
		return err;
	}

	wonder_wiphy = wiphy;
	pr_info("registered %s, mac %pM\n", wiphy_name(wiphy), mac);
	return 0;
}

static void __exit wonder_exit(void)
{
	struct wireless_dev *wdev, *tmp;
	struct wiphy *wiphy = wonder_wiphy;

	rtnl_lock();
#ifdef WONDER_WIPHY_MTX
	wiphy_lock(wiphy);
#endif
	list_for_each_entry_safe(wdev, tmp, &wiphy->wdev_list, list)
		wonder_drop_iface(wdev);
#ifdef WONDER_WIPHY_MTX
	wiphy_unlock(wiphy);
#endif
	rtnl_unlock();

	wiphy_unregister(wiphy);
	wiphy_free(wiphy);
}

module_init(wonder_init);
module_exit(wonder_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dev_DcS");
MODULE_DESCRIPTION("Virtual 'wonder' cfg80211 phy for Mosey / Quick Share");
MODULE_SOFTDEP("pre: cfg80211");
