#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/minmax.h>  // Para min()

MODULE_AUTHOR("DevTITANS <devtitans@icomp.ufam.edu.br>");
MODULE_DESCRIPTION("Driver de acesso ao SmartLamp (ESP32 com Chip Serial CP2102)");
MODULE_LICENSE("GPL");

#define MAX_RECV_LINE 100 // Tamanho máximo de uma linha de resposta do dispositivo USB
#define SMARTLAMP_INTERFACE 1  // Ajuste conforme a interface que seu dispositivo usa

static struct usb_device *smartlamp_device;      // Referência para o dispositivo USB
static uint usb_in, usb_out;                      // Endereços das portas USB de entrada e saída
static char *usb_in_buffer, *usb_out_buffer;     // Buffers de entrada e saída da USB
static int usb_max_size;                          // Tamanho máximo de uma mensagem USB

#define VENDOR_ID   0x1a86
#define PRODUCT_ID  0x55d4

static const struct usb_device_id id_table[] = {
    { USB_DEVICE(VENDOR_ID, PRODUCT_ID) },
    {}
};
MODULE_DEVICE_TABLE(usb, id_table);

int LDR_value = 0;

// Protótipos das funções
static int usb_probe(struct usb_interface *interface, const struct usb_device_id *id);
static void usb_disconnect(struct usb_interface *interface);
static int usb_read_serial(void);

// Driver USB
static struct usb_driver smartlamp_driver = {
    .name       = "smartlamp",
    .probe      = usb_probe,
    .disconnect = usb_disconnect,
    .id_table   = id_table,
};

module_usb_driver(smartlamp_driver);

// Função chamada quando o dispositivo USB é conectado
static int usb_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    int i;

    printk(KERN_INFO "SmartLamp: Dispositivo conectado ...\n");

    if (!interface || !interface->cur_altsetting) {
        printk(KERN_ERR "SmartLamp: Interface ou altsetting inválidos\n");
        return -ENODEV;
    }

    iface_desc = interface->cur_altsetting;

    // Verifica se é a interface esperada
    if (iface_desc->desc.bInterfaceNumber != SMARTLAMP_INTERFACE) {
        printk(KERN_INFO "SmartLamp: Ignorando interface %d (esperado: %d).\n",
               iface_desc->desc.bInterfaceNumber, SMARTLAMP_INTERFACE);
        return -ENODEV;
    }

    printk(KERN_INFO "SmartLamp: Número de endpoints: %d\n", iface_desc->desc.bNumEndpoints);

    usb_in = 0;
    usb_out = 0;

    // Busca endpoints bulk IN e OUT
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

    // Obtém o tamanho máximo do pacote do endpoint IN
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

    // Aloca buffers com espaço extra para o terminador nulo
    usb_in_buffer = kmalloc(usb_max_size + 1, GFP_KERNEL);
    usb_out_buffer = kmalloc(usb_max_size, GFP_KERNEL);

    if (!usb_in_buffer || !usb_out_buffer) {
        printk(KERN_ERR "SmartLamp: Falha na alocação de buffers.\n");
        kfree(usb_in_buffer);
        usb_in_buffer = NULL;
        kfree(usb_out_buffer);
        usb_out_buffer = NULL;
        return -ENOMEM;
    }

    printk(KERN_INFO "SmartLamp: Endpoint IN: 0x%02x, OUT: 0x%02x, tamanho: %d\n",
           usb_in, usb_out, usb_max_size);

    LDR_value = usb_read_serial();
    printk(KERN_INFO "SmartLamp: Valor LDR: %d\n", LDR_value);

    return 0;
}

// Função chamada quando o dispositivo USB é desconectado
static void usb_disconnect(struct usb_interface *interface)
{
    printk(KERN_INFO "SmartLamp: Dispositivo desconectado.\n");

    kfree(usb_in_buffer);
    usb_in_buffer = NULL;
    kfree(usb_out_buffer);
    usb_out_buffer = NULL;
}

// Função para ler dados da serial via USB
static int usb_read_serial(void)
{
    int ret, actual_size;
    int retries = 10;
    const char *prefix = "RES GET_LDR ";
    int value = -1;
    int i;
    char *start;

    while (retries > 0) {
        ret = usb_bulk_msg(smartlamp_device,
                           usb_rcvbulkpipe(smartlamp_device, usb_in),
                           usb_in_buffer,
                           min(usb_max_size, MAX_RECV_LINE),
                           &actual_size,
                           1000);
        if (ret) {
            printk(KERN_ERR "SmartLamp: Erro ao ler dados da USB (tentativa %d). Código: %d\n", retries, ret);
            retries--;
            ssleep(1);
            continue;
        }

        if (actual_size > 0) {
            // Assegura espaço para o terminador nulo
            if (actual_size > usb_max_size)
                actual_size = usb_max_size;
            usb_in_buffer[actual_size] = '\0';

            printk(KERN_INFO "SmartLamp: Dados recebidos: %s\n", usb_in_buffer);

            // // Debug: imprimir bytes
            // for (i = 0; i < actual_size; i++) {
            //     printk(KERN_INFO "Byte %d: 0x%02x (%c)\n", i, usb_in_buffer[i], usb_in_buffer[i]);
            // }

            start = strstr(usb_in_buffer, prefix);
            if (start) {
                if (sscanf(start + strlen(prefix), "%d", &value) == 1) {
                    printk(KERN_INFO "SmartLamp: Valor LDR extraído: %d\n", value);
                    return value;
                }
            } else {
                printk(KERN_ERR "SmartLamp: Resposta inesperada: %s\n", usb_in_buffer);
            }
        }

        retries--;
        ssleep(1);
    }

    return value;  // -1 se falhou
}
