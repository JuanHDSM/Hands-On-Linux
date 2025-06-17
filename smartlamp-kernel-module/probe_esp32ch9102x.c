#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>

MODULE_AUTHOR("DevTITANS <devtitans@icomp.ufam.edu.br>");
MODULE_DESCRIPTION("Driver de acesso ao SmartLamp (ESP32 com Chip Serial CP2102)");
MODULE_LICENSE("GPL");

#define MAX_RECV_LINE 100
#define SMARTLAMP_INTERFACE 1

static struct usb_device *smartlamp_device;
static uint usb_in, usb_out;
static char *usb_in_buffer, *usb_out_buffer;
static int usb_max_size;

#define VENDOR_ID   0x1a86
#define PRODUCT_ID  0x55d4

static const struct usb_device_id id_table[] = {
    { USB_DEVICE(VENDOR_ID, PRODUCT_ID) },
    {}
};
MODULE_DEVICE_TABLE(usb, id_table);

static int usb_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void usb_disconnect(struct usb_interface *interface);

static struct usb_driver smartlamp_driver = {
    .name        = "smartlamp",
    .probe       = usb_probe,
    .disconnect  = usb_disconnect,
    .id_table    = id_table,
};

module_usb_driver(smartlamp_driver);

static int usb_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    int i;

    printk(KERN_INFO "SmartLamp: Dispositivo conectado ...\n");

    if (!interface || !interface->cur_altsetting) {
        printk(KERN_ERR "SmartLamp: Interface ou altsetting inválidos\n");
        return -ENODEV;
    }

    iface_desc = interface->cur_altsetting;

    // Agora filtrando a interface correta (1)
    if (iface_desc->desc.bInterfaceNumber != SMARTLAMP_INTERFACE) {
        printk(KERN_INFO "SmartLamp: Ignorando interface %d (esperado: %d).\n",
               iface_desc->desc.bInterfaceNumber, SMARTLAMP_INTERFACE);
        return -ENODEV;
    }

    printk(KERN_INFO "SmartLamp: Número de endpoints: %d\n", iface_desc->desc.bNumEndpoints);

    usb_in = 0;
    usb_out = 0;

    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
        endpoint = &iface_desc->endpoint[i].desc;
        printk(KERN_INFO "SmartLamp: Endpoint[%d]: addr=0x%02x, attr=0x%02x\n",
               i, endpoint->bEndpointAddress, endpoint->bmAttributes);

        if ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) {
            if (endpoint->bEndpointAddress & USB_DIR_IN)
                usb_in = endpoint->bEndpointAddress;
            else
                usb_out = endpoint->bEndpointAddress;
        }
    }

    if (!usb_in || !usb_out) {
        printk(KERN_ERR "SmartLamp: Endpoints Bulk IN/OUT não encontrados. Dispositivo não suportado.\n");
        return -ENODEV;
    }

    smartlamp_device = interface_to_usbdev(interface);

    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
        endpoint = &iface_desc->endpoint[i].desc;
        if (endpoint->bEndpointAddress == usb_in) {
            usb_max_size = usb_endpoint_maxp(endpoint);
            break;
        }
    }

    if (!usb_max_size) {
        printk(KERN_ERR "SmartLamp: Falha ao obter tamanho do pacote do endpoint IN.\n");
        return -ENODEV;
    }

    usb_in_buffer = kmalloc(usb_max_size, GFP_KERNEL);
    usb_out_buffer = kmalloc(usb_max_size, GFP_KERNEL);

    if (!usb_in_buffer || !usb_out_buffer) {
        printk(KERN_ERR "SmartLamp: Falha na alocação de buffers.\n");
        kfree(usb_in_buffer);
        kfree(usb_out_buffer);
        return -ENOMEM;
    }

    printk(KERN_INFO "SmartLamp: Endpoint IN: 0x%02x, OUT: 0x%02x, tamanho: %d\n",
           usb_in, usb_out, usb_max_size);

    return 0;
}

static void usb_disconnect(struct usb_interface *interface) {
    printk(KERN_INFO "SmartLamp: Dispositivo desconectado.\n");

    kfree(usb_in_buffer);
    kfree(usb_out_buffer);
}
