/*===========================================================================
FILE:
   GobiUSBNet.c

DESCRIPTION:
   Qualcomm USB Network device Driver
   
FUNCTIONS:
   GobiNetSuspend
   GobiNetResume
   GobiNetDriverBind
   GobiNetDriverUnbind
   GobiUSBNetURBCallback
   GobiUSBNetTXTimeout
   GobiUSBNetAutoPMThread
   GobiUSBNetStartXmit
   GobiUSBNetOpen
   GobiUSBNetStop
   GobiUSBNetProbe
   GobiUSBNetModInit
   GobiUSBNetModExit


Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Code Aurora Forum nor
      the names of its contributors may be used to endorse or promote
      products derived from this software without specific prior written
      permission.


THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
===========================================================================*/

//---------------------------------------------------------------------------
// Include Files
//---------------------------------------------------------------------------

#include "Structs.h"
#include "QMIDevice.h"
#include "QMI.h"
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/module.h>

#include <linux/netdevice.h>
#include <linux/kernel.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/ipv6.h>
//-----------------------------------------------------------------------------
// Definitions
//-----------------------------------------------------------------------------

// Version Information
#define DRIVER_VERSION "Fibocom_Linux_GobiNet_Driver 1.00.00.08"
#define DRIVER_AUTHOR "Fibocom Wireless Inc"
#define DRIVER_DESC "GobiNet"

#define NODECOM_VENDOR_ID			0x1508
#define FIBOCOM_VENDOR_ID			0x2CB7
#define QUALCOMM_VENDOR_ID          0x05C6

#ifdef GHT_FEATURE_MULTI_PDN
//qmap number
uint __read_mostly qmap_num = 0;
static struct sk_buff * ether_to_ip_fixup(struct net_device *dev, struct sk_buff *skb) ;
#endif

// Debug flag
int debug = G_LOG_ERR;

// Allow user interrupts
int interruptible = 1;

// Number of IP packets which may be queued up for transmit
int txQueueLength = 100;

// Class should be created during module init, so needs to be global
static struct class * gpClass;

#ifdef GHT_FEATURE_MULTI_PDN


struct qmap_hdr {
    u8 cd_rsvd_pad;
    u8 mux_id;
    u16 pkt_len;
} __packed;

struct qmap_priv {
	struct net_device *real_dev;
	u8 offset_id;
};

static int qmap_open(struct net_device *dev)
{
	struct qmap_priv *priv = netdev_priv(dev);
	struct net_device *real_dev = priv->real_dev;

	struct usbnet * pDev = netdev_priv( real_dev );
	sGobiUSBNet * pQmapDev = (sGobiUSBNet *)pDev->data[0];

	if (!(priv->real_dev->flags & IFF_UP))
	{
		G_ERR("flag is %04x\n", priv->real_dev->flags);
		return -ENETDOWN;
	}
	if (netif_carrier_ok(real_dev))
	{
		G_INFO("Up interface\n");		
		netif_carrier_on(dev);
	}
	else
	{
		G_INFO("Down reason : %d\n",pQmapDev->mDownReason);
	}
	return 0;
}

static int qmap_stop(struct net_device *pNet)
{
	netif_carrier_off(pNet);
	return 0;
}



static int qmap_start_xmit(struct sk_buff *skb, struct net_device *pNet)
{
    int err;
    struct qmap_priv *priv = netdev_priv(pNet);
    unsigned int len;
    struct qmap_hdr *hdr;
   if (ether_to_ip_fixup(pNet, skb) == NULL) {
      dev_kfree_skb_any (skb);
      return NETDEV_TX_OK;
   }
   
    len = skb->len;
    hdr = (struct qmap_hdr *)skb_push(skb, sizeof(struct qmap_hdr));
    hdr->cd_rsvd_pad = 0;
    hdr->mux_id = 0x81 + priv->offset_id;
    hdr->pkt_len = cpu_to_be16(len);

    skb->dev = priv->real_dev;
    err = dev_queue_xmit(skb);
#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,14 ))
    if (err == NET_XMIT_SUCCESS) {
	pNet->stats.tx_packets++;
	pNet->stats.tx_bytes += skb->len;
    } else {
	pNet->stats.tx_errors++;
    }
#endif

    return err;
}


#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,29 ))
#else
static const struct net_device_ops qmap_netdev_ops = {
	.ndo_open       = qmap_open,
	.ndo_stop       = qmap_stop,
	.ndo_start_xmit = qmap_start_xmit,
};
#endif

static int qmap_register_device(sGobiUSBNet * pDev, u8 offset_id)
{
    struct net_device *real_dev = pDev->mpNetDev->net;
    struct net_device *qmap_net;
    struct qmap_priv *priv;
    int err;
    qmap_net = alloc_etherdev(sizeof(*priv));
    if (!qmap_net)
        return -ENOBUFS;

    SET_NETDEV_DEV(qmap_net, &real_dev->dev);
    priv = netdev_priv(qmap_net);
    priv->offset_id = offset_id;
    priv->real_dev = real_dev;
    sprintf(qmap_net->name, "%s.%d", real_dev->name, offset_id + 1);
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,29 ))
    qmap_net->open = qmap_open;
    qmap_net->stop = qmap_stop;
    qmap_net->hard_start_xmit = qmap_start_xmit;
#else
    qmap_net->netdev_ops = &qmap_netdev_ops;
#endif
    memcpy (qmap_net->dev_addr, real_dev->dev_addr, ETH_ALEN);
    err = register_netdev(qmap_net);
    if (err < 0)
        goto out_free_newdev;
    netif_device_attach (qmap_net);

    pDev->mpQmapNetDev[offset_id] = qmap_net;
    qmap_net->flags |= IFF_NOARP;

    G_INFO("%s\n", qmap_net->name);

    return 0;

out_free_newdev:
    free_netdev(qmap_net);
    return err;
}

static void qmap_unregister_device(sGobiUSBNet * pDev, u8 offset_id) {
    struct net_device *net = pDev->mpQmapNetDev[offset_id];
    if (net != NULL) {
    
        G_INFO("%s\n", net->name);
        netif_carrier_off( net );
        unregister_netdev (net);
        free_netdev(net);
    }
    pDev->mpQmapNetDev[offset_id] = NULL;
}

static ssize_t qmap_num_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct net_device *pNet = to_net_dev(dev);
    struct usbnet * pDev = netdev_priv( pNet );
    sGobiUSBNet * pGobiDev = (sGobiUSBNet *)pDev->data[0];

    return snprintf(buf, PAGE_SIZE, "%d\n", pGobiDev->m_qmap_num);
}

static ssize_t qmap_num_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	struct net_device *pNet = to_net_dev(dev);
	struct usbnet * pDev = netdev_priv( pNet );
	sGobiUSBNet * pQmapDev = (sGobiUSBNet *)pDev->data[0];
	unsigned old_num = pQmapDev->m_qmap_num;
	unsigned new_num = 0;
	int i;
    int result = 0;


	new_num = simple_strtoul(buf, NULL, 0);
   	if(new_num > 0)
   	{   		
         	GobiClearDownReason( pQmapDev, NO_NDIS_CONNECTION );
   	}
	if(new_num == old_num)
	{
		return count;
	}
	else
	{	
		{		
		    for (i = 0; i < old_num; i++) {
		        qmap_unregister_device(pQmapDev, i);
		    }
		}
	    if(new_num !=0 && new_num !=1)
		{
		    for (i = 0; i < new_num; i++) {
		        qmap_register_device(pQmapDev, i);
		    }
		}
	}
    pQmapDev->m_qmap_num = new_num;
	result = QMIWDASetDataFormat (pQmapDev, pQmapDev->m_qmap_num, &pQmapDev->qmap_size);
    G_INFO( "QMIWDASetDataFormat result %d-%s\n", result, __func__);
    pQmapDev->mpNetDev->rx_urb_size = pQmapDev->qmap_size;
	if (result != 0)
	{
	}
	result = QMIWDSBindMuxData(pQmapDev);
    G_INFO( "QMIWDSBindMuxData result %d-%s\n", result, __func__);
    if (result != 0)
    {
    }
	return count;
}

static DEVICE_ATTR(qmap_num, S_IWUSR | S_IRUGO, qmap_num_show, qmap_num_store);

static ssize_t qmap_size_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct net_device *pNet = to_net_dev(dev);
    struct usbnet * pDev = netdev_priv( pNet );
    sGobiUSBNet * pGobiDev = (sGobiUSBNet *)pDev->data[0];

    return snprintf(buf, PAGE_SIZE, "%d\n", pGobiDev->qmap_size);
}

static DEVICE_ATTR(qmap_size, S_IRUGO, qmap_size_show, NULL);

static ssize_t link_state_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct net_device *pNet = to_net_dev(dev);
	struct usbnet * pDev = netdev_priv( pNet );
	sGobiUSBNet * pQmapDev = (sGobiUSBNet *)pDev->data[0];

	return snprintf(buf, PAGE_SIZE, "0x%x\n",  pQmapDev->link_state);
}

static ssize_t link_state_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count) {
	struct net_device *pNet = to_net_dev(dev);
	struct usbnet * pDev = netdev_priv( pNet );
	sGobiUSBNet * pQmapDev = (sGobiUSBNet *)pDev->data[0];
	unsigned qmap_num = pQmapDev->m_qmap_num;
	unsigned link_state = 0;
	unsigned old_link = pQmapDev->link_state;

	link_state = simple_strtoul(buf, NULL, 0);
	if (qmap_num == 1)
		pQmapDev->link_state = !!link_state;
	else if (qmap_num > 1) {
		if (0 < link_state && link_state <= qmap_num)
			pQmapDev->link_state |= (1 << (link_state - 1));
		else if (0x80 < link_state && link_state <= (0x80 + qmap_num))
			pQmapDev->link_state &= ~(1 << ((link_state&0xF) - 1));
	}

	if (old_link != pQmapDev->link_state)
		dev_info(dev, "link_state 0x%x -> 0x%x\n", old_link, pQmapDev->link_state);

	return count;
}

static DEVICE_ATTR(link_state, S_IWUSR | S_IRUGO, link_state_show, link_state_store);

static struct attribute *gobinet_sysfs_attrs[] = {
	&dev_attr_qmap_num.attr,
	&dev_attr_qmap_size.attr,
	&dev_attr_link_state.attr,
	NULL,
};

static struct attribute_group gobinet_sysfs_attr_group = {
	.attrs = gobinet_sysfs_attrs,
};

#endif

static struct sk_buff * ether_to_ip_fixup(struct net_device *dev, struct sk_buff *skb) {
	const struct ethhdr *ehdr;
	
	skb_reset_mac_header(skb);
	ehdr = eth_hdr(skb);
	
	if (ehdr->h_proto == htons(ETH_P_IP)) {
		if (unlikely(skb->len <= (sizeof(struct ethhdr) + sizeof(struct iphdr)))) {
			goto drop_skb;
		}
	}
	else if (ehdr->h_proto == htons(ETH_P_IPV6)) {
		if (unlikely(skb->len <= (sizeof(struct ethhdr) + sizeof(struct ipv6hdr)))) {
			goto drop_skb;
		}
	}
	else {
		G_INFO("%s skb h_proto is %04x\n", dev->name, ntohs(ehdr->h_proto));
		goto drop_skb;
	}

	if (unlikely(skb_pull(skb, ETH_HLEN)))
		return skb;

drop_skb:
	return NULL;
}
#ifdef CONFIG_PM
/*===========================================================================
METHOD:
   GobiNetSuspend (Public Method)

DESCRIPTION:
   Stops QMI traffic while device is suspended

PARAMETERS
   pIntf          [ I ] - Pointer to interface
   powerEvent     [ I ] - Power management event

RETURN VALUE:
   int - 0 for success
         negative errno for failure
===========================================================================*/
int GobiNetSuspend(
   struct usb_interface *     pIntf,
   pm_message_t               powerEvent )
{
   struct usbnet * pDev;
   sGobiUSBNet * pGobiDev;
   
   if (pIntf == 0)
   {
      return -ENOMEM;
   }
   
#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,23 ))
   pDev = usb_get_intfdata( pIntf );
#else
   pDev = (struct usbnet *)pIntf->dev.platform_data;
#endif

   if (pDev == NULL || pDev->net == NULL)
   {
      G_ERR( "failed to get netdevice\n" );
      return -ENXIO;
   }
   
   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
      G_ERR( "failed to get QMIDevice\n" );
      return -ENXIO;
   }

   // Is this autosuspend or system suspend?
   //    do we allow remote wakeup?
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,33 ))
#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,14 ))
   if (pDev->udev->auto_pm == 0)
#else
   if (1)
#endif
#else
   if ((powerEvent.event & PM_EVENT_AUTO) == 0)
#endif
   {
      G_INFO( "device suspended to power level %d\n", 
           powerEvent.event );
      GobiSetDownReason( pGobiDev, DRIVER_SUSPENDED );
   }
   else
   {
      G_INFO( "device autosuspend\n" );
   }
     
   if (powerEvent.event & PM_EVENT_SUSPEND)
   {
      // Stop QMI read callbacks
      KillRead( pGobiDev );
#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,22 ))
      pDev->udev->reset_resume = 0;
#endif
      
      // Store power state to avoid duplicate resumes
      pIntf->dev.power.power_state.event = powerEvent.event;
   }
   else
   {
      // Other power modes cause QMI connection to be lost
#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,22 ))
      pDev->udev->reset_resume = 1;
#endif
   }
   
   // Run usbnet's suspend function
   return usbnet_suspend( pIntf, powerEvent );
}
   
/*===========================================================================
METHOD:
   GobiNetResume (Public Method)

DESCRIPTION:
   Resume QMI traffic or recreate QMI device

PARAMETERS
   pIntf          [ I ] - Pointer to interface

RETURN VALUE:
   int - 0 for success
         negative errno for failure
===========================================================================*/
int GobiNetResume( struct usb_interface * pIntf )
{
   struct usbnet * pDev;
   sGobiUSBNet * pGobiDev;
   int nRet;
   int oldPowerState;
   
   if (pIntf == 0)
   {
      return -ENOMEM;
   }
   
#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,23 ))
   pDev = usb_get_intfdata( pIntf );
#else
   pDev = (struct usbnet *)pIntf->dev.platform_data;
#endif

   if (pDev == NULL || pDev->net == NULL)
   {
      G_ERR( "failed to get netdevice\n" );
      return -ENXIO;
   }
   
   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
      G_ERR( "failed to get QMIDevice\n" );
      return -ENXIO;
   }

   oldPowerState = pIntf->dev.power.power_state.event;
   pIntf->dev.power.power_state.event = PM_EVENT_ON;
   G_INFO( "resuming from power mode %d\n", oldPowerState );

   if (oldPowerState & PM_EVENT_SUSPEND)
   {
      // It doesn't matter if this is autoresume or system resume
      GobiClearDownReason( pGobiDev, DRIVER_SUSPENDED );
   
      nRet = usbnet_resume( pIntf );
      if (nRet != 0)
      {
         G_ERR( "usbnet_resume error %d\n", nRet );
         return nRet;
      }

      // Restart QMI read callbacks
      nRet = StartRead( pGobiDev );
      if (nRet != 0)
      {
         G_ERR( "StartRead error %d\n", nRet );
         return nRet;
      }

#ifdef CONFIG_PM
   #if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,29 ))   
      // Kick Auto PM thread to process any queued URBs
      complete( &pGobiDev->mAutoPM.mThreadDoWork );
    #endif
#endif /* CONFIG_PM */
   }
   else
   {
      G_INFO( "nothing to resume\n" );
      return 0;
   }
   
   return nRet;
}
#endif /* CONFIG_PM */

/*===========================================================================
METHOD:
   GobiNetDriverBind (Public Method)

DESCRIPTION:
   Setup in and out pipes

PARAMETERS
   pDev           [ I ] - Pointer to usbnet device
   pIntf          [ I ] - Pointer to interface

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
static int GobiNetDriverBind( 
   struct usbnet *         pDev, 
   struct usb_interface *  pIntf )
{
   int numEndpoints;
   int endpointIndex;
   struct usb_host_endpoint * pEndpoint = NULL;
   struct usb_host_endpoint * pIn = NULL;
   struct usb_host_endpoint * pOut = NULL;
  
   // Verify one altsetting
   if (pIntf->num_altsetting != 1)
   {
      G_ERR( "invalid num_altsetting %u\n", pIntf->num_altsetting );
      return -ENODEV;
   }

   // Verify correct interface (4 for NL650/NL660)
   if (pDev->udev->descriptor.idVendor == NODECOM_VENDOR_ID 
        && pDev->udev->descriptor.idProduct == cpu_to_le16(0x1001))
   {
        if (pIntf->cur_altsetting->desc.bInterfaceNumber != 4)
            return -ENODEV;
   }
   else if (pDev->udev->descriptor.idVendor == NODECOM_VENDOR_ID 
       && pDev->udev->descriptor.idProduct == cpu_to_le16(0x1000))
   {
       if (pIntf->cur_altsetting->desc.bInterfaceNumber != 2)
            return -ENODEV;
   }
   else if (pDev->udev->descriptor.idVendor == FIBOCOM_VENDOR_ID 
       && pDev->udev->descriptor.idProduct == cpu_to_le16(0x0104))
   {
       if (pIntf->cur_altsetting->desc.bInterfaceNumber != 4)
            return -ENODEV;
       G_INFO( "found gobinet infterface,vid:0x%x infc %d\n",pIntf->cur_altsetting->desc.bInterfaceNumber , pIntf->cur_altsetting->desc.bInterfaceNumber );
   }
   else if (pDev->udev->descriptor.idVendor == FIBOCOM_VENDOR_ID 
       && pDev->udev->descriptor.idProduct == cpu_to_le16(0x0109))
   {
       if (pIntf->cur_altsetting->desc.bInterfaceNumber != 2)
            return -ENODEV;
   }
   else if (pDev->udev->descriptor.idVendor == QUALCOMM_VENDOR_ID 
       && pDev->udev->descriptor.idProduct == cpu_to_le16(0x90DB))
   {
       if (pIntf->cur_altsetting->desc.bInterfaceNumber != 2)
            return -ENODEV;
   }
   else if ( pIntf->cur_altsetting->desc.bInterfaceNumber != 4 && pIntf->cur_altsetting->desc.bInterfaceNumber != 2)
   {
      G_ERR( "invalid interface %d\n", 
           pIntf->cur_altsetting->desc.bInterfaceNumber );
      return -ENODEV;
   }

   SetDeviceInterfaceNumber(pIntf->cur_altsetting->desc.bInterfaceNumber);

   // Collect In and Out endpoints
   numEndpoints = pIntf->cur_altsetting->desc.bNumEndpoints;
   for (endpointIndex = 0; endpointIndex < numEndpoints; endpointIndex++)
   {
      pEndpoint = pIntf->cur_altsetting->endpoint + endpointIndex;
      if (pEndpoint == NULL)
      {
         G_ERR( "invalid endpoint %u\n", endpointIndex );
         return -ENODEV;
      }

      if (usb_endpoint_dir_in( &pEndpoint->desc ) == true
      &&  usb_endpoint_xfer_int( &pEndpoint->desc ) == false)
      {
         pIn = pEndpoint;
      }
      else if (usb_endpoint_dir_out( &pEndpoint->desc ) == true)
      {
         pOut = pEndpoint;
      }
   }
   
   if (pIn == NULL || pOut == NULL)
   {
      G_ERR( "invalid endpoints\n" );
      return -ENODEV;
   }

   if (usb_set_interface( pDev->udev, 
                          pIntf->cur_altsetting->desc.bInterfaceNumber,
                          0 ) != 0)
   {
      G_ERR( "unable to set interface[%d]\n",pIntf->cur_altsetting->desc.bInterfaceNumber );
      return -ENODEV;
   }
   G_INFO( "set interface[%d] ok\n",pIntf->cur_altsetting->desc.bInterfaceNumber );
   pDev->in = usb_rcvbulkpipe( pDev->udev,
                   pIn->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK );
   pDev->out = usb_sndbulkpipe( pDev->udev,
                   pOut->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK );

    /* make MAC addr easily distinguishable from an IP header */
    if ((pDev->net->dev_addr[0] & 0xd0) == 0x40) {
        /*clear this bit wil make usbnet apdater named as usbX(instead if ethX)*/
        pDev->net->dev_addr[0] |= 0x02;	/* set local assignment bit */
        pDev->net->dev_addr[0] &= 0xbf;	/* clear "IP" bit */
    }
                   
   G_INFO( "in %x, out %x\n", 
        pIn->desc.bEndpointAddress, 
        pOut->desc.bEndpointAddress );

   // In later versions of the kernel, usbnet helps with this
#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,23 ))
   pIntf->dev.platform_data = (void *)pDev;
#endif
#ifdef GHT_FEATURE_MULTI_PDN
    G_INFO( "qmap_num %x\n", qmap_num);
    if (pDev->net->sysfs_groups[0] == NULL && gobinet_sysfs_attr_group.attrs[0] != NULL) {
#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,32))
        pDev->net->sysfs_groups[1] = &gobinet_sysfs_attr_group; 
#else
        pDev->net->sysfs_groups[0] = &gobinet_sysfs_attr_group;
#endif
    }

    if (!pDev->rx_urb_size) {
        pDev->rx_urb_size = ETH_DATA_LEN + ETH_HLEN + 6;
    }
#endif
   return 0;
}

/*===========================================================================
METHOD:
   GobiNetDriverUnbind (Public Method)

DESCRIPTION:
   Deregisters QMI device (Registration happened in the probe function)

PARAMETERS
   pDev           [ I ] - Pointer to usbnet device
   pIntfUnused    [ I ] - Pointer to interface

RETURN VALUE:
   None
===========================================================================*/
static void GobiNetDriverUnbind( 
   struct usbnet *         pDev, 
   struct usb_interface *  pIntf)
{
   sGobiUSBNet * pGobiDev = (sGobiUSBNet *)pDev->data[0];

   // Should already be down, but just in case...
   netif_carrier_off( pDev->net );

   DeregisterQMIDevice( pGobiDev );
   
#if (LINUX_VERSION_CODE >= KERNEL_VERSION( 2,6,29 ))
   kfree( pDev->net->netdev_ops );
   pDev->net->netdev_ops = NULL;
#endif

#if (LINUX_VERSION_CODE <= KERNEL_VERSION( 2,6,23 ))
   pIntf->dev.platform_data = NULL;
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION( 2,6,19 ))
   pIntf->needs_remote_wakeup = 0;
#endif

   if (atomic_dec_and_test(&pGobiDev->refcount))
      kfree( pGobiDev );
   else
      G_INFO("memory leak!\n");
}

/*===========================================================================
METHOD:
   GobiNetDriverTxFixup (Public Method)

DESCRIPTION:
   Handling data format mode on transmit path

PARAMETERS
   pDev           [ I ] - Pointer to usbnet device
   pSKB           [ I ] - Pointer to transmit packet buffer
   flags          [ I ] - os flags

RETURN VALUE:
   None
===========================================================================*/
struct sk_buff *GobiNetDriverTxFixup(struct usbnet *dev, struct sk_buff *skb, gfp_t flags)
{
    sGobiUSBNet * pGobiDev = (sGobiUSBNet *)dev->data[0];

    if (!pGobiDev) {
	 G_ERR( "failed to get QMIDevice\n" );
	 dev_kfree_skb_any(skb);
	 return NULL;		
    }

    if (!pGobiDev->mbRawIPMode)
        return skb;
#ifdef GHT_FEATURE_MULTI_PDN
    if (pGobiDev->m_qmap_num ) {
        struct qmap_hdr *qhdr;

        if (unlikely(!pGobiDev->link_state)) {
           //dev_info(&dev->net->dev, "link_state 0x%x, drop skb, len = %u\n", pGobiDev->link_state, skb->len);
           goto drop_skb;
        }

        if (pGobiDev->m_qmap_num > 1) {
            qhdr = (struct qmap_hdr *)skb->data;
            if (qhdr->cd_rsvd_pad != 0) {
                goto drop_skb;
            }
            if ((qhdr->mux_id&0xF0) != 0x80) {
                goto drop_skb;
            } 
        } else {
            if (ether_to_ip_fixup(dev->net, skb) == NULL)
               goto drop_skb;
            qhdr = (struct qmap_hdr *)skb_push(skb, sizeof(struct qmap_hdr));
            qhdr->cd_rsvd_pad = 0;
            qhdr->mux_id = 0x81;
            qhdr->pkt_len = cpu_to_be16(skb->len - sizeof(struct qmap_hdr));
        }

        return skb;
    }
#endif
      
// Skip Ethernet header from message
    if (likely(ether_to_ip_fixup(dev->net, skb))) {
        return skb;
    } else {
#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,22 ))
        dev_err(&dev->intf->dev,  "Packet Dropped ");
#elif (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,18 ))
        dev_err(dev->net->dev.parent,  "Packet Dropped ");
#else
        INFO("Packet Dropped ");
#endif
    }
#ifdef GHT_FEATURE_MULTI_PDN 
drop_skb:
#endif
   // Filter the packet out, release it
   dev_kfree_skb_any(skb);
   return NULL;
}
#ifdef GHT_FEATURE_MULTI_PDN
static int GobiNetDriverRxQmapFixup(struct usbnet *dev, struct sk_buff *skb)
{
    sGobiUSBNet * pGobiDev = (sGobiUSBNet *)dev->data[0];
    static int debug_len = 0;
    int debug_pkts = 0;
    int update_len = skb->len;

    G_INFO( "Rx recived,update_len : %d,%ld\n" ,update_len,sizeof(struct qmap_hdr));
    while (skb->len > sizeof(struct qmap_hdr)) {

        struct qmap_hdr *qhdr = (struct qmap_hdr *)skb->data;
        struct net_device *qmap_net;
        struct sk_buff *qmap_skb;
        __be16 proto;
        int pkt_len;
        u8 offset_id = 0;
        int err;
        unsigned len = (be16_to_cpu(qhdr->pkt_len) + sizeof(struct qmap_hdr));


        if (skb->len < (be16_to_cpu(qhdr->pkt_len) + sizeof(struct qmap_hdr))) {
            G_ERR("drop qmap unknow pkt, len=%d, pkt_len=%d\n", skb->len, be16_to_cpu(qhdr->pkt_len));
            
            PrintHex(skb->data, 16);
            
            goto out;
        }

        debug_pkts++;

        if (qhdr->cd_rsvd_pad & 0x80) {
            G_ERR("drop qmap command packet %x\n", qhdr->cd_rsvd_pad);
            goto skip_pkt;;
        }

        offset_id = qhdr->mux_id - 0x81;
        if (offset_id >= pGobiDev->m_qmap_num) {
            G_ERR("drop qmap unknow mux_id %x,%x\n", qhdr->mux_id,pGobiDev->m_qmap_num);
            goto skip_pkt;
        }

        if (pGobiDev->m_qmap_num > 1) {
            qmap_net = pGobiDev->mpQmapNetDev[offset_id];
        } else {
            qmap_net = dev->net;
        }

        if (qmap_net == NULL) {
            G_ERR("drop qmap unknow mux_id %x\n", qhdr->mux_id);
            goto skip_pkt;
        }
        
        switch (skb->data[sizeof(struct qmap_hdr)] & 0xf0) {
        case 0x40:
        	proto = htons(ETH_P_IP);
        	break;
        case 0x60:
        	proto = htons(ETH_P_IPV6);
        	break;
        default:
        	goto skip_pkt;
        }

        pkt_len = be16_to_cpu(qhdr->pkt_len) - (qhdr->cd_rsvd_pad&0x3F);
        qmap_skb = netdev_alloc_skb(qmap_net, ETH_HLEN + pkt_len);

        skb_reset_mac_header(qmap_skb);
        memcpy(eth_hdr(qmap_skb)->h_dest, qmap_net->dev_addr, ETH_ALEN);
        eth_hdr(qmap_skb)->h_proto = proto;        
        memcpy(skb_put(qmap_skb, ETH_HLEN + pkt_len) + ETH_HLEN, skb->data + sizeof(struct qmap_hdr), pkt_len);

        if (pGobiDev->m_qmap_num > 1) {
            qmap_skb->protocol = eth_type_trans (qmap_skb, qmap_net);
            memset(qmap_skb->cb, 0, sizeof(struct skb_data));
            err = netif_rx(qmap_skb);
#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,14 ))
            if (err == NET_RX_SUCCESS) {
                qmap_net->stats.rx_packets++;
                qmap_net->stats.rx_bytes += qmap_skb->len;
            } else {
                qmap_net->stats.rx_errors++;
            }
#endif
        } else {
            usbnet_skb_return(dev, qmap_skb);
        }

skip_pkt:
        skb_pull(skb, len);
    }

out:
    if (update_len > debug_len) {
        debug_len = update_len;
        G_ERR("rx_pkts=%d, rx_len=%d\n", debug_pkts, debug_len);
    }
    return 0;
}
#endif
/*===========================================================================
METHOD:
   GobiNetDriverRxFixup (Public Method)

DESCRIPTION:
   Handling data format mode on receive path

PARAMETERS
   pDev           [ I ] - Pointer to usbnet device
   pSKB           [ I ] - Pointer to received packet buffer

RETURN VALUE:
   None
===========================================================================*/
static int GobiNetDriverRxFixup(struct usbnet *dev, struct sk_buff *skb)
{
    __be16 proto;
    sGobiUSBNet * pGobiDev = (sGobiUSBNet *)dev->data[0];

    if (!pGobiDev->mbRawIPMode)
        return 1;

    /* This check is no longer done by usbnet */
    if (skb->len < dev->net->hard_header_len)
		return 0;
#ifdef GHT_FEATURE_MULTI_PDN
    if (pGobiDev->m_qmap_num) {
	return GobiNetDriverRxQmapFixup(dev, skb);
    }
#endif
    switch (skb->data[0] & 0xf0) {
    case 0x40:
    	proto = htons(ETH_P_IP);
    	break;
    case 0x60:
    	proto = htons(ETH_P_IPV6);
    	break;
    case 0x00:
    	if (is_multicast_ether_addr(skb->data))
    		return 1;
    	/* possibly bogus destination - rewrite just in case */
    	skb_reset_mac_header(skb);
    	goto fix_dest;
    default:
    	/* pass along other packets without modifications */
    	return 1;
    }
    if (skb_headroom(skb) < ETH_HLEN && pskb_expand_head(skb, ETH_HLEN, 0, GFP_ATOMIC)) {
        G_ERR("%s: couldn't pskb_expand_head\n", __func__);
        return 0;
    }
    skb_push(skb, ETH_HLEN);
    skb_reset_mac_header(skb);
    eth_hdr(skb)->h_proto = proto;
    memset(eth_hdr(skb)->h_source, 0, ETH_ALEN);
fix_dest:
    memcpy(eth_hdr(skb)->h_dest, dev->net->dev_addr, ETH_ALEN);
    return 1;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,29 ))
#ifdef CONFIG_PM
/*===========================================================================
METHOD:
   GobiUSBNetURBCallback (Public Method)

DESCRIPTION:
   Write is complete, cleanup and signal that we're ready for next packet

PARAMETERS
   pURB     [ I ] - Pointer to sAutoPM struct

RETURN VALUE:
   None
===========================================================================*/
#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,14 ))
void GobiUSBNetURBCallback( struct urb * pURB )
#else
void GobiUSBNetURBCallback(struct urb *pURB, struct pt_regs *regs)
#endif
{
   unsigned long activeURBflags;
   sAutoPM * pAutoPM = (sAutoPM *)pURB->context;
   if (pAutoPM == NULL)
   {
      // Should never happen
      G_ERR( "bad context\n" );
      return;
   }

   if (pURB->status != 0)
   {
      // Note that in case of an error, the behaviour is no different
      G_ERR( "urb finished with error %d\n", pURB->status );
   }

   // Remove activeURB (memory to be freed later)
   spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );

   // EAGAIN used to signify callback is done
   pAutoPM->mpActiveURB = ERR_PTR( -EAGAIN );

   spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );

   complete( &pAutoPM->mThreadDoWork );
   
   usb_free_urb( pURB );
}

/*===========================================================================
METHOD:
   GobiUSBNetTXTimeout (Public Method)

DESCRIPTION:
   Timeout declared by the net driver.  Stop all transfers

PARAMETERS
   pNet     [ I ] - Pointer to net device

RETURN VALUE:
   None
===========================================================================*/
void GobiUSBNetTXTimeout( struct net_device * pNet )
{
   struct sGobiUSBNet * pGobiDev;
   sAutoPM * pAutoPM;
   sURBList * pURBListEntry;
   unsigned long activeURBflags, URBListFlags;
   struct usbnet * pDev = netdev_priv( pNet );
   struct urb * pURB;

   if (pDev == NULL || pDev->net == NULL)
   {
      G_ERR( "failed to get usbnet device\n" );
      return;
   }
   
   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
      G_ERR( "failed to get QMIDevice\n" );
      return;
   }
   pAutoPM = &pGobiDev->mAutoPM;

   // Grab a pointer to active URB
   spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );
   pURB = pAutoPM->mpActiveURB;
   spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );
   // Stop active URB
   if (pURB != NULL)
   {
      usb_kill_urb( pURB );
   }

   // Cleanup URB List
   spin_lock_irqsave( &pAutoPM->mURBListLock, URBListFlags );

   pURBListEntry = pAutoPM->mpURBList;
   while (pURBListEntry != NULL)
   {
      pAutoPM->mpURBList = pAutoPM->mpURBList->mpNext;
      atomic_dec( &pAutoPM->mURBListLen );
      usb_free_urb( pURBListEntry->mpURB );
      kfree( pURBListEntry );
      pURBListEntry = pAutoPM->mpURBList;
   }

   spin_unlock_irqrestore( &pAutoPM->mURBListLock, URBListFlags );

   complete( &pAutoPM->mThreadDoWork );

   return;
}

/*===========================================================================
METHOD:
   GobiUSBNetAutoPMThread (Public Method)

DESCRIPTION:
   Handle device Auto PM state asynchronously
   Handle network packet transmission asynchronously

PARAMETERS
   pData     [ I ] - Pointer to sAutoPM struct

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
static int GobiUSBNetAutoPMThread( void * pData )
{
   unsigned long activeURBflags, URBListFlags;
   sURBList * pURBListEntry;
   int status;
   struct usb_device * pUdev;
   sAutoPM * pAutoPM = (sAutoPM *)pData;
   struct urb * pURB;

   if (pAutoPM == NULL)
   {
      G_ERR( "passed null pointer\n" );
      return -EINVAL;
   }
   
   pUdev = interface_to_usbdev( pAutoPM->mpIntf );

   G_INFO( "traffic thread started\n" );

   while (pAutoPM->mbExit == false)
   {
      // Wait for someone to poke us
      wait_for_completion_interruptible( &pAutoPM->mThreadDoWork );

      // Time to exit?
      if (pAutoPM->mbExit == true)
      {
         // Stop activeURB
         spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );
         pURB = pAutoPM->mpActiveURB;
         spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );

         // EAGAIN used to signify callback is done
         if (IS_ERR( pAutoPM->mpActiveURB )
                 &&  PTR_ERR( pAutoPM->mpActiveURB ) == -EAGAIN )
         {
             pURB = NULL;
         }

         if (pURB != NULL)
         {
            usb_kill_urb( pURB );
         }
         // Will be freed in callback function

         // Cleanup URB List
         spin_lock_irqsave( &pAutoPM->mURBListLock, URBListFlags );

         pURBListEntry = pAutoPM->mpURBList;
         while (pURBListEntry != NULL)
         {
            pAutoPM->mpURBList = pAutoPM->mpURBList->mpNext;
            atomic_dec( &pAutoPM->mURBListLen );
            usb_free_urb( pURBListEntry->mpURB );
            kfree( pURBListEntry );
            pURBListEntry = pAutoPM->mpURBList;
         }

         spin_unlock_irqrestore( &pAutoPM->mURBListLock, URBListFlags );

         break;
      }
      
      // Is our URB active?
      spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );

      // EAGAIN used to signify callback is done
      if (IS_ERR( pAutoPM->mpActiveURB ) 
      &&  PTR_ERR( pAutoPM->mpActiveURB ) == -EAGAIN )
      {
         pAutoPM->mpActiveURB = NULL;

         // Restore IRQs so task can sleep
         spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );
         
         // URB is done, decrement the Auto PM usage count
         usb_autopm_put_interface( pAutoPM->mpIntf );

         // Lock ActiveURB again
         spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );
      }

      if (pAutoPM->mpActiveURB != NULL)
      {
         // There is already a URB active, go back to sleep
         spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );
         continue;
      }
      
      // Is there a URB waiting to be submitted?
      spin_lock_irqsave( &pAutoPM->mURBListLock, URBListFlags );
      if (pAutoPM->mpURBList == NULL)
      {
         // No more URBs to submit, go back to sleep
         spin_unlock_irqrestore( &pAutoPM->mURBListLock, URBListFlags );
         spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );
         continue;
      }

      // Pop an element
      pURBListEntry = pAutoPM->mpURBList;
      pAutoPM->mpURBList = pAutoPM->mpURBList->mpNext;
      atomic_dec( &pAutoPM->mURBListLen );
      spin_unlock_irqrestore( &pAutoPM->mURBListLock, URBListFlags );

      // Set ActiveURB
      pAutoPM->mpActiveURB = pURBListEntry->mpURB;
      spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );

      // Tell autopm core we need device woken up
      status = usb_autopm_get_interface( pAutoPM->mpIntf );
      if (status < 0)
      {
         G_ERR( "unable to autoresume interface: %d\n", status );

         // likely caused by device going from autosuspend -> full suspend
         if (status == -EPERM)
         {
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,33 ))
#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,14 ))
            pUdev->auto_pm = 0;
#else
             pUdev = pUdev;
#endif
#endif
            GobiNetSuspend( pAutoPM->mpIntf, PMSG_SUSPEND );
         }

         // Add pURBListEntry back onto pAutoPM->mpURBList
         spin_lock_irqsave( &pAutoPM->mURBListLock, URBListFlags );
         pURBListEntry->mpNext = pAutoPM->mpURBList;
         pAutoPM->mpURBList = pURBListEntry;
         atomic_inc( &pAutoPM->mURBListLen );
         spin_unlock_irqrestore( &pAutoPM->mURBListLock, URBListFlags );
         
         spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );
         pAutoPM->mpActiveURB = NULL;
         spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );
         
         // Go back to sleep
         continue;
      }

      // Submit URB
      status = usb_submit_urb( pAutoPM->mpActiveURB, GFP_KERNEL );
      if (status < 0)
      {
         // Could happen for a number of reasons
         G_ERR( "Failed to submit URB: %d.  Packet dropped\n", status );
         spin_lock_irqsave( &pAutoPM->mActiveURBLock, activeURBflags );
         usb_free_urb( pAutoPM->mpActiveURB );
         pAutoPM->mpActiveURB = NULL;
         spin_unlock_irqrestore( &pAutoPM->mActiveURBLock, activeURBflags );
         usb_autopm_put_interface( pAutoPM->mpIntf );

         // Loop again
         complete( &pAutoPM->mThreadDoWork );
      }
      
      kfree( pURBListEntry );
   }   
   
   G_INFO( "traffic thread exiting\n" );
   pAutoPM->mpThread = NULL;
   return 0;
}      

/*===========================================================================
METHOD:
   GobiUSBNetStartXmit (Public Method)

DESCRIPTION:
   Convert sk_buff to usb URB and queue for transmit

PARAMETERS
   pNet     [ I ] - Pointer to net device

RETURN VALUE:
   NETDEV_TX_OK on success
   NETDEV_TX_BUSY on error
===========================================================================*/
int GobiUSBNetStartXmit( 
   struct sk_buff *     pSKB,
   struct net_device *  pNet )
{
   unsigned long URBListFlags;
   struct sGobiUSBNet * pGobiDev;
   sAutoPM * pAutoPM;
   sURBList * pURBListEntry, ** ppURBListEnd;
   void * pURBData;
   struct usbnet * pDev = netdev_priv( pNet );
      
   if (pDev == NULL || pDev->net == NULL)
   {
      G_ERR( "failed to get usbnet device\n" );
      return NETDEV_TX_BUSY;
   }
   
   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
      G_ERR( "failed to get QMIDevice\n" );
      return NETDEV_TX_BUSY;
   }
   pAutoPM = &pGobiDev->mAutoPM;
   
   if( NULL == pSKB )
   {
       G_ERR( "Buffer is NULL \n" );
       return NETDEV_TX_BUSY;
   }

   if (GobiTestDownReason( pGobiDev, DRIVER_SUSPENDED ) == true)
   {
      // Should not happen
      G_ERR( "device is suspended\n" );
      dump_stack();
      return NETDEV_TX_BUSY;
   }
   
   if (GobiTestDownReason( pGobiDev, NO_NDIS_CONNECTION ))
   {
      G_ERR( "device is disconnected\n" );
      return NETDEV_TX_BUSY;
   }
   
   // Convert the sk_buff into a URB

   // Check if buffer is full
   if ( atomic_read( &pAutoPM->mURBListLen ) >= txQueueLength)
   {
      G_ERR( "not scheduling request, buffer is full\n" );
      return NETDEV_TX_BUSY;
   }

   // Allocate URBListEntry
   pURBListEntry = kmalloc( sizeof( sURBList ), GFP_ATOMIC );
   if (pURBListEntry == NULL)
   {
      G_ERR( "unable to allocate URBList memory\n" );
      return NETDEV_TX_BUSY;
   }
   pURBListEntry->mpNext = NULL;

   // Allocate URB
   pURBListEntry->mpURB = usb_alloc_urb( 0, GFP_ATOMIC );
   if (pURBListEntry->mpURB == NULL)
   {
      G_ERR( "unable to allocate URB\n" );
      // release all memory allocated by now 
      if (pURBListEntry)
         kfree( pURBListEntry );
      return NETDEV_TX_BUSY;
   }

   GobiNetDriverTxFixup(pNet, pSKB, GFP_ATOMIC);	

   // Allocate URB transfer_buffer
   pURBData = kmalloc( pSKB->len, GFP_ATOMIC );
   if (pURBData == NULL)
   {
      G_ERR( "unable to allocate URB data\n" );
      // release all memory allocated by now
      if (pURBListEntry)
      {
         usb_free_urb( pURBListEntry->mpURB );
         kfree( pURBListEntry );
      }
      return NETDEV_TX_BUSY;
   }
   // Fill with SKB's data
   memcpy( pURBData, pSKB->data, pSKB->len );

   usb_fill_bulk_urb( pURBListEntry->mpURB,
                      pGobiDev->mpNetDev->udev,
                      pGobiDev->mpNetDev->out,
                      pURBData,
                      pSKB->len,
                      GobiUSBNetURBCallback,
                      pAutoPM );

   /* Handle the need to send a zero length packet and release the
    * transfer buffer
    */
    pURBListEntry->mpURB->transfer_flags |= (URB_ZERO_PACKET | URB_FREE_BUFFER);

   // Aquire lock on URBList
   spin_lock_irqsave( &pAutoPM->mURBListLock, URBListFlags );
   
   // Add URB to end of list
   ppURBListEnd = &pAutoPM->mpURBList;
   while ((*ppURBListEnd) != NULL)
   {
      ppURBListEnd = &(*ppURBListEnd)->mpNext;
   }
   *ppURBListEnd = pURBListEntry;
   atomic_inc( &pAutoPM->mURBListLen );

   spin_unlock_irqrestore( &pAutoPM->mURBListLock, URBListFlags );

   complete( &pAutoPM->mThreadDoWork );

   // Start transfer timer
   pNet->trans_start = jiffies;
   // Free SKB
   if (pSKB)
      dev_kfree_skb_any( pSKB );

   return NETDEV_TX_OK;
}
#endif
static int (*local_usbnet_start_xmit) (struct sk_buff *skb, struct net_device *net);
#endif

static int GobiUSBNetStartXmit2( struct sk_buff *pSKB, struct net_device *pNet ){
   struct sGobiUSBNet * pGobiDev;
   struct usbnet * pDev = netdev_priv( pNet );
      
   if (pDev == NULL || pDev->net == NULL)
   {
      G_ERR( "failed to get usbnet device\n" );
      return NETDEV_TX_BUSY;
   }
   
   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
      G_ERR( "failed to get QMIDevice\n" );
      return NETDEV_TX_BUSY;
   }
   
   if( NULL == pSKB )
   {
       G_ERR( "Buffer is NULL \n" );
       return NETDEV_TX_BUSY;
   }

   if (GobiTestDownReason( pGobiDev, DRIVER_SUSPENDED ) == true)
   {
      // Should not happen
      G_ERR( "device is suspended\n" );
      dump_stack();
      return NETDEV_TX_BUSY;
   }
   
   if (GobiTestDownReason( pGobiDev, NO_NDIS_CONNECTION ))
   {
      G_ERR( "device is disconnected\n" );
      return NETDEV_TX_BUSY;
   }

#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,29 ))
   return local_usbnet_start_xmit(pSKB, pNet);
#else
   return usbnet_start_xmit(pSKB, pNet);
#endif
}

/*===========================================================================
METHOD:
   GobiUSBNetOpen (Public Method)

DESCRIPTION:
   Wrapper to usbnet_open, correctly handling autosuspend
   Start AutoPM thread (if CONFIG_PM is defined)

PARAMETERS
   pNet     [ I ] - Pointer to net device

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
int GobiUSBNetOpen( struct net_device * pNet )
{
   int status = 0;
   struct sGobiUSBNet * pGobiDev;
   struct usbnet * pDev = netdev_priv( pNet );

   if (pDev == NULL)
   {
      G_ERR( "failed to get usbnet device\n" );
      return -ENXIO;
   }

   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
      G_ERR( "failed to get QMIDevice\n" );
      return -ENXIO;
   }

#ifdef CONFIG_PM
   #if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,29 ))
   // Start the AutoPM thread
   pGobiDev->mAutoPM.mpIntf = pGobiDev->mpIntf;
   pGobiDev->mAutoPM.mbExit = false;
   pGobiDev->mAutoPM.mpURBList = NULL;
   pGobiDev->mAutoPM.mpActiveURB = NULL;
   spin_lock_init( &pGobiDev->mAutoPM.mURBListLock );
   spin_lock_init( &pGobiDev->mAutoPM.mActiveURBLock );
   atomic_set( &pGobiDev->mAutoPM.mURBListLen, 0 );
   init_completion( &pGobiDev->mAutoPM.mThreadDoWork );
   
   pGobiDev->mAutoPM.mpThread = kthread_run( GobiUSBNetAutoPMThread, 
                                               &pGobiDev->mAutoPM, 
                                               "GobiUSBNetAutoPMThread" );
   if (IS_ERR( pGobiDev->mAutoPM.mpThread ))
   {
      G_ERR( "AutoPM thread creation error\n" );
      return PTR_ERR( pGobiDev->mAutoPM.mpThread );
   }
   #endif
#endif /* CONFIG_PM */

   // Allow traffic
   GobiClearDownReason( pGobiDev, NET_IFACE_STOPPED );

   // Pass to usbnet_open if defined
   if (pGobiDev->mpUSBNetOpen != NULL)
   {
      status = pGobiDev->mpUSBNetOpen( pNet );
#ifdef CONFIG_PM
      // If usbnet_open was successful enable Auto PM
      if (status == 0)
      {
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,33 ))
         usb_autopm_enable( pGobiDev->mpIntf );
#else
         usb_autopm_put_interface( pGobiDev->mpIntf );
#endif
      }
#endif /* CONFIG_PM */
   }
   else
   {
      G_ERR( "no USBNetOpen defined\n" );
   }
   
   return status;
}

/*===========================================================================
METHOD:
   GobiUSBNetStop (Public Method)

DESCRIPTION:
   Wrapper to usbnet_stop, correctly handling autosuspend
   Stop AutoPM thread (if CONFIG_PM is defined)

PARAMETERS
   pNet     [ I ] - Pointer to net device

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
int GobiUSBNetStop( struct net_device * pNet )
{
   struct sGobiUSBNet * pGobiDev;
   struct usbnet * pDev = netdev_priv( pNet );

   if (pDev == NULL || pDev->net == NULL)
   {
      G_ERR( "failed to get netdevice\n" );
      return -ENXIO;
   }

   pGobiDev = (sGobiUSBNet *)pDev->data[0];
   if (pGobiDev == NULL)
   {
      G_ERR( "failed to get QMIDevice\n" );
      return -ENXIO;
   }

   // Stop traffic
   GobiSetDownReason( pGobiDev, NET_IFACE_STOPPED );

#ifdef CONFIG_PM
   #if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,29 ))
   // Tell traffic thread to exit
   pGobiDev->mAutoPM.mbExit = true;
   complete( &pGobiDev->mAutoPM.mThreadDoWork );

   // Wait for it to exit
   while( pGobiDev->mAutoPM.mpThread != NULL )
   {
      msleep( 100 );
   }
   G_INFO( "thread stopped\n" );
   #endif
#endif /* CONFIG_PM */

   // Pass to usbnet_stop, if defined
   if (pGobiDev->mpUSBNetStop != NULL)
   {
      return pGobiDev->mpUSBNetStop( pNet );
   }
   else
   {
      return 0;
   }
}

/*=========================================================================*/
// Struct driver_info
/*=========================================================================*/
static const struct driver_info GobiNetInfo = 
{
   .description   = "GobiNet Ethernet Device",
//begin modify mantis 0051318 by kaibo.zhang
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,39 ))
   .flags         = FLAG_ETHER,
#else
   .flags         = FLAG_ETHER | FLAG_POINTTOPOINT,
#endif
//end modify mantis 0051318 by kaibo.zhang
   .bind          = GobiNetDriverBind,
   .unbind        = GobiNetDriverUnbind,
   .rx_fixup      = GobiNetDriverRxFixup,
   .tx_fixup      = GobiNetDriverTxFixup,
   .data          = 0,
};

/*=========================================================================*/
// Qualcomm Gobi 3000 VID/PIDs
/*=========================================================================*/
static const struct usb_device_id GobiVIDPIDTable [] =
{
   // Fibocom NL678
   { 
      USB_DEVICE( 0x2CB7, 0x0104 ),
      .driver_info = (unsigned long)&GobiNetInfo 
   },
   { 
      USB_DEVICE( 0x2CB7, 0x0109 ),
      .driver_info = (unsigned long)&GobiNetInfo 
   },
   { 
      USB_DEVICE( 0x05C6, 0x90DB ),
      .driver_info = (unsigned long)&GobiNetInfo 
   },
   { 
      USB_DEVICE( 0x1508, 0x1001 ),
      .driver_info = (unsigned long)&GobiNetInfo 
   },
   { 
      USB_DEVICE( 0x1508, 0x1000 ),
      .driver_info = (unsigned long)&GobiNetInfo 
   },
   { 
      USB_DEVICE( 0x2C7C, 0x0306 ),
      .driver_info = (unsigned long)&GobiNetInfo 
   },
   //Terminating entry
   { }
};

MODULE_DEVICE_TABLE( usb, GobiVIDPIDTable );

/*===========================================================================
METHOD:
   GobiUSBNetProbe (Public Method)

DESCRIPTION:
   Run usbnet_probe
   Setup QMI device

PARAMETERS
   pIntf        [ I ] - Pointer to interface
   pVIDPIDs     [ I ] - Pointer to VID/PID table

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
int GobiUSBNetProbe( 
   struct usb_interface *        pIntf, 
   const struct usb_device_id *  pVIDPIDs )
{
   int status;
   struct usbnet * pDev;
   sGobiUSBNet * pGobiDev;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION( 2,6,29 ))
   struct net_device_ops * pNetDevOps;
#endif   
//begin modified by kaibo.zhangkaibo@fibocom.com mantis 0047724  20200527

	/* Workaround to enable dynamic IDs.  This disables usbnet
	 * blacklisting functionality.  Which, if required, can be
	 * reimplemented here by using a magic "blacklist" value
	 * instead of 0 in the static device id table
	 */
   struct usb_device_id *id = (struct usb_device_id *)pVIDPIDs;

   if(!id->driver_info)
   {
   	dev_dbg(&pIntf->dev, "setting defaults for dynamic device id\n");
	id->driver_info = (unsigned long)&GobiNetInfo;
   }

   status = usbnet_probe( pIntf, id );
//end modified by kaibo.zhangkaibo@fibocom.com mantis 0047724  20200527
   if (status < 0)
   {
      G_ERR( "usbnet_probe failed %d\n", status );
	  return status; 
   }

#if (LINUX_VERSION_CODE >= KERNEL_VERSION( 2,6,19 ))
   pIntf->needs_remote_wakeup = 1;
#endif

#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,23 ))
   pDev = usb_get_intfdata( pIntf );
#else
   pDev = (struct usbnet *)pIntf->dev.platform_data;
#endif

   if (pDev == NULL || pDev->net == NULL)
   {
      G_ERR( "failed to get netdevice\n" );
      usbnet_disconnect( pIntf );
      return -ENXIO;
   }

   pGobiDev = kzalloc( sizeof( sGobiUSBNet ), GFP_KERNEL );
   if (pGobiDev == NULL)
   {
      G_ERR( "falied to allocate device buffers\n" );
      usbnet_disconnect( pIntf );
      return -ENOMEM;
   }
   
   atomic_set(&pGobiDev->refcount, 1);

   pDev->data[0] = (unsigned long)pGobiDev;
   
   pGobiDev->mpNetDev = pDev;

   // Clearing endpoint halt is a magic handshake that brings 
   // the device out of low power (airplane) mode
   usb_clear_halt( pGobiDev->mpNetDev->udev, pDev->out );

   // Overload PM related network functions
#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,29 ))
   pGobiDev->mpUSBNetOpen = pDev->net->open;
   pDev->net->open = GobiUSBNetOpen;
   pGobiDev->mpUSBNetStop = pDev->net->stop;
   pDev->net->stop = GobiUSBNetStop;
#if defined(CONFIG_PM) && (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,14 ))
   pDev->net->hard_start_xmit = GobiUSBNetStartXmit;
   pDev->net->tx_timeout = GobiUSBNetTXTimeout;
#else  //nodecom donot send dhcp request before ndis connect for uc20
    local_usbnet_start_xmit = pDev->net->hard_start_xmit;
    pDev->net->hard_start_xmit = GobiUSBNetStartXmit2;
#endif
#else
   pNetDevOps = kmalloc( sizeof( struct net_device_ops ), GFP_KERNEL );
   if (pNetDevOps == NULL)
   {
      G_ERR( "falied to allocate net device ops\n" );
      usbnet_disconnect( pIntf );
      return -ENOMEM;
   }
   memcpy( pNetDevOps, pDev->net->netdev_ops, sizeof( struct net_device_ops ) );
   
   pGobiDev->mpUSBNetOpen = pNetDevOps->ndo_open;
   pNetDevOps->ndo_open = GobiUSBNetOpen;
   pGobiDev->mpUSBNetStop = pNetDevOps->ndo_stop;
   pNetDevOps->ndo_stop = GobiUSBNetStop;
   //nodecom donot send dhcp request before ndis connect for uc20
   pNetDevOps->ndo_start_xmit = GobiUSBNetStartXmit2;

   pNetDevOps->ndo_tx_timeout = usbnet_tx_timeout;

   pDev->net->netdev_ops = pNetDevOps;
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,31 ))
   memset( &(pGobiDev->mpNetDev->stats), 0, sizeof( struct net_device_stats ) );
#else
   memset( &(pGobiDev->mpNetDev->net->stats), 0, sizeof( struct net_device_stats ) );
#endif

   pGobiDev->mpIntf = pIntf;
   memset( &(pGobiDev->mMEID), '0', 14 );
//begin add kaibo for mac address from imei last 6 bit
   memset( &(pGobiDev->mESN), '0', 10 );
   memset( &(pGobiDev->mIMEI), '0', 15 );
//end add kaibo for mac address from imei last 6 bit  
   G_INFO( "Mac Address:\n" );
   PrintHex( &pGobiDev->mpNetDev->net->dev_addr[0], 6 );

   pGobiDev->mbQMIValid = false;
   memset( &pGobiDev->mQMIDev, 0, sizeof( sQMIDev ) );
   pGobiDev->mQMIDev.mbCdevIsInitialized = false;

   pGobiDev->mQMIDev.mpDevClass = gpClass;
   
#ifdef CONFIG_PM
   #if (LINUX_VERSION_CODE < KERNEL_VERSION( 2,6,29 ))
   init_completion( &pGobiDev->mAutoPM.mThreadDoWork );
   #endif
#endif /* CONFIG_PM */
   spin_lock_init( &pGobiDev->mQMIDev.mClientMemLock );

   // Default to device down
   pGobiDev->mDownReason = 0;

   GobiSetDownReason( pGobiDev, NO_NDIS_CONNECTION );
   GobiSetDownReason( pGobiDev, NET_IFACE_STOPPED );
#ifdef GHT_FEATURE_MULTI_PDN
  pGobiDev->mbRawIPMode |= (pDev->udev->descriptor.idVendor == cpu_to_le16(0x2cb7));
   if (pGobiDev->mbRawIPMode) {

	  pGobiDev->m_qmap_num = qmap_num;
   	G_INFO( "qmap mode:%d\n" ,pGobiDev->m_qmap_num);	  
   }
#endif
   // Register QMI
   status = RegisterQMIDevice( pGobiDev );
   if (status != 0)
   {
      // usbnet_disconnect() will call GobiNetDriverUnbind() which will call
      // DeregisterQMIDevice() to clean up any partially created QMI device
      usbnet_disconnect( pIntf );
      return status;
   }
#ifdef GHT_FEATURE_MULTI_PDN
  if (pGobiDev->m_qmap_num > 1) {
        unsigned i;
        for (i = 0; i < pGobiDev->m_qmap_num; i++) {
            qmap_register_device(pGobiDev, i);
        }
   }
#endif
   // Success
   return 0;
}
#ifdef GHT_FEATURE_MULTI_PDN
static void GobiUSBNetDisconnect (struct usb_interface *intf) {

	   struct usbnet *pDev = usb_get_intfdata(intf);
	   sGobiUSBNet * pGobiDev = (sGobiUSBNet *)pDev->data[0];
	   unsigned i;
   
	   for (i = 0; i < pGobiDev->m_qmap_num; i++) {
		   qmap_unregister_device(pGobiDev, i);
	   }
   
	   usbnet_disconnect(intf);
}
#endif
static struct usb_driver GobiNet =
{
   .name       = "GobiNet",
   .id_table   = GobiVIDPIDTable,
   .probe      = GobiUSBNetProbe,
#ifdef GHT_FEATURE_MULTI_PDN
   .disconnect = GobiUSBNetDisconnect,
#else
   .disconnect = usbnet_disconnect,
#endif
#ifdef CONFIG_PM
   .suspend    = GobiNetSuspend,
   .resume     = GobiNetResume,
//begin modified by kaibo.zhangkaibo@fibocom.com mantis 0048566  20200603
   /*Fibocom add for reset_resume*/
	/*
	resume and reset_resume should not be the same process flow
	only PM_EVENT_SUSPEND should call resume other power state will
	power off usb controller, that means qmi connection will be lost,
	driver need be re-probe.
	*/
   //.reset_resume     = GobiNetResume,
//begin modified by kaibo.zhangkaibo@fibocom.com mantis 0048566  20200603
#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,14 ))   
   .supports_autosuspend = true,
#endif   
#else
   .suspend    = NULL,
   .resume     = NULL,
#if (LINUX_VERSION_CODE > KERNEL_VERSION( 2,6,14 ))   
   .supports_autosuspend = false,
#endif   
#endif /* CONFIG_PM */
};

/*===========================================================================
METHOD:
   GobiUSBNetModInit (Public Method)

DESCRIPTION:
   Initialize module
   Create device class
   Register out usb_driver struct

RETURN VALUE:
   int - 0 for success
         Negative errno for error
===========================================================================*/
static int __init GobiUSBNetModInit( void )
{
   gpClass = class_create( THIS_MODULE, "GobiQMI" );
   if (IS_ERR( gpClass ) == true)
   {
      G_ERR( "error at class_create %ld\n",
           PTR_ERR( gpClass ) );
      return -ENOMEM;
   }

   // This will be shown whenever driver is loaded
   printk( KERN_INFO "%s: %s\n", DRIVER_DESC, DRIVER_VERSION );

   return usb_register( &GobiNet );
}
module_init( GobiUSBNetModInit );

/*===========================================================================
METHOD:
   GobiUSBNetModExit (Public Method)

DESCRIPTION:
   Deregister module
   Destroy device class

RETURN VALUE:
   void
===========================================================================*/
static void __exit GobiUSBNetModExit( void )
{
   usb_deregister( &GobiNet );

   class_destroy( gpClass );
}
module_exit( GobiUSBNetModExit );

MODULE_VERSION( DRIVER_VERSION );
MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("Dual BSD/GPL");
 
#ifdef bool
#undef bool
#endif
#ifdef GHT_FEATURE_MULTI_PDN
module_param( qmap_num, uint, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC( qmap_num, "qmap sub network device number" );
#endif
module_param( debug, int, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC( debug, "Debuging enabled or not" );

module_param( interruptible, int, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC( interruptible, "Listen for and return on user interrupt" );
module_param( txQueueLength, int, S_IRUGO | S_IWUSR );
MODULE_PARM_DESC( txQueueLength, 
                  "Number of IP packets which may be queued up for transmit" );

